<template>
    <v-row class="flex-grow-0">
        <v-scale-transition group leave-absolute>
            <v-col key="0" cols="12" sm="8" v-if="!dev">
                <v-combobox
                    v-model="cmd"
                    density="compact"
                    variant="outlined"
                    :items="cmds"
                    hide-details
                    clearable
                    :loading
                    :placeholder
                    @keyup.enter="runCommand"
                    @keyup.ctrl.c="cmd = null"
                >
                    <template #append-inner>
                        <v-btn
                            variant="plain"
                            density="compact"
                            @click="runCommand"
                            :icon="mdiSendCircleOutline"
                            :loading
                        ></v-btn>
                    </template>
                </v-combobox>
            </v-col>
            <v-col key="1" cols="12" :sm="dev ? 12 : 4" class="d-flex">
                <v-select
                    v-if="hid"
                    label="HID input"
                    density="compact"
                    variant="outlined"
                    v-model="dev"
                    hide-details
                    clearable
                    :append-icon="dev ? mdiFullscreen : null"
                    @click:append="toggleFullscreen()"
                    :items="Object.keys(devs)"
                ></v-select>
            </v-col>
        </v-scale-transition>
    </v-row>
    <v-row class="flex-grow-1">
        <v-col cols="12">
            <v-expand-transition>
                <component :is="devs[dev]" @event="onEvent" v-bind="config" />
            </v-expand-transition>
        </v-col>
    </v-row>
    <v-snackbar
        v-if="config.showEvent"
        v-model="event.show"
        location="top"
        min-width="auto"
        rounded="lg"
        class="ma-4"
        contained
        :timeout="event.timeout"
    >
        {{ event.message }}
        <v-btn
            icon
            variant="plain"
            density="compact"
            @click="config.showEvent = false"
        >
            <v-icon :icon="mdiClose"></v-icon>
            <v-tooltip activator="parent" location="bottom">
                Disable
            </v-tooltip>
        </v-btn>
    </v-snackbar>
</template>

<script setup>
import { randomId } from '@/utils'
import KeyBoard from '@/components/KeyBoard.vue'
import Mouse from '@/components/TouchPad.vue'
import GamePad from '@/components/GamePad.vue'
import SurfaceDial from '@/components/SurfaceDial.vue'

import { mdiClose, mdiSendCircleOutline, mdiFullscreen } from '@mdi/js'
import { useStorage, useWebSocket } from '@vueuse/core'

const devs = { KeyBoard, Mouse, GamePad, SurfaceDial }

const cmd = ref(null)
const hid = ref(false)
const dev = ref(null)
const hist = ref([])
const loading = ref(false)
const placeholder = ref('Send command through RPC')

const event = ref({
    show: false,
    message: '',
    timeout: 0,
})

const cmds = computed(() => [
    ...new Set(hist.value.map(_ => _?.command).filter(_ => _)),
])

const notify = inject('notify', console.log)

const { toggleFullscreen } = inject('fullscreen')

const config = useStorage('hid', {
    hidDev: '',
    cmdTout: 5000,
    histSize: 20,
    tpadScale: 0,
    showEvent: true,
})

const url = process.env.BUILD_INFO
    ? location.host
    : `${location.hostname}:${process.env.API_SERVER}`

const { data, send } = useWebSocket(`ws://${url}/api/ws`, {
    autoReconnect: {
        retries: 3,
        delay: 1000,
        onFailed: () => alert(`Failed to connect to WebSocket ${url}`),
    },
})

function showEvent(msg, timeout = 3000) {
    if (!config.value.showEvent) return
    event.value.message = `${msg}`
    event.value.timeout = timeout - 1
    event.value.show = true
    nextTick(() => (event.value.timeout = timeout))
}

function rpcCall(method) {
    let req = { id: randomId(), method, params: Array(...arguments).slice(1) }
    send(JSON.stringify(req))
    return new Promise((resolve, reject) =>
        (function wait(tout) {
            for (let rep of hist.value) {
                if (req.id !== rep?.id) continue
                rep.command = [req.method, ...req.params].join(' ')
                return resolve(`${rep?.result ?? rep.error}`)
            }
            if (tout < 0) return reject('Timeout')
            setTimeout(wait, 500, tout - 500)
        })(config.value.cmdTout)
    )
}

function rpcNotify(method) {
    send(JSON.stringify({ method, params: Array(...arguments).slice(1) }))
}

function runCommand() {
    let args = (cmd.value || '').split(' ').filter(_ => _)
    if (!args.length) return
    loading.value = true
    cmd.value = placeholder.value = null
    rpcCall(...args)
        .then(rep => (placeholder.value = rep ?? null))
        .catch(notify)
        .finally(() => (loading.value = false))
}

/* Backend command syntax for HID event:
    hid [-k CODE] [-s STR] [-m B|XYVH]
        [-p BTXYXY] [-c 1-15]
        [-d BLRUD] [-t 0-65535]
        [--ts MSEC] [--to 0-2|UBS]
 */

const gamepad_buttons = [
    'A',
    'B',
    'X',
    'Y',
    'LB',
    'RB',
    'LS',
    'RS',
    'BACK',
    'START',
    'HOME',
    'SHARE',
    'U',
    'R',
    'D',
    'L',
]

function onEvent(evt, val, msec) {
    if (msec === undefined) msec = performance.now()
    let arg
    if (evt === 'keydn') {
        arg = `-k ${val}`
        val = val.replace(/(^|\W)\w(?=\w|-)/g, m => m.toUpperCase())
        val = val.replace(/\|/g, ' + ')
    } else if (evt === 'click') {
        arg = `-m ${val}`
    } else if (evt === 'mouse') {
        arg = `-m ,${val}`
    } else if (evt === 'wheel') {
        arg = `-m 0,0,${val}`
    } else if (evt === 'btndn') {
        let idx = gamepad_buttons.indexOf(val.toUpperCase())
        if (idx >= 0) arg = `-p A0x${(1 << idx).toString(16)}`
    } else if (evt === 'btnup') {
        let idx = gamepad_buttons.indexOf(val.toUpperCase())
        if (idx >= 0) arg = `-p D0x${(1 << idx).toString(16)}`
    } else if (evt === 'trig') {
        arg = `-p ${val}`
    } else if (evt === 'joyst') {
        arg = `-p ,${val}`
    } else if (evt === 'sdial') {
        arg = `-d ${val}`
    }
    if (arg) rpcNotify(...`hid --ts ${Math.round(msec)} ${arg}`.split(' '))
    showEvent(`${evt} ${val}`)
}

watch(dev, val => val && (config.value.hidDev = val))

watch(data, val => {
    try {
        let len = hist.value.push(JSON.parse(val))
        let size = config.value.histSize
        if (len > size) hist.value.splice(0, len - size)
    } catch (err) {
        notify(err.message)
    }
})

const noscale = ', user-scalable=no'

onMounted(() => {
    rpcCall('hid')
        .then(rep => {
            if ((hid.value = !rep.toLowerCase().includes('command')))
                dev.value = config.value.hidDev || Object.keys(devs)[0]
        })
        .catch(() => notify('HID command not supported!'))
    let meta = document.querySelector('meta[name=viewport]')
    if (meta && !meta.content.includes(noscale)) meta.content += noscale
})

onBeforeUnmount(() => {
    let meta = document.querySelector('meta[name=viewport]')
    if (meta && meta.content.includes(noscale))
        meta.content = meta.content.replace(noscale, '')
})
</script>
