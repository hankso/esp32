<script setup>
import { type, rules } from '@/utils'
import { listDir, readFile, uploadFile, createPath, deletePath } from '@/apis'

import {
    mdiUpload,
    mdiDownload,
    mdiDeleteOutline as mdiDelete,
    mdiPencilBoxOutline as mdiPencilBox,
    mdiFolderPlusOutline as mdiFolderPlus,
} from '@mdi/js'
import { join, resolve, basename } from 'path-browserify'

const route = useRoute()
const notify = inject('notify', console.log)
const confirm = inject('confirm', (p, f) => window.confirm(p) && f())

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
const form = reactive({
    create: false,
    fileName: '',
    fileList: [],

    upload: false,
    folderName: '',
    folderRoot: '/',
})

const title = computed(() => {
    let arr = select.value
    if (!arr.length) return `Files under ${root.value}`
    if (arr.length === 1) return arr[0]
    return `Selected ${arr.length} files`
})

const prompt = computed(() => {
    let arr = select.value
    if (!arr.length) return
    if (arr.length === 1) return `Delete ${arr[0]} permanently?`
    return ['Delete these paths permanently?', ...arr].join('\n - ')
})

const sfiles = computed(() => select.value.filter(isfile))

const folders = computed(() => {
    let nodes = ['/']
    ;(function findFolder(arr) {
        for (let node of arr) {
            if (node.type === 'dir') {
                nodes.push(node.link)
                findFolder(node.childs ?? [])
            }
        }
    })(items.value)
    return nodes
})

function refresh(path, target, parent) {
    path ??= root.value
    target ??= items.value
    if (type(path) === 'object') {
        parent = path
        if (parent.type === 'file') return
        if ((parent.id || '').split('-').length > 5)
            return notify('Too deep into subfolders. Maybe recursive loop?')
        path = parent.link
        parent.childs ??= [] // let vue track this new array
        target = parent.childs
    } else if (type(path) !== 'string') {
        return
    }
    listDir(path)
        .then(({ data }) => {
            let nodes = data.map((node, idx) => ({
                ...node,
                id: parent ? `${parent.id}-${idx}` : `${idx}`,
                link: join(parent ? parent.link : '/', node.name),
            }))
            target.splice(0, target.length, ...nodes)
            target.forEach(refresh)
        })
        .catch(({ message }) => notify(message))
}

function pop(arr, item) {
    let idx = arr.indexOf(item)
    if (idx >= 0) return arr.splice(idx, 1)
}

function isfile(path) {
    let bool = false
    ;(function findPath(arr) {
        for (let node of arr) {
            if ((node.link ?? node.name) === path)
                return (bool = node.type === 'file')
            if (node.type === 'dir') findPath(node.childs ?? [])
        }
    })(items.value)
    return bool
}

function remove(arr) {
    let len = arr.length
    loading.value = true
    arr.forEach(path =>
        deletePath(path, !isfile(path))
            .then(() => pop(arr, path))
            .catch(({ message }) => notify(message))
            .finally(() => {
                if (--len) return
                loading.value = false
                notify('Deleted!')
                refresh()
            })
    )
}

async function create(e) {
    if (e && !(await e).valid) return
    loading.value = true
    createPath(resolve(form.folderRoot, form.folderName))
        .then(() => notify('Created!'))
        .catch(({ message }) => notify(message))
        .finally(() => {
            loading.value = false
            form.create = false
            refresh()
        })
}

async function upload(e) {
    if (e && !(await e).valid) return
    let len = form.fileList.length
    loading.value = true
    form.fileList.forEach(file => {
        let filename = file.name
        if (form.fileList.length === 1 && form.fileName)
            filename = form.fileName
        uploadFile(resolve(form.folderRoot, filename), file)
            .then(() => pop(form.fileList, file))
            .catch(({ message }) => notify(message))
            .finally(() => {
                if (--len) return
                loading.value = false
                form.upload = false
                refresh()
            })
    })
}

watchEffect(() => {
    let arr = select.value
    location.hash = props.useLink && arr.length === 1 ? arr[0] : ''
})

onMounted(refresh)
</script>

<template>
    <v-sheet border rounded="lg" class="overflow-x-hidden">
        <v-dialog v-model="form.create" min-width="350" width="auto">
            <v-card>
                <v-card-title class="d-flex align-center">
                    Create new folder
                    <v-btn
                        text="ESC"
                        size="x-small"
                        color="grey"
                        class="ms-auto"
                        variant="outlined"
                        @click="form.create = false"
                    ></v-btn>
                </v-card-title>
                <v-form @submit.prevent="create" class="pa-4">
                    <v-select
                        label="Under *"
                        v-model="form.folderRoot"
                        :rules="[rules.required]"
                        :items="folders"
                        variant="outlined"
                        required
                    ></v-select>
                    <v-text-field
                        label="Folder name *"
                        v-model="form.folderName"
                        :rules="[rules.required]"
                        variant="outlined"
                        required
                    ></v-text-field>
                    <div class="d-flex align-center justify-space-between">
                        <small>* indicates required field</small>
                        <v-btn :loading type="submit" variant="outlined">
                            Create
                        </v-btn>
                    </div>
                </v-form>
            </v-card>
        </v-dialog>

        <v-dialog v-model="form.upload" min-width="350" width="auto">
            <v-card>
                <v-card-title class="d-flex align-center">
                    Upload files
                    <v-btn
                        text="ESC"
                        size="x-small"
                        color="grey"
                        class="ms-auto"
                        variant="outlined"
                        @click="form.upload = false"
                    ></v-btn>
                </v-card-title>
                <v-form @submit.prevent="upload" class="pa-4">
                    <v-select
                        label="Under *"
                        v-model="form.folderRoot"
                        :rules="[rules.required]"
                        :items="folders"
                        variant="outlined"
                        required
                    ></v-select>
                    <v-file-input
                        label="Files *"
                        v-model="form.fileList"
                        show-size
                        :counter="form.fileList.length > 1"
                        :rules="[rules.length]"
                        prepend-icon=""
                        variant="outlined"
                        multiple
                        required
                    >
                        <template #selection="{ fileNames }">
                            <template v-for="(key, idx) in fileNames" :key>
                                <v-chip color="primary">{{ key }}</v-chip>
                                <v-spacer
                                    v-if="idx !== form.fileList.length - 1"
                                    class="flex-1-1-100"
                                ></v-spacer>
                            </template>
                        </template>
                    </v-file-input>
                    <v-text-field
                        label="Rename to"
                        :disabled="form.fileList.length !== 1"
                        v-model="form.fileName"
                        variant="outlined"
                    ></v-text-field>
                    <div class="d-flex align-center justify-space-between">
                        <small>* indicates required field</small>
                        <v-btn :loading type="submit" variant="outlined">
                            Upload
                        </v-btn>
                    </div>
                </v-form>
            </v-card>
        </v-dialog>

        <v-toolbar
            height="36"
            class="border-b"
            :color="select.length ? 'primary' : ''"
        >
            <span class="px-3 me-auto">{{ title }}</span>

            <v-scale-transition group leave-absolute>
                <v-btn icon v-if="!select.length" @click="form.create = true">
                    <v-icon :icon="mdiFolderPlus"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Create new folder
                    </v-tooltip>
                </v-btn>
                <v-btn icon v-if="!select.length" @click="form.upload = true">
                    <v-icon :icon="mdiUpload"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Upload files
                    </v-tooltip>
                </v-btn>
            </v-scale-transition>

            <v-scale-transition group>
                <v-btn
                    icon
                    v-if="sfiles.length === 1"
                    @click="readFile(sfiles[0], true)"
                >
                    <v-icon :icon="mdiDownload"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Download as {{ basename(select[0]) }}
                    </v-tooltip>
                </v-btn>
                <v-btn
                    icon
                    v-if="sfiles.length === 1"
                    :target="useLink ? '_blank' : ''"
                    :href="`${useLink ? 'editor' : ''}#${sfiles[0]}`"
                >
                    <v-icon :icon="mdiPencilBox"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Edit {{ basename(select[0]) }}
                    </v-tooltip>
                </v-btn>
                <v-btn
                    icon
                    v-if="select.length"
                    @click="confirm(prompt, () => remove(select))"
                    :loading
                >
                    <v-icon :icon="mdiDelete"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Delete from disk
                    </v-tooltip>
                </v-btn>
                <v-btn icon="$close" v-if="select.length" @click="select = []">
                </v-btn>
            </v-scale-transition>
        </v-toolbar>

        <TreeView v-model:select="select" :items />
    </v-sheet>
</template>

<style scoped>
.tree-view {
    height: calc(100% - 36px - 1px);
}

.v-btn {
    width: 36px;
    height: inherit;
    border-radius: 0;
    margin-inline-end: 0 !important;
    border-inline-start: thin solid
        rgba(var(--v-border-color), var(--v-border-opacity)) !important;
}
</style>
