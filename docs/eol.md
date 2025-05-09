---
layout: doc
---

# FTS Flatcurve (Dovecot CE 2.3.x) EOL (End of Life)

::: danger FTS Flatcurve
FTS Flatcurve for Dovecot Community Edition (CE) is EOL (end of life).

There will be no further updates or fixes to this repository absent
critical security issues!
:::

## Dovecot CE 2.4

FTS Flatcurve became the default Dovecot Community Edition (CE) FTS driver
in v2.4.x ([merged into Dovecot core in April 2022](https://github.com/dovecot/core/commit/137572e77fdf79b2e8d607021667741ed3f19da1)).

ALL development of Flatcurve in the future will be done in
[Dovecot CE core](https://github.com/dovecot/core/).

Every Dovecot CE user is recommended to upgrade to 2.4.x, as this is the
only currently maintained branch.

## Known Issues

::: info
These are known issues with Flatcurve in Dovecot 2.3.

This is not an exhaustive list - there may be other issues.
:::

* Flatcurve will fail if multiple mailbox directories require updates within
  a single session.
  * See https://github.com/slusarz/dovecot-fts-flatcurve/issues/40
  * This is fixed in Dovecot CE 2.4.2+.
