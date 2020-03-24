/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "mail-storage-hooks.h"
#include "fts-user.h"
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

	for (tmp = t_strsplit_spaces(str, " "); *tmp != NULL; tmp++) {
		if (str_begins(*tmp, "debug=")) {
			set->debug = TRUE;
		} else if (str_begins(*tmp, "no_position=")) {
			set->no_position = TRUE;
		}
	}

	return 0;
}

static void fts_flatcurve_mail_user_created(struct mail_user *user)
{
	struct mail_user_vfuncs *v = user->vlast;
	struct fts_flatcurve_user *fuser;
	const char *env, *error;

	fuser = p_new(user->pool, struct fts_flatcurve_user, 1);
	env = mail_user_plugin_getenv(user, "fts_flatcurve");
	if (env == NULL)
		env = "";

	if (fts_flatcurve_plugin_init_settings(&fuser->set, env) < 0) {
		/* Invalid settings, disabling */
		return;
	}

	if (fts_mail_user_init(user, &error) < 0) {
		i_error("fts_flatcurve: %s", error);
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
