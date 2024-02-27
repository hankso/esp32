<template>
    <SchemaEditor v-model="schema" />
    <v-sheet border rounded="lg">
        <SchemaForm
            v-model="config"
            :schema
            :backup
            null-val=""
            show-schema
            @submit.prevent="submit"
        />
    </v-sheet>
</template>

<script setup>
import { isEmpty, deepcopy } from '@/utils'
import { getSchema, getConfig, setConfig } from '@/apis'

const notify = inject('notify', console.log)

const schema = ref({})
const config = ref({})
const backup = ref({})

async function submit(e) {
    if (e && !(await e).valid) return
    setConfig(toValue(config))
        .then(() => notify('Configuration saved!') && refresh())
        .catch(({ message }) => notify(message))
}

function refresh() {
    if (isEmpty(toValue(schema))) {
        getSchema('config')
            .then(({ data }) => (schema.value = data))
            .catch(({ message }) => notify(message))
    }
    getConfig()
        .then(({ data }) => (backup.value = deepcopy((config.value = data))))
        .catch(({ message }) => notify(message))
}

onMounted(refresh)
</script>
