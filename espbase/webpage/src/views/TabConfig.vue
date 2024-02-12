<template>
    <v-sheet border rounded="lg" elevation="1" class="ma-4">
        <SchemaForm v-model="config" :schema @submit="submit" />
    </v-sheet>
</template>

<script setup>
import { type, isEmpty } from '@/utils'
import { getConfig, setConfig, getAsset } from '@/apis'

const notify = inject('notify', console.log)

const config = ref({})
const schema = ref({})

function submit() {
    setConfig(toValue(config))
        .then(() => notify('Configuration saved!'))
        .catch(({ message }) => notify(message))
}

function refresh() {
    if (isEmpty(toValue(schema))) {
        getAsset('/config.schema.json')
            .then(({ data }) => (schema.value = data))
            .catch(({ message }) => notify(message))
    }
    getConfig()
        .then(({ data }) => (config.value = data))
        .catch(({ message }) => notify(message))
}

onMounted(refresh)
</script>
