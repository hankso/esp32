<template>
    <v-sheet border rounded="lg">
        <SchemaForm
            v-model="config"
            :schema
            :backup
            show-schema
            @submit.prevent="submit"
        />
    </v-sheet>
    <v-sheet border rounded="lg" class="mt-4">
        <MediaStream
            :video="video ? 'media?video=mjpg' : ''"
            :audio="audio ? 'media?audio=wav' : ''"
            :show-audio="!video"
        />
    </v-sheet>
</template>

<script setup>
import { isEmpty, deepcopy } from '@/utils'
import { getSchema, getMedia, setCamera } from '@/apis'

const notify = inject('notify', console.log)

const video = ref(false)
const audio = ref(false)
const schema = ref({})
const config = ref({})
const backup = ref({})

async function submit(e) {
    if (e && !(await e).valid) return
    setCamera(toValue(config))
        .then(() => {
            notify('Camera param updated!')
            refresh()
        })
        .catch(({ message }) => notify(message))
}

function refresh() {
    if (isEmpty(toValue(schema))) {
        getSchema('camera')
            .then(({ data }) => (schema.value = data))
            .catch(({ message }) => notify(message))
    }
    getMedia()
        .then(([vresp, aresp]) => {
            video.value = vresp.status === 200
            audio.value = aresp.status === 200
            if (toValue(video)) {
                config.value = vresp.data
                if (isEmpty(toValue(backup)))
                    backup.value = deepcopy(vresp.data)
            }
        })
        .catch(({ message }) => notify(message))
}

onMounted(refresh)
</script>
