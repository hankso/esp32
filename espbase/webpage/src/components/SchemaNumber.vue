<template>
    <v-list-item :title="schema?.title ?? prop" :subtitle="schema?.description">
        <template #append>
            <v-text-field
                :modelValue="value"
                @update:modelValue="update"
            ></v-text-field>
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
