---
layout: doc
---

# What is Flatcurve?

::: danger EOL (End of Life) Notice
See [Flatcurve Dovecot CE 2.3 EOL Notice](eol).
:::

This is a [Dovecot](https://dovecot.org/) FTS (Full Text Search) plugin to enable message indexing using the [Xapian](https://xapian.org/) Open Source Search Engine Library.

The plugin relies on Dovecot to do the necessary stemming. It is intended to act as a simple interface to the Xapian storage/search query functionality.

This driver supports match scoring and substring matches, which means it is [RFC 3501 (IMAP4rev1)](https://datatracker.ietf.org/doc/html/rfc3501) compliant (although substring searches are off by default). This driver does not support fuzzy searches, as there is no built-in support in Xapian for it.

The driver passes all of the [ImapTest](https://imapwiki.org/ImapTest) search tests.
