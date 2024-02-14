<template>
    <v-list-item :title="schema?.title ?? prop" :subtitle="schema?.description">
        <template #append>
            <!--
            <v-checkbox
                :modelValue="parseBool(value)"
                @update:modelValue="val => update?.(val ? '1' : '0')"
            ></v-checkbox>
            -->
            <v-switch
                inset
                color="primary"
                true-value="1"
                false-value="0"
                :modelValue="parseBool(value) ? '1' : '0'"
                @update:modelValue="update"
            ></v-switch>
        </template>
    </v-list-item>
</template>

<script setup>
import { parseBool } from '@/utils'

defineProps({
    prop: { type: String },
    value: {  },
    schema: { type: Object },
    update: { type: Function },
})
</script>
