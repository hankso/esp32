<template>
    <div
        ref="elem"
        class="w-100 h-100 rounded-lg bg-grey"
        style="touch-action: none; cursor: crosshair"
    ></div>
</template>

<script setup>
import { useEventListener } from '@vueuse/core'

const ctx = {
    touch: {},
    click: '',
    mouse: [0, 0],
    wheel: [0, 0],
    zoom: 0,
}

const btns = ['left', 'middle', 'right', 'backward', 'forward']

const state = {
    val: 0,
    enter(val = 1) {
        this.val = Math.max(this.val, val)
    },
    leave() {
        clearTimeout(this.hdl)
        this.hdl = setTimeout(() => (this.val = 0), 100)
    },
}

const emits = defineEmits(['event', 'click', 'mouse', 'wheel', 'zoom'])

const props = defineProps({
    splitEvents: {
        type: Boolean,
        default: false,
    },
    tpadScale: {
        type: Number,
        default: 0,
    },
})

const elem = ref(null)

const scale = computed(() => 1.5 ** props.tpadScale)

function emit(evt, val, ts) {
    if (props.splitEvents) emits(evt, val, ts)
    emits('event', evt, val, ts)
}

useEventListener(elem, 'contextmenu', e => e.preventDefault())

useEventListener(elem, 'pointerdown', e => (ctx.touch[e.pointerId] = e))

useEventListener(elem, 'pointermove', e => {
    if (!(e.pointerId in ctx.touch)) return
    ctx.touch[e.pointerId] = e
    let touches = Object.values(ctx.touch)
    if (touches.length > 2) {
        // TODO: three+ fingers
    } else if (touches.length === 2) {
        if (e.pointerId !== touches[1].pointerId) return
        let [a, b] = touches
        let sx = a.movementX > 0 == b.movementX > 0
        let sy = a.movementY > 0 == b.movementY > 0
        let dx = a.clientX - b.clientX
        let dy = a.clientY - b.clientY
        let dist = Math.sqrt(dx ** 2 + dy ** 2)
        let old = ctx.zoom
        ctx.zoom = dist
        if (Math.abs(dist - old) > 10) {
            state.enter(4)
            if (old) emit('zoom', dist > old ? 'out' : 'in', e.timeStamp)
            state.leave()
        } else if (state.val <= 3 && sx == sy) {
            state.enter(3)
            let v = ((a.movementY + b.movementY) / 2) | 0
            let h = ((a.movementX + b.movementX) / 2) | 0
            if (v || h) emit('wheel', (ctx.wheel = [-v, h]), e.timeStamp)
            state.leave()
        }
    } else if (state.val <= 2 && e.buttons) {
        state.enter(2)
        let x = (e.movementX * scale.value) | 0
        let y = (e.movementY * scale.value) | 0
        if (x || y) emit('mouse', (ctx.mouse = [x, y]), e.timeStamp)
        state.leave()
    }
})

useEventListener(elem, 'pointerup', e => {
    delete ctx.touch[e.pointerId]
    if (Object.keys(ctx.touch).length) {
        state.enter(1)
        state.leave()
    } else if (state.val === 1) {
        emit('click', (ctx.click = btns[2]), e.timeStamp)
    } else if (state.val === 0) {
        emit('click', (ctx.click = btns[e.button]), e.timeStamp)
    }
    ctx.zoom = 0
})

useEventListener(elem, 'wheel', e => {
    let v = e.deltaY | 0
    let h = e.deltaX | 0
    if (v || h) emit('wheel', (ctx.wheel = [-v, h]), e.timeStamp)
})
</script>
