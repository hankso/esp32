<template>
    <v-text-field
        type="number"
        :rules="[validator]"
        clearable
        hide-details="auto"
        :model-value="value.includes('.') ? parseFloat(value) : parseInt(value)"
        @update:model-value="val => update(val.toString())"
    ></v-text-field>
</template>

<script setup>
import ajv from '@/plugins/ajv'

const props = defineProps({
    value: {
        type: String,
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
})

function validator() {
    if (props.schema && !ajv.validate(props.schema, props.value))
        return ajv.errorsText().split(',')[0]
    return true
}
</script>
