<script setup>
import Vue from '@/components/InfoVue.vue'
import Vuetify from '@/components/InfoVuetify.vue'
import EspBase from '@/components/InfoEspBase.vue'

const tab = ref(null)
const components = {
    EspBase: EspBase,
    Vue: Vue,
    Vuetify: Vuetify
}
</script>

<template>
    <v-tabs v-model="tab" align-tabs="center" class="mb-4">
        <v-tab
            v-for="name in Object.keys(components)"
            :key="name"
            :value="name"
        >
            {{ name }}
        </v-tab>
    </v-tabs>
    <transition appear mode="out-in">
        <component :is="components[tab]" />
    </transition>
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
</style>
