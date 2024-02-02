<script setup>
const mode = process.env.VITE_MODE
const info = process.env.BUILD_INFO
</script>

<template>
    <v-row class="justify-center">
        <v-col cols="auto">
            <p v-if="info" v-for="[k, v] of info" :key="k">
                <strong>{{ k }}: </strong>
                <span>{{ v }}</span>
            </p>
            <p v-else>
                Running in {{ mode }} mode (
                <span class="text-primary">npm run dev</span>
                ).
            </p>
        </v-col>
    </v-row>
</template>
