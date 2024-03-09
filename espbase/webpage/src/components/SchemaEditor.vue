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
                <div class="fix-schema-editor-margin">
                    <!--
                    <v-textarea
                        v-model="plain"
                        hide-details="auto"
                        :error-messages="error"
                    ></v-textarea>
                    -->
                    <CodeJar v-model="plain" language="json" line-number />
                    <v-scroll-y-transition>
                        <p v-show="error">{{ error }}</p>
                    </v-scroll-y-transition>
                </div>
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

watch(schema, val => (plain.value = JSON.stringify(val, null, 4)), {
    immediate: true,
})

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
.fix-schema-editor-margin {
    margin: -8px -24px -16px; /* hotfix for v-expansion-panel-text__wrapper */
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
