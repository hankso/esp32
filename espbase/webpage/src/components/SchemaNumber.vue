<template>
    <v-text-field
        clearable
        hide-details
        type="number"
        :modelValue="value.includes('.') ? parseFloat(value) : parseInt(value)"
        @update:modelValue="val => update?.(val.toString())"
    ></v-text-field>
</template>

<script setup>
defineProps({
    name: String,
    value: String,
    schema: Object,
    update: Function,
})
</script>
