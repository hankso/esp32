<template>
    <T
        ref="instance"
        class="fix-terminal"
        :key="forceUpdate"
        :name="title"
        :title
        :context
        :init-log
        :input-filter
        :command-store
        context-suffix=""
        :enable-default-command="defaultCommands"
        @exec-cmd="onCommand"
        @on-keydown="onKeydown"
        @on-click="(event, name) => emits('click', event, name)"
    >
        <template #header>
            <slot name="header"></slot>
        </template>
    </T>
</template>

<script setup>
import { type, escape } from '@/utils'

import { Terminal as T, TerminalApi, TerminalFlash } from 'vue-web-terminal'
import 'vue-web-terminal/lib/theme/dark.css'

const emits = defineEmits(['keydown', 'click'])
const props = defineProps({
    title: {
        type: String,
        default: 'Terminal',
    },
    prompt: {
        type: String,
        default: undefined,
    },
    welcome: {
        type: [Array, Boolean, String],
        default: undefined,
    },
    callback: {
        type: Function,
        default: undefined,
    },
    commands: {
        type: Array,
        default: []
    },
    defaultCommands: {
        type: Boolean,
        default: true,
    },
})

const flash = Object.fromEntries([[props.title, null]])
const instance = ref(null)
const forceUpdate = ref(0)
const commandStore = ref([])

const context = computed(() => (
    props.prompt ?? (props.title.split(' ')[0].toLowerCase() + ' > ')
))

const initLog = computed(() => {
    if (type(props.welcome) === 'boolean' && !props.welcome) return []
    if (type(props.welcome) === 'array') return props.welcome
    let logs = [
        `Welcome to ${props.title}!`,
        `Current login time: ${new Date().toLocaleString()}`,
        `Type <span class="t-cmd-key">Ctrl + C</span> to interrupt command.
        Use <span class="t-cmd-key">help</span> command to show more info.`,
    ].map(content => ({ content }))
    if (type(props.welcome) === 'string') logs.push({ content: props.welcome })
    return logs
})

const basicCommands = computed(() => {
    if (!props.defaultCommands) return []
    return [
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
})

const allCommands = computed(() => toValue(commandStore).map(_ => _.key))

watch(
    [() => props.defaultCommands, () => props.commands, () => props.welcome],
    () => {
        commandStore.value.length = 0
        commandStore.value.push(...toValue(basicCommands))
        props.commands.forEach(cmd => {
            let idx = toValue(allCommands).indexOf(cmd.key)
            if (idx < 0) {
                commandStore.value.push(cmd)
            } else {
                commandStore.value.splice(1, 1, cmd)
            }
        })
        forceUpdate.value++
    },
    { deep: true, immediate: true }
)

function inputFilter(curChar, curString, e) {
    if (!e.isTrusted) return ''
    return curString.trimStart().replace(/[\u4e00-\u9fa5]/g, '')
}

function onKeydown(event, name) {
    emits('keydown', event, name)
    if (!event.ctrlKey) return
    if (event.key === 'l') {
        TerminalApi.clearLog(name)
    } else if (event.key === 'C') {
        flash[name] = flash[name]?.finish()
        toValue(instance).$el
            ?.querySelector('.t-last-line input.t-cmd-input')
            ?.dispatchEvent(new InputEvent('input'))
    } else return
    event.preventDefault()
}

function onCommand(key, cmdline, success, failed, name) {
    switch (key) {
        case 'echo':
            return success({ content: cmdline.replace(key, '') })
        case 'info':
            return success({
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
            return success()
        case 'ansi':
            return success({
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
            TerminalApi.fullscreen(name)
            return success({
                type: 'normal',
                class: 'success',
                content: 'ok',
            })
        default:
            let cmds = toValue(allCommands)
            if (cmds.length && !cmds.includes(key))
                return failed(`Unknown command ${key}`)
            if (!props.callback)
                return failed(`Unhandled command ${key}`)
    }
    if (flash[name])
        return failed('Command is still running (this should not happen)!')
    flash[name] = new TerminalFlash()
    let ts = new Date().getTime()
    let rst = props.callback(key, cmdline, success, failed, name)
    if (type(rst) === 'promise') {
        rst
            .then(onSuccess(ts, name))
            .catch(err => flash[name].flush(err))
            .finally(() => (flash[name] = flash[name].finish()))
        return success(flash[name])
    }
    if (rst instanceof Error) {
        failed(rst)
    } else if (rst) {
        onSuccess(ts, name)(rst)
        success()
    }
    flash[name] = flash[name].finish()
}

function onSuccess(ts, name) {
    return data => {
        let dt = (new Date().getTime() - ts) / 1e3
        if (type(data) === 'object' && data.hasOwnProperty('type')) {
            TerminalApi.pushMessage(name, data)
        } else {
            TerminalApi.pushMessage(name, {
                type: 'ansi',
                content: data.toString() + escape(`\n\nDone in ${dt}s`, 32),
            })
        }
    }
}

defineExpose({
    flush: (msg, name) => flash[name ?? props.title]?.flush(msg),
    finish: (name = props.title) => (flash[name] = flash[name]?.finish()),
    pushMessage: msg => TerminalApi.pushMessage(props.title, msg),
    appendMessage: msg => TerminalApi.appendMessage(props.title, msg),
})
</script>

<style>
.fix-terminal {
    min-height: 400px;
    max-height: 800px;
}

.fix-terminal .t-last-line {
    /* fix vuetify css `* { margin: 0; }` */
    margin: 1em 0;
}

.fix-terminal .t-shell-dots {
    /* align dots position */
    left: 2px !important;
}

.fix-terminal input,
.fix-terminal .t-header {
    /* fix base.css `body { line-height: 1.6; }` */
    line-height: normal;
}

.fix-terminal .t-prompt span {
    /* fix vuetify css `* { font-weight: normal; }` */
    font-weight: 800;
}

.fix-terminal .t-cmd-help code,
.fix-terminal .t-example-li code {
    /* display \n and \t */
    white-space: pre;
}
</style>
