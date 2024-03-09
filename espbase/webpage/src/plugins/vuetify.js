import 'vuetify/styles'

import { createVuetify } from 'vuetify'
import { aliases, mdi } from 'vuetify/iconsets/mdi-svg' // internally used icons

export default createVuetify({
    icons: {
        defaultSet: 'mdi',
        aliases,
        sets: {
            mdi,
        },
    },
    theme: {
        themes: {
            light: {
                colors: {
                    'surface-light': '#F2F2F2',
                },
            },
        },
    },
})
