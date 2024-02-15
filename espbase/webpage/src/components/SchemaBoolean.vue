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
        :modelValue="parseBool(value) ? '1' : '0'"
        @update:modelValue="update"
    ></v-switch>
</template>

<script setup>
import { parseBool } from '@/utils'

defineProps({
    name: String,
    value: String,
    schema: Object,
    update: Function,
})
</script>
