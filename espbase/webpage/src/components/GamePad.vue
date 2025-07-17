<template>
    <svg ref="svg" viewBox="0 0 441 383" stroke-width="2" fill="none">
        <path
            name="LOutline"
            d="M220.5 294.5C220.5 294.5 195 294.5 150 294.5C105 294.5 81.5 378.5 49.5 378.5C17.5 378.5 4 363.9 4 317.5C4 271.1 43.5 165.5 55 137.5C66.5 109.5 95.5 92.0001 128 92.0001C154 92.0001 200.5 92.0001 220.5 92.0001"
        ></path>
        <path
            name="ROutline"
            d="M220 294.5C220 294.5 245.5 294.5 290.5 294.5C335.5 294.5 359 378.5 391 378.5C423 378.5 436.5 363.9 436.5 317.5C436.5 271.1 397 165.5 385.5 137.5C374 109.5 345 92.0001 312.5 92.0001C286.5 92.0001 240 92.0001 220 92.0001"
        ></path>
        <circle name="LeftStickOutline" cx="113" cy="160" r="37.5"></circle>
        <circle name="RightStickOutline" cx="278" cy="238" r="37.5"></circle>
        <circle name="DOutline" cx="166" cy="238" r="37.5"></circle>
        <circle name="BOutline" cx="329" cy="160" r="37.5"></circle>
        <line name="LHOutline" x1="30" y1="210" x2="130" y2="300"></line>
        <line name="RHOutline" x1="411" y1="210" x2="311" y2="300"></line>
        <path
            name="DUp"
            button="u"
            :fill-opacity="xbox.u"
            d="M177.669 222.335C180.793 219.21 180.816 213.997 176.868 212.014C176.327 211.743 175.776 211.491 175.215 211.258C172.182 210.002 168.931 209.355 165.648 209.355C162.365 209.355 159.114 210.002 156.081 211.258C155.521 211.491 154.969 211.743 154.429 212.014C150.48 213.997 150.503 219.21 153.627 222.335L159.991 228.698C163.116 231.823 168.181 231.823 171.305 228.698L177.669 222.335Z"
        ></path>
        <path
            name="DRight"
            button="r"
            :fill-opacity="xbox.r"
            d="M181.447 249.669C184.571 252.793 189.785 252.816 191.768 248.868C192.039 248.327 192.291 247.776 192.523 247.215C193.78 244.182 194.426 240.931 194.426 237.648C194.426 234.365 193.78 231.114 192.523 228.081C192.291 227.521 192.039 226.969 191.768 226.429C189.785 222.48 184.571 222.503 181.447 225.627L175.083 231.991C171.959 235.116 171.959 240.181 175.083 243.305L181.447 249.669Z"
        ></path>
        <path
            name="DDown"
            button="d"
            :fill-opacity="xbox.d"
            d="M154.113 253.447C150.989 256.571 150.966 261.785 154.914 263.767C155.455 264.039 156.006 264.291 156.566 264.523C159.6 265.78 162.85 266.426 166.134 266.426C169.417 266.426 172.667 265.78 175.701 264.523C176.261 264.291 176.812 264.039 177.353 263.767C181.301 261.785 181.279 256.571 178.154 253.447L171.79 247.083C168.666 243.959 163.601 243.959 160.477 247.083L154.113 253.447Z"
        ></path>
        <path
            name="DLeft"
            button="l"
            :fill-opacity="xbox.l"
            d="M150.335 226.113C147.21 222.989 141.997 222.966 140.014 226.914C139.743 227.455 139.491 228.006 139.258 228.566C138.002 231.6 137.355 234.85 137.355 238.134C137.355 241.417 138.002 244.667 139.258 247.701C139.491 248.261 139.743 248.812 140.014 249.353C141.997 253.301 147.21 253.279 150.335 250.154L156.698 243.79C159.823 240.666 159.823 235.601 156.698 232.477L150.335 226.113Z"
        ></path>
        <path
            name="BTop"
            button="y"
            :fill-opacity="xbox.y"
            d="M340.669 144.335C343.793 141.21 343.816 135.997 339.868 134.014C339.327 133.743 338.776 133.491 338.215 133.258C335.182 132.002 331.931 131.355 328.648 131.355C325.365 131.355 322.114 132.002 319.081 133.258C318.521 133.491 317.969 133.743 317.429 134.014C313.48 135.997 313.503 141.21 316.627 144.335L322.991 150.698C326.116 153.823 331.181 153.823 334.305 150.698L340.669 144.335Z"
        ></path>
        <path
            name="BRight"
            button="b"
            :fill-opacity="xbox.b"
            d="M344.447 171.669C347.571 174.793 352.785 174.816 354.768 170.868C355.039 170.327 355.291 169.776 355.523 169.215C356.78 166.182 357.426 162.931 357.426 159.648C357.426 156.365 356.78 153.114 355.523 150.081C355.291 149.521 355.039 148.969 354.768 148.429C352.785 144.48 347.571 144.503 344.447 147.627L338.083 153.991C334.959 157.116 334.959 162.181 338.083 165.305L344.447 171.669Z"
        ></path>
        <path
            name="BBottom"
            button="a"
            :fill-opacity="xbox.a"
            d="M317.113 175.447C313.989 178.571 313.966 183.785 317.914 185.767C318.455 186.039 319.006 186.291 319.566 186.523C322.6 187.78 325.85 188.426 329.134 188.426C332.417 188.426 335.667 187.78 338.701 186.523C339.261 186.291 339.812 186.039 340.353 185.767C344.301 183.785 344.279 178.571 341.154 175.447L334.79 169.083C331.666 165.959 326.601 165.959 323.477 169.083L317.113 175.447Z"
        ></path>
        <path
            name="BLeft"
            button="x"
            :fill-opacity="xbox.x"
            d="M313.335 148.113C310.21 144.989 304.997 144.966 303.014 148.914C302.743 149.455 302.491 150.006 302.258 150.566C301.002 153.6 300.355 156.851 300.355 160.134C300.355 163.417 301.002 166.668 302.258 169.701C302.491 170.261 302.743 170.812 303.014 171.353C304.997 175.301 310.21 175.279 313.335 172.154L319.698 165.79C322.823 162.666 322.823 157.601 319.698 154.477L313.335 148.113Z"
        ></path>
        <rect
            name="L1"
            button="lb"
            :fill-opacity="xbox.lb"
            x="111.5"
            y="61.5"
            width="41"
            height="13"
            rx="6.5"
        ></rect>
        <rect
            name="R1"
            button="rb"
            :fill-opacity="xbox.rb"
            x="289.5"
            y="61.5"
            width="41"
            height="13"
            rx="6.5"
        ></rect>
        <path
            name="L2"
            trigger="lt"
            :fill-opacity="xbox.lt"
            d="M152.5 37C152.5 41.1421 149.142 44.5 145 44.5H132C127.858 44.5 124.5 41.1421 124.5 37V16.5C124.5 8.76801 130.768 2.5 138.5 2.5C146.232 2.5 152.5 8.76801 152.5 16.5V37Z"
        ></path>
        <path
            name="R2"
            trigger="rt"
            :fill-opacity="xbox.rt"
            d="M317.5 37C317.5 41.1421 314.142 44.5 310 44.5H297C292.858 44.5 289.5 41.1421 289.5 37V16.5C289.5 8.76801 295.768 2.5 303.5 2.5C311.232 2.5 317.5 8.76801 317.5 16.5V37Z"
        ></path>
        <circle
            name="LeftStick"
            joystick="l"
            :fill-opacity="xbox.ld"
            :cx="113 + xbox.lx * 10"
            :cy="160 + xbox.ly * 10"
            r="28"
        ></circle>
        <circle
            name="RightStick"
            joystick="r"
            :fill-opacity="xbox.rd"
            :cx="278 + xbox.rx * 10"
            :cy="238 + xbox.ry * 10"
            r="28"
        ></circle>
        <circle
            name="LMeta"
            button="back"
            :fill-opacity="xbox.back"
            cx="185"
            cy="162"
            r="10"
        ></circle>
        <circle
            name="Home"
            button="home"
            :fill-opacity="xbox.home"
            cx="222"
            cy="140"
            r="12"
        ></circle>
        <circle
            name="RMeta"
            button="start"
            :fill-opacity="xbox.start"
            cx="259"
            cy="162"
            r="10"
        ></circle>
    </svg>
</template>

<script setup>
const svg = ref(null)

const emits = defineEmits(['event', 'btndn', 'btnup', 'trig', 'joyst'])

const props = defineProps({
    gamepad: {
        type: Object,
        default: null,
    },
    splitEvents: {
        type: Boolean,
        default: false,
    },
})

const xbox = reactive({
    u: 0,
    d: 0,
    l: 0,
    r: 0,
    a: 0,
    b: 0,
    x: 0,
    y: 0,
    lb: 0,
    rb: 0,
    lt: 0,
    rt: 0,
    lx: 0,
    ly: 0,
    rx: 0,
    ry: 0,
    ld: 0,
    rd: 0,
    back: 0,
    home: 0,
    start: 0,
})

const std = computed(() => props.gamepad?.mapping === 'standard')

watchEffect(() => {
    if (!std.value) return
    let btns = props.gamepad.buttons,
        axes = props.gamepad.axes
    xbox.lx = axes[0]
    xbox.ly = axes[1]
    xbox.rx = axes[2]
    xbox.ry = axes[3]
    xbox.ld = Math.sqrt(axes[0] * axes[0] + axes[1] * axes[1])
    xbox.rd = Math.sqrt(axes[2] * axes[2] + axes[3] * axes[3])
    xbox.a = btns[0].value
    xbox.b = btns[1].value
    xbox.x = btns[2].value
    xbox.y = btns[3].value
    xbox.lb = btns[4].value
    xbox.rb = btns[5].value
    xbox.lt = btns[6].value
    xbox.rt = btns[7].value
    xbox.back = btns[8].value
    xbox.start = btns[9].value
    xbox.u = btns[12].value
    xbox.d = btns[13].value
    xbox.l = btns[14].value
    xbox.r = btns[15].value
    xbox.home = btns[16].value
})

const ctx = {}

function emit(evt, val, ts) {
    if (props.splitEvents) emits(evt, val, ts)
    emits('event', evt, val, ts)
}

function onButton(e) {
    let btn = e.target.getAttribute('button')
    if (e.type === 'pointerdown') {
        e.target.setPointerCapture(e.pointerId)
        xbox[btn] = 1
        emit('btndn', btn, e.timeStamp)
    } else {
        xbox[btn] = 0
        emit('btnup', btn, e.timeStamp)
    }
}

function onTrigger(e) {
    let trig = e.target.getAttribute('trigger')
    if (e.type === 'pointerdown') {
        let { height } = e.target.getBoundingClientRect()
        ctx[e.pointerId] = [e.pageY, height]
        return e.target.setPointerCapture(e.pointerId)
    } else if (e.type === 'pointerup') {
        xbox[trig] = 0
        delete ctx[e.pointerId]
    } else if (e.pointerId in ctx) {
        let dy = ctx[e.pointerId][0] - e.pageY
        xbox[trig] = Math.max(0, Math.min(dy / ctx[e.pointerId][1], 1))
    } else return
    emit(
        'trig',
        [Math.round(xbox['lt'] * 0xff), Math.round(xbox['rt'] * 0xff)],
        e.timeStamp
    )
}

function onJoystick(e) {
    let joyst = e.target.getAttribute('joystick')
    if (e.type === 'pointerdown') {
        let name = e.target.getAttribute('name')
        let elem = svg.value.querySelector(`*[name=${name}Outline]`)
        if (!elem) return console.error(`${name}Outline not found`)
        let { left, top, width, height } = elem.getBoundingClientRect()
        ctx[e.pointerId] = {
            x: left + width / 2,
            y: top + height / 2,
            r: Math.sqrt(width ** 2 + height ** 2) / 2,
        }
        return e.target.setPointerCapture(e.pointerId)
    } else if (e.type === 'pointerup') {
        xbox[joyst + 'x'] = xbox[joyst + 'y'] = xbox[joyst + 'd'] = 0
        delete ctx[e.pointerId]
    } else if (e.pointerId in ctx) {
        let outline = ctx[e.pointerId]
        let dx = e.pageX - outline.x
        let dy = e.pageY - outline.y
        let dist = Math.sqrt(dx ** 2 + dy ** 2)
        let norm = Math.max(dist, outline.r)
        xbox[joyst + 'x'] = dx / norm
        xbox[joyst + 'y'] = dy / norm
        xbox[joyst + 'd'] = dist / norm
    } else return
    emit(
        'joyst',
        [
            Math.round(xbox['lx'] * 0x7fff),
            Math.round(xbox['ly'] * 0x7fff),
            Math.round(xbox['rx'] * 0x7fff),
            Math.round(xbox['ry'] * 0x7fff),
        ],
        e.timeStamp
    )
}

function register() {
    svg.value.querySelectorAll('*[button]').forEach(e => {
        for (let event of ['pointerdown', 'pointerup'])
            e.addEventListener(event, onButton)
    })
    svg.value.querySelectorAll('*[trigger]').forEach(e => {
        for (let event of ['pointerdown', 'pointermove', 'pointerup'])
            e.addEventListener(event, onTrigger)
    })
    svg.value.querySelectorAll('*[joystick]').forEach(e => {
        for (let event of ['pointerdown', 'pointermove', 'pointerup'])
            e.addEventListener(event, onJoystick)
    })
}

function unregister() {
    svg.value.childNodes.forEach(e => {
        for (let event in ['pointerdown', 'pointermove', 'pointerup']) {
            e.removeEventListener(event, onButton)
            e.removeEventListener(event, onTrigger)
            e.removeEventListener(event, onJoystick)
        }
    })
}

onMounted(() => !std.value && register())
onBeforeUnmount(unregister)
</script>

<style scoped>
svg [name*='Outline'] {
    stroke: hsl(210, 50%, 85%);
}

svg *:not([name*='Outline']) {
    stroke: rgb(var(--v-theme-on-background));
    fill: rgb(var(--v-theme-on-background));
}
</style>
