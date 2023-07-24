export default {
  load() {
    return {
      fts_flatcurve_expunge: {
        summary: "Emitted when a message is expunged from a mailbox.",
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
          maybe: "Are the results uncertain?",
          query: "The query text sent to Xapian",
          uids: "The list of UIDs returned by the query"
        },
        options: {
          maybe: [ "yes", "no" ]
        }
      },
      fts_flatcurve_rescan: {
        summary: "Emitted when a rescan is completed",
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
