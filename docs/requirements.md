---
layout: doc
---

# Requirements

## [Dovecot CE](https://dovecot.org/) v2.3.17+

::: warning Dovecot CE < v2.3.17
Use [fts-flatcurve version 0.2.0](https://github.com/slusarz/dovecot-fts-flatcurve/releases/tag/v0.2.0) if you need support for older Dovecot CE versions.
:::

::: danger Dovecot CE 2.3.x ONLY!
This code only works with Dovecot CE 2.3.x releases. fts-flatcurve has been integrated with Dovecot CE 2.4 as the default FTS engine and is now maintained in [core](https://github.com/dovecot/core/).
:::

- It is recommended that you use the most up-to-date version of Dovecot (see https://repo.dovecot.org/). New code is developed and tested against the [Dovecot git main branch](https://github.com/dovecot/core/). This code appears on the `release-2.3` branch.
- Flatcurve relies on Dovecot's built-in FTS stemming library.
  - **REQUIREMENTS**
    - stemmer support (`--with-stemmer`)
  - *Optional*
    - icu support (`--with-icu`)
    - libtextcat support (`--with-textcat`)

## Xapian 1.2.x+

- Tested on [Xapian](https://xapian.org/) 1.2.22, 1.4.11, 1.4.18, 1.4.19, 1.4.22
- 1.4+ is required for automatic optimization support
  - 1.2.x versions require manual optimization (this is a limitation of the
    Xapian library)
