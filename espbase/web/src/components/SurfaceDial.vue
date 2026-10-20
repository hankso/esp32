<template>
    <div class="w-100 h-100 d-flex justify-center align-center">
        <span ref="elem" :style class="rounded-xl bg-purple hide-caret"></span>
    </div>
</template>

<script setup>
import { useElementBounding, useEventListener } from '@vueuse/core'

const emits = defineEmits(['event', 'sdial'])

const props = defineProps({
    splitEvents: {
        type: Boolean,
        default: false,
    },
})

const ctx = reactive({
    event: '',
    pntid: 0,
    press: 0,
    angle: 0,
    rotate: 0,
    degstep: 30,
})

const elem = ref(null)

const { top, left, width, height } = useElementBounding(elem)
const X = computed(() => left.value + width.value / 2)
const Y = computed(() => top.value + height.value / 2)
const R = computed(() => Math.max(20, Math.min(X.value, Y.value) / 5))

const style = computed(() => ({
    cursor: ctx.pntid ? 'grabbing' : 'grab',
    transform: `scale(${ctx.press ? 2 : 1}) rotate(${ctx.rotate}deg)`,
}))

function emit(evt, val, ts) {
    if (props.splitEvents) emits(evt, val, ts)
    emits('event', evt, val, ts)
}

useEventListener(elem, 'pointerdown', e => {
    if (ctx.pntid) return
    elem.value.setPointerCapture((ctx.pntid = e.pointerId))
    emit('sdial', (ctx.event = 'D'), e.timeStamp)
})

useEventListener(elem, 'pointermove', e => {
    if (ctx.pntid !== e.pointerId) return
    let dx = e.pageX - X.value
    let dy = e.pageY - Y.value
    let deg = (Math.atan2(dy, dx) / Math.PI) * 180
    let old = ctx.angle
    ctx.press = Math.sqrt(dx ** 2 + dy ** 2) > R.value
    ctx.angle = deg
    if (!old || !ctx.press) return
    let min = Math.ceil(Math.min(deg, old) / ctx.degstep)
    let max = Math.floor(Math.max(deg, old) / ctx.degstep)
    if (max >= min) {
        ctx.event = (max > min) ^ (deg > old) ? 'R' : 'L'
        ctx.rotate += ctx.event === 'R' ? ctx.degstep : -ctx.degstep
        emit('sdial', ctx.event, e.timeStamp)
    }
})

useEventListener(elem, 'pointerup', e => {
    if (ctx.pntid !== e.pointerId) return
    emit('sdial', (ctx.event = 'U'), e.timeStamp)
    ctx.pntid = ctx.press = ctx.angle = ctx.rotate = 0
})
</script>

<style scoped>
span {
    width: 5em;
    height: 5em;
    transition: transform 0.5s;
    touch-action: none;
}
</style>
