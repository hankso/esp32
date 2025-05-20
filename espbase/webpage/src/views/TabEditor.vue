<script setup>
import { readFile, uploadFile } from '@/apis'
import { cvtcase, copyToClipboard, downloadAsFile } from '@/utils'

import TabFileman from '@/views/TabFileman.vue'

import {
    mdiCounter,
    mdiDownload,
    mdiFormatColorText,
    mdiLockOutline as mdiLock,
    mdiFileTreeOutline as mdiFileTree,
    mdiClipboardTextOutline as mdiClipboardText,
    mdiClipboardCheckOutline as mdiClipboardCheck,
} from '@mdi/js'

import { basename, resolve, extname } from 'path-browserify'

const route = useRoute()
const notify = inject('notify', console.log)
const loading = inject('progbar')

const path = ref('demo.css')
const code = ref(`body {
    /* In readonly mode, you can visit this link
     * https://developer.mozilla.org/zh-CN/docs/Web/CSS
     */
    color: hsl(100, 70%, 40%);
    transform: rotate(45deg);
    transition: 3s;
}`)

const copied = ref(false)

const config = ref({
    readonly: false,
    highlight: true,
    lineNumber: true,
    treeView: false,
    language: 'css',
})

const propIcons = {
    readonly: mdiLock,
    highlight: mdiFormatColorText,
    lineNumber: mdiCounter,
    treeView: mdiFileTree,
}

const links = computed(() => {
    let chunks = toValue(path)
        .replace(/^\/|\/$/g, '')
        .split('/')
    return chunks.map((v, i) => ({
        title: v,
        href: `#${chunks.slice(0, i + 1).join('/')}`,
    }))
})

const copyIcon = computed(() =>
    toValue(copied) ? mdiClipboardCheck : mdiClipboardText
)

const uploadIcon = computed(() =>
    toValue(loading) === false ? '$complete' : '$close'
)

function copy() {
    copyToClipboard(toValue(code)).then(() => {
        copied.value = true
        setTimeout(() => (copied.value = false), 3000)
    })
}

function upload() {
    if (toValue(loading) !== false && upload.ctrl) return upload.ctrl.abort()
    loading.value = 0
    upload.ctrl = new AbortController()
    uploadFile(toValue(path), toValue(code), {
        signal: upload.ctrl.signal,
        onUploadProgress(e) {
            if (e.total === undefined) {
                loading.value = true
            } else {
                loading.value = e.progress * 100
            }
        },
    })
        .then(() => notify('Uploaded!'))
        .catch(({ message }) => notify(message))
        .finally(() => (loading.value = upload.ctrl = false))
}

watchPostEffect(() => {
    let filename = resolve('/', route.hash.slice(1))
    if (!filename.slice(1) || toValue(path) === filename) return
    readFile(filename)
        .then(resp => {
            path.value = filename
            code.value = resp.data
            location.hash = filename
            if (filename.endsWith('.gz'))
                filename = filename.slice(0, filename.length - 3)
            config.value.language = extname(filename).slice(1) || 'txt'
        })
        .catch(({ message }) => {
            notify(message)
            location.hash = ''
        })
})
</script>

<template>
    <div class="d-flex flex-column flex-lg-row-reverse ga-4">
        <v-scale-transition>
            <TabFileman
                v-show="config.treeView"
                :use-link="false"
                min-width="30%"
                height="40vh"
            />
        </v-scale-transition>

        <v-sheet border rounded="lg" class="flex-grow-1 overflow-hidden">
            <v-toolbar height="36" class="border-b">
                <v-breadcrumbs density="compact" :items="links"></v-breadcrumbs>
                <v-spacer></v-spacer>

                <span class="text-success d-none d-md-inline me-4">
                    {{ code.trim().split('\n').length }} lines -
                    {{ code.length }} bytes
                </span>

                <v-btn icon @click="upload">
                    <v-icon :icon="uploadIcon"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        {{ loading === false ? `Save as ${path}` : 'Cancel' }}
                    </v-tooltip>
                </v-btn>
                <v-btn icon @click="copy">
                    <v-icon :icon="copyIcon"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        {{ copied ? 'Copied!' : 'Copy to clipboard' }}
                    </v-tooltip>
                </v-btn>
                <v-btn icon @click="downloadAsFile(code, basename(path))">
                    <v-icon :icon="mdiDownload"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Download as {{ basename(path) }}
                    </v-tooltip>
                </v-btn>
                <v-btn
                    icon
                    v-for="(icon, prop) in propIcons"
                    :key="prop"
                    :color="config[prop] ? 'green' : ''"
                    @click="config[prop] = !config[prop]"
                >
                    <v-icon :icon="icon"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Turn {{ config[prop] ? 'off' : 'on' }}
                        {{ cvtcase(prop, 'title') }}
                    </v-tooltip>
                </v-btn>
            </v-toolbar>

            <CodeJar v-model="code" v-bind="config" />
        </v-sheet>
    </div>
</template>

<style scoped>
.v-btn {
    width: 36px;
    height: inherit;
    border-radius: 0;
    margin-inline-end: 0 !important;
    border-inline-start: thin solid
        rgba(var(--v-border-color), var(--v-border-opacity)) !important;
}
</style>
