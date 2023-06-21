---
layout: doc
---

# Configuration

See [Dovecot FTS Configuration](https://doc.dovecot.org/configuration_manual/fts/) for configuration information regarding general FTS plugin options.

::: info NOTE

Flatcurve REQUIRES the core [Dovecot FTS stemming](https://doc.dovecot.org/configuration_manual/fts/tokenization/) feature.

:::

## FTS-Flatcurve Plugin Settings

**The default parameters should be fine for most people.**

### `fts_flatcurve_commit_limit`

* Default: `500`
* Value: integer, set to `0` to use the Xapian default

Commit database changes after this many documents are updated. Higher commit
limits will result in faster indexing for large transactions (i.e. indexing a
large mailbox) at the expense of high memory usage. The default value should
be sufficient to allow indexing in a 256 MB maximum size process.

### `fts_flatcurve_max_term_size`

* Default: `30`
* Value: integer, maximum `200`

The maximum number of characters in a term to index.

### `fts_flatcurve_min_term_size`

* Default: `2`
* Value: integer

The minimum number of characters in a term to index.

### `fts_flatcurve_optimize_limit`

* Default: `10`
* Value: integer, set to 0 to disable

Once the database reaches this number of shards, automatically optimize the DB
at shutdown.

### `fts_flatcurve_rotate_size`

* Default: `5000`
* Value: integer, set to `0` to disable rotation

When the "current" fts database reaches this number of messages, it is rotated
to a read-only database and replaced by a new write DB. Most people should not
change this setting.

### `fts_flatcurve_rotate_time`

* Default: `5000`
* Value: integer, set to `0` to disable rotation

When the "current" fts database exceeds this length of time (in msecs) to
commit changes, it is rotated to a read-only database and replaced by a new
write DB. Most people should not change this setting.

### `fts_flatcurve_substring_search`

* Default: `no`
* Value: boolean (`yes` or `no`)

If enabled, allows substring searches (RFC 3501 compliant). However, this
requires significant additional storage space. Most users today expect
"Google-like" behavior, which is prefix searching, so substring searching is
arguably not the "modern, expected" behavior. Therefore, even though it
is not strictly RFC compliant, prefix (non-substring) searching is enabled
by default.


## FTS-Flatcurve Plugin Settings Example

```
mail_plugins = $mail_plugins fts fts_flatcurve

plugin {
  fts = flatcurve

  # Recommended default FTS core configuration
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords

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
