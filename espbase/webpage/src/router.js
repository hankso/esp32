import { createRouter, createWebHistory } from 'vue-router'

// Route pages under '@/views' according to filename
let tabs = import.meta.glob('@/views/Tab*.vue'),
    apps = import.meta.glob('@/views/App*.vue')

function routePages(paths, category) {
    return Object.entries(paths).map(([path, importFunc]) => {
        let basename = path.replace(/.*\/|\.vue$/g, '').replace(category, '')
        return {
            path: basename.toLowerCase(),
            name: basename,
            category: category,
            // Route level code-splitting which generates a separate
            // chunk ([page].[hash].js) for each routes. Lazy-loaded
            // when the route is visited.
            component: importFunc
        }
    })
}

export default createRouter({
    history: createWebHistory(import.meta.env.BASE_URL),
    routes: [
        {
            path: '/',
            children: [
                { path: '', redirect: 'home' },
                { path: 'index.html', redirect: 'home' },
                ...routePages(tabs, 'Tab'),
                ...routePages(apps, 'App')
            ]
        },
        {
            path: '/redirect/:url?',
            redirect: ({ hash, params, query }) => {
                if (params.url) return { path: params.url, query, hash }
                if (query.to) return { path: query.to, hash }
                if (hash) return { path: hash.slice(1), query }
                return { path: '/404', query: { reason: 'api-only' } }
            }
        },
        {
            path: '/:url?',
            redirect: ({ hash, params, query }) => ({
                path: '/404', query: { hash, url: params.url }
            })
        }
    ]
})
