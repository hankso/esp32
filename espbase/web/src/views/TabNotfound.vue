<script setup>
import { pause } from '@/utils'

const route = useRoute()
const router = useRouter()

const timeout = ref(route.query.timeout ?? 10)

const progbar = inject('progbar')

onMounted(async function () {
    let value = toValue(timeout)
    if (value <= 0) return
    if (value > 1000) value /= 1000
    for (let i = 0; i < value; i += 1) {
        if (progbar) progbar.value = i / value
        await pause(1000)
        timeout.value = value - i
    }
    progbar.value = false
    router.push('/home')
})
</script>

<template>
    <v-row class="align-center justify-center text-center h-100">
        <v-col cols="auto">
            <p class="text-h3">404 Error</p>
            <p class="text-h5 my-4" v-if="$route.query.reason">
                Access denied:
                <span>{{ $route.query.reason }}</span>
            </p>
            <p class="text-h5 my-4" v-else>
                Page
                <span class="text-error">{{
                    $route.query.url
                        ? $route.query.url
                        : $route.redirectedFrom
                          ? $route.redirectedFrom.substr(1)
                          : ''
                }}</span>
                not found
            </p>
            <v-divider class="my-4"></v-divider>
            <p class="text-body-1">
                <v-btn to="/home" variant="outlined">Go back home</v-btn>
            </p>
        </v-col>
    </v-row>
</template>
