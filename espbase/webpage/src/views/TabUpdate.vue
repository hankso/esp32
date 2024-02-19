<script setup>
import { rules } from '@/utils'
import { updateOTA, execCommand } from '@/apis'

const notify = inject('notify', console.log)

const version = ref('')
const loadver = ref(false)
const loading = ref(false)
const firmware = ref([])

async function upgrade(e) {
    if (toValue(loading) !== false && upgrade.ctrl) return upgrade.ctrl.abort()
    if (!(await e).valid) return
    loading.value = 0
    upgrade.ctrl = new AbortController()
    updateOTA(toValue(firmware)[0], {
        signal: upgrade.ctrl.signal,
        onUploadProgress(e) {
            if (e.total === undefined) {
                loading.value = true
            } else {
                loading.value = e.progress * 100
            }
        },
    })
        .then(() => notify('Upgraded!'))
        .catch(({ message }) => notify(message))
        .finally(() => (loading.value = upgrade.ctrl = false))
}

function getVersion() {
    loadver.value = true
    version.value = ''
    execCommand('version')
        .then(({ data }) => (version.value = data))
        .finally(() => (loadver.value = false))
}

onMounted(getVersion)
</script>

<template>
    <v-sheet border rounded="lg">
        <v-form class="ma-4" @submit.prevent="upgrade">
            <v-file-input
                accept=".bin"
                label="Firmware *"
                v-model="firmware"
                :rules="[rules.length]"
                prepend-icon=""
                variant="outlined"
                show-size
            >
                <template #loader>
                    <ProgressBar :loading />
                </template>
            </v-file-input>
            <div class="d-flex align-center justify-space-between">
                <v-btn variant="text" @click="getVersion" :loading="loadver">
                    Version
                    <v-tooltip v-if="version" activator="parent">
                        <h3 class="text-amber">Current Version</h3>
                        <pre>{{ version }}</pre>
                        <span class="text-amber">Click to reload</span>
                    </v-tooltip>
                </v-btn>
                <v-btn type="submit" variant="outlined">
                    {{ loading === false ? 'Upgrade' : 'Cancel' }}
                </v-btn>
            </div>
        </v-form>
    </v-sheet>
</template>
