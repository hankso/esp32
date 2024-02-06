// Plugins
import vue from '@vitejs/plugin-vue'
import prismjs from 'vite-plugin-prismjs'
import autoimport from 'unplugin-auto-import/vite'
import components from 'unplugin-vue-components/vite'
import compression from 'vite-plugin-compression'
import vuedevtools from 'vite-plugin-vue-devtools'
import { visualizer } from 'rollup-plugin-visualizer'
import vuetify, { transformAssetUrls } from 'vite-plugin-vuetify'

// Utilities
import { resolve } from 'node:path'
import { defineConfig } from 'vite'
import { spawn, execSync } from 'node:child_process'
import { fileURLToPath, URL } from 'node:url'

// Colorful logging
function kolorist(start, end) {
    if (
        !process.stdout.isTTY ||
        process.env.FORCE_COLOR === '0' ||
        process.env.NODE_DISABLE_COLORS ||
        process.env.NO_COLOR ||
        process.env.TERM === 'dumb'
    ) {
        return str => '' + str
    }
    const open = `\x1b[${start}m`
    const close = `\x1b[${end}m`
    const regex = new RegExp(`\\x1b\\[${end}m`, 'g')
    return str => open + ('' + str).replace(regex, open) + close
}

var bold = kolorist(1, 22),
    dim = kolorist(2, 22),
    green = kolorist(32, 39),
    yellow = kolorist(33, 39),
    cyan = kolorist(36, 39)

// Custom plugin
const pythonServerPlugin = () => {
    let pserver

    class PythonServer {
        constructor(
            port = 8080,
            host = '0.0.0.0',
            cmd = 'python',
            verbose = false
        ) {
            let entry = resolve(__dirname, '..', 'helper.py')
            this.proc = this.pid = null
            this.port = port
            this.host = host
            this.verb = verbose
            this.addr = `${host.replace('0.0.0.0', 'localhost')}:${port}`
            this.args = [cmd, '-u', entry, 'serve', '-H', host, '-P', port]
            if (!verbose) this.args.push('--quiet')
        }
        log() {
            console.log(
                dim(new Date().toLocaleTimeString()),
                cyan(bold('[pser]')),
                this.pid ? `[${this.pid}]` : '',
                ...arguments
            )
        }
        info() {
            if (this.verb) this.log(...argument)
        }
        onMessage(msg) {
            msg.toString().trim().split('\n').forEach(this.log)
        }
        start() {
            if (this.proc) return this
            // DO NOT use `exec` on windows system
            this.proc = spawn(this.args[0], this.args.slice(1))
            this.pid = this.proc.pid
            this.proc.on('exit', code => {
                this.info(`Python Server exit(${code || 0})`)
                this.proc = this.pid = null
            })
            this.proc.on('error', err => console.log(`Error: ${err}`))
            this.proc.stdout.on('data', this.onMessage)
            this.proc.stderr.on('data', this.onMessage)
            this.info('Python Server start')
            return this
        }
        close() {
            if (!this.proc) return this
            this.info('Python Server killed')
            this.proc.kill()
            this.proc = null
            return this
        }
        toString() {
            let [host, port] = this.addr.split(':')
            return [
                green('  \u279c '),
                bold('Python: '),
                cyan('http://' + host + bold(port)),
                green(`(PID ${this.pid})`),
            ].join(' ')
        }
    }
    return {
        name: 'python-api-server',
        apply: 'serve',
        enforce: 'pre',
        buildStart() {
            pserver && pserver.start()
        },
        buildEnd() {
            pserver && pserver.close()
        },
        configResolved(resolvedConfig) {
            pserver = new PythonServer(
                resolvedConfig.server.sport,
                resolvedConfig.server.host
            )
        },
        configureServer(server) {
            let printUrls = server.printUrls
            server.printUrls = () => {
                if (pserver && pserver.proc) {
                    server.config.logger.info(pserver.toString())
                }
                printUrls()
            }
        },
    }
}

// https://vitejs.dev/config/
export default defineConfig(({ command, mode, isSsrBuild, isPreview }) => {
    let SRC_VER, BUILD_INFO
    if (command === 'build') {
        BUILD_INFO = {
            NODE_JS: process.versions.node,
            BUILD_OS: `${process.platform}-${process.arch}`,
            BUILD_TIME: new Date().toString(),
        }
        try {
            SRC_VER = `${execSync('git describe --tags --always')}`
        } catch {}
    }
    return {
        build: {
            outDir: './dist',
            assetsDir: 'assets',
        },
        server: {
            host: '0.0.0.0',
            port: 5173,
            sport: 5172, // python api server
            proxy: {
                '/api': {
                    target: `http://localhost:5172`,
                    rewrite: path => path.replace(/^\/api/, ''),
                    changeOrigin: true,
                    secure: false,
                },
            },
        },
        define: {
            'process.env': {
                VITE_PATH: __dirname,
                VITE_MODE: mode,
                VITE_CMD: command,
                BUILD_INFO,
                SRC_VER,
            },
        },
        plugins: [
            vue({
                template: { transformAssetUrls }, // add more types
            }),
            vuetify(),
            prismjs({
                languages: [
                    'markup', // 0.8kB
                    'css', // 0.1kB
                    'js', // 1.6kB
                    'clike', // 0.1kB
                ],
                plugins: [
                    'previewers', // 3kB
                    'autolinker', // 0.4kB
                    'inline-color', // 0.5kB
                    'show-invisibles', // 0.2kB
                ],
                theme: 'default',
                css: true,
            }),
            autoimport({
                imports: ['vue', 'vue-router', 'vue-i18n'],
            }),
            components({
                dts: false,
            }),
            compression({
                verbose: false,
                deleteOriginFile: true,
            }),
            visualizer({
                filename: 'resources.html',
                template: 'treemap', // sunburst|treemap|network|raw-data|list
                emitFile: true,
                gzipSize: true,
                brotliSize: true,
            }),
            vuedevtools(),
            pythonServerPlugin(),
        ],
        resolve: {
            alias: {
                '@': fileURLToPath(new URL('./src', import.meta.url)),
            },
        },
    }
})
