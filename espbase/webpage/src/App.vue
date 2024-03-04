<script setup>
import { name, version } from '@/../package.json'

import { isApmode } from '@/apis'

import { useTheme, useDisplay } from 'vuetify'
import {
    mdiConsole,
    mdiUsbPort,
    mdiFileTree,
    mdiPencilBoxMultiple,
    mdiCog,
    mdiUpdate,
    mdiInformation,
    mdiCompare,
} from '@mdi/js'

var desc = process.env.VITE_MODE
if (process.env.SRC_VER) {
    desc += '-' + process.env.SRC_VER
}

const theme = useTheme()
const { lg } = useDisplay()

const title = `${name} WebUI`
const apmode = ref(true)
const drawer = ref(false)

const aponly = [
    { type: 'divider' },
    { type: 'subheader', title: 'AP mode' },
    ...[
        ['File Manager', '/fileman', mdiFileTree],
        ['Online Editor', '/editor', mdiPencilBoxMultiple],
        ['Configuration', '/configs', mdiCog],
        ['Firmware OTA', '/updation', mdiUpdate],
        ['About', '/about', mdiInformation],
    ].map(([t, l, i]) => ({
        title: t,
        props: {
            prependIcon: i,
            color: 'primary',
            to: l,
        },
    })),
]

const items = computed(() => [
    { type: 'divider' },
    { type: 'subheader', title: 'STA mode' },
    {
        title: 'Web Console',
        props: {
            prependIcon: mdiConsole,
            to: '/home',
        },
    },
    {
        title: 'Web Serial',
        props: {
            prependIcon: mdiUsbPort,
            to: '/serial',
        },
    },
    ...(toValue(apmode) ? aponly : []),
])

function toggleTheme() {
    theme.global.name.value = theme.global.current.value.dark ? 'light' : 'dark'
}

const progbar = ref(false)
provide('progbar', progbar)

const snackbar = ref({
    show: false,
    message: '',
    timeout: 0,
})

provide('notify', function (msg, timeout = 3000) {
    if (msg === undefined || snackbar.value.show) return false
    snackbar.value.message = `${msg}`
    snackbar.value.timeout = timeout
    return (snackbar.value.show = true)
})

const dialog = ref({
    show: false,
    message: '',
    callback: null,
    persist: false,
})

provide('confirm', function (msg, callback, persistent = false) {
    if (msg === undefined || dialog.value.show) return false
    dialog.value.message = `${msg}`
    dialog.value.callback = () => {
        dialog.value.show = false
        callback?.(dialog.value)
    }
    dialog.value.persist = persistent
    return (dialog.value.show = true)
})

onMounted(() => {
    // maybe redirected from other html subpages
    let redirect = new URLSearchParams(location.search).get('to')
    if (redirect) return useRouter().push(redirect)
    // unlock some features if http client is connected from AP
    isApmode()
        .then(() => (apmode.value = true))
        .catch(() => (apmode.value = false))
})
</script>

<template>
    <a class="skip-link" href="#main-content">Skip to main content</a>

    <v-snackbar v-model="snackbar.show" :timeout="snackbar.timeout">
        <template #actions>
            <v-btn
                icon="$close"
                color="primary"
                @click="snackbar.show = false"
            ></v-btn>
        </template>
        {{ snackbar.message }}
    </v-snackbar>

    <v-dialog v-model="dialog.show" :persistent="dialog.persist" width="auto">
        <v-card color="red" title="Warning">
            <v-card-text style="white-space: pre">
                {{ dialog.message }}
            </v-card-text>
            <v-card-actions>
                <v-spacer></v-spacer>
                <v-btn @click="dialog.show = false">Cancel</v-btn>
                <v-btn @click="dialog.callback">Confirm</v-btn>
            </v-card-actions>
        </v-card>
    </v-dialog>

    <v-app>
        <v-navigation-drawer
            v-model="drawer"
            :rounded="lg ? 0 : 'e-lg'"
            @keyup.esc="drawer = false"
        >
            <v-list nav class="mt-n1 mb-n3">
                <v-list-item :title :subtitle="`${desc} v${version}`">
                    <template #append v-if="!lg">
                        <v-icon
                            size="small"
                            icon="$close"
                            @click="drawer = false"
                        ></v-icon>
                    </template>
                </v-list-item>
            </v-list>
            <v-list nav :items></v-list>
        </v-navigation-drawer>

        <v-app-bar border="b" elevation="0" density="comfortable" :title>
            <ProgressBar :loading="progbar" style="position: absolute" />
            <template #prepend>
                <v-app-bar-nav-icon
                    @click="drawer = !drawer"
                    @keyup.esc="drawer = false"
                ></v-app-bar-nav-icon>
            </template>
            <template #append>
                <v-btn variant="text">STA mode</v-btn>
                <v-btn variant="text" v-if="apmode">AP mode</v-btn>
                <v-divider vertical class="mx-2"></v-divider>
                <v-btn icon @click="toggleTheme">
                    <v-icon :icon="mdiCompare"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Toggle theme
                    </v-tooltip>
                </v-btn>
            </template>
        </v-app-bar>

        <v-main id="main-content">
            <v-container class="h-100">
                <RouterView />
            </v-container>
        </v-main>
    </v-app>
</template>

<style scoped>
.v-container:has(> .v-row) {
    display: flex;
}
</style>
