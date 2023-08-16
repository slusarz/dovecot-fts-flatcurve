import { defineConfig } from 'vitepress'

import { createWriteStream } from 'node:fs'
import { resolve } from 'node:path'
import { SitemapStream } from 'sitemap'

const links = []

// https://vitepress.dev/reference/site-config
export default defineConfig({
  base: '/dovecot-fts-flatcurve/',
  title: "FTS Flatcurve",
  lang: 'en-US',
  description: "Documentation for the fts-flatcurve Dovecot plugin",
  srcExclude: ['/DOCS.md'],

  transformHtml: (_, id, { pageData }) => {
    if (!/[\\/]404\.html$/.test(id))
      links.push({
        url: pageData.relativePath.replace(/\/index\.md$/, '/').replace(/\.md$/, '.html'),
        lastmod: pageData.lastUpdated
      })
  },
  buildEnd: async ({ outDir }) => {
    const sitemap = new SitemapStream({
      hostname: 'https://slusarz.github.io/dovecot-fts-flatcurve/'
    })
    const writeStream = createWriteStream(resolve(outDir, 'sitemap.xml'))
    sitemap.pipe(writeStream)
    links.forEach((link) => sitemap.write(link))
    sitemap.end()
    await new Promise((r) => writeStream.on('finish', r))
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

    outline: 'deep',
    externalLinkIcon: true
  }
})
