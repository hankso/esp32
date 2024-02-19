<template>
    <v-text-field
        clearable
        hide-details
        type="number"
        :model-value="value.includes('.') ? parseFloat(value) : parseInt(value)"
        @update:model-value="val => update(val.toString())"
    ></v-text-field>
</template>

<script setup>
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
