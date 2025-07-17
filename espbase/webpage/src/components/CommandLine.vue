<template>
    <Terminal
        ref="instance"
        class="command-line fix-terminal"
        :key="forceUpdate"
        :name="title"
        :theme="isDark ? 'dark' : 'light'"
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
            <slot name="header" />
        </template>
    </Terminal>
</template>

<script setup>
import { type, escape, strftime, getMonotonic } from '@/utils'

import { Terminal, TerminalApi, TerminalFlash } from 'vue-web-terminal'

const emits = defineEmits(['keydn', 'click'])
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
        default: () => [],
    },
    defaultCommands: {
        type: Boolean,
        default: true,
    },
    usedTime: {
        type: Boolean,
        default: true,
    },
})

const flash = Object.fromEntries([[props.title, null]])
const instance = ref(null)
const forceUpdate = ref(0)
const commandStore = ref([])

const { isDark } = inject('theme', {})

const context = computed(
    () => props.prompt ?? props.title.split(' ')[0].toLowerCase() + ' > '
)

const initLog = computed(() => {
    if (type(props.welcome) === 'boolean' && !props.welcome) return []
    if (type(props.welcome) === 'array') return props.welcome
    let logs = [
        `Welcome to ${props.title}!`,
        strftime('Current login time: %F %T'),
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
    emits('keydn', event, name)
    if (!event.ctrlKey) return
    if (event.key === 'l') {
        // Ctrl-l to clear screen
        TerminalApi.clearLog(name)
    } else if (event.key === 'C') {
        // Ctrl-C to interrupt current command
        flash[name] = flash[name]?.finish()
        toValue(instance)
            .$el?.querySelector('.t-last-line input.t-cmd-input')
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
                    content: `Logging at ${cls} level`,
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
    }
    if (toValue(allCommands).length && !toValue(allCommands).includes(key))
        return failed(`Unknown command ${key}`)
    if (!props.callback) return failed(`Unhandled command ${key}`)
    if (flash[name]) return failed('Command running (this should not happen)!')
    flash[name] = new TerminalFlash()
    let ts = getMonotonic()
    let rst = props.callback(key, cmdline, success, failed, name)
    if (type(rst) === 'promise') {
        rst.then(onSuccess(ts, name))
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
        if (type(data) === 'object' && 'content' in data) {
            TerminalApi.pushMessage(name, data)
        } else if (data) {
            TerminalApi.pushMessage(name, { type: 'ansi', content: data })
        }
        if (!props.usedTime) return
        TerminalApi.pushMessage(name, {
            type: 'ansi',
            content: escape(`Used ${(getMonotonic() - ts).toFixed(0)}ms`, 32),
        })
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
