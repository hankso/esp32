<script setup>
const { src, hidebar } = defineProps({
    src: {
        type: String,
        required: true
    },
    hidebar: {
        type: Boolean,
        default: true
    }
})
</script>

<template>
<div :class="{ hidebar }">
    <iframe :src></iframe>
</div>
</template>

<style scoped>
div {
    height: 100%;
    display: flex;
    overflow-x: hidden;
    min-height: 200px;
}

iframe {
    border: 0;
    flex-grow: 1;
}

.hidebar > iframe {
    margin-right: -14px;
}
</style>
