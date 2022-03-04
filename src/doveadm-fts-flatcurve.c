/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "array.h"
#include "doveadm-mail.h"
#include "doveadm-mailbox-list-iter.h"
#include "doveadm-print.h"
#include "hash.h"
#include "mail-search.h"
#include "str.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
#include "fts-flatcurve-config.h"
#include "fts-flatcurve-plugin.h"

#define DOVEADM_FLATCURVE_CMD_NAME_CHECK FTS_FLATCURVE_LABEL " check"
#define DOVEADM_FLATCURVE_CMD_NAME_DUMP FTS_FLATCURVE_LABEL " dump"
#define DOVEADM_FLATCURVE_CMD_NAME_REMOVE FTS_FLATCURVE_LABEL " remove"
#define DOVEADM_FLATCURVE_CMD_NAME_ROTATE FTS_FLATCURVE_LABEL " rotate"
#define DOVEADM_FLATCURVE_CMD_NAME_STATS FTS_FLATCURVE_LABEL " stats"

const char *doveadm_fts_flatcurve_plugin_version = DOVECOT_ABI_VERSION;

void doveadm_fts_flatcurve_plugin_init(struct module *module);
void doveadm_fts_flatcurve_plugin_deinit(void);

enum fts_flatcurve_cmd_type {
	FTS_FLATCURVE_CMD_CHECK,
	FTS_FLATCURVE_CMD_DUMP,
	FTS_FLATCURVE_CMD_REMOVE,
	FTS_FLATCURVE_CMD_ROTATE,
	FTS_FLATCURVE_CMD_STATS
};

struct fts_flatcurve_mailbox_cmd_context {
	struct doveadm_mail_cmd_context ctx;
	enum fts_flatcurve_cmd_type cmd_type;
	struct mail_search_args *search_args;

	HASH_TABLE_TYPE(term_counter) terms;
	bool dump_header:1;
};

struct fts_flatcurve_dump_term {
	const char *term;
	unsigned int count;
};

static void
cmd_fts_flatcurve_mailbox_run_box(struct flatcurve_fts_backend *backend,
				  struct fts_flatcurve_mailbox_cmd_context *ctx,
				  struct mailbox *box)
{
	struct fts_flatcurve_xapian_db_check check;
	const char *guid;
	uint32_t last_uid;
	struct mailbox_metadata metadata;
	bool result;
	struct fts_flatcurve_xapian_db_stats stats;

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_CHECK:
		fts_flatcurve_xapian_mailbox_check(backend, &check);
		result = (check.shards > 0);
		break;
	case FTS_FLATCURVE_CMD_DUMP:
		if (ctx->dump_header)
			fts_flatcurve_xapian_mailbox_headers(backend, ctx->terms);
		else
			fts_flatcurve_xapian_mailbox_terms(backend, ctx->terms);
		return;
	case FTS_FLATCURVE_CMD_REMOVE:
		result = (fts_backend_flatcurve_delete_dir(backend, str_c(backend->db_path)) > 0) ;
		break;
	case FTS_FLATCURVE_CMD_ROTATE:
		result = fts_flatcurve_xapian_mailbox_rotate(backend);
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

	if (!result)
		return;

	guid = (mailbox_get_metadata(box, MAILBOX_METADATA_GUID, &metadata) < 0)
		? ""
		: guid_128_to_string(metadata.guid);
	doveadm_print(str_c(backend->boxname));
	doveadm_print(guid);

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_CHECK:
		doveadm_print_num(check.errors);
		doveadm_print_num(check.shards);
		break;
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

static int
cmd_fts_flatcurve_dump_sort(struct fts_flatcurve_dump_term *const *n1,
			    struct fts_flatcurve_dump_term *const *n2)
{
	return ((*n1)->count == (*n2)->count)
		? strcmp((*n1)->term, (*n2)->term)
		: ((*n1)->count < (*n2)->count);
}

static int
cmd_fts_flatcurve_mailbox_run_do(struct flatcurve_fts_backend *backend,
				 struct mail_user *user,
				 struct fts_flatcurve_mailbox_cmd_context *ctx)
{
	struct mailbox *box;
	ARRAY(struct fts_flatcurve_dump_term *) dterms;
	struct hash_iterate_context *hiter;
	const struct mailbox_info *info;
	struct doveadm_mailbox_list_iter *iter;
	char *k;
	enum mailbox_list_iter_flags iter_flags =
		MAILBOX_LIST_ITER_NO_AUTO_BOXES |
		MAILBOX_LIST_ITER_SKIP_ALIASES |
		MAILBOX_LIST_ITER_RETURN_NO_FLAGS;
	struct fts_flatcurve_dump_term *term;
	void *v;

	iter = doveadm_mailbox_list_iter_init(&ctx->ctx, user,
					      ctx->search_args, iter_flags);
	while ((info = doveadm_mailbox_list_iter_next(iter)) != NULL) {
		box = doveadm_mailbox_find(ctx->ctx.cur_mail_user, info->vname);
		fts_backend_flatcurve_set_mailbox(backend, box);
		cmd_fts_flatcurve_mailbox_run_box(backend, ctx, box);
		fts_backend_flatcurve_close_mailbox(backend);
		mailbox_free(&box);
	}

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_DUMP:
		p_array_init(&dterms, backend->pool,
			     hash_table_count(ctx->terms));
		hiter = hash_table_iterate_init(ctx->terms);
	        while (hash_table_iterate(hiter, ctx->terms, &k, &v)) {
			term = p_new(backend->pool,
				     struct fts_flatcurve_dump_term, 1);
			term->count = POINTER_CAST_TO(v, unsigned int);
			term->term = k;
			array_push_back(&dterms, &term);
		}
	        hash_table_iterate_deinit(&hiter);

		array_sort(&dterms, cmd_fts_flatcurve_dump_sort);
		array_foreach_elem(&dterms, term) {
			doveadm_print(term->term);
			doveadm_print_num(term->count);
		}
		array_free(&dterms);
		break;
	default:
		break;
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

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_DUMP:
		if (ctx->dump_header)
			doveadm_print_header("header", "header",
					     DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
		else
			doveadm_print_header("term", "term",
					     DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
		break;
	default:
		doveadm_print_header("mailbox", "mailbox",
				     DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
		doveadm_print_header_simple("guid");
		break;
	}

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_CHECK:
		doveadm_print_header_simple("errors");
		doveadm_print_header_simple("shards");
		break;
	case FTS_FLATCURVE_CMD_DUMP:
		doveadm_print_header_simple("count");
		break;
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
		case FTS_FLATCURVE_CMD_CHECK:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_CHECK);
			break;
		case FTS_FLATCURVE_CMD_DUMP:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_DUMP);
			break;
		case FTS_FLATCURVE_CMD_REMOVE:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_REMOVE);
			break;
		case FTS_FLATCURVE_CMD_ROTATE:
			doveadm_mail_help_name(DOVEADM_FLATCURVE_CMD_NAME_ROTATE);
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

	switch (ctx->cmd_type) {
	case FTS_FLATCURVE_CMD_DUMP:
		if (hash_table_is_created(ctx->terms))
			hash_table_destroy(&ctx->terms);
		break;
	default:
		break;
	}

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

	doveadm_print_init(DOVEADM_PRINT_TYPE_FLOW);

	return &ctx->ctx;
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_check_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_CHECK);
}

static bool
cmd_fts_flatcurve_dump_parse_arg(struct doveadm_mail_cmd_context *_ctx, int c)
{
	struct fts_flatcurve_mailbox_cmd_context *ctx =
		(struct fts_flatcurve_mailbox_cmd_context *)_ctx;

	switch (c) {
	case 'h':
		ctx->dump_header = TRUE;
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_dump_alloc(void)
{
	struct doveadm_mail_cmd_context *_ctx;
	struct fts_flatcurve_mailbox_cmd_context *ctx;

	_ctx = cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_DUMP);
	_ctx->getopt_args = "h";
	_ctx->v.parse_arg = cmd_fts_flatcurve_dump_parse_arg;

	ctx = (struct fts_flatcurve_mailbox_cmd_context *)_ctx;
	hash_table_create(&ctx->terms, default_pool, 1024, str_hash, strcmp);

	return _ctx;
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_remove_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_REMOVE);
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_rotate_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_ROTATE);
}

static struct doveadm_mail_cmd_context *cmd_fts_flatcurve_stats_alloc(void)
{
	return cmd_fts_flatcurve_mailbox_alloc(FTS_FLATCURVE_CMD_STATS);
}

static struct doveadm_cmd_ver2 fts_flatcurve_commands[] = {
	{
		.name = DOVEADM_FLATCURVE_CMD_NAME_CHECK,
		.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX "<mailbox query>",
		.mail_cmd = cmd_fts_flatcurve_check_alloc,
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('\0', "mailbox-mask", CMD_PARAM_ARRAY, CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
	},
	{
		.name = DOVEADM_FLATCURVE_CMD_NAME_DUMP,
		.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX "[-h] <mailbox query>",
		.mail_cmd = cmd_fts_flatcurve_dump_alloc,
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('h', "header", CMD_PARAM_BOOL, 0)
DOVEADM_CMD_PARAM('\0', "mailbox-mask", CMD_PARAM_ARRAY, CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
	},
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
		.name = DOVEADM_FLATCURVE_CMD_NAME_ROTATE,
		.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX "<mailbox query>",
		.mail_cmd = cmd_fts_flatcurve_rotate_alloc,
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
