import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vitejs.dev/config/
export default defineConfig(({ command, mode, isSsrBuild, isPreview }) => {
    let config = {
        build: {
            outDir: './dist',
            assetsDir: 'assets'
        },
        server: {
            host: '0.0.0.0'
        },
        defines: {
            __VITE_MODE__: mode,
            __VITE_COMMAND__: command
        },
        plugins: [vue()],
        resolve: {
            alias: {
                '@': fileURLToPath(new URL('./src', import.meta.url))
            }
        }
    }
    if (command === 'build') {
        config.defines['__BUILD_INFO__'] = {
            NodeJS: process.versions.node,
            OSInfo: `${process.platform}-${process.arch}`,
            BuildTime: new Date().toString()
        }
    }
    return config
})
