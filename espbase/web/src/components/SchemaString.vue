<template>
    <v-text-field
        :name
        :type="pass && !show ? 'password' : 'text'"
        :rules="[validator]"
        variant="outlined"
        hide-details="auto"
        :clearable="!required"
        :model-value="value"
        @update:model-value="update"
        :append-inner-icon="!pass ? '' : show ? mdiEyeOff : mdiEye"
        @click:append-inner="show = !show"
    ></v-text-field>
</template>

<script setup>
import ajv from '@/plugins/ajv'
import { debounce } from '@/utils'

import { mdiEye, mdiEyeOff } from '@mdi/js'

const props = defineProps({
    name: {
        type: String,
        default: '',
    },
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
    required: {
        type: Boolean,
        default: false,
    },
})

const show = ref(false)

const pass = computed(() => props.name.endsWith('pass'))

const update = debounce(props.update)

function validator() {
    if (props.schema && !ajv.validate(props.schema, props.value))
        return ajv.errorsText().split(', ')[0]
    return true
}
</script>
