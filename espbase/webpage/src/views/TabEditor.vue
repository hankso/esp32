<script setup>
import { readFile, uploadFile } from '@/apis'
import { copyToClipboard, downloadAsFile, camelToSnake } from '@/utils'

import {
    mdiUpload,
    mdiDownload,
    mdiLockOutline,
    mdiClipboardTextOutline,
    mdiClipboardCheckOutline,
    mdiFormatColorText,
    mdiCounter,
    mdiFileTreeOutline,
} from '@mdi/js'

import { basename, dirname, resolve, extname } from 'path-browserify'

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
const loading = ref(false)

const config = ref({
    readonly: false,
    highlight: true,
    lineNumber: true,
    treeView: false,
    language: 'css',
})

const propIcons = {
    readonly: mdiLockOutline,
    highlight: mdiFormatColorText,
    lineNumber: mdiCounter,
    treeView: mdiFileTreeOutline,
}

const links = computed(() => {
    let chunks = path.value.replace(/^\/|\/$/g, '').split('/')
    return chunks.map((v, i) => ({
        title: v,
        href: `#${chunks.slice(0, i + 1).join('/')}`,
    }))
})

const select = computed(() => {
    return Object.values(config.value).map((v, i) => (v === true ? i : ''))
})

const copyIcon = computed(() => {
    return copied.value ? mdiClipboardCheckOutline : mdiClipboardTextOutline
})

function copy() {
    copyToClipboard(code.value)
    copied.value = true
    setTimeout(() => (copied.value = false), 3000)
}

const notify = inject('notify', console.log)

function upload() {
    let ctrl = new AbortController()
    loading.value = true
    uploadFile(path.value, code.value, {
        signal: ctrl.signal,
        onUploadProgress(e) {
            // TODO: progressbar
            console.log(`
                total size: ${e.total}
                loaded size: ${e.loaded}
                percentage: ${e.progress}
                current packet size: ${e.bytes}
                transfer speed: ${e.rate}B/s
                left time: ${e.estimated}s
            `)
        },
    })
        .then(() => notify('Uploaded!'))
        .catch(({ message }) => notify(message))
        .finally(() => (loading.value = false))
    // call ctrl.abort() to cancel uploading
}

const route = useRoute()

watch(
    () => route.hash,
    () => {
        let filename = resolve('/', route.hash.slice(1))
        if (!filename.slice(1) || path.value === filename) return
        readFile(filename)
            .then(resp => {
                path.value = filename
                code.value = resp.data
                config.value.language = extname(filename).slice(1) || 'txt'
                location.hash = filename.slice(1)
            })
            .catch(err => {
                console.error(err.message)
                location.hash = ''
            })
    },
    { immediate: true, flush: 'post' }
)
</script>

<template>
    <div class="d-flex flex-column flex-lg-row-reverse ma-4 ga-4">
        <v-fade-transition>
            <Frame
                src="fileman"
                class="border rounded-lg"
                v-show="config.treeView"
            />
        </v-fade-transition>

        <v-sheet
            border
            rounded="lg"
            elevation="1"
            class="flex-grow-1 overflow-hidden"
        >
            <v-toolbar density="comfortable" class="border-b">
                <v-tooltip location="bottom">
                    <template #activator="{ props }">
                        <v-btn
                            class="ms-0 me-n4"
                            :icon="mdiUpload"
                            :loading
                            @click="upload"
                            v-bind="props"
                        ></v-btn>
                    </template>
                    Upload as {{ path }}
                </v-tooltip>

                <v-breadcrumbs :items="links"></v-breadcrumbs>

                <v-spacer></v-spacer>

                <span class="text-error d-none d-md-inline">
                    {{ code.trim().split('\n').length }} lines -
                    {{ code.length }} bytes
                </span>

                <v-btn-group
                    divided
                    rounded="s-lg e-0"
                    density="comfortable"
                    variant="outlined"
                >
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
                </v-btn-group>

                <v-btn-toggle
                    :modelValue="select"
                    rounded="s-0 e-lg"
                    density="comfortable"
                    variant="outlined"
                    color="primary"
                    class="me-2"
                    multiple
                    divided
                >
                    <v-btn
                        icon
                        v-for="(icon, prop) in propIcons"
                        @click="config[prop] = !config[prop]"
                    >
                        <v-icon :icon="icon"></v-icon>
                        <v-tooltip activator="parent" location="bottom">
                            Turn {{ config[prop] ? 'off' : 'on' }}
                            {{ camelToSnake(prop, ' ') }}
                        </v-tooltip>
                    </v-btn>
                </v-btn-toggle>
            </v-toolbar>
            <CodeJar v-model="code" v-bind="config" />
        </v-sheet>
    </div>
</template>

<style scoped>
.v-btn-group .v-btn {
    padding: 0 20px;
}
</style>
