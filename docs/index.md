---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "FTS Flatcurve"
  text: "Dovecot (2.3.x) Full Text Search (FTS) Plugin"
  tagline: An easy-to-setup Dovecot plugin that uses Xapian to locally index messages
  image:
    src: /curve.jpg
    alt: Flatcurve
  actions:
    - theme: brand
      text: Get Started
      link: /what
    - theme: alt
      text: Configuration
      link: /configuration

features:
  - title: Xapian
    details: Uses the popular open-source Xapian library to index
    link: https://xapian.org/
    icon:
      src: ./xapian.png
      alt: Xapian Logo
  - title: Dovecot FTS Integration
    details: Fully integrated with Dovecot's core FTS functionality
    link: https://dovecot.org/
    icon:
      src: ./dovecot_logo.png
      alt: Dovecot Logo
  - title: Local Search
    details: Indexing is done on the Dovecot backend - no external service needed
    icon:
      src: ./search_icon.svg
      alt: Search Icon
  - title: RFC Compliant
    details: Supports match scoring and substring matches, making it RFC 3501 (IMAP4rev1) compliant (although substring searches are off by default)
    icon:
      src: ./doc.png
      alt: Document Icon
---
