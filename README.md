FTS Flatcurve plugin for Dovecot
================================

***fts-flatcurve will become the default Dovecot Community Edition (CE) FTS driver
in v2.4 (merged into Dovecot core in April 2022:
https://github.com/dovecot/core/commit/137572e77fdf79b2e8d607021667741ed3f19da1).
fts-flatcurve will continue to be maintained in this repository
for backwards support with Dovecot CE v2.3.x. However, it is possible that configuration
and features may differ between this v2.3 code and core v2.4 code.***

What?
-----

This is a Dovecot FTS plugin to enable message indexing using the
[Xapian](https://xapian.org/) Open Source Search Engine Library.

The plugin relies on Dovecot to do the necessary stemming. It is intended
to act as a simple interface to the Xapian storage/search query
functionality.

This driver supports match scoring and substring matches, which means it is RFC
3501 (IMAP4rev1) compliant (although substring searches are off by default). This
driver does not support fuzzy searches, as there is no built-in support in Xapian
for it.

The driver passes all of the [ImapTest](https://imapwiki.org/ImapTest) search
tests.

Why Flatcurve?
--------------

This plugin was originally written during the initial stages of the 2020
Coronavirus pandemic.

Get it?

For details on design philosophy, see
https://github.com/slusarz/dovecot-fts-flatcurve/issues/4#issuecomment-902425597.


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
* Xapian 1.2.x+ (tested on Xapian 1.2.22, 1.4.11, 1.4.18, 1.4.19)
  - 1.4+ is required for automatic optimization support
    - 1.2.x versions require manual optimization (this is a limitation of the
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

### FTS-Flatcurve Plugin Settings

**The default parameters should be fine for most people.**

#### ***fts_flatcurve_commit_limit***

* Default: `500`
* Value: integer, set to `0` to use the Xapian default

Commit database changes after this many documents are updated. Higher commit
limits will result in faster indexing for large transactions (i.e. indexing a
large mailbox) at the expense of high memory usage. The default value should
be sufficient to allow indexing in a 256 MB maximum size process.

#### ***fts_flatcurve_max_term_size***

* Default: `30`
* Value: integer, maximum `200`

The maximum number of characters in a term to index.

#### ***fts_flatcurve_min_term_size***

* Default: `2`
* Value: integer

The minimum number of characters in a term to index.

#### ***fts_flatcurve_optimize_limit***

* Default: `10`
* Value: integer, set to 0 to disable

Once the database reaches this number of shards, automatically optimize the DB
at shutdown.

#### ***fts_flatcurve_rotate_size***

* Default: `5000`
* Value: integer, set to `0` to disable rotation

When the "current" fts database reaches this number of messages, it is rotated
to a read-only database and replaced by a new write DB. Most people should not
change this setting.

#### ***fts_flatcurve_rotate_time***

* Default: `5000`
* Value: integer, set to `0` to disable rotation

When the "current" fts database exceeds this length of time (in msecs) to
commit changes, it is rotated to a read-only database and replaced by a new
write DB. Most people should not change this setting.

#### ***fts_flatcurve_substring_search***

* Default: `no`
* Value: boolean (`yes` or `no`)

If enabled, allows substring searches (RFC 3501 compliant). However, this
requires significant additional storage space. Most users today expect
"Google-like" behavior, which is prefix searching, so substring searching is
arguably not the "modern, expected" behavior. Therefore, even though it
is not strictly RFC compliant, prefix (non-substring) searching is enabled
by default.


### FTS-Flatcurve Plugin Settings Example

```
mail_plugins = $mail_plugins fts fts_flatcurve

plugin {
  fts = flatcurve
  
  # All of these are optional, and indicate the default values.
  # They are listed here for documentation purposes; most people should
  # not need to define/override in their config.
  fts_flatcurve_commit_limit = 500
  fts_flatcurve_max_term_size = 30
  fts_flatcurve_min_term_size = 2
  fts_flatcurve_optimize_limit = 10
  fts_flatcurve_rotate_size = 5000
  fts_flatcurve_rotate_time = 5000
  fts_flatcurve_substring_search = no
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

#### ***fts_flatcurve_expunge***

Emitted when a message is expunged from a mailbox.

| Field     | Description                              |
| --------- | ---------------------------------------- |
| `mailbox` | The mailbox name                         |
| `uid`     | The UID that was expunged from FTS index |

#### ***fts_flatcurve_index***

Emitted when a message is indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The UID that was added to the FTS index |

#### ***fts_flatcurve_last_uid***

Emitted when the system queries for the last UID indexed.

| Field     | Description                             |
| --------- | --------------------------------------- |
| `mailbox` | The mailbox name                        |
| `uid`     | The last UID contained in the FTS index |

#### ***fts_flatcurve_optimize***

Emitted when a mailbox is optimized.

| Field     | Description      |
| --------- | ---------------- |
| `mailbox` | The mailbox name |

#### ***fts_flatcurve_query***

Emitted when a query is completed.

| Field     | Description                            |
| --------- | -------------------------------------- |
| `count`   | The number of messages matched         |
| `mailbox` | The mailbox name                       |
| `maybe`   | Are the results uncertain? \[yes\|no\] |
| `query`   | The query text sent to Xapian          |
| `uids`    | The list of UIDs returned by the query |

#### ***fts_flatcurve_rescan***

Emitted when a rescan is completed.

| Field      | Description                                              |
| ---------- | -------------------------------------------------------- |
| `expunged` | The list of UIDs that were expunged during rescan        |
| `mailbox`  | The mailbox name                                         |
| `status`   | Status of rescan \[expunge_msgs\|missing_msgs\|ok\]      |
| `uids`     | The list of UIDs that triggered a non-ok status response |

#### ***fts_flatcurve_rotate***

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


doveadm Commands
----------------

This plugin implements several `fts-flatcurve` specific doveadm commands.

### `doveadm fts-flatcurve check <mailbox mask>`

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

### `doveadm fts-flatcurve dump [-h] <mailbox mask>`

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

### `doveadm fts-flatcurve remove <mailbox mask>`

Removes all FTS data for a mailbox.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox removed, it outputs the following key/value fields:

| Key       | Value                                            |
| --------- | ------------------------------------------------ |
| `mailbox` | The human-readable mailbox name. (key is hidden) |
| `guid`    | The GUID of the mailbox.                         |

### `doveadm fts-flatcurve rotate <mailbox mask>`

Triggers an index rotation for a mailbox.

`<mailbox mask>` is the list of mailboxes to process. It is possible to use
wildcards (`*` and `?`) in this value.

For each mailbox rotated, it outputs the following key/value fields:

| Key       | Value                                            |
| --------- | ------------------------------------------------ |
| `mailbox` | The human-readable mailbox name. (key is hidden) |
| `guid`    | The GUID of the mailbox.                         |

### `doveadm fts-flatcurve stats <mailbox mask>`

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
- Marco Bettini, who did the heavy lifting necessary to merge this code into
  Dovecot core; most backported fixes from 2.4 is due to his work.
- Timo Siriainen for helping Marco with code review and cleaning up rough
  edges in the design.


Benchmarking
------------

### Indexing benchmark with substring matching ENABLED

```
Linux 5.14.18-300.fc35.x86_64 (Fedora 35)
Dovecot 2.3.17; Xapian 1.4.18
Host CPU: AMD RYZEN 7 1700 8-Core 3.0 GHz (3.7 GHz Turbo)
Using fts_flatcurve as of 20 November 2021

-- Indexing Trash Mailbox w/25867 messages
-- (e.g. this is "legitimate" mail; it does not include Spam)
-- FTS index deleted before run (Dovecot caches NOT deleted)
-- Dovecot plugin configuration: "fts_flatcurve ="
-- Limit process to 256 MB
$ ulimit -v 256000 && /usr/bin/time -v doveadm index -u foo Trash
	User time (seconds): 200.83
	System time (seconds): 2.79
	Percent of CPU this job got: 99%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 3:24.66
	Maximum resident set size (kbytes): 104972
	Minor (reclaiming a frame) page faults: 26176
	Voluntary context switches: 39
	Involuntary context switches: 1569
	File system outputs: 2410928

Median throughput: ~125 msgs/second

$ doveadm fts-flatcurve stats -u foo Trash
Trash guid=72dfe40cb7f4996156000000da7fd742 last_uid=25867 messages=25867 shards=6 version=1

-- Compacting mailbox
$ du -s fts-flatcurve/
753448 fts-flatcurve/
$ /usr/bin/time -v doveadm fts optimize -u foo
	User time (seconds): 5.87
	System time (seconds): 0.48
	Percent of CPU this job got: 99%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 0:06.39
        Maximum resident set size (kbytes): 13024
        Minor (reclaiming a frame) page faults: 1202
        Voluntary context switches: 7
        Involuntary context switches: 109
        File system outputs: 1240504
$ du -s fts-flatcurve/
399476 fts-flatcurve/

-- Comparing to size of Trash mailbox
$ doveadm mailbox status -u foo vsize Trash
Trash vsize=1162552360
$ echo "scale=3; (512348 * 1024) / 1162426786" | bc
.351  [Index = ~35% the size of the total mailbox data size]
```

### Indexing benchmark with substring matching DISABLED (*DEFAULT* configuration)

```
Linux 5.14.18-300.fc35.x86_64 (Fedora 35)
Dovecot 2.3.17; Xapian 1.4.18
Host CPU: AMD RYZEN 7 1700 8-Core 3.0 GHz (3.7 GHz Turbo)
Using fts_flatcurve as of 20 November 2021

-- Indexing Trash Mailbox w/25867 messages
-- (e.g. this is "legitimate" mail; it does not include Spam)
-- FTS index deleted before run (Dovecot caches NOT deleted)
-- Dovecot plugin configuration: "fts_flatcurve = substring_search=no"
-- Limit process to 256 MB
$ ulimit -v 256000 && /usr/bin/time -v doveadm index -u foo Trash
	User time (seconds): 93.90
	System time (seconds): 1.18
	Percent of CPU this job got: 99%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 1:35.52
        Maximum resident set size (kbytes): 46316
        Minor (reclaiming a frame) page faults: 10224
        Voluntary context switches: 40
        Involuntary context switches: 460
        File system outputs: 3479522

Median throughput: ~270 msgs/second

$ doveadm fts-flatcurve stats -u foo Trash
Trash guid=126e7a0269fc99615c0000006d6fda7a last_uid=25867 messages=25867 shards=6 version=1

-- Compacting mailbox
$ du -s fts-flatcurve/
147400 fts-flatcurve/
$ /usr/bin/time -v doveadm fts optimize -u foo
	User time (seconds): 0.82
	System time (seconds): 0.09
	Percent of CPU this job got: 98%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 0:00.93
        Maximum resident set size (kbytes): 13104
        Minor (reclaiming a frame) page faults: 1162
        Voluntary context switches: 7
        Involuntary context switches: 7
        File system outputs: 242472
$ du -s fts-flatcurve/
84812 fts-flatcurve/

-- Comparing to size of Trash mailbox
$ doveadm mailbox status -u foo vsize Trash
Trash vsize=1162552360
$ echo "scale=3; (84812 * 1024) / 1162552360" | bc
.074  [Index = ~7.4% the size of the total mailbox data size]
```

#### FTS Plugin configuration for the tests

```
plugin {
  fts = flatcurve
  fts_autoindex = no
  fts_enforced = yes
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords
  fts_flatcurve_substring_search = [yes|no]
  fts_index_timeout = 60s
  fts_languages = en es de
  fts_tokenizer_generic = algorithm=simple
  fts_tokenizers = generic email-address
}
```


Technical Information
---------------------

### Database Design

See https://github.com/slusarz/dovecot-fts-flatcurve/blob/master/src/fts-backend-flatcurve-xapian.cpp#L25


Licensing
---------

LGPL v2.1 (see COPYING)

(c) Michael Slusarz
