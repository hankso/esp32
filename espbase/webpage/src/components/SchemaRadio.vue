<template>
    <v-list-item :title="schema?.title ?? prop" :subtitle="schema?.description">
        <template #append>
        </template>
    </v-list-item>
</template>

<script setup>
defineProps({
    prop: { type: String },
    value: {  },
    schema: { type: Object },
    update: { type: Function },
})
</script>
