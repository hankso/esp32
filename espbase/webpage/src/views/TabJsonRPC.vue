<template>
    <p>TODO: chatbot WebSocket {{ url }} {{ status }}</p>
</template>

<script setup>
import { useWebSocket } from '@vueuse/core'

const url = ref(`ws://${location.host}/ws`)

const { status, data, send } = useWebSocket(url, {
    autoReconnect: {
        retries: 3,
        delay: 500,
        onFailed: () => alert('Failed to connect to WebSocket'),
    },
})

function call(id, method) {
    send(JSON.stringify({ id, method, params: Array(...arguments).slice(2) }))
}

function notify(method) {
    send(JSON.stringify({ method, params: Array(...arguments).slice(1) }))
}

// debug
window._debug = { url, status, data, send, call, notify }
</script>
