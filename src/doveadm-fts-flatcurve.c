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
#define DOVEADM_FLATCURVE_CMD_NAME_STATS FTS_FLATCURVE_LABEL " stats"

const char *doveadm_fts_flatcurve_plugin_version = DOVECOT_ABI_VERSION;

void doveadm_fts_flatcurve_plugin_init(struct module *module);
void doveadm_fts_flatcurve_plugin_deinit(void);

enum fts_flatcurve_cmd_type {
	FTS_FLATCURVE_CMD_REMOVE,
	FTS_FLATCURVE_CMD_STATS
};

struct fts_flatcurve_mailbox_cmd_context {
	struct doveadm_mail_cmd_context ctx;
	enum fts_flatcurve_cmd_type cmd_type;
	struct mail_search_args *search_args;
};

static int
cmd_fts_flatcurve_mailbox_run_do(struct flatcurve_fts_backend *backend,
				 struct mail_user *user,
				 struct fts_flatcurve_mailbox_cmd_context *ctx)
{
	struct mailbox *box;
	const char *guid;
	const struct mailbox_info *info;
	struct doveadm_mailbox_list_iter *iter;
	enum mailbox_list_iter_flags iter_flags =
		MAILBOX_LIST_ITER_NO_AUTO_BOXES |
		MAILBOX_LIST_ITER_SKIP_ALIASES |
		MAILBOX_LIST_ITER_RETURN_NO_FLAGS;
	uint32_t last_uid;
	struct mailbox_metadata metadata;
	bool result;
	struct fts_flatcurve_xapian_db_stats stats;

	iter = doveadm_mailbox_list_iter_init(&ctx->ctx, user,
					      ctx->search_args, iter_flags);
	while ((info = doveadm_mailbox_list_iter_next(iter)) != NULL) {
		box = doveadm_mailbox_find(ctx->ctx.cur_mail_user, info->vname);
		fts_backend_flatcurve_set_mailbox(backend, box);

		switch (ctx->cmd_type) {
		case FTS_FLATCURVE_CMD_REMOVE:
			result = (fts_backend_flatcurve_delete_dir(backend, str_c(backend->db_path)) > 0) ;
			break;
		case FTS_FLATCURVE_CMD_STATS:
			fts_flatcurve_xapian_mailbox_stats(backend, &stats);
			if ((result = (stats.version > 0)))
				fts_flatcurve_xapian_get_last_uid(backend,
								  &last_uid);
			break;
		default:
			i_unreached();
		}

		if (result) {
			guid = (mailbox_get_metadata(box, MAILBOX_METADATA_GUID, &metadata) < 0)
				? ""
				: guid_128_to_string(metadata.guid);
			doveadm_print(str_c(backend->boxname));
			doveadm_print(guid);

			switch (ctx->cmd_type) {
			case FTS_FLATCURVE_CMD_STATS:
				doveadm_print_num(last_uid);
				doveadm_print_num(stats.messages);
				doveadm_print_num(stats.shards);
				doveadm_print_num(stats.version);
				break;
			default:
				break;
			}
		}

		fts_backend_flatcurve_close_mailbox(backend);
		mailbox_free(&box);
	}

	return (doveadm_mailbox_list_iter_deinit(&iter) < 0);
}

static int
cmd_fts_flatcurve_mailbox_run(struct doveadm_mail_cmd_context *_ctx,
			      struct mail_user *user)
{
	struct fts_flatcurve_mailbox_cmd_context *ctx =
		(struct fts_flatcurve_mailbox_cmd_context *)_ctx;
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

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_STATS:
		doveadm_print_header_simple("last_uid");
		doveadm_print_header_simple("messages");
		doveadm_print_header_simple("shards");
		doveadm_print_header_simple("version");
		break;
	default:
		break;
	}

	return cmd_fts_flatcurve_mailbox_run_do(backend, user, ctx);
}

static void
cmd_fts_flatcurve_mailbox_init(struct doveadm_mail_cmd_context *_ctx,
			       const char *const args[])
{
	struct fts_flatcurve_mailbox_cmd_context *ctx =
		(struct fts_flatcurve_mailbox_cmd_context *)_ctx;

	if (args[0] == NULL) {
		switch (ctx->cmd_type) {
		case FTS_FLATCURVE_CMD_REMOVE:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_REMOVE);
			break;
		case FTS_FLATCURVE_CMD_STATS:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_STATS);
			break;
		default:
			i_unreached();
		}
	}

	ctx->search_args = doveadm_mail_mailbox_search_args_build(args);
}

static void
cmd_fts_flatcurve_mailbox_deinit(struct doveadm_mail_cmd_context *_ctx)
{
	struct fts_flatcurve_mailbox_cmd_context *ctx =
		(struct fts_flatcurve_mailbox_cmd_context *)_ctx;

	if (ctx->search_args != NULL)
		mail_search_args_unref(&ctx->search_args);
}

static struct doveadm_mail_cmd_context *
cmd_fts_flatcurve_mailbox_alloc(enum fts_flatcurve_cmd_type type)
{
	struct fts_flatcurve_mailbox_cmd_context *ctx;

	ctx = doveadm_mail_cmd_alloc(struct fts_flatcurve_mailbox_cmd_context);
	ctx->ctx.v.init = cmd_fts_flatcurve_mailbox_init;
	ctx->ctx.v.deinit = cmd_fts_flatcurve_mailbox_deinit;
	ctx->ctx.v.run = cmd_fts_flatcurve_mailbox_run;
	ctx->cmd_type = type;

	return &ctx->ctx;
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_remove_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_REMOVE);
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_stats_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_STATS);
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
	},
	{
		.name = DOVEADM_FLATCURVE_CMD_NAME_STATS,
		.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX "<mailbox query>",
		.mail_cmd = cmd_fts_flatcurve_stats_alloc,
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
