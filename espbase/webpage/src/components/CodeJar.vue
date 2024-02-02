<script setup>
import { CodeJar } from 'codejar'
import { withLineNumbers } from 'codejar-linenumbers'

import { highlightElement as prism } from 'prismjs' // 3.6kB
// import 'prismjs/themes/prism.css' provided by vite.config.js:prism:css

var editor = null

const elem = ref(null)

const model = defineModel({
    type: String,
    default: 'Type in your code here'
})

const props = defineProps({
    language: String,
    readonly: Boolean,
    highlight: [Function, String, Boolean],
    lineNumber: Boolean
})

const config = { // see https://github.com/antonmedv/codejar
    tab: ' '.repeat(4),
    spellcheck: true,
    catchTab: true
}

function rainbow(ch, i) {
    let r = Math.round(Math.sin(0.01 * i + 0) * 127 + 128);
    let g = Math.round(Math.sin(0.01 * i + 2 * Math.PI / 3) * 127 + 128);
    let b = Math.round(Math.sin(0.01 * i + 4 * Math.PI / 3) * 127 + 128);
    return `<span style="color: rgb(${r}, ${g}, ${b})">${ch}</span>`;
}

const highlighters = {
    prism, 'lolcat': editor => {
        editor.innerHTML = editor.textContent.split('').map(rainbow).join('')
    }
}

const highlight = computed(() => { // parse highlight from user provided props
    let func = highlighters[props.highlight] || (() => {})
    if (typeof props.highlight == 'function') {
        func = props.highlight
    } else if (typeof props.highlight == 'boolean' && props.highlight) {
        func = highlighters['prism'] // default one
    }
    return props.lineNumber ? withLineNumbers(func) : func
})

function setReadonly(val) {
    if (!elem.value) return // not ready yet
    if (val instanceof Array) // as watch callback
        val = val[0]
    elem.value.setAttribute('contenteditable', val ? false : 'plaintext-only')
}

function destroy() {
    editor && editor.destroy()
    let e = elem.value // hotfix for codejar-linenumbers:exit
    if (e && e.parentNode.classList.contains('codejar-wrap')) {
        e.style.paddingLeft = ''
        e.style.whiteSpace = 'pre-wrap'
        e.parentNode.replaceWith(elem.value)
    }
}

function refresh() {
    if (!elem.value) return // not ready yet
    destroy()
    editor = CodeJar(elem.value, highlight.value, config)
    editor.updateCode(model.value)
    editor.onUpdate(code => model.value = code)
    setReadonly(props.readonly)
}

onMounted(refresh)
onBeforeUnmount(destroy)
watch([() => props.readonly], setReadonly)
watch([() => props.language], () => editor.updateCode(editor.toString()))
watch([() => props.lineNumber, () => props.highlight], refresh)
</script>

<template>
    <div ref="elem" :class="`jar-editor lang-${language}`"></div>
</template>

<style scoped>
.jar-editor[contenteditable="false"] {
    cursor: not-allowed;
}

.jar-editor {
    padding-left: 8px;
}
</style>

<style>
.jar-editor .token:before {
    opacity: 0.3;
}

.codejar-linenumbers-inner-wrap {
    position: absolute;
}

.codejar-linenumber {
    text-align: right;
    padding-right: 5px;
}
</style>
