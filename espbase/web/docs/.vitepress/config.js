import { statSync } from 'node:fs'
import { resolve } from 'node:path'
import { name as title } from '../../package.json'

import { defineConfig } from 'vitepress'
import { withPwa } from '@vite-pwa/vitepress'

export default withPwa(defineConfig(() => {
    // let dist = resolve(__dirname, '..', '..', '..', 'files', 'docs')
    // if (!statSync(dist, { throwIfNoEntry: false })?.isDirectory())
    // TODO: font compression
    let dist = './.vitepress/dist'
    return {
        vite: {
            server: {
                host: '0.0.0.0',
                port: 5174,
            },
        },
        title,
        base: '/docs/',
        outDir: dist,
        lastUpdated: true,
        themeConfig: {
            footer: {
                message: 'Released under the MIT License.',
                copyright: 'Copyright 2024-PRESENT Hankso',
            },
            search: {
                provider: 'local',
            },
        },
        pwa: {
            outDir: dist,
            registerType: 'autoUpdate',
            manifest: {
                theme_color: '#ffffff',
            },
            experimental: {
                includeAllowlist: true,
            },
        },
    }
}))
