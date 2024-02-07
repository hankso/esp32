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
        if (cache[(key = Object.prototype.toString.call(obj))]) return key
        return (cache[key] = key.slice(8, -1).toLowerCase())
    }
})()

export var formatSize = (function () {
    let units = 'BKMGTP'
    let decimals = [0, 1, 2, 3, 3, 3]
    return (size, base = 1024) => {
        let exp = size ? Math.log2(size) / Math.log2(base) : 0
        exp = Math.min(Math.floor(exp), units.length - 1)
        return (size / base ** exp).toFixed(decimals[exp]) + units[exp]
    }
})()

// For compatiability
// - Chromium
// - Webkit
// - Mozilla Geko
// - Opera Presto
// - IE Trident?
// - Fallback function
export var requestAnimationFrame =
    window.requestAnimationFrame ||
    window.webkitRequestAnimationFrame ||
    window.mozRequestAnimationFrame ||
    window.oRequestAnimationFrame ||
    window.msRequestAnimationFrame ||
    (callback => window.setTimeout(callback, 17))

export var cancelAnimationFrame =
    window.cancelAnimationFrame ||
    window.webkitCancelAnimationFrame ||
    window.mozCancelAnimationFrame ||
    window.oCancelAnimationFrame ||
    window.msCancelAnimationFrame ||
    window.clearTimeout

export function getFPS(frames = 5, verbose = true) {
    let cache = []
    return new Promise((resolve, reject) => {
        if (frames <= 1) return reject('Invalid frame number')
        ;(function wrapper(ts) {
            if (ts) cache.push(ts / 1000)
            if (--frames) return requestAnimationFrame(wrapper)
            resolve((cache.length - 1) / (cache[cache.length - 1] - cache[0]))
        })()
    }).then(fps => {
        if (verbose) console.log(`Current refresh rate is ${fps.toFixed(2)}Hz`)
        return fps
    })
}

export function LoopFrame(
    callback,
    timeout = 1000,
    verbose = false,
    divider = 1
) {
    // loop counter
    let cnt
    // used by stopByCancel / forceStop
    let rid
    // used by displayFPS / forceStop
    let tid
    // lock used to avoid multi-start
    let run
    // browser render FPS
    let fps
    // calculated realtime FPS
    let rfps
    // function to be executed after task finished
    let hooks
    // calculate real FPS
    let lasttime
    // used to sync all duration
    let starttime

    function init() {
        cnt = 0
        rid = null
        tid = null
        run = false
        fps = 0
        rfps = 0
        hooks = []
        lasttime = 0
        starttime = 0
        getFPS().then(v => (fps = v))
    }

    function loop(dt) {
        if (cnt++ % divider < 0.1) return
        if (!lasttime) lasttime = performance.now()
        callback(dt)
        let thistime = performance.now()
        let frametime = thistime - lasttime
        lasttime = thistime
        rfps = 1000 / frametime
        if (fps && rfps < fps / 2 / divider)
            console.warn(
                `Frame ${cnt} lost warning!`,
                `start at ${(starttime + dt).toFixed(2)}`,
                `end at ${thistime.toFixed(2)}`,
                `frame time ${frametime.toFixed(2)}`,
                `FPS ${rfps.toFixed(2)}`
            )
        if (verbose > 1)
            console.log(
                `Frame ${cnt} dt ${dt.toFixed(2)}`,
                `start at ${(starttime + dt).toFixed(2)}`,
                `end at ${thistime.toFixed(2)}`,
                `frame time ${frametime.toFixed(2)}`,
                `FPS ${rfps.toFixed(2)}`
            )
    }

    function byTimeout(ts) {
        // time passed since task start
        let duration = ts - starttime
        if (duration >= timeout) return taskStop()
        rid = requestAnimationFrame(byTimeout)
        loop(duration)
    }

    function byCancel(ts) {
        rid = requestAnimationFrame(byCancel)
        loop(ts - starttime)
    }

    function taskStart(ts) {
        if (verbose)
            console.log('LoopFrame start at', new Date().toLocaleTimeString())
        run = true
        starttime = ts || Date.now()
    }

    function taskStop() {
        if (verbose)
            console.log('LoopFrame stop at', new Date().toLocaleTimeString())
        hooks.forEach(requestAnimationFrame)
        init()
    }

    function forceStop() {
        cancelAnimationFrame(rid)
        clearTimeout(tid)
        taskStop()
    }

    init()
    return {
        onStop(callback) {
            hooks.push(callback)
            return this
        },
        byTimeout() {
            if (!run)
                requestAnimationFrame(ts => {
                    taskStart(ts)
                    byTimeout(ts)
                })
            return this
        },
        byCancel() {
            if (!run) {
                requestAnimationFrame(ts => {
                    taskStart(ts)
                    byCancel(ts)
                })
                tid = setTimeout(forceStop, timeout)
            }
            return this
        },
        forceStop() {
            forceStop()
            return this
        },
        displayFPS(id, update = 100) {
            let elem = document.getElementById(id)
            if (!elem) return this
            let oldcolor = elem.style.color
            elem.style.color = 'green'
            this.onStop(() => (elem.style.color = oldcolor))
            let did = 0
            ;(function render() {
                elem.innerText = 'FPS: ' + (rfps ? rfps.toFixed(2) : '')
                did = setTimeout(render, update)
            })()
            this.onStop(() => clearTimeout(did))
            return this
        },
    }
}

export function debounce(func, timeout = 300) {
    let handler = null
    return (...args) => {
        clearTimeout(handler)
        handler = setTimeout(func, timeout, ...args)
    }
}

export function camelToSnake(s, sep = '_') {
    return s.replace(/([a-z])([A-Z])/g, `$1${sep}$2`).toLowerCase()
}

export function copyToClipboard(text) {
    let input = document.createElement('input')
    input.value = text
    document.body.appendChild(input)
    input.select()
    document.execCommand('Copy')
    document.body.removeChild(input)
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

export function deepcopy(obj) {
    return JSON.parse(JSON.stringify(obj))
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
    inRange(low = 0, high = 255) {
        return v =>
            (low <= v && v <= high) || `Must be a number between ${low}-${high}`
    },
    isInteger(v) {
        return Number(v) === parseInt(v) || 'Must be an integer'
    },
    required(v) {
        return v !== '' || 'This field is required'
    },
}

Array.prototype.remove = function (item) {
    let idx = this.indexOf(item)
    if (idx >= 0) return this.splice(idx, 1)
}

// Modified based on github.com/sindresorhus/ip-regex

const v4reg = `
(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)
(?:\\.(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)){3}
`
    .replace(/\n/g, '')
    .trim()

const v6seg = '[a-fA-F\\d]{1,4}'

const v6reg = `
(?:
(?:${v6seg}:){7}(?:${v6seg}|:)|
(?:${v6seg}:){6}(?:${v4reg}|:${v6seg}|:)|
(?:${v6seg}:){5}(?::${v4reg}|(?::${v6seg}){1,2}|:)|
(?:${v6seg}:){4}(?:(?::${v6seg}){0,1}:${v4reg}|(?::${v6seg}){1,3}|:)|
(?:${v6seg}:){3}(?:(?::${v6seg}){0,2}:${v4reg}|(?::${v6seg}){1,4}|:)|
(?:${v6seg}:){2}(?:(?::${v6seg}){0,3}:${v4reg}|(?::${v6seg}){1,5}|:)|
(?:${v6seg}:){1}(?:(?::${v6seg}){0,4}:${v4reg}|(?::${v6seg}){1,6}|:)|
(?::(?:(?::${v6seg}){0,5}:${v4reg}|(?::${v6seg}){1,7}|:))
)(?:%[0-9a-zA-Z]{1,})?
`
    .replace(/\s*\/\/.*$/gm, '')
    .replace(/\n/g, '')
    .trim()

const ipreg = {
    v4: new RegExp(`^${v4reg}$`),
    v6: new RegExp(`^${v6reg}$`),
    v46: new RegExp(`(?:^${v4reg}$)|(?:^${v6reg}$)`),
    v4c: new RegExp(v4reg, 'g'),
    v6c: new RegExp(v6reg, 'g'),
    v46c: new RegExp(`(?:${v4reg})|(?:${v6reg})`, 'g'),
}

export var ipaddr = {
    is_ip: (addr, find = false) => (find ? ipreg.v46 : ipreg.v46c).test(addr),
    is_ipv4: (addr, find = false) => (find ? ipreg.v4 : ipreg.v4c).test(addr),
    is_ipv6: (addr, find = false) => (find ? ipreg.v6 : ipreg.v6c).test(addr),
    version: str => (ipreg.v4c.test(str) ? 4 : ipreg.v6c.test(str) ? 6 : -1),
    extract: str => str.match(ipreg.v46c),
    extract_int: str => {
        if (ipreg.v4c.test(str))
            return str
                .match(ipreg.v4c)[0]
                .split('.')
                .map(_ => (_ ? parseInt(_) : 0))
        if (ipreg.v6c.test(str))
            return str
                .match(ipreg.v6c)[0]
                .split(':')
                .map(_ => (_ ? parseInt(_, 16) : 0))
        return []
    },
}
