---
layout: doc
---

# Docker

A container image is generated after every commit, to allow for easy use and exploration of the code.  The image is a full Dovecot stack (w/[Pigeonhole](https://pigeonhole.dovecot.org/])), although it is not compiled with most optional features.  Only IMAP and LMTP are available, and SSL is disabled.

The image comes with a default configuration which accepts any user with password `pass`.  To customize the image, mount `/etc/dovecot` (for custom configuration) and `/srv/mail` (for persistent mail storage) volumes.

These ports are exposed by default:

| Ports | Service |
| ----- | ------- |
| 24    | LMTP    |
| 143   | IMAP    |

To run the latest version from master, use the command:

```sh
docker run ghcr.io/slusarz/dovecot-fts-flatcurve:master
```

:::details Further Details

https://github.com/users/slusarz/packages/container/package/dovecot-fts-flatcurve
:::
