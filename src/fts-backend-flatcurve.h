/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifndef FTS_FLATCURVE_BACKEND_H
#define FTS_FLATCURVE_BACKEND_H

#include "file-dotlock.h"
#include "fts-flatcurve-plugin.h"

#define FTS_FLATCURVE_PLUGIN_LABEL "fts_flatcurve"
#define FTS_FLATCURVE_LABEL "fts-flatcurve"
#define FTS_FLATCURVE_DEBUG_PREFIX FTS_FLATCURVE_LABEL ": "

struct flatcurve_fts_backend {
	struct fts_backend backend;
	string_t *boxname, *db_path;

	struct event *event;

	struct fts_flatcurve_user *fuser;
	struct flatcurve_xapian *xapian;

	struct dotlock_settings dotlock_set;

	pool_t pool;

	bool debug_init:1;
};

struct flatcurve_fts_backend_update_context {
	struct fts_backend_update_context ctx;

	struct flatcurve_fts_backend *backend;
	enum fts_backend_build_key_type type;
	string_t *hdr_name;
	uint32_t uid;
	struct timeval start;

	bool indexed_hdr:1;
	bool skip_uid:1;
};

struct flatcurve_fts_query {
	struct mail_search_arg *args;
	enum fts_lookup_flags flags;
	string_t *qtext;

	struct flatcurve_fts_backend *backend;
	struct flatcurve_fts_query_xapian *xapian;

	pool_t pool;

	bool match_all:1;
	bool maybe:1;
};

struct flatcurve_fts_result {
	ARRAY_TYPE(fts_score_map) scores;
	ARRAY_TYPE(seq_range) uids;
};


void fts_backend_flatcurve_set_mailbox(struct flatcurve_fts_backend *backend,
                                       struct mailbox *box);

// Returns -1 on error, 0 if FTS directory doesn't exist, 1 on success
int fts_backend_flatcurve_delete_dir(struct flatcurve_fts_backend *backend,
				     const char *path);

#endif
