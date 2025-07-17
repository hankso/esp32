<template>
    <v-sheet border rounded="lg" class="overflow-hidden h-100">
        <CommandLine :title :prompt :welcome :commands :callback />
    </v-sheet>
</template>

<script setup>
import { getConfig, getCommands, execCommand } from '@/apis'

const notify = inject('notify', console.log)

const title = `${process.env.PROJECT_NAME} Console`

const welcome = `
This terminal emulator is connected to the HTTP server running on the
<span class="text-cyan font-weight-bold">ESP32 chip</span>.<br>
Commands typed here will be sent to the backend and parsed by
<span class="text-cyan font-weight-bold">argtable3</span>.
`

const prompt = ref()
const commands = ref([
    {
        key: 'reload',
        group: 'web',
        usage: 'reload',
        description: 'Reload configuration of current terminal',
    },
])

function refresh() {
    let opt = { timeout: 0 }
    return Promise.all([getConfig(opt), getCommands(opt)])
        .then(([cfg, cmds]) => {
            if (cfg.data['app.prompt']) prompt.value = cfg.data['app.prompt']
            cmds.data.forEach(cmd => {
                let idx = commands.value.map(_ => _.key).indexOf(cmd.key)
                if (idx < 0) {
                    commands.value.push(cmd)
                } else {
                    commands.value.splice(1, 1, cmd)
                }
            })
        })
        .catch(({ message }) => notify(message))
}

function callback(key, cmdline) {
    if (key === 'reload') return refresh()
    return execCommand(cmdline)
        .then(({ data }) => data)
        .catch(({ message }) => {
            throw new Error(message)
        })
}

onMounted(refresh)
</script>
