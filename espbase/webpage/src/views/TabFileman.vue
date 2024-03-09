<script setup>
import { type, rules, debounce } from '@/utils'
import { listDir, uploadFile, createPath, deletePath } from '@/apis'

import {
    mdiUpload,
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
    path ??= toValue(root)
    target ??= toValue(items)
    if (type(path) === 'object') {
        parent = path
        if (parent.type !== 'folder') return
        if ((parent.id || '').split('-').length > 5)
            return notify('Too deep into subfolders. Maybe recursive loop?')
        path = parent.link
        parent.children ??= [] // let vue track this new array
        target = parent.children
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

function remove(arr) {
    let len = arr.length
    loading.value = true
    arr.forEach(path =>
        deletePath(path)
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

async function upload(e) {
    if (e && !(await e).valid) return
    let data = toValue(form)
    let len = data.fileList.length
    loading.value = true
    data.fileList.forEach(file => {
        let filename = file.name
        if (data.fileList.length === 1 && data.fileName)
            filename = data.fileName
        uploadFile(resolve(data.folderRoot, filename), file)
            .then(() => pop(data.fileList, file))
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

            <v-scale-transition :group="true" leave-absolute>
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

            <v-scale-transition :group="true">
                <v-btn
                    icon
                    v-if="select.length === 1"
                    :target="useLink ? '_blank' : ''"
                    :href="`${useLink ? 'editor' : ''}#${select[0]}`"
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

        <TreeView v-model:selection="select" :items auto-icon />
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
