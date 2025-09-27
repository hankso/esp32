<template>
    <v-expansion-panels class="schema-editor my-2">
        <v-expansion-panel>
            <v-expansion-panel-title v-slot="{ expanded }">
                Edit Schema
                <span class="mx-auto text-red" v-if="!expanded">
                    {{ error.split(':')[0] }}
                </span>
            </v-expansion-panel-title>
            <v-expansion-panel-text>
                <CodeJar v-model="plain" language="json" line-number />
                <v-scroll-y-transition>
                    <p v-show="error">{{ error }}</p>
                </v-scroll-y-transition>
            </v-expansion-panel-text>
        </v-expansion-panel>
    </v-expansion-panels>
</template>

<script setup>
import ajv from '@/plugins/ajv'
import { debounce } from '@/utils'

const schema = defineModel({
    type: Object,
    default: {},
})

const plain = ref('')
const error = ref('')

watchEffect(() => (plain.value = JSON.stringify(schema.value, null, 4)))

watch(
    plain,
    debounce(val => {
        try {
            let obj = JSON.parse(val)
            if (!ajv.validateSchema(obj)) throw ajv.errorsText()
            schema.value = obj
            error.value = ''
        } catch (err) {
            error.value = err + ''
        }
    })
)
</script>

<style scoped>
:deep(.v-expansion-panel-text__wrapper) {
    padding: 0;
    overflow: hidden;
    border-radius: 4px;
}

.code-jar {
    max-height: 30vh;
    overflow-y: auto;
}

.code-jar + p {
    color: #b00020;
    font-size: 12px;
    margin-left: 8px;
}
</style>
