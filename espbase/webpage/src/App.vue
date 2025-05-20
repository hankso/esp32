<script setup>
import { isAPmode } from '@/apis'

import { useDark, useToggle } from '@vueuse/core'
import { useTheme, useDisplay } from 'vuetify'
import {
    mdiConsole,
    mdiUsbPort,
    mdiKeyboardOutline,
    mdiFileTree,
    mdiPencilBoxMultiple,
    mdiCog,
    mdiUpdate,
    mdiInformation,
    mdiCompare,
    mdiMultimedia,
} from '@mdi/js'

const desc =
    import.meta.env.MODE +
    (process.env.BUILD_INFO?.SOURCE
        ? `-${process.env.BUILD_INFO.SOURCE}`
        : '') +
    ` v${process.env.PROJECT_VERSION}`

const { lg } = useDisplay()
const theme = useTheme()
const isDark = useDark({
    onChanged(dark) {
        theme.global.name.value = dark ? 'dark' : 'light'
    },
})
const toggleDark = useToggle(isDark)

provide('theme', { isDark, toggleDark })

const title = `${process.env.PROJECT_NAME} WebUI`
const apmode = ref(true)
const drawer = ref(false)

function updateAP() {
    // unlock some features if http client is connected from AP
    isAPmode()
        .then(() => (apmode.value = true))
        .catch(() => (apmode.value = false))
}

function parseLink(title, links) {
    return [
        { type: 'divider' },
        { type: 'subheader', title },
        ...links.map(([t, to, prependIcon]) => ({
            title: t,
            props: {
                to,
                color: 'primary',
                prependIcon,
            },
        })),
    ]
}

const aponly = parseLink('AP mode', [
    ['File Manager', '/fileman', mdiFileTree],
    ['Online Editor', '/editor', mdiPencilBoxMultiple],
    ['Configuration', '/configs', mdiCog],
    ['Firmware OTA', '/updation', mdiUpdate],
    ['About', '/about', mdiInformation],
])

const staonly = parseLink('STA mode', [
    ['Web Console', '/home', mdiConsole],
    ['Web Serial', '/serial', mdiUsbPort],
    ['Web Socket', '/jsonrpc', mdiKeyboardOutline],
    ['Web Stream', '/stream', mdiMultimedia],
])

const progbar = ref(false)

provide('progbar', progbar)

const snackbar = ref({
    show: false,
    message: '',
    timeout: 0,
})

provide('notify', function (msg, timeout = 5000) {
    if (msg === undefined) return false
    snackbar.value.message = `${msg}`
    snackbar.value.timeout = timeout - 1
    nextTick(() => (snackbar.value.timeout = timeout))
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
    updateAP()
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
                <v-list-item :title :subtitle="desc">
                    <template #append v-if="!lg">
                        <v-icon
                            size="small"
                            icon="$close"
                            @click="drawer = false"
                        ></v-icon>
                    </template>
                </v-list-item>
            </v-list>
            <v-list nav :items="staonly.concat(apmode ? aponly : [])"></v-list>
        </v-navigation-drawer>

        <v-app-bar border="b" elevation="0" density="comfortable" :title>
            <ProgressBar :loading="progbar" class="position-absolute" />
            <template #prepend>
                <v-app-bar-nav-icon
                    @click="drawer = !drawer"
                    @keyup.esc="drawer = false"
                ></v-app-bar-nav-icon>
            </template>
            <template #append>
                <v-btn
                    @click="updateAP"
                    variant="outlined"
                    :color="apmode ? 'success' : 'grey'"
                >
                    AP mode
                </v-btn>
                <v-divider vertical class="mx-2"></v-divider>
                <v-btn icon @click="toggleDark()">
                    <v-icon :icon="mdiCompare"></v-icon>
                    <v-tooltip activator="parent" location="bottom">
                        Toggle theme
                    </v-tooltip>
                </v-btn>
            </template>
        </v-app-bar>

        <v-main id="main-content">
            <v-container class="h-100 position-relative">
                <RouterView />
            </v-container>
        </v-main>
    </v-app>
</template>
