<template>
    <v-form ref="form" style="position: relative">
        <slot name="schema">
            <v-overlay
                contained
                v-model="overlay"
                class="align-center justify-center pa-8 rounded-lg"
                style="z-index: 999"
                scroll-strategy="reposition"
            >
                <v-container class="bg-grey overflow-auto rounded-xl">
                    <pre>TODO schema</pre>
                </v-container>
            </v-overlay>
        </slot>

        <template v-for="item in items" :key="item.prop">
            <slot :name="item.prop" v-bind="item">
                <component
                    v-if="item.schema"
                    :is="inputComponent(item.schema)"
                    v-bind="item"
                ></component>
                <p v-else>{{ item.prop }}: {{ item.value }}</p>
            </slot>
        </template>

        <slot name="actions">
            <div class="d-flex align-center border-t pa-2">
                <small class="me-auto">* indicates required field</small>
                <v-btn variant="text" @click="overlay = true">Schema</v-btn>
                <v-btn variant="text" @click="$refs.form.reset()">Reset</v-btn>
                <v-btn variant="text" color="blue" type="submit">Submit</v-btn>
            </div>
        </slot>
    </v-form>
</template>

<script setup>
import SchemaText from '@/components/SchemaText.vue'
import SchemaRadio from '@/components/SchemaRadio.vue'
import SchemaNumber from '@/components/SchemaNumber.vue'
import SchemaSelect from '@/components/SchemaSelect.vue'
import SchemaBoolean from '@/components/SchemaBoolean.vue'

const data = defineModel({ type: Object })
const props = defineProps({
    schema: {
        type: Object,
        default: {}
    }
})

const overlay = ref(false)

function scaffold(schema, func) {
    if (schema.type === 'object') {
        let output = {}
        for (let property in schema.properties) {
            output[property] = scaffold(schema.properties[property], func)
        }
        return output
    } else if (schema.type === 'array') {
        return [scaffold(schema.items, func)]
    } else {
        return func?.(schema)
    }
}

function inputComponent(schema) {
    if (schema.widget) return schema.widget
    if (schema.anyOf) return SchemaRadio
    if (schema.enum) return SchemaSelect
    switch (schema.type) {
        case 'boolean': return SchemaBoolean
        case 'integer': return SchemaNumber
        case 'number': return SchemaNumber
        default: return SchemaText
    }
}

function genItem(obj) {
    let rst = {}
    for (let key in obj) {
        rst[key] = {
            prop: key,
            value: obj[key],
            schema: props.schema.properties?.[key],
            update: val => (toValue(data) && (data.value[key] = val)),
        }
    }
    return rst
}

const items = computed(() => genItem(toValue(data) ?? scaffold(props.schema)))
</script>
