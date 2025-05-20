<script setup>
import { strftime, formatSize } from '@/utils'

defineProps({
    items: {
        type: Array,
        default: () => [],
    },
    useLink: Boolean,
})

const { isActive, guessIcon } = inject('TreeView')
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
            v-if="!item.childs?.length"
            color="primary"
            :title="item.name"
            :value="item.link ?? item.name"
            :to="useLink ? `#${item.link}` : ''"
            :active="isActive(item)"
            :prepend-icon="guessIcon(item)"
        >
            <template #subtitle>
                {{ item.date ? strftime('%F %T', item.date) : '' }}
                {{ item.date && item.size ? '-' : '' }}
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
                        {{ item.childs.length }} childs
                    </template>
                </v-list-item>
            </template>
            <!-- Rescursion call the component to create the tree -->
            <TreeViewInner :items="item.childs" :use-link />
        </v-list-group>
    </template>
</template>
