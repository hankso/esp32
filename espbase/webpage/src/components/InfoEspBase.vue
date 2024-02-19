<script setup>
import { unescape } from '@/utils'
import { execCommand } from '@/apis'

const notify = inject('notify', console.log)

const mode = process.env.VITE_MODE
const info = process.env.BUILD_INFO

const runtime = ref({
    ap: '',
    sta: '',
    lshw: '',
    lsmem: '',
    lspart: '',
    lstask: '',
    update: '',
    version: '',
})

onMounted(() => {
    let cmds = Object.keys(toValue(runtime))
    Promise.all(cmds.map(cmd => execCommand(cmd)))
        .then(rsts => {
            for (let [idx, rst] of rsts.entries()) {
                if (!rst.data.includes('Unrecognized command'))
                    runtime.value[cmds[idx]] = unescape(rst.data)
            }
        })
        .catch(({ message }) => notify(message))
})
</script>

<template>
    <v-row class="align-start justify-center">
        <v-col cols="6" lg="5">
            <template v-if="info">
                <v-row v-for="(val, key) of info" :key>
                    <v-col cols="6" class="text-right">
                        <strong>{{ key }}: </strong>
                    </v-col>
                    <v-col cols="6" class="text-left">
                        <span>{{ val }}</span>
                    </v-col>
                </v-row>
            </template>
            <p v-else>
                Running in {{ mode }} mode (
                <span class="text-primary">npm run dev</span>
                ).
            </p>
        </v-col>
        <v-col cols="12" lg="7">
            <v-expansion-panels>
                <template v-for="(rst, cmd) in runtime" :key="cmd">
                    <v-expansion-panel v-if="rst.length" :title="cmd">
                        <v-expansion-panel-text>
                            <pre class="overflow-auto">{{ rst }}</pre>
                        </v-expansion-panel-text>
                    </v-expansion-panel>
                </template>
            </v-expansion-panels>
        </v-col>
    </v-row>
</template>
