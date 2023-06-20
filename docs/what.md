---
layout: doc
---

# What is Flatcurve?

::: warning Important Note
FTS Flatcurve will become the default Dovecot Community Edition (CE) FTS driver in v2.4 ([merged into Dovecot core in April 2022](https://github.com/dovecot/core/commit/137572e77fdf79b2e8d607021667741ed3f19da1)).

FTS Flatcurve will continued to be maintained separately in GitHub for backwards support with ___Dovecot CE v2.3.x___. However, it is possible that configuration and features may differ between this v2.3 code and core v2.4 code.
:::

This is a [Dovecot](https://dovecot.org/) FTS (Full Text Search) plugin to enable message indexing using the [Xapian](https://xapian.org/) Open Source Search Engine Library.

The plugin relies on Dovecot to do the necessary stemming. It is intended to act as a simple interface to the Xapian storage/search query functionality.

This driver supports match scoring and substring matches, which means it is [RFC 3501 (IMAP4rev1)](https://datatracker.ietf.org/doc/html/rfc3501) compliant (although substring searches are off by default). This driver does not support fuzzy searches, as there is no built-in support in Xapian for it.

The driver passes all of the [ImapTest](https://imapwiki.org/ImapTest) search tests.
