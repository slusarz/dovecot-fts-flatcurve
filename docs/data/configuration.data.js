export default {
  load() {
    // Each config item is listed with key as config name and value as object
    return {
      fts_flatcurve_commit_limit: {
        // Default value of the config.
        default: "500",
        // Value type of the config. Processed w/Markdown.
        value: "integer, set to `0` to use the Xapian default",
        // Summary of the configuration item. Processed w/Markdown.
        summary: `
Commit database changes after this many documents are updated. Higher commit
limits will result in faster indexing for large transactions (i.e. indexing a
large mailbox) at the expense of high memory usage. The default value should
be sufficient to allow indexing in a 256 MB maximum size process.`
      },
      fts_flatcurve_max_term_size: {
        default: "30",
        value: "integer, set to `0` to use the Xapian default",
        summary: `The maximum number of characters in a term to index.`,
      },
      fts_flatcurve_min_term_size: {
        default: "2",
        value: "integer",
        summary: `The minimum number of characters in a term to index.`,
      },
      fts_flatcurve_optimize_limit: {
        default: "10",
        value: "integer, set to `0` to disable",
        summary: `Once the database reaches this number of shards, automatically optimize the DB at shutdown.`
      },
      fts_flatcurve_rotate_size: {
        default: "5000",
        value: "integer, set to `0` to disable rotation",
        summary: `
When the "current" fts database reaches this number of messages, it is rotated
to a read-only database and replaced by a new write DB. Most people should not
change this setting.`
      },
      fts_flatcurve_rotate_time: {
        default: "5000",
        value: "integer, set to `0` to disable rotation",
        summary: `
When the "current" fts database exceeds this length of time (in msecs) to
commit changes, it is rotated to a read-only database and replaced by a new
write DB. Most people should not change this setting.`
      },
      fts_flatcurve_substring_search: {
        default: "no",
        value: "boolean (`yes` or `no`)",
        summary:`
If enabled, allows substring searches (RFC 3501 compliant). However, this
requires significant additional storage space. Most users today expect
"Google-like" behavior, which is prefix searching, so substring searching is
arguably not the "modern, expected" behavior. Therefore, even though it
is not strictly RFC compliant, prefix (non-substring) searching is enabled
by default.`
      }
    }
  }
}
