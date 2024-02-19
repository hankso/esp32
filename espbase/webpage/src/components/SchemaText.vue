<template>
    <v-text-field
        :name
        clearable
        hide-details
        variant="outlined"
        :model-value="value"
        @update:model-value="updateLazy"
        :type="pass && !show ? 'password' : 'text'"
        :append-inner-icon="!pass ? '' : show ? mdiEyeOff : mdiEye"
        @click:append-inner="show = !show"
        @click:clear="update('')"
    ></v-text-field>
</template>

<script setup>
import { debounce } from '@/utils'

import { mdiEye, mdiEyeOff } from '@mdi/js'

const { name, update } = defineProps({
    name: {
        type: String,
        default: '',
    },
    value: {
        type: String,
        required: true,
    },
    update: {
        type: Function,
        default: () => {},
    },
})

const show = ref(false)

const pass = computed(() => name.endsWith('pass'))

const updateLazy = debounce(update)
</script>

<style scoped>
.v-text-field {
    min-width: 200px;
}
</style>
