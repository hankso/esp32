<script setup>
const route = useRoute()
const router = useRouter()
const timeout = ref(route.query.timeout ? route.query.timeout : 10)

onMounted(function redirect() {
    if (timeout.value < 0) {
        return
    } else if (timeout.value > 1) {
        timeout.value--
    } else {
        router.push('/home')
    }
    setTimeout(redirect, 1000)
})
</script>

<template>
    <v-row class="fill-height align-center justify-center">
        <v-col cols="auto">
            <p class="text-h3 text-primary">Sorry! 404 Error.</p>
            <p class="text-h5 my-4" v-if="$route.query.reason">
                Access denied:
                <span class="text-error">{{ $route.query.reason }}</span>
            </p>
            <p class="text-h5 my-4" v-else>
                Page
                <span class="text-primary">{{
                    $route.query.url
                        ? $route.query.url
                        : $route.redirectedFrom
                          ? $route.redirectedFrom.substr(1)
                          : ''
                }}</span>
                not found!
            </p>
            <p class="text-body-1">
                Go back to <a href="/home">Home</a> in {{ timeout }}s ...
            </p>
        </v-col>
    </v-row>
</template>
