// Styles
import '@/assets/base.css'

// Plugins
import router from '@/router'
import vuetify from '@/plugins/vuetify'

import { createApp } from 'vue'
import App from '@/App.vue'

createApp(App).use(router).use(vuetify).mount('#app')
