<template>
    <v-combobox
        :type="isNumber ? 'number' : 'text'"
        :rules="[validator]"
        :clearable="!required"
        variant="outlined"
        hide-details="auto"
        :items="schema.enum"
        v-model="proxy"
    ></v-combobox>
</template>

<script setup>
import { type, debounce, parseNum } from '@/utils'
import ajv from '@/plugins/ajv'

const props = defineProps({
    value: {
        type: [String, Number],
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

const isNumber = computed(() =>
    ['number', 'integer'].includes(props.schema?.type ?? type(props.value))
)

const proxy = computed({
    get: () => (isNumber.value ? parseNum(props.value) : props.value),
    set: debounce(val => props.update(isNumber.value ? parseNum(val) : val)),
})

function validator() {
    if (!ajv.validate(props.schema, props.value))
        return ajv.errorsText().split(', ')[0]
    return true
}
</script>
