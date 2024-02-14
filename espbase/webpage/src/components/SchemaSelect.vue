<template>
    <v-list-item :title="schema?.title ?? prop" :subtitle="schema?.description">
        <template #append>
            <v-select
                clearable
                :items="schema.enum"
                :modelValue="value"
                @update:modelValue="update"
            ></v-select>
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
