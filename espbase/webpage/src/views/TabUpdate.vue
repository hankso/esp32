<script setup>
import { update } from '@/apis'
import { rules, type } from '@/utils'

const notify = inject('notify', console.log)

const loading = ref(false)
const firmware = ref([])

async function upgrade(e) {
    if (toValue(loading) !== false && upgrade.ctrl) return upgrade.ctrl.abort()
    if (!(await e).valid) return
    loading.value = 0
    upgrade.ctrl = new AbortController()
    update(toValue(firmware)[0], {
        signal: upgrade.ctrl.signal,
        onUploadProgress(e) {
            if (e.total === undefined) {
                loading.value = true
            } else {
                loading.value = e.progress * 100
            }
        }
    })
        .then(() => notify('Upgraded!'))
        .catch(({ message }) => notify(message))
        .finally(() => (loading.value = upgrade.ctrl = false))
}
</script>

<template>
    <v-sheet border rounded="lg" elevation="1" class="ma-4">
        <v-form class="ma-4" @submit.prevent="upgrade">
            <v-file-input
                label="Firmware *"
                v-model="firmware"
                :rules="[rules.length]"
                prepend-icon=""
                variant="outlined"
                show-size
            >
                <template v-slot:loader>
                    <ProgressBar :loading />
                </template>
            </v-file-input>
            <div class="d-flex align-center justify-space-between">
                <small>* indicates required field</small>
                <v-btn type="submit" variant="outlined">
                    {{ loading === false ? 'Upgrade' : 'Cancel' }}
                </v-btn>
            </div>
        </v-form>
    </v-sheet>
    TODO: fetch runtime info (lshw, lsver, etc) to display
</template>
