<template>
    <v-select
        clearable
        :items="schema.enum"
        :modelValue="value"
        @update:modelValue="update"
    ></v-select>
</template>

<script setup>
defineProps({
    name: String,
    value: String,
    schema: Object,
    update: Function,
})
</script>
