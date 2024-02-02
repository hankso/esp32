window.requestAnimationFrame = (                    // for compatiability
    window.requestAnimationFrame       ||           // Chromium  
    window.webkitRequestAnimationFrame ||           // Webkit 
    window.mozRequestAnimationFrame    ||           // Mozilla Geko 
    window.oRequestAnimationFrame      ||           // Opera Presto 
    window.msRequestAnimationFrame                  // IE Trident? 
) || (callback => window.setTimeout(callback, 17)); // Fallback function

export function getFPS(ts, times=5) {
    ts = ts || performance.now();
    if (window.FPS != undefined) window.FPS.push(ts); else window.FPS = [ts];
    if (--times) window.requestAnimationFrame(ts => getFPS(ts, times));
    else {
        for (var sum = 0, i = 1, l = window.FPS.length; i < l; i++) {
            sum += window.FPS[i] - window.FPS[i - 1];
        }
        window.FPS = 1000 / (sum / (l - 1));
        console.log('Browser refresh rate detected as', window.FPS.toFixed(2), 'Hz');
    }
}

export var type = (function(global) {
    var key, cache = {};
    return obj => obj === null ? 'null'
        : obj === global ? 'global' // window in browser or global in nodejs
        : (key = typeof obj) !== 'object' ? key // basic: string, boolean, number, undefined, function
        : obj.nodeType ? 'element' // DOM element
        : cache[key = Object.prototype.toString.call(obj)] // date, regexp, error, object, array, math
        || (cache[key] = key.slice(8, -1).toLowerCase());
})(this);

export var LoopTask = function(callback, timeout=1000, verbose=false, divider=1) {
    var fps = 0;
    var frame = 0;         // loop counter
    var run = false;       // lock used to avoid multi-start
    var done = false;      // indicate loop task finished
    var doneHooks = [];    // function to be executed after task finished
    var lasttime = 0;      // calculate real FPS
    var thistime = 0;      // calculate real FPS
    var starttime = 0;     // used to sync all duration
    var duration = 0;      // time passed since task start
    var req_id = null;     // used by stopByCancel / forceStop
    var tout_id = null;    // used by displayFPS / forceStop

    function loop(ts) {
        if ((frame++ % divider) < 0.1) return;
        if (!lasttime) lasttime = performance.now();
        callback(ts);
        thistime = performance.now();
        fps = 1000 / (thistime - lasttime);
        if (window.FPS && fps < (window.FPS / 2 / divider)) {
            console.warn(
                'Frame', frame, 'lost warning!', 
                'start at', (starttime + ts).toFixed(2),
                'end at', thistime.toFixed(2),
                'frame time', (thistime - lasttime).toFixed(2),
                'FPS', fps.toFixed(2)
            );
        }
        if (verbose > 1) {
            console.log(
                'Frame', frame, 'dt', ts.toFixed(2),
                'start at', (starttime + ts).toFixed(2),
                'end at', thistime.toFixed(2),
                'frame time', (thistime - lasttime).toFixed(2),
                'FPS', fps.toFixed(2)
            );
        }
        lasttime = thistime;
    }
    function byTimeout(ts) {
        duration = ts - starttime;
        if (duration >= timeout) return taskStop();
        req_id = window.requestAnimationFrame(byTimeout);
        loop(duration);
    }
    function byCancel(ts) {
        req_id = window.requestAnimationFrame(byCancel);
        loop(ts - starttime);
    }
    function taskStart(ts) {
        verbose && console.log('LoopTask start at', new Date().toLocaleTimeString());
        starttime = ts || Date.now();
    }
    function taskStop() {
        done = true; let donetime = new Date().toLocaleTimeString();
        verbose && console.log('LoopTask finished at', donetime);
        for (let cb of doneHooks) window.requestAnimationFrame(cb);
    }
    function forceStop() {
        window.cancelAnimationFrame(req_id);
        taskStop();
    }
    return {
        byTimeout: function() {
            if (!run) run = true; else return this;
            window.requestAnimationFrame((ts)=>{
                taskStart(ts); byTimeout(ts);
            });
            return this;
        },
        byCancel: function() {
            if (!run) run = true; else return this;
            window.requestAnimationFrame((ts)=>{
                taskStart(ts); byCancel(ts);
            });
            tout_id = setTimeout(forceStop, timeout);
            return this;
        },
        forceStop: function() {
            forceStop();
            window.clearTimeout(tout_id);
            return this;
        },
        displayFPS: function(id, update=100) {
            if (!run) return this;
            let elem = document.getElementById(id);
            if (elem != undefined) {
                let oldcolor = elem.style.color;
                elem.style.color = 'green';
                (function render() {
                    if (done) return elem.style.color = oldcolor;
                    if (fps && fps != Infinity) {
                        elem.innerText = 'FPS: ' + fps.toFixed(2);
                    }
                    setTimeout(render, update);
                })();
            }
            return this;
        },
        done: function(callback) { doneHooks.push(callback); return this;  }
    }
}

export function copyToClipboard(text) {
    let input = document.createElement('input');
    input.value = text;
    document.body.appendChild(input);
    input.select();
    document.execCommand('Copy');
    document.body.removeChild(input);
}

export function downloadAsFile(data, filename='data.txt', type='text/plain') {
    let file = new File([data], filename, { type });
    let link = document.createElement('a');
    let url = URL.createObjectURL(file);
    link.href = url;
    link.download = file.name;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
}

export function deepcopy(obj) {
    return JSON.parse(JSON.stringify(obj));
}

export function toggleFullscreen(e) {
    var elem = document.documentElement;
    switch (type(e)) {
        case 'string': elem = document.getElementById(e); break;
        case 'element': elem = e; break;
        case 'mouseevent': elem = e.target; break;
    }
    if (!elem || !elem.requestFullscreen) return;
    if (!document.fullscreenElement) {
        elem.requestFullscreen();
    } else if (document.exitFullscreen) {
        document.exitFullscreen();
    }
}

export var rules = {
    require: v => v !== '' || 'This field is required',
    inRange: (low=0, high=255) => (v => (low <= v && v <= high) || `Must be a number between ${low}-${high}`),
    isInteger: v => Number(v) === parseInt(v) || 'Must be an integer',
};

// Modified based on github.com/sindresorhus/ip-regex

const _v4reg = `
(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)
(?:\\.(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)){3}
`.replace(/\n/g, '').trim();

const _v6seg = '[a-fA-F\\d]{1,4}';

const _v6reg = `
(?:
(?:${_v6seg}:){7}(?:${_v6seg}|:)|
(?:${_v6seg}:){6}(?:${_v4reg}|:${_v6seg}|:)|
(?:${_v6seg}:){5}(?::${_v4reg}|(?::${_v6seg}){1,2}|:)|
(?:${_v6seg}:){4}(?:(?::${_v6seg}){0,1}:${_v4reg}|(?::${_v6seg}){1,3}|:)|
(?:${_v6seg}:){3}(?:(?::${_v6seg}){0,2}:${_v4reg}|(?::${_v6seg}){1,4}|:)|
(?:${_v6seg}:){2}(?:(?::${_v6seg}){0,3}:${_v4reg}|(?::${_v6seg}){1,5}|:)|
(?:${_v6seg}:){1}(?:(?::${_v6seg}){0,4}:${_v4reg}|(?::${_v6seg}){1,6}|:)|
(?::(?:(?::${_v6seg}){0,5}:${_v4reg}|(?::${_v6seg}){1,7}|:))
)(?:%[0-9a-zA-Z]{1,})?
`.replace(/\s*\/\/.*$/gm, '').replace(/\n/g, '').trim();

const ipreg = {
    v4: new RegExp(`^${_v4reg}$`), v6: new RegExp(`^${_v6reg}$`),
    v46: new RegExp(`(?:^${_v4reg}$)|(?:^${_v6reg}$)`),
    v4c: new RegExp(_v4reg, 'g'), v6c: new RegExp(_v6reg, 'g'),
    v46c: new RegExp(`(?:${_v4reg})|(?:${_v6reg})`, 'g'),
};

export var ipaddr = {
    is_ip: (addr, exact=false) => (exact ? ipreg.v46 : ipreg.v46c).test(addr),
    is_ipv4: (addr, exact=false) => (exact ? ipreg.v4 : ipreg.v4c).test(addr),
    is_ipv6: (addr, exact=false) => (exact ? ipreg.v6 : ipreg.v6c).test(addr),
    version: str => ipreg.v4c.test(str) ? 4 : (ipreg.v6c.test(str) ? 6 : undefined),
    extract: str => str.match(ipreg.v46c),
    extract_int: str => (
        ipreg.v4c.test(str) ? str.match(ipreg.v4c)[0].split('.').map(_ => _ || 0) :
        ipreg.v6c.test(str) ? str.match(ipreg.v6c)[0].split(':').map(_ => `0x${_ || 0}`) :
        []
    ).map(parseInt)
};
