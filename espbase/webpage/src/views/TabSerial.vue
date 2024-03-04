<script setup>
import { name } from '@/../package.json'
import { escape, strftime } from '@/utils'
import SerialController from '@/utils/serial'

import { mdiCog, mdiOctagon, mdiCloseOctagon } from '@mdi/js'

const title = name + ' Serial'
const serial = new SerialController()
const opened = serial.opened

const notify = inject('notify', console.log)

const dialog = ref(false)
const filter = ref(serial.portIds?.[0] ?? '')
const welcome = ref([])
const terminal = ref(null)

const info = computed(() => {
    let opt = serial.options
    return `${opt.dataBits}${opt.parity[0].toUpperCase()}${opt.stopBits}`
})

function request() {
    serial
        .request()
        .catch(notify)
        .finally(() => {
            if (!toValue(filter) && serial.portIds.length)
                filter.value = serial.portIds[0]
        })
}

function print(msg, timestamp = true) {
    msg = msg.trimEnd()
    if (!msg || !toValue(terminal)) return
    msg.split('\n').forEach(line => {
        if (timestamp) line = strftime('%T ') + line
        toValue(terminal).pushMessage({ type: 'ansi', content: line })
    })
}

function toggle(val) {
    if (val ?? !toValue(opened)) {
        return serial
            .open(serial.options, toValue(filter))
            .then(function readloop() {
                serial
                    .read()
                    .then(msg => print(msg ?? '') || readloop())
                    .catch(e => print(escape(e, 31)))
            })
            .catch(notify)
    } else {
        return serial.close()
    }
}

onBeforeUnmount(() => toggle(false))
</script>

<template>
    <v-sheet border rounded="lg" class="overflow-hidden h-100">
        <CommandLine
            ref="terminal"
            :title
            prompt=""
            :welcome
            :callback="(k, c) => serial.write(c + '\n')"
            :used-time="false"
            :default-commands="false"
        >
            <template #header>
                <div class="t-header">
                    <h4 style="display: inline-block">
                        <span class="t-disable-select cursor-pointer">
                            {{ title }}
                        </span>
                    </h4>
                    <ul class="t-shell-dots d-flex">
                        <v-btn
                            size="xs"
                            :icon="mdiCog"
                            variant="plain"
                            @click="dialog = true"
                        ></v-btn>
                        <v-btn
                            size="xs"
                            variant="plain"
                            :color="`${opened ? 'red' : 'green'}-lighten-1`"
                            :icon="opened ? mdiCloseOctagon : mdiOctagon"
                            @click="toggle()"
                        ></v-btn>
                        <h4 v-if="opened" class="ma-1">
                            {{ serial.options.baudRate }}
                            {{ info }}
                        </h4>
                    </ul>
                </div>
            </template>
        </CommandLine>
    </v-sheet>
    <v-dialog v-model="dialog" width="auto">
        <v-card>
            <SchemaForm
                v-model="serial.options"
                :schema="serial.schema"
                @submit.prevent="toggle(true).then(() => (dialog = false))"
            >
                <v-list-item title="USB Filter" subtitle="Vendor:Product ID">
                    <template #append>
                        <v-btn
                            text="auth"
                            variant="outlined"
                            @click="request"
                        ></v-btn>
                        <SchemaSelect
                            class="form-values"
                            no-data-text="Click AUTH to add"
                            :value="filter"
                            :schema="{ enum: serial.portIds }"
                            :update="val => (filter = val ?? '')"
                        />
                    </template>
                </v-list-item>
                <template #buttons>
                    <v-btn
                        text="Cancel"
                        variant="text"
                        @click="dialog = false"
                    ></v-btn>
                </template>
            </SchemaForm>
        </v-card>
    </v-dialog>
</template>

<style scoped>
.t-shell-dots .v-btn {
    margin-top: 1.5px;
    margin-left: 2.5px;
    color: var(--t-header-font-color);
}
</style>
