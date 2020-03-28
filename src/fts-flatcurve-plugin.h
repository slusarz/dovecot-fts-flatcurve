/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
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

#define FTS_FLATCURVE_AUTO_OPTIMIZE_DEFAULT 100
#define FTS_FLATCURVE_COMMIT_LIMIT_DEFAULT 100
#define FTS_FLATCURVE_SAVE_POSITION_DEFAULT FALSE

struct fts_flatcurve_settings {
	unsigned int auto_optimize;
	unsigned int commit_limit;
	bool save_position:1;
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

