<template>
    <v-form>
        <template v-for="(val, key) in items" :key>
            <p>{{ key }}: {{ val }}</p>
        </template>
        <pre>
            Schema is:
            {{ schema }}
        </pre>
    </v-form>
</template>

<script setup>
const data = defineModel({ type: Object })
const props = defineProps({
    schema: {
        type: Object,
        default: {}
    }
})

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
        return func && func(schema)
    }
}

const items = computed(() => (data.value ? data.value : scaffold(props.schema)))
</script>
