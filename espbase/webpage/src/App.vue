<script setup>
import { useTheme } from 'vuetify'

import {
    mdiHome,
    mdiFileTree,
    mdiPencilBoxMultiple,
    mdiCog,
    mdiUpdate,
    mdiInformation,
} from '@mdi/js'

var desc = process.env.VITE_MODE
if (process.env.SRC_VER) {
    desc += '-' + process.env.SRC_VER
}

const theme = useTheme()

const drawer = ref()
const appbar = ref(false)
const apmode = ref(true) // TODO: get STA/AP mode

const admin = [
    ['File Manager', '/fileman', mdiFileTree],
    ['Online Editor', '/editor', mdiPencilBoxMultiple],
    ['Configuration', '/config', mdiCog],
    ['Upgradation', '/update', mdiUpdate],
    ['About', '/about', mdiInformation],
].map(([t, l, i]) => ({
    title: t,
    props: {
        prependIcon: i,
        color: 'primary',
        to: l,
    },
}))

const items = computed(() => {
    return [
        {
            title: 'ESP Base',
            props: {
                subtitle: desc,
                prependIcon: mdiHome,
                to: '/home',
            },
        },
        { type: 'divider' },
        { type: 'subheader', title: apmode.value ? 'AP mode' : '' },
        ...(apmode.value ? admin : []),
    ]
})

function toggleTheme() {
    theme.global.name.value = theme.global.current.value.dark ? 'light' : 'dark'
}

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
        callback && callback(dialog.value)
    }
    dialog.value.persist = persistent
    return (dialog.value.show = true)
})

onMounted(() => {
    // maybe redirected from other html subpage
    let redirect = new URLSearchParams(location.search).get('to')
    redirect && useRouter().push(redirect)
})
</script>

<template>
    <a class="skip-link" href="#main-content">Skip to main content</a>
    <v-app>
        <v-app-bar v-if="appbar"></v-app-bar>

        <v-navigation-drawer v-else v-model="drawer">
            <v-list nav :items="items"></v-list>
            <template #append>
                <v-btn @click="toggleTheme">Toggle theme</v-btn>
                <v-btn block color="primary" @click.stop="drawer = !drawer">
                    Collapse
                </v-btn>
            </template>
        </v-navigation-drawer>

        <v-main id="main-content">
            <router-view></router-view>
        </v-main>

        <v-snackbar v-model="snackbar.show" :timeout="snackbar.timeout">
            <template #actions>
                <v-btn
                    color="primary"
                    variant="text"
                    @click="snackbar.show = false"
                >
                    Close
                </v-btn>
            </template>
            {{ snackbar.message }}
        </v-snackbar>

        <v-dialog
            v-model="dialog.show"
            :persistent="dialog.persist"
            width="auto"
        >
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
    </v-app>
</template>

<style scoped>
.skip-link {
    line-height: 100%;
    position: absolute;
    text-decoration: none;
    left: 50%;
    top: 10px;
    z-index: 9999;
    transform: translate(-50%, -200%); /* align center and hide to top */
    transition: 0.5s;
}
.skip-link:focus {
    transform: translate(-50%, 0%);
}
</style>
