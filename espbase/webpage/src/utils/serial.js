import { type, deepcopy, isEmpty } from '@/utils'

export const WebSerial = navigator.serial !== undefined

const DISABLED = `WebSerial is not enabled in your browser.
Try enabling it by visiting:
    chrome://flags/#enable-experimental-web-platform-features
    opera://flags/#enable-experimental-web-platform-features
    edge://flags/#enable-experimental-web-platform-features
> Firefox, IE and Safari do not support this feature yet.
`

const ports = []
const portIds = reactive([])

function getId(port) {
    let { usbVendorId: v, usbProductId: p } = port?.getInfo() ?? {}
    if (v === undefined || p === undefined) return port.toString()
    return `${v.toString(16).padStart(4, 0)}:${p.toString(16).padStart(4, 0)}`
}

function toFilter(str) {
    if (type(str) === 'array') return str.map(toFilter).filter(_ => _)
    if (type(str) === 'object' && str.usbVendorId) return str
    if (type(str) !== 'string' || !str.includes(':')) return
    let [v, p] = str.split(':').map(_ => parseInt(_, 16))
    return { usbVendorId: v, usbProductId: p }
}

async function refresh(e) {
    e && console.log('Serial port', getId(e.target), e.type)
    ports.splice(0, ports.length, ...(await navigator.serial.getPorts()))
    portIds.splice(0, portIds.length, ...ports.map(getId))
}

if (WebSerial) {
    navigator.serial.onconnect = navigator.serial.ondisconnect = refresh
    refresh()
}

const OptionSchema = {
    type: 'object',
    properties: {
        baudRate: {
            title: 'Baudrate',
            description:
                'A positive, non-zero value indicating the baud rate ' +
                'at which serial communication should be established.',
            type: 'integer',
            exclusiveMinimum: 0,
            enum: [9600, 19200, 38400, 57600, 115200, 921600],
            default: 115200,
        },
        dataBits: {
            title: 'Data bits',
            description:
                'An integer value of 7 or 8 indicating the number of ' +
                'data bits per frame.',
            enum: [7, 8],
            default: 8,
        },
        parity: {
            title: 'Parity',
            description: 'The parity mode, either "none", "even" or "odd".',
            enum: ['none', 'even', 'odd'],
            default: 'none',
        },
        stopBits: {
            title: 'Stop bits',
            description:
                'An integer value of 1 or 2 indicating the number of ' +
                'stop bits at the end of the frame.',
            enum: [1, 2],
            default: 1,
        },
        flowControl: {
            title: 'Flow control',
            description: 'The flow control type, either "none" or "hardware".',
            enum: ['none', 'hardware'],
            default: 'none',
        },
        bufferSize: {
            title: 'Buffer size',
            description:
                'An unsigned long integer indicating the size of the ' +
                'read and write buffers that are to be established.',
            type: 'integer',
            minimum: 0,
            default: 4096,
        },
    },
    required: [
        'baudRate',
        'bufferSize',
        'dataBits',
        'flowControl',
        'parity',
        'stopBits',
    ],
}

const OptionDefault = Object.fromEntries(
    Object.entries(OptionSchema.properties).map(([k, v]) => [k, v.default])
)

const encoder = new TextEncoder()
const decoder = new TextDecoder()

export default class SerialController {
    constructor() {
        this.buffer = ''
        this.opened = ref(false)
        this.portIds = portIds
        this.schema = reactive(deepcopy(OptionSchema))
        this.options = reactive(deepcopy(OptionDefault))
    }

    async request(filters = []) {
        if (!WebSerial) throw DISABLED
        if (type(filters) !== 'array') filters = [filters]
        filters = toFilter(filters)
        let port = await navigator.serial.requestPort({ filters })
        if (isEmpty(port.getInfo())) {
            await port.forget()
            throw new TypeError('Invalid port to use')
        }
        await refresh()
        return port
    }

    async open(port, options = {}, filters = []) {
        if (!WebSerial) throw DISABLED
        for (let arg of arguments) {
            if (['array', 'string'].includes(type(arg))) filters = arg
            if (type(arg) === 'object') options = arg
        }
        if (type(port) !== 'SerialPort') port = this.port
        try {
            port ??= await this.request(filters)
            await this.close()
            await port.open(Object.assign({}, this.options, options))
            port.ondisconnect = this.close.bind(this)
            this.port = port
            this.portId = getId(this.port)
            this.sigals = await this.port.getSignals()
            this.writer = this.port.writable.getWriter()
            this.reader = this.port.readable.getReader()
            this.opened.value = true
        } catch (err) {
            throw `Could not open serial. ${err.message}`
        }
    }

    async close() {
        if (!toValue(this.opened)) return
        await this.reader.cancel().catch(() => {})
        await this.reader.releaseLock()
        await this.writer.close().catch(() => {})
        await this.writer.releaseLock()
        try {
            await this.port.close()
        } catch (err) {
            throw `Could not close serial. ${err.message}`
        } finally {
            this.opened.value = false
        }
    }

    async clear() {
        await this.close().catch()
        await navigator.serial
            .getPorts()
            .then(arr => Promise.all(arr.map(port => port.forget())))
        await refresh()
    }

    async write(data) {
        return await this?.writer.write(encoder.encode(data))
    }

    async read(until = '\n') {
        if (!toValue(this.opened)) throw new TypeError('Port is closed')
        let { value, done } = await this.reader.read()
        if (done) await this.reader.releaseLock()
        if (!value) throw new TypeError('Stream reader is released')
        this.buffer += decoder.decode(value).replace(/\r\n?/g, '\n')
        let idx = this.buffer.lastIndexOf(until) + 1
        if (idx > 0) {
            let msg = this.buffer.slice(0, idx)
            this.buffer = this.buffer.slice(idx)
            return msg
        }
    }

    async setSignals(opt = {}) {
        if (isEmpty(opt) || !toValue(this.opened)) return
        return await this.port?.setSignals(Object.assign(this.signals, opt))
    }

    async setCTS(val) {
        return await this.setSignals({ clearToSend: !!val })
    }

    async setDSR(val) {
        return await this.setSignals({ dataSetReady: !!val })
    }
}
