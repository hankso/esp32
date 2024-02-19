<script setup>
import { formatSize } from '@/utils'

defineProps({
    items: {
        type: Array,
        default: () => [],
    },
})

const { useLink, isActive, guessIcon } = inject('TreeView')

function formatDate(uts) {
    return new Date(uts).toISOString().replace(/[TZ]|\.000/g, ' ')
}
</script>

<template>
    <!--
        `value` will be in selection
        `to` will render v-list-item as <a href="xxx">
        `active` triggers highlight
    -->
    <template v-for="(item, i) in items" :key="item.id">
        <v-divider v-if="i"></v-divider>
        <v-list-item
            v-if="!item.children || !item.children.length"
            color="primary"
            :title="item.name"
            :value="item.link ? item.link : item.name"
            :to="useLink ? `#${item.link}` : ''"
            :active="isActive(item)"
            :prepend-icon="guessIcon(item)"
        >
            <template #subtitle>
                {{ item.date ? formatDate(item.date) : '' }} -
                {{ item.size ? formatSize(item.size) : '' }}
            </template>
        </v-list-item>

        <v-list-group v-else :value="item.id">
            <template #activator="{ props }">
                <v-list-item
                    v-bind="props"
                    :title="item.name"
                    :prepend-icon="guessIcon(item)"
                >
                    <template #subtitle>
                        {{ item.children.length }} childs
                    </template>
                </v-list-item>
            </template>
            <!-- Rescursion call the component to create the tree -->
            <TreeViewInner :items="item.children" />
        </v-list-group>
    </template>
</template>
