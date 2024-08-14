export default {
  load() {
    // Each event is listed with key as event name and value as object
    return {
      fts_flatcurve_expunge: {
        // Summary of event. Processed w/Markdown
        summary: "Emitted when a message is expunged from a mailbox.",
        // List of fields emitted. Keys are field names, values are
        // descriptions. Values are processed w/Markdown.
        fields: {
          mailbox: "The mailbox name",
          uid: "The UID that was expunged from FTS index"
        },
      },
      fts_flatcurve_index: {
        summary: "Emitted when a message is indexed.",
        fields: {
          mailbox: "The mailbox name",
          uid: "The UID that was added to the FTS index"
        }
      },
      fts_flatcurve_index_truncate: {
        summary: "Emitted when an index term is truncated.",
        fields: {
          mailbox: "The mailbox name",
          orig_size: "The original size of the term, before truncation",
          uid: "The UID being indexed"
        }
      },
      fts_flatcurve_last_uid: {
        summary: "Emitted when the system queries for the last UID indexed.",
        fields: {
          mailbox: "The mailbox name",
          uid: "The last UID contained in the FTS index"
        }
      },
      fts_flatcurve_optimize: {
        summary: "Emitted when a mailbox is optimized.",
        fields: {
          mailbox: "The mailbox name"
        }
      },
      fts_flatcurve_query: {
        summary: "Emitted when a query is completed.",
        fields: {
          count: "The number of messages matched",
          mailbox: "The mailbox name",
          maybe_uids: "The list of maybe UIDs returned by the query (these UIDs need to have their contents directly searched by Dovecot core)",
          query: "The query text sent to Xapian",
          uids: "The list of UIDs returned by the query"
        },
        options: {
          maybe: [ "yes", "no" ]
        }
      },
      fts_flatcurve_rescan: {
        summary: "Emitted when a rescan is completed.",
        fields: {
          expunged: "The list of UIDs that were expunged during rescan",
          mailbox: "The mailbox name",
          status: "Status of rescan",
          uids: "The list of UIDs that triggered a non-ok status response"
        },
        options: {
          status: [ "expunge_msgs", "missing_msgs", "ok" ]
        }
      },
      fts_flatcurve_rotate: {
        summary: "Emitted when a mailbox has its underlying Xapian DB rotated.",
        fields: {
          mailbox: "The mailbox name",
        }
      }
    }
  }
}
