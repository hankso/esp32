<template>
    <v-progress-linear v-if="loading === true" :color indeterminate />
    <v-progress-linear
        v-else-if="loading !== false"
        :model-value="loading"
        :stream
        :color
    />
</template>

<script setup>
defineProps({
    color: {
        type: String,
        default: 'primary',
    },
    stream: {
        type: Boolean,
        default: true,
    },
    loading: {
        type: [Boolean, Number],
        required: true,
    },
})
</script>
