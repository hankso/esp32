<template>
    <div v-if="video || audio" class="d-flex flex-column align-center">
        <img
            ref="velem"
            v-if="video"
            :alt="run ? 'Frame' : 'Thumbnail'"
            class="d-block hide-caret cursor-pointer"
            style="max-width: 100%"
            @click="toggle"
            @dblclick="dbl = true"
        />
        <audio
            ref="aelem"
            v-if="audio"
            v-show="showAudio"
            class="w-100 align-self-stretch"
            style="height: 30px"
            preload="none"
            controls
            controlslist="nodownload noremoteplayback noplaybackrate"
        ></audio>
    </div>
</template>

<script setup>
import { debounce, toggleFullscreen } from '@/utils'

const props = defineProps({
    video: {
        type: String,
        default: '',
    },
    audio: {
        type: String,
        default: '',
    },
    lastFrame: {
        type: Boolean,
        default: true,
    },
    showAudio: {
        type: Boolean,
        default: false,
    },
    autoStart: {
        type: Boolean,
        default: false,
    },
})

const run = ref(false)
const dbl = ref(false)
const velem = ref(null)
const aelem = ref(null)

const toggle = debounce(e => {
    if (toValue(dbl)) {
        toggleFullscreen(e)
    } else {
        run.value = !run.value
    }
    dbl.value = false
})

function fixURL(url) {
    if (url.includes('#')) url = url.substr(0, url.indexOf('#'))
    if (!url || !url.indexOf('?') || !url.indexOf('&')) return url
    if (url.endsWith('&')) url = url.substr(0, url.length - 1)
    if (!url.startsWith('/api'))
        url = `/api${url.startsWith('/') ? '' : '/'}${url}`
    return url
}

watchPostEffect(() => {
    let vsrc = fixURL(props.video),
        asrc = fixURL(props.audio),
        cors = process.env.BUILD_INFO ? null : ''
    let v = toValue(velem),
        a = toValue(aelem),
        r = toValue(run)
    if (v) {
        v.style.width = v.width + 'px'
        v.style.height = v.height + 'px'
        v.onload = () => (v.style.width = v.style.height = '')
        v.crossOrigin = cors
        if (v.src.startsWith('blob:')) URL.revokeObjectURL(v.src)
        if (r) {
            v.src = vsrc
        } else if (v.src && v.width && v.height && props.lastFrame) {
            let cvs = document.createElement('canvas')
            cvs.width = v.naturalWidth
            cvs.height = v.naturalHeight
            let ctx = cvs.getContext('2d')
            ctx.drawImage(v, 0, 0)
            cvs.toBlob(blob => {
                v.src = '' // stop loading
                v.src = URL.createObjectURL(blob)
            })
        } else {
            v.src = '' // stop loading
            v.src = vsrc + (vsrc.includes('?') ? '&' : '?') + 'still'
        }
    }
    if (a) {
        a.onplay = () => (run.value = true)
        a.onpause = () => (run.value = false)
        a.crossOrigin = cors
        if (!r) a.src = '' // stop loading
        a.src = asrc
        if (r) a.play()?.catch(() => {})
    }
})

onMounted(() => (run.value = props.autoStart))
</script>
