/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "doveadm-mail.h"
#include "doveadm-mailbox-list-iter.h"
#include "doveadm-print.h"
#include "mail-search.h"
#include "str.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
#include "fts-flatcurve-config.h"
#include "fts-flatcurve-plugin.h"

#define DOVEADM_FLATCURVE_CMD_NAME_REMOVE FTS_FLATCURVE_LABEL " remove"

const char *doveadm_fts_flatcurve_plugin_version = DOVECOT_ABI_VERSION;

void doveadm_fts_flatcurve_plugin_init(struct module *module);
void doveadm_fts_flatcurve_plugin_deinit(void);

struct fts_flatcurve_remove_cmd_context {
	struct doveadm_mail_cmd_context ctx;
	struct mail_search_args *search_args;
};

static int
cmd_fts_flatcurve_remove_run_do(struct flatcurve_fts_backend *backend,
				struct mail_user *user,
				struct fts_flatcurve_remove_cmd_context *ctx)
{
	struct mailbox *box;
	const char *guid;
	const struct mailbox_info *info;
	struct doveadm_mailbox_list_iter *iter;
	enum mailbox_list_iter_flags iter_flags =
		MAILBOX_LIST_ITER_NO_AUTO_BOXES |
		MAILBOX_LIST_ITER_SKIP_ALIASES |
		MAILBOX_LIST_ITER_RETURN_NO_FLAGS;
	struct mailbox_metadata metadata;

	iter = doveadm_mailbox_list_iter_init(&ctx->ctx, user,
					      ctx->search_args, iter_flags);
	while ((info = doveadm_mailbox_list_iter_next(iter)) != NULL) {
		box = doveadm_mailbox_find(ctx->ctx.cur_mail_user, info->vname);
		fts_backend_flatcurve_set_mailbox(backend, box);
		if (fts_backend_flatcurve_delete_dir(backend, str_c(backend->db_path)) > 0) {
			guid = (mailbox_get_metadata(box, MAILBOX_METADATA_GUID, &metadata) < 0)
				? ""
				: guid_128_to_string(metadata.guid);
			doveadm_print(str_c(backend->boxname));
			doveadm_print(guid);
		}
		fts_backend_flatcurve_close_mailbox(backend);
		mailbox_free(&box);
	}

	return (doveadm_mailbox_list_iter_deinit(&iter) < 0);
}

static int
cmd_fts_flatcurve_remove_run(struct doveadm_mail_cmd_context *_ctx,
			     struct mail_user *user)
{
	struct fts_flatcurve_remove_cmd_context *ctx =
		(struct fts_flatcurve_remove_cmd_context *)_ctx;
	struct fts_flatcurve_user *fuser =
		FTS_FLATCURVE_USER_CONTEXT(user);
	struct flatcurve_fts_backend *backend = fuser->backend;

	if (fuser == NULL) {
		e_error(backend->event, FTS_FLATCURVE_LABEL " not enabled");
		doveadm_mail_failed_error(_ctx, MAIL_ERROR_NOTFOUND);
		return -1;
	}

	doveadm_print_init(DOVEADM_PRINT_TYPE_FLOW);
	doveadm_print_header("mailbox", "mailbox",
			     DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
	doveadm_print_header_simple("guid");

	return cmd_fts_flatcurve_remove_run_do(backend, user, ctx);
}

static void
cmd_fts_flatcurve_remove_init(struct doveadm_mail_cmd_context *_ctx,
			      const char *const args[])
{
	struct fts_flatcurve_remove_cmd_context *ctx =
		(struct fts_flatcurve_remove_cmd_context *)_ctx;

	if (args[0] == NULL)
		doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_REMOVE);

	ctx->search_args = doveadm_mail_mailbox_search_args_build(args);
}

static void
cmd_fts_flatcurve_remove_deinit(struct doveadm_mail_cmd_context *_ctx)
{
	struct fts_flatcurve_remove_cmd_context *ctx =
		(struct fts_flatcurve_remove_cmd_context *)_ctx;

	if (ctx->search_args != NULL)
		mail_search_args_unref(&ctx->search_args);
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_remove_alloc(void)
{
	struct fts_flatcurve_remove_cmd_context *ctx;

	ctx = doveadm_mail_cmd_alloc(struct fts_flatcurve_remove_cmd_context);
	ctx->ctx.v.init = cmd_fts_flatcurve_remove_init;
	ctx->ctx.v.deinit = cmd_fts_flatcurve_remove_deinit;
	ctx->ctx.v.run = cmd_fts_flatcurve_remove_run;

	return &ctx->ctx;
}

static struct doveadm_cmd_ver2 fts_flatcurve_commands[] = {
	{
		.name = DOVEADM_FLATCURVE_CMD_NAME_REMOVE,
		.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX "<mailbox query>",
		.mail_cmd = cmd_fts_flatcurve_remove_alloc,
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('\0', "mailbox-mask", CMD_PARAM_ARRAY, CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
	}
};

void doveadm_fts_flatcurve_plugin_init(struct module *module ATTR_UNUSED)
{
	unsigned int i;

	for (i = 0; i < N_ELEMENTS(fts_flatcurve_commands); i++)
		doveadm_cmd_register_ver2(&fts_flatcurve_commands[i]);
}

void doveadm_fts_flatcurve_plugin_deinit(void)
{
}
