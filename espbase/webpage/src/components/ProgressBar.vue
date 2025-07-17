<template>
    <v-progress-linear
        v-if="loading === true"
        class="progress-bar"
        indeterminate
        :color
    ></v-progress-linear>
    <v-progress-linear
        v-else-if="loading !== false && !vertical"
        class="progress-bar"
        :model-value="percent"
        :stream
        :color
    ></v-progress-linear>
    <div
        v-else-if="loading !== false && vertical"
        class="bg-grey-lighten-2 position-relative h-100 mx-2"
        style="width: 4px"
    >
        <div
            class="w-100 bg-primary position-absolute"
            :style="{ height: percent + '%', bottom: 0 }"
        ></div>
    </div>
</template>

<script setup>
const props = defineProps({
    color: {
        type: String,
        default: 'primary',
    },
    stream: {
        type: Boolean,
        default: true,
    },
    loading: {
        type: [Boolean, Number],
        required: true,
    },
    vertical: {
        type: Boolean,
        default: false,
    },
})

const percent = computed(() => {
    if (typeof props.loading !== 'number') return 0
    let val = props.loading > 1 ? props.loading : props.loading * 100
    return Math.max(0, Math.min(val, 100))
})
</script>
