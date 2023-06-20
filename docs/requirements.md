---
layout: doc
---

# Requirements

### Dovecot CE v2.3.17+

- Older versions of dovecot-fts-flatcurve supported [Dovecot CE](https://dovecot.org/) < v2.3.17. Use [version 0.2.0](https://github.com/slusarz/dovecot-fts-flatcurve/releases/tag/v0.2.0) if you need support for these older Dovecot CE versions.
- It is recommended that you use the most up-to-date version of Dovecot (see https://repo.dovecot.org/). New code is developed and tested against the [Dovecot git main branch](https://github.com/dovecot/core/).
- Flatcurve relies on Dovecot's built-in FTS stemming library.
  - REQUIRES stemmer support (`--with-stemmer`)
  - Optional icu support (`--with-icu`)
  - Optional libtextcat support (`--with-textcat`)

### Xapian 1.2.x+

- Tested on [Xapian](https://xapian.org/) 1.2.22, 1.4.11, 1.4.18, 1.4.19, 1.4.22
- 1.4+ is required for automatic optimization support
  - 1.2.x versions require manual optimization (this is a limitation of the
    Xapian library)
