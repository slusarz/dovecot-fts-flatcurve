---
layout: doc
---

# doveadm Commands

This plugin implements several `fts-flatcurve` specific doveadm commands.

## `doveadm fts-flatcurve check <mailbox mask>`

Run a simple check on Dovecot Xapian databases, and attempt to fix basic
errors (it is the same checking done by the `xapian-check` command with the `F`
option).

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox that has FTS data, it outputs the following key/value fields:

| Key       | Value                                                |
| --------- | ---------------------------------------------------- |
| `mailbox` | The human-readable mailbox name. (key is hidden)     |
| `guid`    | The GUID of the mailbox.                             |
| `errors`  | The number of errors reported by the Xapian library. |
| `shards`  | The number of index shards processed.                |

## `doveadm fts-flatcurve dump [-h] <mailbox mask>`

Dump the headers or terms of the Xapian databases.

If `-h` command line option is given, a list of headers and the number of
times that header was indexed is output. Without that option, the list of
search terms are output with the number of times it appears in the databse.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

All mailboxes are processed together and a single value for all headers/terms
is given.

The following key/value fields are output:

| Key       | Value                                                 |
| --------- | ----------------------------------------------------- |
| `count`   | The number of times the header/term appears in the DB |
| `header`  | The header (if `-h` is given)                         |
| `term`    | Term (if `-h` is NOT given)                           |

## `doveadm fts-flatcurve remove <mailbox mask>`

Removes all FTS data for a mailbox.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox removed, it outputs the following key/value fields:

| Key       | Value                                            |
| --------- | ------------------------------------------------ |
| `mailbox` | The human-readable mailbox name. (key is hidden) |
| `guid`    | The GUID of the mailbox.                         |

## `doveadm fts-flatcurve rotate <mailbox mask>`

Triggers an index rotation for a mailbox.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox rotated, it outputs the following key/value fields:

| Key       | Value                                            |
| --------- | ------------------------------------------------ |
| `mailbox` | The human-readable mailbox name. (key is hidden) |
| `guid`    | The GUID of the mailbox.                         |

## `doveadm fts-flatcurve stats <mailbox mask>`

Returns FTS data for a mailbox.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox that has FTS data, it outputs the following key/value fields:

| Key        | Value                                            |
| ---------- | ------------------------------------------------ |
| `mailbox`  | The human-readable mailbox name. (key is hidden) |
| `guid`     | The GUID of the mailbox.                         |
| `last_uid` | The last UID indexed in the mailbox.             |
| `messages` | The number of messages indexed in the mailbox.   |
| `shards`   | The number of index shards.                      |
| `version`  | The (Dovecot internal) version of the FTS data.  |

