<script setup>
import Vue from '@/components/InfoVue.vue'
import Vuetify from '@/components/InfoVuetify.vue'
import EspBase from '@/components/InfoEspBase.vue'

const tab = ref()
const components = {
    EspBase: EspBase,
    Vue: Vue,
    Vuetify: Vuetify,
}
</script>

<template>
    <v-tabs v-model="tab" align-tabs="center">
        <v-tab
            v-for="name in Object.keys(components)"
            :key="name"
            :value="name"
        >
            {{ name }}
        </v-tab>
    </v-tabs>
    <v-container>
        <transition appear mode="out-in">
            <component :is="components[tab]" />
        </transition>
    </v-container>
</template>

<style scoped>
.v-enter-active,
.v-leave-active {
    transition: opacity 0.3s ease;
}

.v-enter-from,
.v-leave-to {
    opacity: 0;
}

.v-container:has(> .v-row) {
    display: flex;
    height: calc(100% - 48px);
    max-height: 70vh;
}
</style>
