<template>
    <v-select
        hide-details
        variant="outlined"
        :items="schema.enum"
        :clearable="!required"
        :model-value="value"
        @update:model-value="update"
    ></v-select>
</template>

<script setup>
defineProps({
    value: {
        required: true,
        validator: () => true,
    },
    schema: {
        type: Object,
        required: true,
    },
    update: {
        type: Function,
        default: () => {},
    },
    required: {
        type: Boolean,
        default: false,
    },
})
</script>
