<template>
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
        :model-value="parseBool(value) ? '1' : '0'"
        @update:model-value="update"
    ></v-switch>
</template>

<script setup>
import { parseBool } from '@/utils'

defineProps({
    value: {
        type: String,
        required: true,
    },
    update: {
        type: Function,
        default: () => {},
    },
})
</script>
