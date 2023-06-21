import { defineConfig } from 'vitepress'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  base: '/dovecot-fts-flatcurve/',
  title: "FTS Flatcurve",
  lang: 'en-US',
  description: "Documentation for the fts-flatcurve Dovecot plugin",
  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Configuration', link: '/configuration' }
    ],

    sidebar: [
      {
        text: 'Introduction',
        items: [
          { text: 'What is Flatcurve?', link: '/what' },
          { text: 'Why "Flatcurve"?', link: '/why' },
        ]
      },
      {
        text: 'Installation',
        items: [
          { text: 'Requirements', link: '/requirements' },
          { text: 'Compilation', link: '/compilation' },
          { text: 'Docker', link: '/docker' },
          { text: 'Configuration', link: '/configuration' },
        ]
      },
      {
        text: 'Operation',
        items: [
          { text: 'Doveadm', link: '/doveadm' },
          { text: 'Events', link: '/events' },
          { text: 'Debugging', link: '/debug' },
        ]
      },
      {
        text: 'Technical',
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

    search: {
      provider: 'local'
    },

    outline: 'deep'
  }
})
