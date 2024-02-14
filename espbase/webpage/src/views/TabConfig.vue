<template>
    <v-expansion-panels class="my-2">
        <v-expansion-panel title="Schema">
            <template #text>
                <v-textarea
                    v-model="schemaPlain"
                    :error-messages="schemaError"
                ></v-textarea>
            </template>
        </v-expansion-panel>
    </v-expansion-panels>
    <v-sheet border rounded="lg" elevation="1">
        <SchemaForm v-model="config" :schema @submit="submit" />
    </v-sheet>
</template>

<script setup>
import { type, isEmpty, debounce } from '@/utils'
import { getConfig, setConfig, getAsset } from '@/apis'
import ajv from '@/plugins/ajv'

const notify = inject('notify', console.log)

const config = ref({})
const schema = ref({})
const schemaPlain = ref('')
const schemaError = ref('')

const validator = computed(() => ajv.compile(toValue(schema)))

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

watch(schema, val => (schemaPlain.value = JSON.stringify(val, null, 4)))
watch(schemaPlain, debounce(val => {
    try {
        let obj = JSON.parse(val)
        if (!ajv.validateSchema(obj))
            throw new Error(ajv.errorsText())
        // TODO: this will trigger schemaPlain, save pos
        schema.value = obj
        schemaError.value = ''
    } catch (e) {
        schemaError.value = e.toString()
    }
}))
</script>
