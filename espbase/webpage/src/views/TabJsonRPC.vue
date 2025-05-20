<template>
    <v-row class="mb-1 pt-1">
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
            <v-col key="1" cols="12" :sm="dev ? 12 : 4">
                <v-select
                    v-if="hid"
                    label="HID input"
                    density="compact"
                    variant="outlined"
                    v-model="dev"
                    hide-details
                    clearable
                    :items="Object.keys(devs)"
                ></v-select>
            </v-col>
        </v-scale-transition>
    </v-row>
    <v-expand-transition>
        <component :is="devs[dev]" @event="onEvent" />
    </v-expand-transition>
    <v-snackbar
        v-if="config.showEvent"
        v-model="event.show"
        location="top right"
        min-width="auto"
        rounded="lg"
        class="ma-4"
        contained
        :timeout="event.timeout"
    >
        {{ event.message }}
    </v-snackbar>
</template>

<script setup>
import { random_id } from '@/utils'
import KeyBoard from '@/components/KeyBoard.vue'

import { mdiSendCircleOutline } from '@mdi/js'
import { useStorage, useWebSocket } from '@vueuse/core'

const devs = {
    Keyboard: KeyBoard,
    // TODO: Joystick, Mouse, S-Dial
}

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
    ...new Set(
        toValue(hist)
            .map(_ => _?.command)
            .filter(_ => _)
    ),
])

const notify = inject('notify', console.log)

const config = useStorage('hid', {
    hidDev: '',
    cmdTout: 5000,
    histSize: 20,
    showEvent: true,
})

const { data, send } = useWebSocket(
    `ws://${
        process.env.BUILD_INFO ? location.host : process.env.API_SERVER
    }/api/ws`,
    {
        autoReconnect: {
            retries: 3,
            delay: 1000,
            onFailed: () => alert('Failed to connect to WebSocket'),
        },
    }
)

function showEvent(msg, timeout = 3000) {
    if (!toValue(config).showEvent) return
    event.value.message = `${msg}`
    event.value.timeout = timeout - 1
    event.value.show = true
    nextTick(() => (event.value.timeout = timeout))
}

function rpcCall(method) {
    let req = { id: random_id(), method, params: Array(...arguments).slice(1) }
    send(JSON.stringify(req))
    return new Promise((resolve, reject) =>
        (function wait(tout) {
            for (let rep of toValue(hist)) {
                if (req.id !== rep?.id) continue
                rep.command = [req.method, ...req.params].join(' ')
                return resolve(`${rep?.result ?? rep.error}`)
            }
            if (tout < 0) return reject('Timeout')
            setTimeout(wait, 500, tout - 500)
        })(toValue(config).cmdTout)
    )
}

function rpcNotify(method) {
    send(JSON.stringify({ method, params: Array(...arguments).slice(1) }))
}

function runCommand() {
    let args = (toValue(cmd) || '').split(' ').filter(_ => _)
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
        [-d BLRUD] [-t 0-65535]
        [--ts MSEC] [--to 0-2|UBS]
 */

function onEvent(event, data, msec) {
    if (event === 'keydn') onKeyDn(data, msec)
}

function onKeyDn(key, msec) {
    rpcNotify(...`hid -k ${key} --ts ${msec}`.split(' '))
    key = key.replace(/(^|\W)\w(?=\w|-)/g, m => m.toUpperCase())
    showEvent(key.replace(/\|/g, ' + '))
}

watch(dev, val => val && (config.value.hidDev = val))

watch(data, val => {
    try {
        let len = hist.value.push(JSON.parse(val))
        let size = toValue(config).histSize
        if (len > size) hist.value.splice(0, len - size)
    } catch (err) {
        notify(err.message)
    }
})

onMounted(() => {
    rpcCall('hid')
        .then(rep => {
            if ((hid.value = !rep.toLowerCase().includes('command')))
                dev.value = toValue(config)?.hidDev || Object.keys(devs)[0]
        })
        .catch(() => notify('HID command not supported!'))
})
</script>
