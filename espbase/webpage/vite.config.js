// Plugins
import vue from '@vitejs/plugin-vue'
import prismjs from 'vite-plugin-prismjs'
import autoimport from 'unplugin-auto-import/vite'
import components from 'unplugin-vue-components/vite'
import compression from 'vite-plugin-compression'
import vuedevtools from 'vite-plugin-vue-devtools'
import { visualizer } from 'rollup-plugin-visualizer'
import vuetify, { transformAssetUrls } from 'vite-plugin-vuetify'
import pythonServer from './src/plugins/vite-python'

// Utilities
import { statSync } from 'node:fs'
import { resolve } from 'node:path'
import { execSync } from 'node:child_process'
import { fileURLToPath, URL } from 'node:url'
import { defineConfig, splitVendorChunkPlugin } from 'vite'

// https://vitejs.dev/config/
export default defineConfig(({ command, mode }) => {
    let info
    let sport = 5172 // python api server
    if (command === 'build') {
        info = {
            NODE_JS: process.versions.node,
            BUILD_OS: `${process.platform}-${process.arch}`,
            BUILD_TIME: new Date().toLocaleString(),
        }
        try {
            info['SOURCE'] = `${execSync('git describe --tags --always')}`
        } catch {}
    }
    let dist = resolve(__dirname, '..', 'files', 'www')
    if (!statSync(dist, { throwIfNoEntry: false })?.isDirectory())
        dist = './dist'
    return {
        build: {
            outDir: dist,
            emptyOutDir: true,
            assetsDir: 'assets',
        },
        server: {
            host: '0.0.0.0',
            port: 5173,
            sport,
            proxy: {
                '/api': {
                    target: `http://localhost:${sport}`,
                    changeOrigin: true,
                    secure: false,
                },
            },
        },
        define: {
            'process.env': {
                PROJECT_VERSION: process.env.npm_package_version,
                PROJECT_NAME: process.env.npm_package_name,
                API_SERVER: sport,
                BUILD_INFO: info,
                VITE_PATH: __dirname,
                VITE_CMD: command,
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
                    'json', // 0.1kB
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
                template: 'treemap', // sunburst|treemap|network|raw-data|list
                emitFile: dist === './dist',
                gzipSize: true,
                brotliSize: true,
            }),
            vuedevtools(),
            pythonServer({
                entry: resolve(__dirname, '..', 'helper.py'),
                verbose: false,
            }),
            splitVendorChunkPlugin(),
        ],
        resolve: {
            alias: {
                '@': fileURLToPath(new URL('./src', import.meta.url)),
            },
        },
    }
})
