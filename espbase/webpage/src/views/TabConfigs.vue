<template>
    <v-expansion-panels class="my-2">
        <v-expansion-panel>
            <v-expansion-panel-title v-slot="{ expanded }">
                Edit Schema
                <span class="mx-auto text-red" v-if="!expanded">
                    {{ schemaError.split(':')[0] }}
                </span>
            </v-expansion-panel-title>
            <v-expansion-panel-text>
                <div class="fix-margin">
                    <!--
                    <v-textarea
                        v-model="schemaPlain"
                        hide-details="auto"
                        :error-messages="schemaError"
                    ></v-textarea>
                    -->
                    <CodeJar
                        v-model="schemaPlain"
                        language="json"
                        line-number
                    />
                    <v-scroll-y-transition>
                        <p v-show="schemaError">{{ schemaError }}</p>
                    </v-scroll-y-transition>
                </div>
            </v-expansion-panel-text>
        </v-expansion-panel>
    </v-expansion-panels>
    <v-sheet border rounded="lg">
        <SchemaForm v-model="config" :schema @submit.prevent="submit" />
    </v-sheet>
</template>

<script setup>
import ajv from '@/plugins/ajv'
import { isEmpty, debounce } from '@/utils'
import { getSchema, getConfig, setConfig } from '@/apis'

const notify = inject('notify', console.log)

const config = ref({})
const schema = ref({})
const schemaPlain = ref('')
const schemaError = ref('')

/* const validator = computed(() => ajv.compile(toValue(schema))) */
// TODO validate before setConfig

function submit() {
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
        .then(({ data }) => (config.value = data))
        .catch(({ message }) => notify(message))
}

onMounted(refresh)

watch(schema, val => (schemaPlain.value = JSON.stringify(val, null, 4)))
watch(
    schemaPlain,
    debounce(val => {
        try {
            let obj = JSON.parse(val)
            if (!ajv.validateSchema(obj)) throw new Error(ajv.errorsText())
            schema.value = obj
            schemaError.value = ''
        } catch (e) {
            schemaError.value = e.toString()
        }
    })
)
</script>

<style scoped>
/* hotfix for v-expansion-panel-text__wrapper */
.fix-margin {
    margin: -8px -24px -16px;
}

.codejar {
    max-height: 30vh;
    overflow-y: auto;
}

.codejar + p {
    color: #b00020;
    font-size: 12px;
    margin-left: 8px;
}
</style>
