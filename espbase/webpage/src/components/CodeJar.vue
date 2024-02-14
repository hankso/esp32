<template>
    <div ref="elem" :class="`jar-editor lang-${language}`"></div>
</template>

<script setup>
import { type, debounce } from '@/utils'

import { CodeJar } from 'codejar'
import { withLineNumbers } from 'codejar-linenumbers'

import { highlightElement as prism } from 'prismjs' // 3.6kB
// import 'prismjs/themes/prism.css' provided by vite.config.js:prism:css

var editor = null

const elem = ref()

const model = defineModel({
    type: String,
    default: 'Type in your code here',
})

const props = defineProps({
    language: String,
    readonly: Boolean,
    highlight: [Function, String, Boolean],
    lineNumber: Boolean,
})

const config = {
    // see https://github.com/antonmedv/codejar
    tab: ' '.repeat(4),
    spellcheck: true,
    catchTab: true,
}

function rainbow(ch, i) {
    let r = Math.round(Math.sin(0.01 * i + 0) * 127 + 128)
    let g = Math.round(Math.sin(0.01 * i + (2 * Math.PI) / 3) * 127 + 128)
    let b = Math.round(Math.sin(0.01 * i + (4 * Math.PI) / 3) * 127 + 128)
    return `<span style="color: rgb(${r}, ${g}, ${b})">${ch}</span>`
}

const highlighters = {
    prism,
    lolcat: e => (e.innerHTML = e.textContent.split('').map(rainbow).join('')),
}

const highlight = computed(() => {
    // parse highlight from user provided props
    let func = highlighters[props.highlight] || (() => {})
    if (type(props.highlight) === 'function') {
        func = props.highlight
    } else if (type(props.highlight) === 'boolean' && props.highlight) {
        func = highlighters['prism'] // default one
    }
    return props.lineNumber ? withLineNumbers(func) : func
})

function setReadonly(val) {
    if (!toValue(elem)) return // not ready yet
    elem.value.setAttribute('contenteditable', val ? false : 'plaintext-only')
}

function destroy() {
    editor?.destroy()
    let e = toValue(elem) // hotfix for codejar-linenumbers:exit
    if (e?.parentNode.classList.contains('codejar-wrap')) {
        e.style.paddingLeft = ''
        e.style.whiteSpace = 'pre-wrap'
        e.parentNode.replaceWith(e)
    }
    return e
}

function refresh() {
    if (!destroy()) return // not ready yet
    editor = CodeJar(toValue(elem), toValue(highlight), config)
    editor.updateCode(toValue(model))
    editor.onUpdate(debounce(() => (model.value = editor.toString())))
    setReadonly(props.readonly)
}

watch(highlight, refresh)

watch(() => props.readonly, setReadonly)

watch(model, val => {
    if (editor && editor.toString() !== val) {
        try {
            var pos = editor.save()
        } catch {
            var pos = null
        }
        editor.updateCode(val) // this will trigger editor.onUpdate
        pos && editor.restore(pos)
    }
})

watchPostEffect(() => {
    // highlight when class `lang-xxx` is applied to DOM
    if (props.language && editor) editor.updateCode(editor.toString())
})

onMounted(refresh)
onBeforeUnmount(destroy)
</script>

<style scoped>
.jar-editor[contenteditable='false'] {
    cursor: not-allowed;
}

.jar-editor {
    padding-left: 8px;
}
</style>

<style>
/* These elements are generated at runtime and not tracked by scoped CSS */
.jar-editor .token:before {
    opacity: 0.3;
}

.codejar-linenumbers-inner-wrap {
    position: absolute;
    z-index: 2;
}

.codejar-linenumbers {
    /* rgba(128, 128, 128, 0.15) => rgb(235, 235, 235) */
    background-color: rgb(var(--v-theme-surface-light)) !important;
}

.codejar-linenumber {
    text-align: right;
    padding-right: 5px;
}
</style>
