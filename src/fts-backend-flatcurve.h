/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifndef FTS_FLATCURVE_BACKEND_H
#define FTS_FLATCURVE_BACKEND_H

#include "lib.h"
#include "fts-flatcurve-plugin.h"

#define FLATCURVE_INDEX_NAME "flatcurve-index"
#define FLATCURVE_INDEX_OPTIMIZE_SUFFIX ".optimize"

/* Xapian "recommendations" are that you begin your local prefix identifier
 * with "X" for data that doesn't match with a data type listed as a Xapian
 * "convention". However, this recommendation is for maintaining
 * compatability with the search front-end (Omega) that they provide. We don't
 * care about compatability, so save storage space by using single letter
 * prefixes. Bodytext is stored without prefixes, as it is expected to be the
 * single largest storage pool. */
#define FLATCURVE_ALL_HEADERS_PREFIX "A"
#define FLATCURVE_BOOLEAN_FIELD_PREFIX "B"
#define FLATCURVE_HEADER_PREFIX "H"

#define FTS_FLATCURVE_LABEL "fts-flatcurve"
#define FTS_FLATCURVE_DEBUG_PREFIX FTS_FLATCURVE_LABEL ": "

struct flatcurve_fts_backend {
	struct fts_backend backend;
	char *boxname, *db;

	struct event *event;

	struct fts_flatcurve_user *fuser;
	struct flatcurve_xapian *xapian;
};

struct flatcurve_fts_backend_update_context {
	struct fts_backend_update_context ctx;

	enum fts_backend_build_key_type type;
	char *hdr_name;
	uint32_t uid;

	bool indexed_hdr:1;
};

struct flatcurve_fts_query {
	struct mail_search_arg *args;
	enum fts_lookup_flags flags;
	string_t *qtext;

	struct flatcurve_fts_query_xapian *xapian;

	pool_t pool;

	bool maybe:1;
};

struct flatcurve_fts_result {
	ARRAY_TYPE(fts_score_map) scores;
	ARRAY_TYPE(seq_range) uids;
};

#endif
