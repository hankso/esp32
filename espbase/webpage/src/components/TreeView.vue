<script setup>
import { mdiFile, mdiFolder, mdiFolderOpen } from '@mdi/js'

const opened = defineModel('opened', { type: Array, default: [] })
const selection = defineModel('selection', { type: Array, default: [] })

const props = defineProps({
    items: {
        type: Array,
        default: [
            {
                id: 'D0',
                name: 'DEMO-Parent',
                children: [
                    { id: 'D1', name: 'DEMO-Child 1' },
                    { id: 'D2', name: 'DEMO-Child 2' },
                    {
                        id: 'D3',
                        name: 'DEMO-Folder',
                        type: 'folder',
                        children: [
                            { id: 'D4', name: 'DEMO-File', type: 'file' },
                        ],
                    },
                ],
            },
        ],
    },
    debug: Boolean,
    useLink: Boolean,
    autoIcon: Boolean,
})

const route = useRoute()

provide('TreeView', {
    useLink: computed(() => props.useLink),
    autoIcon: computed(() => props.autoIcon),
    isActive(node) {
        if (props.useLink) return node.link === route.hash.slice(1)
        return selection.value.includes(node.link ? node.link : node.name)
    },
    guessIcon(node) {
        if (node.icon || !props.autoIcon) return node.icon
        if (node.type === 'file') return mdiFile
        if (node.type === 'folder')
            return opened.value.includes(node.id) ? mdiFolderOpen : mdiFolder
        if (node.children && node.children.length)
            return opened.value.includes(node.id) ? '$collapse' : '$expand'
        let key = node.link ? node.link : node.name
        return selection.value.includes(key) ? '$close' : ''
    },
})
</script>

<template>
    <v-list
        class="pa-0"
        v-model:opened="opened"
        v-model:selected="selection"
        open-strategy="multiple"
        select-strategy="leaf"
        @keyup.esc="selection = []"
    >
        <v-list-group v-if="debug" title="debug options" value="debug group">
            <template #activator="{ props }">
                <v-list-item v-bind="props"></v-list-item>
            </template>
            <v-list-item title="opened" :subtitle="opened.join('|')" />
            <v-list-item title="selection" :subtitle="selection.join('|')" />
            <v-list-item title="use-link" :subtitle="`${useLink}`" />
            <v-list-item title="auto-icon" :subtitle="`${autoIcon}`" />
        </v-list-group>
        <TreeViewInner :items />
    </v-list>
</template>
