/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "mail-storage-hooks.h"
#include "fts-user.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
#include "fts-flatcurve-plugin.h"

const char *fts_flatcurve_plugin_version = DOVECOT_ABI_VERSION;

struct fts_flatcurve_user_module fts_flatcurve_user_module =
	MODULE_CONTEXT_INIT(&mail_user_module_register);

static void fts_flatcurve_mail_user_deinit(struct mail_user *user)
{
	struct fts_flatcurve_user *fuser =
		FTS_FLATCURVE_USER_CONTEXT_REQUIRE(user);

	fts_mail_user_deinit(user);
	fuser->module_ctx.super.deinit(user);
}

static int
fts_flatcurve_plugin_init_settings(struct fts_flatcurve_settings *set,
				   const char *str)
{
	const char *const *tmp;
	unsigned int val;

	set->commit_limit = FTS_FLATCURVE_COMMIT_LIMIT_DEFAULT;
	set->max_term_size = FTS_FLATCURVE_MAX_TERM_SIZE_DEFAULT;
	set->min_term_size = FTS_FLATCURVE_MIN_TERM_SIZE_DEFAULT;
	set->rotate_size = FTS_FLATCURVE_ROTATE_SIZE_DEFAULT;
	set->substring_search = FTS_FLATCURVE_SUBSTRING_SEARCH_DEFAULT;

	for (tmp = t_strsplit_spaces(str, " "); *tmp != NULL; tmp++) {
		if (str_begins(*tmp, "commit_limit=")) {
			if (str_to_uint(*tmp + 13, &val) < 0) {
				i_warning(FTS_FLATCURVE_DEBUG_PREFIX
					  "Invalid commit_limit: %s",
					  *tmp + 13);
				return -1;
			}
			set->commit_limit = val;
		} else if (str_begins(*tmp, "max_term_size=")) {
			if (str_to_uint(*tmp + 14, &val) < 0) {
				i_warning(FTS_FLATCURVE_DEBUG_PREFIX
					  "Invalid max_term_size: %s",
					  *tmp + 14);
				return -1;
			}
			set->max_term_size = I_MIN(val,
						   FTS_FLATCURVE_MAX_TERM_SIZE_MAX);
		} else if (str_begins(*tmp, "min_term_size=")) {
			if (str_to_uint(*tmp + 14, &val) < 0) {
				i_warning(FTS_FLATCURVE_DEBUG_PREFIX
					  "Invalid min_term_size: %s",
					  *tmp + 14);
				return -1;
			}
			set->min_term_size = val;
		} else if (str_begins(*tmp, "rotate_size=")) {
			if (str_to_uint(*tmp + 12, &val) < 0) {
				i_warning(FTS_FLATCURVE_DEBUG_PREFIX
					  "Invalid rotate_size: %s",
					  *tmp + 12);
				return -1;
			}
			set->rotate_size = val;
		} else if (str_begins(*tmp, "substring_search=")) {
			if (strcasecmp(*tmp + 17, "yes")) {
				set->substring_search = TRUE;
			} else if (strcasecmp(*tmp + 17, "no")) {
				set->substring_search = FALSE;
			} else {
				i_warning(FTS_FLATCURVE_DEBUG_PREFIX
					  "Invalid substring_search: %s",
					  *tmp + 17);
				return -1;
			}
		}
	}

	return 0;
}

static void fts_flatcurve_mail_user_created(struct mail_user *user)
{
	struct mail_user_vfuncs *v = user->vlast;
	struct fts_flatcurve_user *fuser;
	const char *env, *error;

	if (fts_mail_user_init(user, &error) < 0) {
		i_error(FTS_FLATCURVE_DEBUG_PREFIX "%s", error);
		return;
	}

	fuser = p_new(user->pool, struct fts_flatcurve_user, 1);
	env = mail_user_plugin_getenv(user, FTS_FLATCURVE_PLUGIN_LABEL);
	if (env == NULL)
		env = "";

	if (fts_flatcurve_plugin_init_settings(&fuser->set, env) < 0) {
		/* Invalid settings, disabling */
		return;
	}

	fuser->module_ctx.super = *v;
	user->vlast = &fuser->module_ctx.super;
	v->deinit = fts_flatcurve_mail_user_deinit;
	MODULE_CONTEXT_SET(user, fts_flatcurve_user_module, fuser);
}

static struct mail_storage_hooks fts_backend_mail_storage_hooks = {
	.mail_user_created = fts_flatcurve_mail_user_created
};

void fts_flatcurve_plugin_init(struct module *module)
{
	fts_backend_register(&fts_backend_flatcurve);
	mail_storage_hooks_add(module, &fts_backend_mail_storage_hooks);
}

void fts_flatcurve_plugin_deinit(void)
{
	fts_backend_unregister(fts_backend_flatcurve.name);
	mail_storage_hooks_remove(&fts_backend_mail_storage_hooks);
}

const char *fts_flatcurve_plugin_dependencies[] = { "fts", NULL };
