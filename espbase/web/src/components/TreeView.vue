<script setup>
import {
    mdiFolder,
    mdiFolderOpen,
    mdiFileImage,
    mdiFileDocumentOutline,
    mdiNodejs,
    mdiCodeJson,
    mdiLanguageHtml5,
    mdiLanguageMarkdown,
    mdiCheckboxIntermediate,
    mdiCheckboxMarked,
} from '@mdi/js'
import { basename } from 'path-browserify'

const opened = defineModel('opened', { type: Array, default: [] })
const select = defineModel('select', { type: Array, default: [] })

const props = defineProps({
    items: {
        type: Array,
        default: () => [
            {
                id: 'D0',
                name: 'DEMO-Parent',
                childs: [
                    { id: 'D1', name: 'DEMO-Child 1' },
                    { id: 'D2', name: 'DEMO-Child 2' },
                    {
                        id: 'D3',
                        name: 'DEMO-Folder',
                        type: 'dir',
                        childs: [{ id: 'D4', name: 'DEMO-File', type: 'file' }],
                    },
                ],
            },
        ],
    },
    debug: Boolean,
    useLink: Boolean,
    autoIcon: {
        type: Boolean,
        default: true,
    },
})

const route = useRoute()

function isActive(node) {
    if (props.useLink && node.link === route.hash.slice(1)) return true
    return toValue(select).includes(node.link ?? node.name)
}

function guessIcon(node) {
    if (node.icon || !props.autoIcon) return node.icon
    if (node.type === 'file') {
        let base = basename(node.name, '.gz')
        if (base.endsWith('.json')) return mdiCodeJson
        if (base.endsWith('.js')) return mdiNodejs
        if (base.endsWith('.html')) return mdiLanguageHtml5
        if (base.endsWith('.md')) return mdiLanguageMarkdown
        if (base.endsWith('.ico') || base.endsWith('png')) return mdiFileImage
        return mdiFileDocumentOutline
    }
    let isOpened = toValue(opened).includes(node.id)
    if (node.type !== 'dir') {
        if (node.childs?.length) return isOpened ? '$collapse' : '$expand'
        return isActive(node) ? '$close' : ''
    } else if (node.childs?.length) {
        let selected = [0, 0]
        ;(function IterChilds(arr) {
            for (let node of arr) {
                if (node.type === 'dir') {
                    IterChilds(node.childs ?? [])
                } else {
                    selected[0 + isActive(node)]++
                }
            }
        })(node.childs)
        if (!selected[0]) return mdiCheckboxMarked // all selected
        if (selected[1]) return mdiCheckboxIntermediate
    }
    return isOpened ? mdiFolderOpen : mdiFolder
}

provide('TreeView', { isActive, guessIcon })
</script>

<template>
    <v-list
        class="tree-view pa-0"
        v-model:opened="opened"
        v-model:selected="select"
        open-strategy="multiple"
        select-strategy="leaf"
        @keyup.esc="select = []"
    >
        <v-list-group v-if="debug" title="debug options" value="debug group">
            <template #activator="attrs">
                <v-list-item v-bind="attrs.props"></v-list-item>
            </template>
            <v-list-item title="opened" :subtitle="opened.join('|')" />
            <v-list-item title="select" :subtitle="select.join('|')" />
            <v-list-item title="use-link" :subtitle="`${useLink}`" />
            <v-list-item title="auto-icon" :subtitle="`${autoIcon}`" />
        </v-list-group>
        <TreeViewInner :items :use-link />
    </v-list>
</template>
