export function get_timestamp() {
    return Date.now()
}

export function get_monotonic() {
    // See https://www.w3.org/TR/hr-time/
    // Monotonic High Resolution Time
    return performance.now()
}

export var type = (function () {
    let cache = {}
    return obj => {
        if (obj === null) return 'null'
        let key = typeof obj
        // basic: string, boolean, number, undefined, function
        if (key !== 'object') return key
        // DOM elements
        if (obj.nodeType) return 'element'
        // date, regexp, error, object, array, math
        if (cache[(key = Object.prototype.toString.call(obj))])
            return cache[key]
        return (cache[key] = key.slice(8, -1).toLowerCase())
    }
})()

export function isEmpty(obj) {
    return (
        obj === undefined ||
        obj === null ||
        (type(obj) === 'string' && obj.trim().length === 0) ||
        (type(obj) === 'object' &&
            Object.keys(obj).length === 0 &&
            obj.constructor === Object)
    )
}

export function notEmpty(obj) {
    return !isEmpty(obj)
}

export function deepcopy(obj) {
    return JSON.parse(JSON.stringify(obj))
}

export function parseBool(str) {
    return ['1', 'y', 'on', 'true'].includes((str + '').toLowerCase())
}

export function parseNum(str) {
    let num = (str + '').includes('.') ? parseFloat(str) : parseInt(str)
    return isFinite(num) ? num : null
}

export var formatSize = (function () {
    let units = 'BKMGTP'
    let decimals = [0, 1, 2, 3, 3, 3]
    return (size, base = 1024) => {
        let exp = size ? Math.log2(size) / Math.log2(base) : 0
        exp = Math.min(Math.floor(exp), units.length - 1)
        return (size / base ** exp).toFixed(decimals[exp]) + units[exp]
    }
})()

export function debounce(func, timeout = 300) {
    if (!func) return () => {}
    let handler = null
    return (...args) => {
        clearTimeout(handler)
        handler = setTimeout(func, timeout, ...args)
    }
}

export function pause(msec) {
    return new Promise(resolve => setTimeout(resolve, msec))
}

export function gzipCompress(string) {
    return new Response(
        new Blob([string]).stream().pipeThrough(new CompressionStream('gzip'))
    ).arrayBuffer()
}

export function gzipDecompress(bytes) {
    return new Response(
        new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))
    ).text()
}

export function escape(str = '', ...code) {
    return `\x1b[${code.join(';')}m${str}\x1b[0m`
}

export function unescape(str) {
    return (str + '').replace(/\x1b\[[\d;]*m/g, '') // eslint-disable-line
}

const htmlCodes = {
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quto;',
    "'": '&#39;',
    '`': '&#96;',
    '&': '&amp;',
    ' ': '&nbsp;',

    '&lt;': '<',
    '&gt;': '>',
    '&quto;': '"',
    '&#39;': "'",
    '&#96;': '`',
    '&amp;': '&',
    '&nbsp;': ' ',
}

export function html(str, space = false, crlf = false) {
    return (str + '')
        .replace(/\r?\n/g, crlf ? '<br>' : '\n')
        .replace(/ /g, space ? '&nbsp;' : ' ')
        .replace(/[<"'`>]|&(?![\w#]+;)/g, _ => htmlCodes[_])
}

export function unhtml(str) {
    return (str + '').replace(/&[\w#]+;/g, _ => htmlCodes[_] || _)
}

export function copyToClipboard(text) {
    if (!text) return
    if (navigator?.clipboard) return navigator.clipboard.writeText(unhtml(text))
    let input = document.createElement('input')
    input.value = unhtml(text)
    document.body.appendChild(input)
    input.select()
    document.execCommand('copy')
    input.remove()
    return Promise.resolve()
}

export function readFromClipboard() {
    if (navigator?.clipboard) return navigator.clipboard.readText()
    return new Promise((resolve, reject) => {
        try {
            let input = document.createElement('input')
            document.body.appendChild(input)
            input.focus()
            document.execCommand('paste')
            input.remove()
            resolve(input.value)
        } catch (e) {
            reject(e)
        }
    })
}

export function downloadAsFile(data, fn = 'data.txt', type = 'text/plain') {
    let file = new File([data], fn, { type })
    let link = document.createElement('a')
    link.href = URL.createObjectURL(file)
    link.download = file.name
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    URL.revokeObjectURL(link.href)
}

export function toggleFullscreen(e) {
    let elem = document.documentElement
    switch (type(e)) {
        case 'string':
            elem = document.getElementById(e)
            break
        case 'element':
            elem = e
            break
        case 'mouseevent':
            elem = e.target
            break
    }
    if (!elem || !elem.requestFullscreen) return
    if (!document.fullscreenElement) {
        elem.requestFullscreen()
    } else if (document.exitFullscreen) {
        document.exitFullscreen()
    }
}

export var rules = {
    inRange(r0 = 0, r1 = 255) {
        return v =>
            (r0 <= v && v <= r1) || `Must be a number between ${r0} - ${r1}`
    },
    isInteger(v) {
        return Number(v) === parseInt(v) || 'Must be an integer'
    },
    length(v, r0, r1) {
        if (r0 !== undefined && r1 !== undefined)
            return v =>
                (r0 <= v.length && v.length <= r1) ||
                `Length must between ${r0}-${r1}`
        if (r0 !== undefined)
            return v => v.length >= r0 || `Length must be longer than ${r0}`
        if (r1 !== undefined)
            return v => v.length <= r1 || `Length must be shorter than ${r1}`
        return v.length > 0 || 'This field is required'
    },
    required(v) {
        return v !== '' || 'This field is required'
    },
}

import cvtcase from './cvtcase'
import strftime from './strftime'

export { cvtcase, strftime }
