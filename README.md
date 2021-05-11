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

* Dovecot 2.x+ (tested on Dovecot CE 2.3.10, 2.3.13)
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

| Field     | Description                                              |
| --------- | -------------------------------------------------------- |
| `mailbox` | The mailbox name                                         |
| `status`  | Status of rescan \[expunge_msgs\|missing_msgs\|ok\]      |
| `uids`    | The list of UIDs that triggered a non-ok status response |

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

### Indexing benchmark with substring matching DISABLED

```
Linux ... 5.4.73-1-pve #1 SMP PVE 5.4.73-1 ... x86_64 GNU/Linux
CentOS 7; Dovecot 2.3.13; Xapian 1.2.22
VM:
  - 2x Intel(R) Core(TM) i7-4770 CPU @ 3.40GHz
  - 512 MB RAM
Using fts_flatcurve as of 22 January 2021


-- Indexing Trash Mailbox w/43120 messages
-- (e.g. this is "legitimate" mail; it does not include Spam)
$ time doveadm index Trash
doveadm(): Info: Trash: Caching mails seq=1..43120
43120/43120

real    2m3.947s   [~347 msgs/second]
user    1m47.174s
sys     0m2.083s


-- Listing Xapian files for the mailbox
$ ls -la flatcurve-index/
total 280508
drwx------ 2 vmail vmail      4096 Jan 21 23:39 .
drwx------ 3 vmail vmail      4096 Jan 21 23:39 ..
-rw------- 1 vmail vmail         0 Jan 21 23:37 flintlock
-rw------- 1 vmail vmail        28 Jan 21 23:37 iamchert
-rw------- 1 vmail vmail 206168064 Jan 21 23:39 postlist.DB
-rw------- 1 vmail vmail      2779 Jan 21 23:39 postlist.baseA
-rw------- 1 vmail vmail      3164 Jan 21 23:39 postlist.baseB
-rw------- 1 vmail vmail    548864 Jan 21 23:39 record.DB
-rw------- 1 vmail vmail        23 Jan 21 23:39 record.baseA
-rw------- 1 vmail vmail        24 Jan 21 23:39 record.baseB
-rw------- 1 vmail vmail  80478208 Jan 21 23:39 termlist.DB
-rw------- 1 vmail vmail      1156 Jan 21 23:39 termlist.baseA
-rw------- 1 vmail vmail      1246 Jan 21 23:39 termlist.baseB


-- Compacting mailbox (Xapian 1.2 does not support auto-compaction)
$ xapian-compact flatcurve-index flatcurve-index-compact
postlist: Reduced by 63% 127584K (201336K -> 73752K)
record: Reduced by 5% 32K (536K -> 504K)
termlist: Reduced by 6% 5056K (78592K -> 73536K)
position: doesn't exist
spelling: doesn't exist
synonym: doesn't exist
$ ls -la flatcurve-index-compact/
total 147828
drwx------ 2 root  root      4096 Jan 21 23:45 .
drwx------ 4 vmail vmail     4096 Jan 21 23:45 ..
-rw------- 1 root  root        28 Jan 21 23:45 iamchert
-rw------- 1 root  root  75522048 Jan 21 23:45 postlist.DB
-rw------- 1 root  root        13 Jan 21 23:45 postlist.baseA
-rw------- 1 root  root      1171 Jan 21 23:45 postlist.baseB
-rw------- 1 root  root    516096 Jan 21 23:45 record.DB
-rw------- 1 root  root        13 Jan 21 23:45 record.baseA
-rw------- 1 root  root        23 Jan 21 23:45 record.baseB
-rw------- 1 root  root  75300864 Jan 21 23:45 termlist.DB
-rw------- 1 root  root        13 Jan 21 23:45 termlist.baseA
-rw------- 1 root  root      1167 Jan 21 23:45 termlist.baseB


-- Comparing to size of Trash mailbox
$ doveadm mailbox status vsize Trash
Trash vsize=1712148272
$ echo "scale=3; (147828 * 1024) / 1712148272" | bc
.088  [Index = ~9% the size of the total mailbox data size]
```

Licensing
---------

LGPL v2.1 (see COPYING)

(c) 2020-2021 Michael Slusarz
