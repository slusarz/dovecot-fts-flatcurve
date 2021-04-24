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

 - `auto_optimize` - The number of database revisions that occur before the
                     index is automatically optimized in the background
                     (integer, set to 0 to disable; DEFAULT: 500)
 - `auto_optimize_msgs` - The number of messages processed in a single
                          transaction that will trigger an optimization
                          (integer, set to 0 to disable; DEFAULT: 1000)
 - `commit_limit` - Commit database changes after this many documents are
                    updated
		    (integer, set to 0 to disable; DEFAULT: 0 [use Xapian
		     internal defaults])
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
  fts_flatcurve = auto_optimize=500 auto_optimize_msgs=1000 commit_limit=0 \
                  max_term_size=30 min_term_size=2 substring_search=yes
}
```


Data Storage
------------

Xapian search data is stored separately for each mailbox.

The data is stored under a 'flatcurve-index' directory in the Dovecot index
file location for the mailbox.  The Xapian library is responsible for the
data stored in that directory - no Dovecot code directly writes to any file.


Logging/Events
--------------

This plugin emits [events](https://doc.dovecot.org/admin_manual/event_design/)
with the category `fts_flatcurve`.

Flatcurve outputs copious debug information.  To view, add this to
`dovecot.conf`:

```
# This requires Dovecot v2.3.13+
log_debug = category=fts_flatcurve
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
