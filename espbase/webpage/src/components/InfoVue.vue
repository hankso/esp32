<script setup>
import { version } from 'vue'

import {
    // create-vue/IconDocumentation.vue
    mdiBookOpenVariant,

    // create-vue/IconTooling.vue
    mdiToolboxOutline,

    // create-vue/IconEcosystem.vue
    mdiCollage,

    // create-vue/IconCommunity.vue
    mdiForum,

    // create-vue/IconSupport.vue
    mdiHeart,
} from '@mdi/js'
</script>

<template>
    <div class="wrapper">
        <div class="greetings">
            <svg
                xmlns="http://www.w3.org/2000/svg"
                viewBox="0 0 261.76 226.69"
                class="logo"
                width="125"
                height="125"
                alt="Vue logo"
            >
                <path
                    d="M161.096.001l-30.225 52.351L100.647.001H-.005l130.877 226.688L261.749.001z"
                    fill="#41b883"
                />
                <path
                    d="M161.096.001l-30.225 52.351L100.647.001H52.346l78.526 136.01L209.398.001z"
                    fill="#34495e"
                />
            </svg>
            <h2>
                JS Powered by
                <a href="https://vitejs.dev/" target="_blank" rel="noopener">
                    Vite
                </a>
                and
                <a href="https://vuejs.org/" target="_blank" rel="noopener">
                    Vue {{ version }}
                </a>
            </h2>
        </div>
        <div>
            <WelcomeItem>
                <template #icon><v-icon :icon="mdiBookOpenVariant" /></template>
                <template #heading>Documentation</template>

                Vue’s
                <a href="https://vuejs.org/" target="_blank" rel="noopener">
                    official documentation
                </a>
                provides you with all information you need to get started.
            </WelcomeItem>

            <WelcomeItem>
                <template #icon><v-icon :icon="mdiToolboxOutline" /></template>
                <template #heading>Tooling</template>

                This project is served and bundled with
                <a
                    href="https://vitejs.dev/guide/features.html"
                    target="_blank"
                    rel="noopener"
                    >Vite</a
                >. The recommended IDE setup is
                <a
                    href="https://code.visualstudio.com/"
                    target="_blank"
                    rel="noopener"
                    >VSCode</a
                >
                +
                <a
                    href="https://github.com/johnsoncodehk/volar"
                    target="_blank"
                    rel="noopener"
                    >Volar</a
                >. If you need to test your components and web pages, check out
                <a href="https://www.cypress.io/" target="_blank" rel="noopener"
                    >Cypress</a
                >
                and
                <a
                    href="https://on.cypress.io/component"
                    target="_blank"
                    rel="noopener"
                    >Cypress Component Testing</a
                >.

                <br />

                More instructions are available in <code>README.md</code>.
            </WelcomeItem>

            <WelcomeItem>
                <template #icon><v-icon :icon="mdiCollage" /></template>
                <template #heading>Ecosystem</template>

                Get official tools and libraries for your project:
                <a
                    href="https://pinia.vuejs.org/"
                    target="_blank"
                    rel="noopener"
                    >Pinia</a
                >,
                <a
                    href="https://router.vuejs.org/"
                    target="_blank"
                    rel="noopener"
                    >Vue Router</a
                >,
                <a
                    href="https://test-utils.vuejs.org/"
                    target="_blank"
                    rel="noopener"
                    >Vue Test Utils</a
                >, and
                <a
                    href="https://github.com/vuejs/devtools"
                    target="_blank"
                    rel="noopener"
                    >Vue Dev Tools</a
                >. If you need more resources, we suggest paying
                <a
                    href="https://github.com/vuejs/awesome-vue"
                    target="_blank"
                    rel="noopener"
                    >Awesome Vue</a
                >
                a visit.
            </WelcomeItem>

            <WelcomeItem>
                <template #icon><v-icon :icon="mdiForum" /></template>
                <template #heading>Community</template>

                Got stuck? Ask your question on
                <a href="https://chat.vuejs.org" target="_blank" rel="noopener"
                    >Vue Land</a
                >, our official Discord server, or
                <a
                    href="https://stackoverflow.com/questions/tagged/vue.js"
                    target="_blank"
                    rel="noopener"
                    >StackOverflow</a
                >. You should also subscribe to
                <a href="https://news.vuejs.org" target="_blank" rel="noopener"
                    >our mailing list</a
                >
                and follow the official
                <a
                    href="https://twitter.com/vuejs"
                    target="_blank"
                    rel="noopener"
                    >@vuejs</a
                >
                twitter account for latest news in the Vue world.
            </WelcomeItem>

            <WelcomeItem>
                <template #icon><v-icon :icon="mdiHeart" /></template>
                <template #heading>Support Vue</template>

                As an independent project, Vue relies on community backing for
                its sustainability. You can help us by
                <a
                    href="https://vuejs.org/sponsor/"
                    target="_blank"
                    rel="noopener"
                    >becoming a sponsor</a
                >.
            </WelcomeItem>
        </div>
    </div>
</template>

<style scoped>
.logo {
    display: block;
    margin: 0 auto 0.5em;
}

h2 {
    text-align: center;
}

a {
    text-decoration: none;
    color: hsla(160, 100%, 37%, 1);
    transition: 0.4s;
    padding: 3px;
}

@media (hover: hover) {
    a:hover {
        background-color: hsla(160, 100%, 37%, 0.2);
    }
}

@media (min-width: 1024px) {
    .wrapper {
        display: grid;
        grid-gap: 2em;
        grid-template-columns: 1fr 1fr;
        grid-auto-flow: column;
    }

    .greetings {
        display: flex;
        flex-direction: column;
        justify-content: center;
        align-items: center;
    }
}
</style>
