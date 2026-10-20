<template>
    <v-text-field
        type="number"
        :rules="[validator]"
        :clearable="!required"
        variant="outlined"
        hide-details="auto"
        v-model="proxy"
    ></v-text-field>
</template>

<script setup>
import { type, parseNum } from '@/utils'
import ajv from '@/plugins/ajv'

const props = defineProps({
    value: {
        type: [String, Number],
        required: true,
    },
    schema: {
        type: Object,
        default: undefined,
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
    get: () => parseNum(props.value),
    set: val => props.update(isNumber.value ? parseNum(val) : val),
})

function validator() {
    if (props.schema && !ajv.validate(props.schema, props.value))
        return ajv.errorsText().split(', ')[0]
    return true
}
</script>
