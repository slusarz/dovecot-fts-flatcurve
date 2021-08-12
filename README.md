FTS Flatcurve plugin for Dovecot
================================

What?
-----

This is a Dovecot FTS plugin to enable message indexing using the
[Xapian](https://xapian.org/) Open Source Search Engine Library.

The plugin relies on Dovecot to do the necessary stemming. It is intended
to act as a simple interface to the Xapian storage/search query
functionality.

This driver supports match scoring and substring matches (on by default),
which means it is RFC 3501 (IMAP4rev1) compliant. This driver does not
support fuzzy searches.

The driver passes all of the [ImapTest](https://imapwiki.org/ImapTest) search
tests.


Why Flatcurve?
--------------

This plugin was originally written during the 2020 Coronavirus pandemic.

Get it?


Requirements
------------

* Dovecot 2.3.10+
  - It is recommended that you use the most up-to-date version of Dovecot
    (see https://repo.dovecot.org/). New code is developed and tested
    against the Dovecot git master branch (https://github.com/dovecot/core/).
  - Flatcurve relies on Dovecot's built-in FTS stemming library.
    - REQUIRES icu support (--with-icu)
    - REQUIRES stemmer support (--with-stemmer)
    - Optional libtextcat support (--with-textcat)
* Xapian 1.2.x+ (tested on Xapian 1.2.22, 1.4.11)
  - 1.4+ is required for automatic optimization support
    - older versions require manual optimization (this is a limitation of the
      Xapian library)


Compilation
-----------

If you downloaded this package using Git, you will first need to run
`autogen.sh` to generate the configure script and some other files:

```
./autogen.sh
```

The following compilation software/packages must be installed:

 - autoconf
 - automake
 - libtool
 - GNU make

After this script is executed successfully, `configure` needs to be executed
with the following parameters:

 - `--with-dovecot=<path>`

   Path to the dovecot-config file. This can either be a compiled dovecot
   source tree or point to the location where the dovecot-config file is
   installed on your system (typically in the `$prefix/lib/dovecot` directory).

When these parameters are omitted, the configure script will try to find thex
local Dovecot installation implicitly.

For example, when compiling against compiled Dovecot sources:

```
./configure --with-dovecot=../dovecot-src
```

Or when compiling against a Dovecot installation:

```
./configure --with-dovecot=/path/to/dovecot
```

To compile and install, execute the following:

```
make
sudo make install
```

Configuration
-------------

See https://doc.dovecot.org/configuration_manual/fts/ for configuration
information regarding general FTS plugin options.

Note: flatcurve REQUIRES the core
[Dovecot FTS stemming](https://doc.dovecot.org/configuration_manual/fts/tokenization/)
feature.

Flatcurve provies a single plugin option for configuration: `fts_flatcurve`.

Optional parameters for the `fts_flatcurve` plugin setting:

 - `commit_limit` - Commit database changes after this many documents are
                    updated. Higher commit limits will result in faster
                    indexing for large transactions (i.e. indexing a large
                    mailbox) at the expense of high memory usage. The default
                    value should be sufficient to allow indexing in a 256 MB
                    maximum size process. (integer, set to 0 to use the
                    Xapian default; DEFAULT: 500)
 - `max_term_size` - The maximum number of characters in a term to index.
		     (integer, maximum 200; DEFAULT: 30) 
 - `min_term_size` - The minimum number of characters in a term to index.
		     (integer; DEFAULT: 2)
 - `optimize_limit` - Once the database reaches this number of shards,
                      automatically optimize the DB at shutdown. (integer,
                      set to 0 to disable; DEFAULT: 10)
 - `rotate_size` - When the mail ("current") database reaches this number
                   of messages, it is rotated to a read-only database and
                   replaced by a new write DB. Most people should not
                   change this setting. (integer, set to 0 to disable
                   rotation; DEFAULT: 5000)
 - `rotate_time` - When the mail ("current") database exceeds this length
                   of time (in msecs) to commit changes, it is rotated to a
                   read-only database and replaced by a new write DB. Most
                   people should not change this setting. (integer, set to 0
                   to disable rotation; DEFAULT: 5000)
 - `substring_search` - If enabled, allows substring searches (RFC 3501
                        compliant). However, this requires significant
                        additional storage space, so substring searches can
                        be disabled, if necessary. ("yes" or "no"; DEFAULT:
                        "yes")

Example:

```
mail_plugins = $mail_plugins fts fts_flatcurve

plugin {
  fts = flatcurve
  fts_flatcurve = commit_limit=500 max_term_size=30 min_term_size=2 \
                  optimize_limit=10 rotate_size=5000 rotate_time=5000 \
                  substring_search=yes
}
```


Data Storage
------------

Xapian search data is stored separately for each mailbox.

The data is stored under a 'fts-flatcurve' directory in the Dovecot index
file location for the mailbox.  The Xapian library is responsible for all
data stored in that directory - no Dovecot code directly writes to any file.


Logging/Events
--------------

This plugin emits [events](https://doc.dovecot.org/admin_manual/event_design/)
with the category `fts-flatcurve` (a child of the category `fts`).

### Named Events

The following named events are emitted:

#### fts_flatcurve_expunge

Emitted when a message is expunged from a mailbox.

| Field     | Description                              |
| --------- | ---------------------------------------- |
| `mailbox` | The mailbox name                         |
| `uid`     | The UID that was expunged from FTS index |

#### fts_flatcurve_index

Emitted when a message is indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The UID that was added to the FTS index |

#### fts_flatcurve_last_uid

Emitted when the system queries for the last UID indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The last UID contained in the FTS index |

#### fts_flatcurve_optimize

Emitted when a mailbox is optimized.

| Field     | Description      |
| --------- | ---------------- |
| `mailbox` | The mailbox name |

#### fts_flatcurve_query

Emitted when a query is completed.

| Field     | Description                            |
| --------- | -------------------------------------- |
| `count`   | The number of messages matched         |
| `mailbox` | The mailbox name                       |
| `maybe`   | Are the results uncertain? \[yes\|no\] |
| `query`   | The query text sent to Xapian          |
| `uids`    | The list of UIDs returned by the query |

#### fts_flatcurve_rescan

Emitted when a rescan is completed.

| Field      | Description                                              |
| ---------- | -------------------------------------------------------- |
| `expunged` | The list of UIDs that were expunged during rescan        |
| `mailbox`  | The mailbox name                                         |
| `status`   | Status of rescan \[expunge_msgs\|missing_msgs\|ok\]      |
| `uids`     | The list of UIDs that triggered a non-ok status response |

#### fts_flatcurve_rotate

Emitted when a mailbox has it's underlying Xapian DB rotated.

| Field     | Description      |
| --------- | ---------------- |
| `mailbox` | The mailbox name |

### Debugging

Flatcurve outputs copious debug information.  To view, add this to
`dovecot.conf`:

```
# This requires Dovecot v2.3.13+
log_debug = category=fts-flatcurve
```

Example output (only showing selected log entries):

```
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Xapian library version: 1.4.11
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: DELETE
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 1: Expunge requested
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 2: Expunge requested
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 3: Expunge requested
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 4: Expunge requested
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Expunge mailbox=imaptest uid=1
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Opened DB (RW) mailbox=imaptest version=1; /dovecot/sdbox/foo/sdbox/mailboxes/imaptest/dbox-Mails/flatcurve-index
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: update_expunge (Document 1 not found)
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 1: Mail expunged
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Expunge mailbox=imaptest uid=2
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: update_expunge (Document 2 not found)
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 2: Mail expunged
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Expunge mailbox=imaptest uid=3
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: update_expunge (Document 3 not found)
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 3: Mail expunged
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Expunge mailbox=imaptest uid=4
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: update_expunge (Document 4 not found)
Apr 26 18:30:42 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: UID 4: Mail expunged
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: CREATE
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: APPEND
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: APPEND
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: APPEND
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: APPEND
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: Mailbox imaptest: Mailbox opened because: SELECT
Apr 26 18:30:43 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=0
Apr 26 18:30:44 indexer-worker(20875 foo)<>: Debug: Module loaded: /usr/local/lib/dovecot/lib20_fts_plugin.so
Apr 26 18:30:45 indexer-worker(20875 foo)<>: Debug: Module loaded: /usr/local/lib/dovecot/lib21_fts_flatcurve_plugin.so
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Xapian library version: 1.4.11
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: Mailbox opened because: indexing
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=0
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 1: Opened mail because: prefetch
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 1: Looked up field mime.parts from mail cache
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=0
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Indexing mailbox=imaptest uid=1
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Opened DB (RW) mailbox=imaptest version=1; /dovecot/sdbox/foo/sdbox/mailboxes/imaptest/dbox-Mails/flatcurve-index
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 2: Opened mail because: prefetch
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 2: Looked up field mime.parts from mail cache
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Indexing mailbox=imaptest uid=2
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 3: Opened mail because: prefetch
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 3: Looked up field mime.parts from mail cache
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Indexing mailbox=imaptest uid=3
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 4: Opened mail because: prefetch
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: UID 4: Looked up field mime.parts from mail cache
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: fts_flatcurve: Indexing mailbox=imaptest uid=4
Apr 26 18:30:45 indexer-worker(20875 foo)<kn+4V+TAJuN/AAAB:4MzdBtUGh2CLUQAABMskXQ>: Debug: Mailbox imaptest: Indexed 4 messages (UIDs 1..4)
Apr 26 18:30:45 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Opened DB (RO) mailbox=imaptest version=1; /dovecot/sdbox/foo/sdbox/mailboxes/imaptest/dbox-Mails/flatcurve-index
Apr 26 18:30:45 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:asdfghjkl* OR asdfghjkl*) mailbox=imaptest matches=1 uids=1:2
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:zxcvbnm* OR zxcvbnm*) mailbox=imaptest matches=2 uids=1,3:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:qwertyuiop* OR qwertyuiop*) mailbox=imaptest matches=1 uids=2:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (asdfghjkl*) mailbox=imaptest matches=1 uids=1:2
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (zxcvbnm*) mailbox=imaptest matches=2 uids=1,3:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (qwertyuiop*) mailbox=imaptest matches=1 uids=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:sdfghjk* OR sdfghjk*) mailbox=imaptest matches=1 uids=1:2
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:xcvbn* OR xcvbn*) mailbox=imaptest matches=2 uids=1,3:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (allhdrs:wertyuio* OR wertyuio*) mailbox=imaptest matches=1 uids=2:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (sdfghjk*) mailbox=imaptest matches=1 uids=1:2
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (xcvbn*) mailbox=imaptest matches=2 uids=1,3:4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Last UID mailbox=imaptest uid=4
Apr 26 18:30:46 imap(20873 foo)<kn+4V+TAJuN/AAAB>: Debug: fts_flatcurve: Query (wertyuio*) mailbox=imaptest matches=1 uids=4

```


Acknowledgements
----------------

Thanks to:

- Joan Moreau <jom@grosjo.net>;
  [fts-xapian](https://github.com/grosjo/fts-xapian) was the inspiration to
  use Xapian as the FTS library, although fts-flatcurve is not based or
  derived from that code
- Aki Tuomi <aki.tuomi@open-xchange.com> and Jeff
  Sipek <jeff.sipek@open-xchange.com>; conversations with them directly
  convinced me to pursue this project


Benchmarking
------------

### Indexing benchmark with substring matching ENABLED (default configuration)

```
Linux ... 5.4.72-microsoft-standard-WSL2 #1 SMP ... x86_64 GNU/Linux
VM Running on Docker Desktop (Windows 10)
Debian Buster; Dovecot 2.3.14; Xapian 1.4.11
Host CPU: AMD RYZEN 7 1700 8-Core 3.0 GHz (3.7 GHz Turbo)
Using fts_flatcurve as of 18 May 2021


-- Indexing Trash Mailbox w/25863 messages
-- (e.g. this is "legitimate" mail; it does not include Spam)
-- FTS index deleted before run (Dovecot caches NOT deleted)
-- Dovecot plugin configuration: "fts_flatcurve ="
-- Limit process to 256 MB 
$ ulimit -v 256000 && /usr/bin/time -v doveadm index -u foo Trash
        User time (seconds): 212.78
        System time (seconds): 14.62
        Percent of CPU this job got: 93%
        Elapsed (wall clock) time (h:mm:ss or m:ss): 4:02.48
        Maximum resident set size (kbytes): 120684
        Minor (reclaiming a frame) page faults: 29421
        Voluntary context switches: 1366
        Involuntary context switches: 165
        File system outputs: 5559904

Median throughput: ~107 msgs/second


-- Listing Xapian files for the mailbox
$ ls -laR fts-flatcurve/
fts-flatcurve:
total 2164
drwx------ 8 vmail vmail    4096 May 19 04:43 .
drwx------ 3 vmail vmail 2183168 May 19 04:46 ..
drwx------ 2 vmail vmail    4096 May 19 04:41 index.1640
drwx------ 2 vmail vmail    4096 May 19 04:40 index.4081
drwx------ 2 vmail vmail    4096 May 19 04:41 index.5954
drwx------ 2 vmail vmail    4096 May 19 04:42 index.7108
drwx------ 2 vmail vmail    4096 May 19 04:43 index.7628
drwx------ 2 vmail vmail    4096 May 19 04:43 index.current

fts-flatcurve/index.1640:
total 164740
drwx------ 2 vmail vmail      4096 May 19 04:41 .
drwx------ 8 vmail vmail      4096 May 19 04:43 ..
-rw------- 1 vmail vmail         0 May 19 04:41 flintlock
-rw------- 1 vmail vmail       113 May 19 04:41 iamglass
-rw------- 1 vmail vmail 139853824 May 19 04:41 postlist.glass
-rw------- 1 vmail vmail  28827648 May 19 04:41 termlist.glass

fts-flatcurve/index.4081:
total 203188
drwx------ 2 vmail vmail      4096 May 19 04:40 .
drwx------ 8 vmail vmail      4096 May 19 04:43 ..
-rw------- 1 vmail vmail         0 May 19 04:39 flintlock
-rw------- 1 vmail vmail       113 May 19 04:40 iamglass
-rw------- 1 vmail vmail 176029696 May 19 04:40 postlist.glass
-rw------- 1 vmail vmail  32014336 May 19 04:40 termlist.glass

fts-flatcurve/index.5954:
total 168712
drwx------ 2 vmail vmail      4096 May 19 04:41 .
drwx------ 8 vmail vmail      4096 May 19 04:43 ..
-rw------- 1 vmail vmail         0 May 19 04:40 flintlock
-rw------- 1 vmail vmail       114 May 19 04:41 iamglass
-rw------- 1 vmail vmail 143818752 May 19 04:41 postlist.glass
-rw------- 1 vmail vmail  28925952 May 19 04:41 termlist.glass

fts-flatcurve/index.7108:
total 249116
drwx------ 2 vmail vmail      4096 May 19 04:42 .
drwx------ 8 vmail vmail      4096 May 19 04:43 ..
-rw------- 1 vmail vmail         0 May 19 04:41 flintlock
-rw------- 1 vmail vmail       115 May 19 04:42 iamglass
-rw------- 1 vmail vmail 218718208 May 19 04:42 postlist.glass
-rw------- 1 vmail vmail  36356096 May 19 04:42 termlist.glass

fts-flatcurve/index.7628:
total 249828
drwx------ 2 vmail vmail      4096 May 19 04:43 .
drwx------ 8 vmail vmail      4096 May 19 04:43 ..
-rw------- 1 vmail vmail         0 May 19 04:42 flintlock
-rw------- 1 vmail vmail       117 May 19 04:43 iamglass
-rw------- 1 vmail vmail 220364800 May 19 04:43 postlist.glass
-rw------- 1 vmail vmail  35438592 May 19 04:43 termlist.glass

fts-flatcurve/index.current:
total 22324
drwx------ 2 vmail vmail     4096 May 19 04:43 .
drwx------ 8 vmail vmail     4096 May 19 04:43 ..
-rw------- 1 vmail vmail        0 May 19 04:43 flintlock
-rw------- 1 vmail vmail      111 May 19 04:43 iamglass
-rw------- 1 vmail vmail 18685952 May 19 04:43 postlist.glass
-rw------- 1 vmail vmail  4161536 May 19 04:43 termlist.glass


-- Compacting mailbox
$ du -s fts-flatcurve/
1057888 fts-flatcurve/
$ /usr/bin/time -v doveadm fts optimize -u foo
        User time (seconds): 10.13
        System time (seconds): 1.13
        Percent of CPU this job got: 88%
        Elapsed (wall clock) time (h:mm:ss or m:ss): 0:12.66
        Maximum resident set size (kbytes): 14372
        Minor (reclaiming a frame) page faults: 1162
        Voluntary context switches: 1154
        Involuntary context switches: 1
        File system outputs: 1670056
$ du -s fts-flatcurve/
512348 fts-flatcurve/
$ ls -laR fts-flatcurve/
fts-flatcurve/:
total 2144
drwx------ 3 vmail vmail    4096 May 19 04:52 .
drwx------ 3 vmail vmail 2183168 May 19 04:46 ..
drwx------ 2 vmail vmail    4096 May 19 04:52 index.4595

fts-flatcurve/index.4595:
total 512348
drwx------ 2 vmail vmail      4096 May 19 04:52 .
drwx------ 3 vmail vmail      4096 May 19 04:52 ..
-rw------- 1 vmail vmail         0 May 19 04:52 flintlock
-rw------- 1 vmail vmail       110 May 19 04:52 iamglass
-rw------- 1 vmail vmail 334757888 May 19 04:52 postlist.glass
-rw------- 1 vmail vmail 189874176 May 19 04:52 termlist.glass


-- Comparing to size of Trash mailbox
$ doveadm mailbox status -u foo vsize Trash
Trash vsize=1162426786
$ echo "scale=3; (512348 * 1024) / 1162426786" | bc
.451  [Index = ~45% the size of the total mailbox data size]
```

### Indexing benchmark with substring matching DISABLED (non-default configuration)

```
Linux ... 5.4.72-microsoft-standard-WSL2 #1 SMP ... x86_64 GNU/Linux
VM Running on Docker Desktop (Windows 10)
Debian Buster; Dovecot 2.3.14; Xapian 1.4.11
Host CPU: AMD RYZEN 7 1700 8-Core 3.0 GHz (3.7 GHz Turbo)
Using fts_flatcurve as of 18 May 2021


-- Indexing Trash Mailbox w/25863 messages
-- (e.g. this is "legitimate" mail; it does not include Spam)
-- FTS index deleted before run (Dovecot caches NOT deleted)
-- Dovecot plugin configuration: "fts_flatcurve = substring_search=no"
-- Limit process to 256 MB 
$ ulimit -v 256000 && /usr/bin/time -v doveadm index -u foo Trash
        User time (seconds): 79.13
        System time (seconds): 0.94
        Percent of CPU this job got: 96%
        Elapsed (wall clock) time (h:mm:ss or m:ss): 1:23.07
        Maximum resident set size (kbytes): 48088
        Minor (reclaiming a frame) page faults: 11366
        Voluntary context switches: 3455
        Involuntary context switches: 104
        File system outputs: 790328


Median throughput: ~311 msgs/second


-- Compacting mailbox
$ du -s fts-flatcurve/
169364 fts-flatcurve/
$ /usr/bin/time -v doveadm fts optimize -u foo
        User time (seconds): 1.06
        System time (seconds): 0.13
        Percent of CPU this job got: 77%
        Elapsed (wall clock) time (h:mm:ss or m:ss): 0:01.54
        Maximum resident set size (kbytes): 14532
        Minor (reclaiming a frame) page faults: 1154
        Voluntary context switches: 832
        Involuntary context switches: 1
        File system outputs: 278728
$ du -s fts-flatcurve/
96300 fts-flatcurve/


-- Comparing to size of Trash mailbox
$ doveadm mailbox status -u foo vsize Trash
Trash vsize=1162426786
$ echo "scale=3; (96300 * 1024) / 1162426786" | bc
.084  [Index = ~8.4% the size of the total mailbox data size]
```

As can be seen, substring matching in Xapian requires significantly more
resources (mainly CPU and disk storage).

Licensing
---------

LGPL v2.1 (see COPYING)

(c) 2020-2021 Michael Slusarz
