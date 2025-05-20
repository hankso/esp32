<template>
    <v-form class="schema-form fix-schema-form-content rounded-lg">
        <slot />

        <template v-for="item in items" :key="item.name">
            <slot :name="item.name" v-bind="item">
                <v-list-item
                    v-if="item.schema || !hideUnknown"
                    :subtitle="item.schema?.description"
                >
                    <template #title>
                        {{ item.schema?.title ?? item.name }}
                        {{ item.required ? '*' : '' }}
                    </template>
                    <template #append>
                        <div class="form-values">
                            <component
                                v-bind="item"
                                v-if="item.schema"
                                :is="inputType(item.schema)"
                            ></component>
                            <template v-else>
                                {{ item.value }}
                            </template>
                        </div>
                    </template>
                </v-list-item>
            </slot>
        </template>

        <slot name="actions">
            <div class="d-flex align-center border-t pa-2 mt-4">
                <small v-if="schema?.required" class="ps-2">
                    * indicates required field
                </small>
                <v-spacer></v-spacer>
                <slot name="buttons" />
                <v-btn
                    v-if="modified"
                    text="Reset"
                    variant="text"
                    @click="modified = Object.assign(data, backup) != data"
                ></v-btn>
                <v-btn
                    v-if="schema && showSchema"
                    text="Schema"
                    variant="text"
                    ref="overlay"
                ></v-btn>
                <slot name="submit">
                    <v-btn
                        color="blue"
                        type="submit"
                        text="Submit"
                        variant="text"
                    ></v-btn>
                </slot>
            </div>
        </slot>

        <slot name="schema">
            <v-overlay
                contained
                :activator="$refs.overlay"
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
import SchemaEInput from '@/components/SchemaEInput.vue'
import SchemaNumber from '@/components/SchemaNumber.vue'
import SchemaSelect from '@/components/SchemaSelect.vue'
import SchemaString from '@/components/SchemaString.vue'
import SchemaBoolean from '@/components/SchemaBoolean.vue'

const data = defineModel({ type: Object })
const props = defineProps({
    schema: {
        type: Object,
        default: undefined,
    },
    backup: {
        type: Object,
        default: undefined,
    },
    nullVal: {
        type: null,
        default: null,
    },
    showSchema: {
        type: Boolean,
        default: false,
    },
    hideUnknown: {
        type: Boolean,
        default: false,
    },
})

const modified = ref(false)

function scaffold(schema, func) {
    if (!schema) return
    if (schema.type === 'object') {
        let output = {}
        for (let property in schema.properties) {
            output[property] = scaffold(schema.properties[property], func)
        }
        return output
    } else if (schema.type === 'array') {
        return [scaffold(schema.items, func)]
    } else {
        return func ? func(schema) : props.nullVal
    }
}

function inputType(schema) {
    if (schema.widget) return schema.widget
    if (schema.enum) {
        if (['integer', 'number', 'string'].includes(schema.type))
            return SchemaEInput
        return SchemaSelect
    }
    switch (schema.type) {
        case 'boolean':
            return SchemaBoolean
        case 'integer':
            return SchemaNumber
        case 'number':
            return SchemaNumber
        case 'string':
            if (schema.pattern?.includes('01yn')) return SchemaBoolean
        // fall through
        default:
            return SchemaString
    }
}

function genItem(obj) {
    let rst = {}
    for (let key in obj) {
        rst[key] = {
            name: key,
            value: obj[key],
            schema: props.schema?.properties?.[key],
            update(val) {
                if (!toValue(data)) return
                if (val?.trim) val = val.trim()
                data.value[key] = val ?? props.nullVal
                if (props.backup && props.backup[key] != data.value[key])
                    modified.value = true
            },
            required: props.schema?.required?.includes(key) ?? false,
        }
    }
    return rst
}

const items = computed(() => genItem(toValue(data) ?? scaffold(props.schema)))
</script>

<style scoped>
.v-form {
    position: relative; /* for v-overlay contained */
    overflow-x: hidden;
    padding-top: 12px;
}

.v-overlay {
    z-index: 999 !important; /* put it below v-app-bar */
}
</style>

<style>
.v-list-item__append .form-values {
    width: 25vw;
    min-width: 100px;
    max-width: 300px;
    margin-left: 2em;
    overflow-x: auto;
    text-align: end;
}

.fix-schema-form-content .v-overlay__content {
    position: static;
    margin: 4em;
    width: calc(100% - 8em);
}

.fix-schema-form-content .v-list-item-subtitle {
    word-break: auto-phrase;
}
</style>
