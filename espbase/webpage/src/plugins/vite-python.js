// Custom Vite plugin to run python api server at background

// Utilities
import { spawn } from 'node:child_process'

// Colorful logging
function AnsiSGR(start, end) {
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

var bold = AnsiSGR(1, 22),
    dim = AnsiSGR(2, 22),
    green = AnsiSGR(32, 39),
    yellow = AnsiSGR(33, 39),
    cyan = AnsiSGR(36, 39)

let pserver

class PythonServer {
    constructor(
        port = 8080,
        host = '0.0.0.0',
        entry = 'main.py',
        verbose = false,
        command = 'python'
    ) {
        this.proc = this.pid = null
        this.port = port
        this.host = host
        this.verb = verbose
        this.addr = `${host.replace('0.0.0.0', 'localhost')}:${port}`
        this.args = [command, '-u', entry, 'serve', '-H', host, '-P', port]
        if (!verbose) this.args.push('--quiet')
    }
    log() {
        // print timestamp and colorful string same like vite
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

        let logger = line => this.log(line)
        let print = m => m.toString().trim().split('\n').forEach(logger)
        this.proc.stdout.on('data', print)
        this.proc.stderr.on('data', print)

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

export default (options = {}) => ({
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
            options.port ?? resolvedConfig.server.sport,
            options.host ?? resolvedConfig.server.host,
            options.entry ?? 'main.py',
            options.verbose ?? false,
            options.command ?? 'python'
        )
    },
    configureServer(server) {
        let printUrls = server.printUrls
        server.printUrls = () => {
            if (pserver && pserver.proc)
                server.config.logger.info(pserver.toString())
            printUrls()
        }
    },
})
