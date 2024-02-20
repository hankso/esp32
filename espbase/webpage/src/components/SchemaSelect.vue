<template>
    <v-select
        clearable
        hide-details
        :items="schema.enum"
        :model-value="value"
        @update:model-value="update"
    ></v-select>
</template>

<script setup>
defineProps({
    value: {
        type: String,
        required: true,
    },
    schema: {
        type: Object,
        required: true,
    },
    update: {
        type: Function,
        default: () => {},
    },
})
</script>
