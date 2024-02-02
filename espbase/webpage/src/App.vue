<script setup>
import {
    mdiHome,
    mdiFileTree,
    mdiPencilBoxMultiple,
    mdiCog,
    mdiUpdate,
    mdiInformation
} from '@mdi/js'

var desc = process.env.VITE_MODE
if (process.env.SRC_VER) {
    desc += '-' + process.env.SRC_VER
}

const apmode = ref(true) // TODO: get STA/AP mode
const appbar = ref(false)
const drawer = ref()

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
        to: l
    }
}))

const items = computed(() => {
    return [
        {
            title: 'ESP Base',
            props: {
                subtitle: desc,
                prependIcon: mdiHome, // TODO: change to project logo
                to: '/home'
            }
        },
        { type: 'divider' },
        { type: 'subheader', title: apmode.value ? 'AP mode' : '' },
        ...(apmode.value ? admin : [])
    ]
})

</script>

<template>
    <a class="skip-link" href="#main-content">Skip to main content</a>
    <v-app>
        <v-app-bar v-if="appbar"> </v-app-bar>

        <v-navigation-drawer v-else v-model="drawer" class="bg-grey-lighten-4">
            <v-list nav :items="items"></v-list>

            <template #append>
                <v-btn block color="primary" @click.stop="drawer = !drawer">
                    Collapse
                </v-btn>
            </template>
        </v-navigation-drawer>
        <v-main id="main-content">
            <router-view></router-view>
        </v-main>
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
