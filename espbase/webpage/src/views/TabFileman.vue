<script setup>
import { type, debounce } from '@/utils'
import { listDir, deletePath } from '@/apis'

import {
    mdiUpload,
    mdiFolderPlus,
    mdiDelete,
    mdiPencilBoxMultiple,
} from '@mdi/js'
import { join, resolve, basename } from 'path-browserify'

const route = useRoute()

const root = ref(resolve('/', route.query.root || ''))

const items = ref([])
const select = ref(route.hash ? [route.hash.slice(1)] : [])

const loading = ref(false)

const notify = inject('notify', console.log)
const confirm = inject('confirm', () => {})

function syncPath(path, target, parent) {
    if (path === undefined) path = root.value
    if (target === undefined) target = items.value
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
            target.forEach(syncPath)
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
                syncPath()
            })
    )
}

const title = computed(() => {
    let arr = toValue(select)
    if (!arr.length) return `Files under ${root.value}`
    if (arr.length === 1) return arr[0]
    return `Selected ${arr.length} files`
})

const prompt = computed(() => {
    let arr = toValue(select)
    if (!arr.length) return
    if (arr.length === 1) return `Delete ${arr[0]} permanently?`
    return 'Delete these files permanently?' + '<div>TODO</div>'
})

watch(
    select,
    debounce(arr => (location.hash = arr.length !== 1 ? '' : arr[0])),
    { deep: true }
)

onMounted(syncPath)
</script>

<template>
    <v-sheet border rounded="lg" elevation="1" class="ma-4 overflow-hidden">
        <v-toolbar
            class="border-b"
            density="comfortable"
            :color="select.length ? 'primary' : 'grey'"
            :title
        >
            <v-scale-transition :group="true" leave-absolute>
                <v-btn
                    icon
                    v-if="!select.length"
                    @click="console.log('TODO create')"
                >
                    <v-icon :icon="mdiFolderPlus"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Create new folder
                    </v-tooltip>
                </v-btn>
                <v-btn
                    icon
                    v-if="!select.length"
                    @click="console.log('TODO upload')"
                >
                    <v-icon :icon="mdiUpload"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Upload
                    </v-tooltip>
                </v-btn>
            </v-scale-transition>
            <v-scale-transition :group="true">
                <v-btn
                    icon
                    v-if="select.length === 1"
                    :href="`editor#${select[0]}`"
                    target="_blank"
                >
                    <v-icon :icon="mdiPencilBoxMultiple"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Edit {{ basename(select[0]) }}
                    </v-tooltip>
                </v-btn>
                <v-btn
                    icon
                    v-if="select.length"
                    :loading
                    @click="confirm(prompt, () => remove(select))"
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
