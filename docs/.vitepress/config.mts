import { defineConfig } from 'vitepress'
import { pagefindPlugin } from 'vitepress-plugin-pagefind'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  base: '/dovecot-fts-flatcurve/',
  title: "FTS Flatcurve",
  lang: 'en-US',
  description: "Documentation for the fts-flatcurve Dovecot plugin",
  srcExclude: ['/DOCS.md'],

  vite: {
    plugins: [pagefindPlugin()],
  },

  sitemap: {
    hostname: 'https://slusarz.github.io/dovecot-fts-flatcurve/',
  },

  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Configuration', link: '/configuration' }
    ],

    sidebar: [
      {
        text: 'Introduction',
        collapsed: false,
        items: [
          { text: 'What is Flatcurve?', link: '/what' },
          { text: 'Why "Flatcurve"?', link: '/why' },
        ]
      },
      {
        text: 'Installation',
        collapsed: false,
        items: [
          { text: 'Requirements', link: '/requirements' },
          { text: 'Compilation', link: '/compilation' },
          { text: 'Docker', link: '/docker' },
          { text: 'Configuration', link: '/configuration' },
        ]
      },
      {
        text: 'Operation',
        collapsed: false,
        items: [
          { text: 'Doveadm', link: '/doveadm' },
          { text: 'Events', link: '/events' },
          { text: 'Debugging', link: '/debug' },
        ]
      },
      {
        text: 'Technical',
        collapsed: false,
        items: [
          { text: 'Benchmarks', link: '/benchmarks' },
          { text: 'Data Storage', link: '/datastorage' },
          { text: 'Design', link: '/design' },
          { text: 'Testing', link: '/testing' },
        ]
      },
      {
        items: [
          { text: 'Thanks', link: '/thanks' },
          { text: 'License', link: '/license' },
        ]
      },
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/slusarz/dovecot-fts-flatcurve/' }
    ],

    outline: 'deep',
    externalLinkIcon: true
  }
})
