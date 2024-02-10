<script setup>
import { type, rules, debounce } from '@/utils'
import { listDir, uploadFile, createPath, deletePath } from '@/apis'

import {
    mdiUpload,
    mdiFolderPlus,
    mdiDelete,
    mdiPencilBoxMultiple,
} from '@mdi/js'
import { join, resolve, basename } from 'path-browserify'

const route = useRoute()
const notify = inject('notify', console.log)
const confirm = inject('confirm', (p, f) => (window.confirm(p) && f()))

const props = defineProps({
    useLink: {
        type: Boolean,
        default: true,
    },
})

const root = ref(resolve('/', route.query.root || ''))
const items = ref([])
const select = ref(route.hash ? [route.hash.slice(1)] : [])

const loading = ref(false)
const form = ref({
    create: false,
    fileName: '',
    fileList: [],

    upload: false,
    folderName: '',
    folderRoot: '/',
})

const title = computed(() => {
    let arr = toValue(select)
    if (!arr.length) return `Files under ${toValue(root)}`
    if (arr.length === 1) return arr[0]
    return `Selected ${arr.length} files`
})

const prompt = computed(() => {
    let arr = toValue(select)
    if (!arr.length) return
    if (arr.length === 1) return `Delete ${arr[0]} permanently?`
    return ['Delete these files permanently?', ...arr].join('\n - ')
})

const folders = computed(() => {
    let nodes = ['/']
    ;(function findFolder(arr) {
        for (let node of arr) {
            if (node.type === 'folder') {
                nodes.push(node.link)
                findFolder(node.children || [])
            }
        }
    })(toValue(items))
    return nodes
})

function refresh(path, target, parent) {
    if (path === undefined) path = toValue(root)
    if (target === undefined) target = toValue(items)
    if (type(path) === 'object') {
        parent = path
        if (parent.type !== 'folder') return
        if ((parent.id || '').split('-').length > 5)
            return notify('Too deep into subfolders. Maybe recursive loop?')
        path = parent.link
        parent.children = parent.children || [] // let vue track this new array
        target = parent.children
    } else if (type(path) !== 'string') {
        return
    }
    listDir(path)
        .then(resp => {
            let nodes = resp.data.map((node, idx) => ({
                ...node,
                id: parent ? `${parent.id}-${idx}` : `${idx}`,
                link: join(parent ? parent.link : '/', node.name),
            }))
            target.splice(0, target.length, ...nodes)
            target.forEach(refresh)
        })
        .catch(({ message }) => notify(message))
}

function remove(arr) {
    let len = arr.length
    loading.value = true
    arr.forEach(path =>
        deletePath(path)
            .then(() => arr.remove(path))
            .catch(({ message }) => notify(message))
            .finally(() => {
                if (--len) return
                loading.value = false
                notify('Deleted!')
                refresh()
            })
    )
}

function create(validation) {
    if (validation && !validation.valid) return
    let data = toValue(form)
    loading.value = true
    createPath(resolve(data.folderRoot, data.folderName))
        .then(() => notify('Created!'))
        .catch(({ message }) => notify(message))
        .finally(() => {
            loading.value = false
            form.value.create = false
            refresh()
        })
}

function upload(validation) {
    if (validation && !validation.valid) return
    let data = toValue(form)
    let len = data.fileList.length
    loading.value = true
    data.fileList.forEach(file => {
        let filename = file.name
        if (data.fileList.length === 1 && data.fileName)
            filename = data.fileName
        uploadFile(resolve(data.folderRoot, filename), file)
            .then(() => data.fileList.remove(file))
            .catch(({ message }) => notify(message))
            .finally(() => {
                if (--len) return
                loading.value = false
                form.value.upload = false
                refresh()
            })
    })
}

watch(
    select,
    debounce(arr => {
        if (props.useLink) location.hash = arr.length !== 1 ? '' : arr[0]
    }),
    { deep: true }
)

onMounted(refresh)
</script>

<template>
    <v-sheet border rounded="lg" elevation="1" class="ma-4 overflow-x-hidden">
        <v-dialog
            v-model="form.create"
            min-width="300"
            width="auto"
            persistent
        >
            <v-card title="Create new folder">
                <v-card-text>
                    <v-form ref="formCreate">
                        <v-select
                            label="Root *"
                            v-model="form.folderRoot"
                            :rules="[rules.required]"
                            :items="folders"
                            variant="outlined"
                            autofocus
                            required
                        ></v-select>
                        <v-text-field
                            label="Name *"
                            v-model="form.folderName"
                            :rules="[rules.required]"
                            variant="outlined"
                            required
                        ></v-text-field>
                    </v-form>
                    <small>* indicates required field</small>
                </v-card-text>
                <v-card-actions>
                    <v-spacer></v-spacer>
                    <v-btn @click="form.create = false">Cancel</v-btn>
                    <v-btn
                        :loading
                        @click="$refs.formCreate.validate().then(create)"
                    >
                        Create
                    </v-btn>
                </v-card-actions>
            </v-card>
        </v-dialog>

        <v-dialog
            v-model="form.upload"
            min-width="400"
            max-width="80vw"
            width="auto"
            persistent
        >
            <v-card title="Upload files">
                <v-card-text>
                    <v-form ref="formUpload">
                        <v-select
                            label="Root *"
                            v-model="form.folderRoot"
                            :rules="[rules.required]"
                            :items="folders"
                            variant="outlined"
                            autofocus
                            required
                        >
                        </v-select>
                        <v-file-input
                            label="File input *"
                            v-model="form.fileList"
                            :show-size="form.fileList.length > 1"
                            :counter="form.fileList.length > 1"
                            :rules="[rules.length]"
                            variant="outlined"
                            multiple
                            required
                        >
                        </v-file-input>
                        <v-text-field
                            label="Filename"
                            :disabled="form.fileList.length > 1"
                            v-model="form.fileName"
                            variant="outlined"
                        >
                        </v-text-field>
                    </v-form>
                    <small>* indicates required field</small>
                </v-card-text>
                <v-card-actions>
                    <v-spacer></v-spacer>
                    <v-btn @click="form.upload = false">Cancel</v-btn>
                    <v-btn
                        :loading
                        @click="$refs.formUpload.validate().then(upload)"
                    >
                        Upload
                    </v-btn>
                </v-card-actions>
            </v-card>
        </v-dialog>

        <v-toolbar
            class="border-b"
            density="comfortable"
            :color="select.length ? 'primary' : 'grey'"
            :title
        >
            <v-scale-transition :group="true" leave-absolute>
                <v-btn
                    v-if="!select.length"
                    @click="form.create = true"
                    icon
                >
                    <v-icon :icon="mdiFolderPlus"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Create new folder
                    </v-tooltip>
                </v-btn>
                <v-btn
                    v-if="!select.length"
                    @click="form.upload = true"
                    icon
                >
                    <v-icon :icon="mdiUpload"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Upload files
                    </v-tooltip>
                </v-btn>
            </v-scale-transition>
            <v-scale-transition :group="true">
                <v-btn
                    v-if="select.length === 1"
                    :target="useLink ? '_blank' : ''"
                    :href="`${useLink ? 'editor' : ''}#${select[0]}`"
                    icon
                >
                    <v-icon :icon="mdiPencilBoxMultiple"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Edit {{ basename(select[0]) }}
                    </v-tooltip>
                </v-btn>
                <v-btn
                    v-if="select.length"
                    @click="confirm(prompt, () => remove(select))"
                    :loading
                    icon
                >
                    <v-icon :icon="mdiDelete"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Delete from disk
                    </v-tooltip>
                </v-btn>
                <v-btn v-if="select.length" @click="select = []" icon="$close">
                </v-btn>
            </v-scale-transition>
        </v-toolbar>

        <TreeView v-model:selection="select" :items auto-icon />
    </v-sheet>
</template>
