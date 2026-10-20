<template>
    <v-select
        hide-details
        variant="outlined"
        :items="schema.enum"
        :clearable="!required"
        v-model="proxy"
    ></v-select>
</template>

<script setup>
const props = defineProps({
    value: {
        type: null,
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
    required: {
        type: Boolean,
        default: false,
    },
})

const isIndex = computed(() => {
    if (props.schema.enum.includes(props.value)) return false
    let idx = parseInt(props.value)
    return idx >= 0 && idx < props.schema.enum.length
})

const proxy = computed({
    get: () => (isIndex.value ? props.schema.enum[props.value] : props.value),
    set: v => props.update(isIndex.value ? props.schema.enum.indexOf(v) : v),
})
</script>
