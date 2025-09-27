<template>
    <v-sheet border rounded="lg" class="mb-4 overflow-hidden">
        <MediaStream
            :video="video ? 'media?video=mjpg' : ''"
            :audio="audio ? 'media?audio=wav' : ''"
            :show-audio="!video"
        />
    </v-sheet>
    <v-expansion-panels v-if="video">
        <v-expansion-panel>
            <v-expansion-panel-title v-slot="{ expanded }">
                Camera configuration
                <span class="mx-auto" v-if="expanded">
                    {{ strftime('%F %T.%t', now) }}
                </span>
            </v-expansion-panel-title>
            <v-expansion-panel-text>
                <SchemaForm
                    v-model="config"
                    :schema
                    :backup
                    show-schema
                    @submit.prevent="submit"
                />
            </v-expansion-panel-text>
        </v-expansion-panel>
    </v-expansion-panels>
</template>

<script setup>
import { isEmpty, deepcopy, strftime } from '@/utils'
import { getSchema, getMedia, setCamera } from '@/apis'

import { useNow } from '@vueuse/core'

const notify = inject('notify', console.log)

const now = useNow()
const video = ref(false)
const audio = ref(false)
const schema = ref({})
const config = ref({})
const backup = ref({})

async function submit(e) {
    if (e && !(await e).valid) return
    setCamera(config.value)
        .then(() => {
            notify('Camera param updated!')
            refresh()
        })
        .catch(({ message }) => notify(message))
}

function refresh() {
    getMedia()
        .then(([vresp, aresp]) => {
            video.value = vresp.status === 200
            audio.value = aresp.status === 200
            if (!video.value) return
            let sizes = null
            if ('framesizes' in vresp.data) {
                sizes = vresp.data.framesizes.map(([w, h]) => `${w} x ${h}`)
                delete vresp.data.framesizes
            }
            config.value = vresp.data
            if (isEmpty(backup.value)) backup.value = deepcopy(vresp.data)
            if (isEmpty(schema.value)) {
                getSchema('camera')
                    .then(({ data }) => {
                        schema.value = data
                        schema.value.properties.framesize.enum = sizes
                    })
                    .catch(({ message }) => notify(message))
            } else if (sizes) {
                schema.value.properties.framesize.enum = sizes
            }
        })
        .catch(({ message }) => notify(message))
}

onMounted(refresh)
</script>
