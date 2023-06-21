---
layout: doc
---

# Benchmarks

:::warning Note

This information is dated (from 2021) and does not necessarily reflect the current code.

:::

## Indexing Benchmarks

### Substring matching **ENABLED**

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

### Substring matching **DISABLED** (*DEFAULT* configuration)

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

## FTS Plugin configuration for the tests

```
plugin {
  fts = flatcurve
  fts_autoindex = no
  fts_enforced = yes
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords
  fts_flatcurve_substring_search = [yes|no]  # Dependent on test
  fts_index_timeout = 60s
  fts_languages = en es de
  fts_tokenizer_generic = algorithm=simple
  fts_tokenizers = generic email-address
}
```
