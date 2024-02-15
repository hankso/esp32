<template>
    <v-text-field
        :name
        clearable
        hide-details
        variant="outlined"
        :modelValue="value"
        @update:modelValue="update_lazy"
        :type="pass && !show ? 'password' : 'text'"
        :append-inner-icon="!pass ? '' : show ? mdiEyeOff : mdiEye"
        @click:appendInner="show = !show"
        @click:clear="update?.('')"
    ></v-text-field>
</template>

<script setup>
import { debounce } from '@/utils'

import { mdiEye, mdiEyeOff } from '@mdi/js'

const { name, update } = defineProps({
    name: String,
    value: String,
    schema: Object,
    update: Function,
})

const show = ref(false)

const pass = computed(() => name.endsWith('pass'))

const update_lazy = debounce(update)
</script>

<style scoped>
.v-text-field {
    min-width: 200px;
}
</style>
