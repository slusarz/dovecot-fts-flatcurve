/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifndef FTS_FLATCURVE_PLUGIN_H
#define FTS_FLATCURVE_PLUGIN_H

#include "module-context.h"
#include "mail-user.h"
#include "lib.h"
#include "fts-api-private.h"

#define FTS_FLATCURVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, fts_flatcurve_user_module)
#define FTS_FLATCURVE_USER_CONTEXT_REQUIRE(obj) \
	MODULE_CONTEXT_REQUIRE(obj, fts_flatcurve_user_module)

#define FTS_FLATCURVE_COMMIT_LIMIT_DEFAULT 500
#define FTS_FLATCURVE_MAX_TERM_SIZE_DEFAULT 30
#define FTS_FLATCURVE_MAX_TERM_SIZE_MAX 200
#define FTS_FLATCURVE_MIN_TERM_SIZE_DEFAULT 2
#define FTS_FLATCURVE_OPTIMIZE_LIMIT_DEFAULT 10
#define FTS_FLATCURVE_ROTATE_SIZE_DEFAULT 5000
#define FTS_FLATCURVE_SUBSTRING_SEARCH_DEFAULT TRUE

struct fts_flatcurve_settings {
	unsigned int commit_limit;
	unsigned int max_term_size;
	unsigned int min_term_size;
	unsigned int optimize_limit;
	unsigned int rotate_size;
	bool substring_search;
};

struct fts_flatcurve_user {
	union mail_user_module_context module_ctx;
	struct fts_flatcurve_settings set;
};

extern struct fts_backend fts_backend_flatcurve;
extern MODULE_CONTEXT_DEFINE(fts_flatcurve_user_module, &mail_user_module_register);

void fts_flatcurve_plugin_init(struct module *module);
void fts_flatcurve_plugin_deinit(void);

#endif

