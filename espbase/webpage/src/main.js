// Styles
import '@/assets/base.css'

// Plugins
import router from '@/router'
import vuetify from '@/plugins/vuetify'
import { createTerminal } from 'vue-web-terminal'

import App from '@/App.vue'
import { createApp } from 'vue'

createApp(App).use(router).use(vuetify).use(createTerminal()).mount('#app')
