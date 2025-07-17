// Modified on github.com/hankso/EmBCI

import { strftime, getMonotonic } from '@/utils'

// For compatiability
//   - Chromium
//   - Webkit
//   - Mozilla Geko
//   - Opera Presto
//   - IE Trident?
//   - Fallback function
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
        verbose && console.log(`Current refresh rate is ${fps.toFixed(2)}Hz`)
        return fps
    })
}

export default function LoopFrame(
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
        if (!lasttime) lasttime = getMonotonic()
        callback(dt)
        let thistime = getMonotonic()
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
        verbose && console.log(strftime('LoopFrame start at %T'))
        run = true
        starttime = ts
    }

    function taskStop() {
        verbose && console.log(strftime('LoopFrame stop at %T'))
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
