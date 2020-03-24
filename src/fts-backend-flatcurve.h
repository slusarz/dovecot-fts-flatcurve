/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifndef FTS_FLATCURVE_BACKEND_H
#define FTS_FLATCURVE_BACKEND_H

#include "lib.h"
#include "mail-namespace.h"
#include "fts-flatcurve-plugin.h"


#define FLATCURVE_INDEX_NAME "flatcurve-index"
#define FLATCURVE_INDEX_OPTIMIZE_SUFFIX ".optimize"

#define FLATCURVE_ALL_HEADERS_PREFIX "XA"
#define FLATCURVE_BODYTEXT_PREFIX "XB"
#define FLATCURVE_HEADER_PREFIX "XH"

#define FLATCURVE_DEBUG_PREFIX "fts_flatcurve:"


struct flatcurve_fts_backend {
	struct fts_backend backend;
	char *db;

	struct flatcurve_xapian *xapian;

	struct mailbox *box;
	uint32_t last_uid;
};

struct flatcurve_fts_backend_update_context {
	struct fts_backend_update_context ctx;

	enum fts_backend_build_key_type type;
	char *hdr_name;
	uint32_t uid;
};

struct flatcurve_fts_query {
	struct mail_search_arg *args;
	enum fts_lookup_flags flags;

	struct flatcurve_fts_query_xapian *xapian;

	pool_t pool;

	bool and_search:1;
};

#endif
