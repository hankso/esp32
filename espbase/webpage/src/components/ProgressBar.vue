<template>
    <v-progress-linear
        v-if="loading === true"
        class="progress-bar"
        indeterminate
        :color
    ></v-progress-linear>
    <v-progress-linear
        v-else-if="loading !== false"
        class="progress-bar"
        :model-value="loading"
        :stream
        :color
    ></v-progress-linear>
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
