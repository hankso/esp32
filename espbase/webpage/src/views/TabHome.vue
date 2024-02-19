<template>
    <v-sheet border rounded="lg" class="overflow-hidden">
        <Terminal
            class="fix-height"
            :key="forceUpdate"
            :name="title"
            :title
            :context
            :initLog
            :inputFilter
            :commandStore
            contextSuffix=" "
            @execCmd="onCommand"
            @onKeydown="onKeydown"
        />
    </v-sheet>
</template>

<style>
.fix-height {
    min-height: 400px;
    max-height: 800px;
    height: 80vh !important;
}

.fix-height .t-last-line {
    margin: 1em 0;
}

.fix-height .t-prompt span {
    font-weight: 800;
}

.fix-height input,
.fix-height .t-header {
    line-height: normal;
}

.fix-height .t-cmd-help code,
.fix-height .t-example-li code {
    white-space: pre;
}
</style>

<script setup>
import { getConfig, getCommands, execCommand } from '@/apis'

import { Terminal, TerminalApi, TerminalFlash } from 'vue-web-terminal'
import 'vue-web-terminal/lib/theme/dark.css'

const notify = inject('notify', console.log)

const title = 'ESP Base'
const flash = Object.fromEntries([[title, null]])
const context = ref(title.replaceAll(' ', '').toLowerCase() + ' >')
const forceUpdate = ref(0)

const initLog = [
    `Welcome to ${title}!`,
    `Current login time: ${new Date().toLocaleString()}`,
    `This terminal emulator is connected to the HTTP server running on the
    <span class="text-cyan font-weight-bold">ESP32 chip</span>.
    Any commands typed here will be sent to backend and parsed by
    <span class="text-cyan font-weight-bold">argtable3</span>.`,
    `Type <span class="t-cmd-key">Ctrl + C</span> to interrupt command.
    Use <span class="t-cmd-key">help</span> command to show more info.`,
].map(content => ({ content }))

const commandStore = ref(
    [
        ['reload', 'Reload configuration of current terminal'],
        ['info', 'Get information of current window'],
        ['logs', 'Show supported logging levels'],
        ['ansi', 'Show supported ANSI colorful string'],
        ['tgfs', 'Toggle fullscreen of current terminal'],
    ].map(([key, description]) => ({
        key,
        group: 'local',
        usage: key,
        description,
    }))
)

const commands = computed(() => toValue(commandStore).map(obj => obj.key))

function refresh(name) {
    return Promise.all([getConfig(), getCommands()])
        .then(([cfg, cmds]) => {
            if (cfg.data['app.prompt']) context.value = cfg.data['app.prompt']
            cmds.data.forEach(cmd => {
                if (!toValue(commands).includes(cmd.key))
                    commandStore.value.push(cmd)
            })
            forceUpdate.value++
        })
        .catch(({ message }) => flash[name]?.flush(message) || notify(message))
        .finally(() => (flash[name] = flash[name]?.finish()))
}

function inputFilter(curChar, curString, e) {
    if (!e.isTrusted) return ''
    return curString.trimStart().replace(/[\u4e00-\u9fa5]/g, '')
}

function onKeydown(event, name) {
    if (!event.ctrlKey) return
    switch (event.key) {
        case 'l':
            TerminalApi.clearLog(name)
            break
        case 'C':
            flash[name] = flash[name]?.finish()
            document
                .querySelector('.fix-height .t-last-line input')
                ?.dispatchEvent(new InputEvent('input'))
            break
        default:
            return
    }
    event.preventDefault()
}

function escape(str, ...code) {
    return `\x1b[${code.join(';')}m${str}\x1b[0m`
}

function onCommand(key, cmdline, onSuccess, onFailed, name) {
    switch (key) {
        case 'echo':
            return onSuccess({ content: cmdline.replace(key, '') })
        case 'info':
            return onSuccess({
                type: 'json',
                content: JSON.stringify(TerminalApi.elementInfo(name)),
            })
        case 'logs':
            ;['success', 'error', 'system', 'info', 'warning'].forEach(cls =>
                TerminalApi.pushMessage(name, {
                    type: 'normal',
                    class: cls,
                    tag: cls,
                    content: `This is logging level ${cls}`,
                })
            )
            return onSuccess()
        case 'ansi':
            return onSuccess({
                type: 'ansi',
                content: [
                    'This terminal supports ANSI Escape Codes (\\x1b[xm).',
                    `However, it does ${escape('NOT', 31)} support cursor,
                    erase or scroll commands.`.replace(/\s{2,}/g, ' '),
                    escape('\tThis is blue string', 34),
                    escape('\tThis is blink text', 32, 5),
                    'This is xterm-256-color foreground:',
                    Array.from({ length: 256 }, (v, i) =>
                        escape((i % 64 ? '' : '\n') + '0', 38, 5, i)
                    ).join(''),
                    'This is xterm-256-color background:',
                    Array.from({ length: 256 }, (v, i) =>
                        escape((i % 64 ? '' : '\n') + ' ', 48, 5, i)
                    ).join(''),
                ].join('\n'),
            })
        case 'tgfs':
            let old = TerminalApi.isFullscreen(name)
            TerminalApi.fullscreen(name)
            let now = TerminalApi.isFullscreen(name)
            if (now === old) return onFailed('Could not toggle fullscreen')
            return onSuccess({
                type: 'normal',
                class: 'success',
                content: 'ok',
            })
        case 'reload':
            name += '-refresh'
            flash[name] = new TerminalFlash()
            refresh(name)
            return onSuccess(flash[name])
        default:
            if (!toValue(commands).includes(key))
                return onFailed(`Unknown command ${key}`)
    }
    if (flash[name])
        return onFailed('Command is still running (this should not happen)!')
    flash[name] = new TerminalFlash()
    let ts = new Date().getTime()
    execCommand(cmdline)
        .then(({ data }) => {
            let dt = (new Date().getTime() - ts) / 1e3
            TerminalApi.pushMessage(name, {
                type: 'ansi',
                content: data + escape(`\n\nDone in ${dt}s`, 32),
            })
        })
        .catch(({ message }) => flash[name].flush(message))
        .finally(() => (flash[name] = flash[name].finish()))
    return onSuccess(flash[name])
}

onMounted(refresh)
</script>
