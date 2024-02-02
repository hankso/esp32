<script setup>
import {
    mdiUpload, mdiDownload, mdiLockOutline,
    mdiClipboardTextOutline, mdiClipboardCheckOutline,
    mdiFormatColorText, mdiCounter, mdiFileTreeOutline,
} from '@mdi/js'
import { copyToClipboard, downloadAsFile } from '@/utils'
import axios from 'axios'

const path = ref('assets/test.css')
const code = ref(`body {
    /* In readonly mode, you can visit this link
     * https://developer.mozilla.org/zh-CN/docs/Web/CSS
     */
    color: hsl(100, 70%, 40%);
    transform: rotate(45deg);
    transition: 3s;
}`)

const config = ref({
    readonly: false,
    highlight: true,
    lineNumber: true,
    treeView: false,
    language: 'css',
})

const copied = ref(false)
const uploading = ref(false)

const links = computed(() => {
    let chunks = path.value.replace(/^\//, '').split('/')
    return chunks.map((v, i) => ({
        title: v, href: `#${chunks.slice(0, i + 1).join('/')}`
    }))
})
const dirname = computed(() => {
    return path.value.substr(0, path.valueLastIndexOf('/') + 1)
})
const basename = computed(() => {
    return path.value.substr(path.value.lastIndexOf('/') + 1)
})
const selected = computed(() => {
    return Object.values(config.value).map((v, i) => v === true ? i : '')
})

function copy() {
    copyToClipboard(code.value)
    copied.value = true
    setTimeout(() => copied.value = false, 3000)
}

function upload() {
    uploading.value = true
    // TODO: use gzip to compress file
    let formData = new FormData();
    fetch('/editu', {
        method: 'POST',
        body: formData,
    })
    .then(resp => {
        if (!resp.ok)
            throw new Error('')
    })
    .catch(err => console.log(err))
}

function camelToSnake(s, sep='_') {
    return s.replace(/([a-z])([A-Z])/g, `$1${sep}$2`).toLowerCase()
}

const propIcons = {
    readonly: mdiLockOutline,
    highlight: mdiFormatColorText,
    lineNumber: mdiCounter,
    treeView: mdiFileTreeOutline,
}
</script>

<template>
<v-sheet border rounded="lg" elevation=1 class="ma-2 overflow-hidden">
    <v-toolbar density="comfortable" class="border-b">

        <v-tooltip text="save" location="bottom">
            <template #activator="{ props }">
                <v-btn
                    class="ms-0 me-n4"
                    :loading="uploading"
                    :icon="mdiUpload"
                    @click="upload"
                    v-bind="props"
                ></v-btn>
            </template>
        </v-tooltip>

        <v-breadcrumbs :items="links"></v-breadcrumbs>

        <v-spacer></v-spacer>

        <span class="text-error d-none d-md-inline">
            {{ code.split('\n').length }} lines
            - {{ code.length }} bytes
        </span>

        <v-btn-group
            divided
            rounded="s-lg e-0"
            density="comfortable"
            variant="outlined"
        >
            <v-btn icon @click="copy">
                <v-icon :icon="copied ? mdiClipboardCheckOutline :
                                        mdiClipboardTextOutline"></v-icon>
                <v-tooltip activator="parent" location="bottom">
                    {{ copied ? "Copied!" : "Copy to clipboard" }}
                </v-tooltip>
            </v-btn>

            <v-btn icon @click="downloadAsFile(code, basename)">
                <v-icon :icon="mdiDownload"></v-icon>
                <v-tooltip activator="parent" location="bottom">
                    Download as {{ basename }}
                </v-tooltip>
            </v-btn>
        </v-btn-group>

        <v-btn-toggle
            multiple divided
            :modelValue="selected"
            rounded="s-0 e-lg"
            density="comfortable"
            variant="outlined"
            color="primary"
            class="me-2"
        >
            <v-btn icon v-for="(icon, prop) in propIcons"
                @click="config[prop] = !config[prop]"
            >
                <v-icon :icon="icon"></v-icon>
                <v-tooltip activator="parent" location="bottom">
                    Toggle {{ camelToSnake(prop, ' ') }}
                </v-tooltip>
            </v-btn>
        </v-btn-toggle>

    </v-toolbar>
    <CodeJar v-model="code" v-bind="config" />
</v-sheet>
</template>
