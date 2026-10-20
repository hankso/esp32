<template>
    <div class="fix-keyboard">
        <v-textarea
            v-if="showInput"
            v-model="text"
            ref="input"
            rows="1"
            max-rows="5"
            auto-grow
            auto-focus
            hide-details
            variant="outlined"
            :clearable="!text.includes('\n')"
            placeholder="Tap on the keyboard to type in"
        ></v-textarea>
        <div :class="uid"></div>
    </div>
</template>

<script setup>
import { range, randomId } from '@/utils'

import Keyboard from 'simple-keyboard'
import 'simple-keyboard/build/css/index.css'

var kbd = null

const alt = ref(0)
const ctrl = ref(0)
const shift = ref(0)

const input = ref(null)

const text = defineModel({
    type: String,
    default: '',
})

const emits = defineEmits(['event', 'keydn', 'keyup'])

const props = defineProps({
    className: {
        type: String,
        default: 'keyboard',
    },
    showInput: {
        type: Boolean,
        default: false,
    },
    showPress: {
        type: Boolean,
        default: true,
    },
    handleAlt: {
        type: Boolean,
        default: true,
    },
    handleCtrl: {
        type: Boolean,
        default: true,
    },
    handleShift: {
        type: Boolean,
        default: true,
    },
    handleCaps: {
        type: Boolean,
        default: true,
    },
    splitEvents: {
        type: Boolean,
        default: false,
    },
})

const uid = computed(() => `${props.className}-${randomId()}`)

const area = computed(() => input.value?.$el?.querySelector('textarea'))

const shiftMapping = {
    '{f3}': '{mediaplaypause}',
    '{f4}': '{audiovolumemute}',
    '{f5}': '{audiovolumedown}',
    '{f6}': '{audiovolumeup}',
    '{f7}': '{prtscr}',
    '{f8}': '{home}',
    '{f9}': '{end}',
    '{f10}': '{pageup}',
    '{f11}': '{pagedown}',
    '{f12}': '{insert}',
    ...Object.fromEntries(
        "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./"
            .split('')
            .map((e, i) => [
                e,
                '~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:"ZXCVBNM<>?'[i],
            ])
    ),
}

const defaultLayout = [
    '{escape} ' +
        range(1, 13)
            .map(i => `{f${i}}`)
            .join(' ') +
        ' {delete}',
    '` 1 2 3 4 5 6 7 8 9 0 - = {backspace}',
    '{tab} q w e r t y u i o p [ ] \\',
    "{capslock} a s d f g h j k l ; ' {enter}",
    '{shiftleft} z x c v b n m , . / {arrowup} {shiftright}',
    '{controlleft} {metaleft} {altleft} {space} ' +
        '{altright} {contextmenu} {arrowleft} {arrowdown} {arrowright}',
]

function syncState(event) {
    if (!props.showPress) return
    if (event.getModifierState('Alt')) {
        alt.value ||= 1
    } else if (alt.value === 1) {
        alt.value = 0
    }
    if (event.getModifierState('Control')) {
        ctrl.value ||= 1
    } else if (ctrl.value === 1) {
        ctrl.value = 0
    }
    if (event.getModifierState('CapsLock')) {
        shift.value = 2
    } else if (shift.value === 2) {
        shift.value = 0
    }
    if (event.getModifierState('Shift')) {
        shift.value = shift.value === 2 ? 0 : 1
    } else if (shift.value === 1) {
        shift.value = 0
    }
    event.preventDefault()
}

function emitKeyCode(event, key, ts) {
    key = key.replace(/^{|arrow|context|audio|}$/g, '')
    key = key.replace(/^(\w+)(left|right)$/g, (m, g1, g2) => `${g2[0]}-${g1}`)
    if (props.handleAlt && alt.value && !key.includes('alt')) key = 'Alt|' + key
    if (props.handleCtrl && ctrl.value && !key.includes('control'))
        key = 'Ctrl|' + key
    if (props.splitEvents) emits(event, key, ts)
    emits('event', event, key, ts)
}

function onKeyReleased(key) {
    let ts = performance.now()
    if (props.handleAlt && key.includes('alt')) {
        alt.value = (alt.value + 1) % 3
    } else if (alt.value === 1) {
        alt.value = 0
    }
    if (props.handleCtrl && key.includes('control')) {
        ctrl.value = (ctrl.value + 1) % 3
    } else if (ctrl.value === 1) {
        ctrl.value = 0
    }
    if (props.handleShift && key.includes('shift')) {
        shift.value = (shift.value + 1) % 3
    } else if (props.handleCaps && key.includes('caps')) {
        shift.value = shift.value === 2 ? 0 : 2
    } else if (shift.value === 1) {
        shift.value = 0
    }
    emitKeyCode('keyup', key, ts)
}

function onChange(value) {
    text.value = value
    if (!props.showInput || !area.value) return
    let pos = [kbd.getCaretPosition(), kbd.getCaretPositionEnd()]
    if (!pos[0]) return
    setTimeout(() => area.value.setSelectionRange(...pos), 5)
}

watch(text, val => props.showInput && kbd?.setInput(val))

watch(shift, val => kbd?.setOptions({ layoutName: ['default', 's', 'c'][val] }))

watchPostEffect(() => {
    kbd?.destroy()
    kbd = new Keyboard(uid.value, {
        display: {
            '{enter}': 'Enter',
            '{prtscr}': 'PrtScn',
            '{pageup}': 'PgUp',
            '{pagedown}': 'PgDn',
            '{altleft}': 'Alt',
            '{altright}': 'Alt',
            '{metaleft}': '&#x2318',
            '{metaright}': '&#x2318',
            '{arrowup}': '&#x25B2',
            '{arrowdown}': '&#x25BC',
            '{arrowleft}': '&#x25C0',
            '{arrowright}': '&#x25B6',
            '{controlleft}': 'Ctrl',
            '{controlright}': 'Ctrl',
            '{contextmenu}': '&#x1F5C9',
            '{mediaplaypause}': '&#x23EF',
            '{audiovolumemute}': '&#x1F508',
            '{audiovolumedown}': '&#x1F509',
            '{audiovolumeup}': '&#x1F50A',
        },
        layout: {
            default: defaultLayout,
            s: defaultLayout.map(l =>
                l
                    .split(' ')
                    .map(k => shiftMapping[k] ?? k)
                    .join(' ')
            ),
            c: defaultLayout.map(l =>
                l
                    .split(' ')
                    .map(k => ('a' <= k && k <= 'z' ? shiftMapping[k] : k))
                    .join(' ')
            ),
        },
        physicalKeyboardHighlightBgColor: 'rgb(var(--v-theme-surface-light))',
        physicalKeyboardHighlightPress: !props.showInput,
        physicalKeyboardHighlightTextColor: 'inherit',
        physicalKeyboardHighlight: props.showPress,
        preventMouseDownDefault: true,
        preventMouseUpDefault: true,
        newLineOnEnter: true,
        mergeDisplay: true,
        maxLength: 100,
        onKeyReleased,
        onKeyPress: key => emitKeyCode('keydn', key, performance.now()),
        onChange,
    })
    if (!props.showPress) return
    let elem = document
    if (!props.showInput) {
        elem = kbd.keyboardDOM
        elem.tabIndex = 0
        elem.onblur = e => e.target.focus()
        elem.focus()
    }
    for (let event of ['keydown', 'keyup']) {
        elem.addEventListener(event, syncState)
    }
})

onBeforeUnmount(() => {
    for (let event of ['keydown', 'keyup']) {
        document.removeEventListener(event, syncState)
    }
    kbd?.destroy()
})
</script>

<style>
.fix-keyboard .hg-button {
    border: 0px;
    min-width: 30px;
    background: rgb(var(--v-theme-background));
    font-family: monospace;
}
.fix-keyboard .hg-theme-default {
    background: rgb(var(--v-theme-on-surface-variant));
}
.fix-keyboard .hg-button.hg-activeButton {
    background: rgb(var(--v-theme-surface-light));
}
.fix-keyboard .hg-button.hg-functionBtn {
    text-transform: capitalize;
}
.fix-keyboard .hg-button.hg-button-space {
    flex-basis: 24%;
}
.fix-keyboard .hg-button[data-skbtn='@'] {
    max-width: unset;
}
.fix-keyboard .hg-button[data-skbtn*='menu'] {
    font-size: x-large;
}
.fix-keyboard .hg-button[data-skbtn*='alt'] {
    text-decoration: v-bind('["inherit", "underline", "overline"][alt % 3]');
}
.fix-keyboard .hg-button[data-skbtn*='shift'] {
    text-decoration: v-bind('["inherit", "underline", "overline"][shift % 3]');
}
.fix-keyboard .hg-button[data-skbtn*='control'] {
    text-decoration: v-bind('["inherit", "underline", "overline"][ctrl % 3]');
}
.fix-keyboard .hg-button[data-skbtn*='lock'] {
    text-decoration: v-bind('shift === 2 ? "overline" : "inherit"');
}
</style>
