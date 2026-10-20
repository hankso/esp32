<template>
    <v-row v-if="!gamepads.length" class="align-center justify-center">
        <h1 v-if="!isSupported" class="text-warning">
            Gamepad is not supported on this device
        </h1>
        <h2 v-else>Connect your gamepads and press button...</h2>
    </v-row>
    <v-sheet v-else border rounded="lg" class="h-100 px-4">
        <v-tabs v-model="tab" align-tabs="center" color="primary">
            <v-tab v-for="(gamepad, idx) in gamepads" :key="idx" :value="idx">
                Gamepad {{ gamepad.index + 1 }}
            </v-tab>
        </v-tabs>
        <v-divider class="mb-4" />
        <div v-if="dev">
            <h3 class="mb-4">{{ dev.id }}</h3>
            <p>Connected: {{ dev.connected }}</p>
            <p>PadLayout: {{ dev.mapping }}</p>
            <p>Timestamp: {{ dev.timestamp.toFixed(0) }}</p>
            <p>
                Vibration:
                <span
                    v-if="dev.vibrationActuator"
                    class="cursor-pointer text-warning hide-caret"
                    @click="vibrate(dev.vibrationActuator)"
                >
                    test
                </span>
                <span v-else>not supported</span>
            </p>
            <div class="d-flex flex-wrap ga-2 my-4 show-data">
                <div v-for="(btn, idx) in dev.buttons" :key="idx">
                    <ProgressBar vertical :loading="btn.value" />
                    Btn {{ idx }} <br />
                    {{ btn.value.toFixed(4) }}
                </div>
                <div style="flex-basis: 100%"></div>
                <div v-for="(val, idx) in dev.axes" :key="idx">
                    <ProgressBar vertical :loading="val / 2 + 0.5" />
                    Axes {{ idx }} <br />
                    {{ val.toFixed(4) }}
                </div>
            </div>
        </div>
        <div
            v-if="dev?.mapping === 'standard'"
            class="d-flex flex-wrap ma-5 ga-12"
        >
            <div v-for="i in range(dev.axes.length / 2)" :key="i">
                <svg
                    width="150"
                    fill="none"
                    stroke="hsla(210,90%,20%,0.2)"
                    stroke-width="1"
                >
                    <g transform="translate(75 75) scale(0.95 0.95)">
                        <circle cx="0" cy="0" r="75"></circle>
                        <line x1="0" y1="-75" x2="0" y2="75"></line>
                        <line x1="-75" y1="0" x2="75" y2="0"></line>
                        <line
                            x1="0"
                            y1="0"
                            :x2="dev.axes[2 * i + 0] * 75"
                            :y2="dev.axes[2 * i + 1] * 75"
                            stroke="hsl(210,90%,20%)"
                        ></line>
                        <circle
                            :cx="dev.axes[2 * i + 0] * 75"
                            :cy="dev.axes[2 * i + 1] * 75"
                            r="4"
                            fill="hsl(210,90%,20%)"
                        ></circle>
                    </g>
                </svg>
            </div>
            <GamePad :gamepad="dev" width="350" />
        </div>
    </v-sheet>
</template>

<script setup>
import { range } from '@/utils'

import { useGamepad } from '@vueuse/core'

const { isSupported, gamepads } = useGamepad()

const tab = ref()
const dev = computed(() => (tab.value >= 0 ? gamepads.value[tab.value] : null))

watch(
    () => gamepads.value.length,
    () => {
        if (tab.value) return
        for (let [idx, gamepad] of gamepads.value.entries()) {
            if (gamepad.mapping === 'standard') return (tab.value = idx)
        }
        for (let [idx, gamepad] of gamepads.value.entries()) {
            if (gamepad.axes.some(v => v) || gamepad.buttons.some(b => b.value))
                return (tab.value = idx)
        }
    }
)

function vibrate(act) {
    act.playEffect('dual-rumble', {
        startDelay: 0,
        duration: 1000,
        weakMagnitude: 1,
        strongMagnitude: 1,
    })
}
</script>

<style scoped>
.show-data > div {
    display: flex;
    min-width: 6em;
}
</style>
