---
layout: doc
---

# Events

This plugin emits [events](https://doc.dovecot.org/admin_manual/event_design/)
with the category `fts-flatcurve` (a child of the category `fts`).

The following named events are emitted:

## `fts_flatcurve_expunge`

Emitted when a message is expunged from a mailbox.

| Field     | Description                              |
| --------- | ---------------------------------------- |
| `mailbox` | The mailbox name                         |
| `uid`     | The UID that was expunged from FTS index |

## `fts_flatcurve_index`

Emitted when a message is indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The UID that was added to the FTS index |

## `fts_flatcurve_last_uid`

Emitted when the system queries for the last UID indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The last UID contained in the FTS index |

## `fts_flatcurve_optimize`

Emitted when a mailbox is optimized.

| Field     | Description      |
| --------- | ---------------- |
| `mailbox` | The mailbox name |

## `fts_flatcurve_query`

Emitted when a query is completed.

| Field     | Description                            | Options     |
| --------- | -------------------------------------- | ----------- |
| `count`   | The number of messages matched         |             |
| `mailbox` | The mailbox name                       |             |
| `maybe`   | Are the results uncertain?             | `yes`, `no` |
| `query`   | The query text sent to Xapian          |             |
| `uids`    | The list of UIDs returned by the query |             |

## `fts_flatcurve_rescan`

Emitted when a rescan is completed.

| Field      | Description                                              | Options                               |
| ---------- | -------------------------------------------------------- | ------------------------------------- |
| `expunged` | The list of UIDs that were expunged during rescan        |                                       |
| `mailbox`  | The mailbox name                                         |                                       |
| `status`   | Status of rescan                                         | `expunge_msgs`, `missing_msgs`,  `ok` |
| `uids`     | The list of UIDs that triggered a non-ok status response |                                       |

## `fts_flatcurve_rotate`

Emitted when a mailbox has its underlying Xapian DB rotated.

| Field     | Description      |
| --------- | ---------------- |
| `mailbox` | The mailbox name |
