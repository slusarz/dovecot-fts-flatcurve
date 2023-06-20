---
layout: doc
---

# Data Storage

Xapian search data is stored separately for each mailbox.

The data is stored under a 'fts-flatcurve' directory in the [Dovecot index file location for the mailbox](https://doc.dovecot.org/configuration_manual/mail_location/#index-files).

The Xapian library is responsible for all data stored in that directory - no Dovecot code directly writes to any file.
