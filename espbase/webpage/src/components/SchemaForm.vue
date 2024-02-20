<template>
    <v-form>
        <template v-for="item in items" :key="item.name">
            <slot :name="item.name" v-bind="item">
                <v-list-item
                    :title="item.schema?.title ?? item.name"
                    :subtitle="item.schema?.description"
                >
                    <template #append>
                        <component
                            v-if="item.schema"
                            :is="inputType(item.schema)"
                            v-bind="item"
                        ></component>
                        <template v-else>{{ item.value }}</template>
                    </template>
                </v-list-item>
            </slot>
        </template>

        <slot name="actions">
            <div class="d-flex align-center border-t pa-2 mt-4">
                <small class="me-auto ps-2">* indicates required field</small>
                <v-btn variant="text" @click="overlay = true">Schema</v-btn>
                <v-btn
                    v-if="backup"
                    variant="text"
                    @click="Object.assign(data, backup)"
                >
                    Reset
                </v-btn>
                <v-btn variant="text" color="blue" type="submit">Submit</v-btn>
            </div>
        </slot>

        <slot name="schema">
            <v-overlay
                contained
                v-model="overlay"
                class="fix-content rounded-lg"
                scroll-strategy="reposition"
            >
                <v-container class="hide-scrollbar h-100 bg-grey rounded-lg">
                    <pre>{{ schema }}</pre>
                </v-container>
            </v-overlay>
        </slot>
    </v-form>
</template>

<script setup>
import SchemaText from '@/components/SchemaText.vue'
import SchemaNumber from '@/components/SchemaNumber.vue'
import SchemaSelect from '@/components/SchemaSelect.vue'
import SchemaBoolean from '@/components/SchemaBoolean.vue'

const data = defineModel({ type: Object })
const props = defineProps({
    schema: {
        type: Object,
        default: () => ({}),
    },
    backup: {
        type: Object,
        default: undefined,
    },
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

function inputType(schema) {
    if (schema.widget) return schema.widget
    if (schema.enum) return SchemaSelect
    switch (schema.type) {
        case 'boolean':
            return SchemaBoolean
        case 'integer':
            return SchemaNumber
        case 'number':
            return SchemaNumber
        case 'string':
            if (schema?.pattern?.includes('01yn')) return SchemaBoolean
        // fall through
        default:
            return SchemaText
    }
}

function genItem(obj) {
    let rst = {}
    for (let key in obj) {
        rst[key] = {
            name: key,
            value: obj[key],
            schema: props.schema.properties?.[key],
            update: val => toValue(data) && (data.value[key] = val),
        }
    }
    return rst
}

const items = computed(() => genItem(toValue(data) ?? scaffold(props.schema)))
</script>

<style scoped>
.v-form {
    position: relative; /* for v-overlay contained */
}
.v-overlay {
    z-index: 999 !important; /* put it below v-app-bar */
}
.v-list-item:first-child {
    padding-top: 16px;
}
.v-list-item:last-child {
    padding-bottom: 16px;
}
</style>

<style>
.fix-content .v-overlay__content {
    position: static;
    margin: 4em;
    width: 100%;
}
</style>
