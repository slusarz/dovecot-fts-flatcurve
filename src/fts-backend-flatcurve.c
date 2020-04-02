/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "array.h"
#include "mail-storage-private.h"
#include "mail-search-build.h"
#include "mailbox-list-iter.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"

enum fts_backend_flatcurve_action {
	FTS_BACKEND_FLATCURVE_ACTION_OPTIMIZE,
	FTS_BACKEND_FLATCURVE_ACTION_RESCAN
};

struct event_category event_category_fts_flatcurve = {
	.name = "fts_flatcurve"
};

static struct fts_backend *fts_backend_flatcurve_alloc(void)
{
	struct flatcurve_fts_backend *backend;

	backend = i_new(struct flatcurve_fts_backend, 1);
	backend->backend = fts_backend_flatcurve;
	return &backend->backend;
}

static int
fts_backend_flatcurve_init(struct fts_backend *_backend, const char **error_r)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;
	struct fts_flatcurve_user *fuser =
		FTS_FLATCURVE_USER_CONTEXT(_backend->ns->user);

	if (fuser == NULL) {
		/* Invalid Settings */
		*error_r = "Invalid fts_flatcurve settings";
		return -1;
	}

	backend->fuser = fuser;
	backend->xapian = fts_flatcurve_xapian_init();

	backend->event = event_create(_backend->ns->user->event);
	event_add_category(backend->event, &event_category_fts_flatcurve);
	event_set_append_log_prefix(backend->event, FTS_FLATCURVE_DEBUG_PREFIX);

	e_debug(backend->event, "Initialized");

	return 0;
}

static void
fts_backend_flatcurve_close_box(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close(backend);

	i_free_and_null(backend->boxname);
	i_free_and_null(backend->db);
}

static int fts_backend_flatcurve_refresh(struct fts_backend * _backend)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	fts_backend_flatcurve_close_box(backend);

	return 0;
}

static void fts_backend_flatcurve_deinit(struct fts_backend *_backend)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	e_debug(backend->event, "Destroyed");

	fts_backend_flatcurve_close_box(backend);
	fts_flatcurve_xapian_deinit(backend->xapian);
	event_unref(&backend->event);

	i_free(backend);
}

static void
fts_backend_flatcurve_set_mailbox(struct flatcurve_fts_backend *backend,
				  struct mailbox *box)
{
	const char *path;

	if ((backend->boxname != NULL) &&
	    (strcasecmp(box->vname, backend->boxname) == 0))
		return;

	fts_backend_flatcurve_close_box(backend);

	if (mailbox_get_path_to(box, MAILBOX_LIST_PATH_TYPE_INDEX, &path) <= 0)
		i_unreached(); /* fts already checked this */

	backend->boxname = i_strdup(box->vname);
	backend->db = i_strdup_printf("%s/%s", path, FLATCURVE_INDEX_NAME);
}

static int
fts_backend_flatcurve_get_last_uid(struct fts_backend *_backend,
				   struct mailbox *box, uint32_t *last_uid_r)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	fts_backend_flatcurve_set_mailbox(backend, box);

	fts_flatcurve_xapian_get_last_uid(backend, last_uid_r);

	e_debug(backend->event, "Last UID mailbox=%s uid=%d",
		backend->boxname, *last_uid_r);

	return 0;
}

static struct fts_backend_update_context
*fts_backend_flatcurve_update_init(struct fts_backend *_backend)
{
	struct flatcurve_fts_backend_update_context *ctx;

	ctx = i_new(struct flatcurve_fts_backend_update_context, 1);
	ctx->ctx.backend = _backend;
	return &ctx->ctx;
}

static int
fts_backend_flatcurve_update_deinit(struct fts_backend_update_context *_ctx)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	int ret = _ctx->failed ? -1 : 0;

	i_free(ctx);

	return ret;
}

static void
fts_backend_flatcurve_update_set_mailbox(struct fts_backend_update_context *_ctx,
					 struct mailbox *box)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)ctx->ctx.backend;

	if (box == NULL) {
		fts_backend_flatcurve_close_box(backend);
	} else {
		fts_backend_flatcurve_set_mailbox(backend, box);
	}
}

static void
fts_backend_flatcurve_update_expunge(struct fts_backend_update_context *_ctx,
				     uint32_t uid)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)ctx->ctx.backend;

	e_debug(backend->event, "Expunge mailbox=%s uid=%d",
		backend->boxname, uid);

	fts_flatcurve_xapian_expunge(backend, uid);
}

static bool
fts_backend_flatcurve_update_set_build_key(struct fts_backend_update_context *_ctx,
					   const struct fts_backend_build_key *key)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)ctx->ctx.backend;

	i_assert(backend->boxname != NULL);

	if (_ctx->failed)
		return FALSE;

	if (ctx->uid != key->uid)
		e_debug(backend->event, "Indexing mailbox=%s uid=%d",
			backend->boxname, key->uid);

	ctx->type = key->type;
	ctx->uid = key->uid;

	switch (key->type) {
	case FTS_BACKEND_BUILD_KEY_HDR:
		i_assert(key->hdr_name != NULL);
		if (fts_header_want_indexed(key->hdr_name)) {
			ctx->hdr_name = i_strdup(key->hdr_name);
		}
		break;
	case FTS_BACKEND_BUILD_KEY_MIME_HDR:
	case FTS_BACKEND_BUILD_KEY_BODY_PART:
		/* noop */
		break;
	case FTS_BACKEND_BUILD_KEY_BODY_PART_BINARY:
		i_unreached();
	}

	return TRUE;
}

static void
fts_backend_flatcurve_update_unset_build_key(struct fts_backend_update_context *_ctx)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;

	i_free_and_null(ctx->hdr_name);
}

static int
fts_backend_flatcurve_update_build_more(struct fts_backend_update_context *_ctx,
					const unsigned char *data, size_t size)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)ctx->ctx.backend;

	i_assert(ctx->uid != 0);

	if (_ctx->failed)
		return -1;

	switch (ctx->type) {
	case FTS_BACKEND_BUILD_KEY_HDR:
	case FTS_BACKEND_BUILD_KEY_MIME_HDR:
		fts_flatcurve_xapian_index_header(ctx, backend, data, size);
		break;
	case FTS_BACKEND_BUILD_KEY_BODY_PART:
		fts_flatcurve_xapian_index_body(ctx, backend, data, size);
		break;
	default:
		i_unreached();
	}

	return (_ctx->failed) ? -1 : 0;
}

static bool
fts_backend_flatcurve_rescan_box(struct flatcurve_fts_backend *backend,
				 struct mailbox *box)
{
	struct fts_flatcurve_xapian_query_iterate_context *iter;
	struct mail *mail;
	ARRAY_TYPE(seq_range) missing, uids;
	bool dbexist = TRUE, nodupes = TRUE;
	struct fts_flatcurve_xapian_query_result *result;
	struct mail_search_args *search_args;
	struct mail_search_context *search_ctx;
	struct mailbox_transaction_context *trans;

	/* Check for non-indexed mails. */
	if (mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ) < 0)
		return FALSE;

	e_debug(backend->event, "Rescanning mailbox=%s", box->name);

	trans = mailbox_transaction_begin(box, 0, __func__);
	search_args = mail_search_build_init();
	mail_search_build_add_all(search_args);

	i_array_init(&missing, 32);
	i_array_init(&uids, 256);

	search_ctx = mailbox_search_init(trans, search_args, NULL, 0, NULL);
	while (mailbox_search_next(search_ctx, &mail)) {
		seq_range_array_add(&uids, mail->uid);
		switch (fts_flatcurve_xapian_uid_exists(backend, mail->uid)) {
		case -1:
			/* DB doesn't exist. No sense in continuing. */
			dbexist = FALSE;
			goto end;
		case 0:
			seq_range_array_add(&missing, mail->uid);
			e_debug(backend->event,
				"Rescan: Missing mailbox=%s uid=%d",
				box->name, mail->uid);
			break;
		default:
			break;
		}
	}
end:
	(void)mailbox_search_deinit(&search_ctx);

	mail_search_args_unref(&search_args);
	(void)mailbox_transaction_commit(&trans);

	if (!array_is_empty(&missing)) {
		/* There does not seem to be an easy way to indicate what
		 * uids need to be indexed. Only solution is simply to delete
		 * the index and rebuild at a later time. */
		(void)fts_flatcurve_xapian_delete_index(backend);
		e_debug(backend->event, "Rescan: Missing messages; "
			"deleting index mailbox=%s", box->name);
	} else if (dbexist) {
		e_debug(backend->event, "Rescan: No missing messages "
			"mailbox=%s", box->name);

		/* Check for expunged mails. */
		iter = fts_flatcurve_xapian_query_iter_init(backend, NULL);
		while ((result = fts_flatcurve_xapian_query_iter_next(iter)) != NULL) {
			if (!seq_range_exists(&uids, result->uid)) {
				fts_flatcurve_xapian_expunge(backend, result->uid);
				nodupes = FALSE;
				e_debug(backend->event,
					"Rescan: Missing expunged "
					"message; deleting mailbox=%s "
					"uid=%d", box->name, result->uid);
			}
		}
		fts_flatcurve_xapian_query_iter_deinit(&iter);

		if (nodupes)
			e_debug(backend->event,
				"Rescan: No expunged messages mailbox=%s",
				box->name);
	}

	array_free(&missing);
	array_free(&uids);

	return dbexist;
}

static void
fts_backend_flatcurve_box_action(struct flatcurve_fts_backend *backend,
				 const char *boxname,
				 enum fts_backend_flatcurve_action act)
{
	struct mailbox *box;
	enum mailbox_flags mbox_flags = 0;
	bool optimize = TRUE;

	box = mailbox_alloc(backend->backend.ns->list, boxname, mbox_flags);
	fts_backend_flatcurve_set_mailbox(backend, box);
	if (act == FTS_BACKEND_FLATCURVE_ACTION_RESCAN) {
		optimize = fts_backend_flatcurve_rescan_box(backend, box);
	}
	if (optimize) {
		e_debug(backend->event, "Optimizing mailbox=%s", box->vname);
		fts_flatcurve_xapian_optimize_box(backend);
	}
	mailbox_free(&box);
}

static int
fts_backend_flatcurve_iterate_ns(struct fts_backend *_backend,
				 enum fts_backend_flatcurve_action act)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;
	const struct mailbox_info *info;
	struct mailbox_list_iterate_context *iter;
	const enum mailbox_list_iter_flags iter_flags =
		MAILBOX_LIST_ITER_NO_AUTO_BOXES |
		MAILBOX_LIST_ITER_RETURN_NO_FLAGS;

	iter = mailbox_list_iter_init(_backend->ns->list, "*", iter_flags);
	while ((info = mailbox_list_iter_next(iter)) != NULL) {
		fts_backend_flatcurve_box_action(backend, info->vname, act);
	}
	(void)mailbox_list_iter_deinit(&iter);

	return 0;
}

static int fts_backend_flatcurve_optimize(struct fts_backend *_backend)
{
	return fts_backend_flatcurve_iterate_ns(_backend,
			FTS_BACKEND_FLATCURVE_ACTION_OPTIMIZE);
}

static int fts_backend_flatcurve_rescan(struct fts_backend *_backend)
{
	return fts_backend_flatcurve_iterate_ns(_backend,
			FTS_BACKEND_FLATCURVE_ACTION_RESCAN);
}

static int
fts_backend_flatcurve_lookup_multi(struct fts_backend *_backend,
				   struct mailbox *const boxes[],
				   struct mail_search_arg *args,
				   enum fts_lookup_flags flags,
				   struct fts_multi_result *result)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;
	ARRAY(struct fts_result) box_results;
	struct flatcurve_fts_result *fresult;
	unsigned int i;
	struct flatcurve_fts_query *query;
	struct fts_result *r;
	int ret = 0;

	/* Create query */
	query = p_new(result->pool, struct flatcurve_fts_query, 1);
	query->args = args;
	query->flags = flags;
	query->pool = result->pool;
	if (!fts_flatcurve_xapian_build_query(backend, query))
		return -1;

	p_array_init(&box_results, result->pool, 8);
	for (i = 0; boxes[i] != NULL; i++) {
		r = array_append_space(&box_results);
		r->box = boxes[i];

		fresult = p_new(result->pool, struct flatcurve_fts_result, 1);
		p_array_init(&fresult->scores, result->pool, 32);
		p_array_init(&fresult->uids, result->pool, 32);

		fts_backend_flatcurve_set_mailbox(backend, r->box);

		if (!fts_flatcurve_xapian_run_query(backend, query, fresult)) {
			ret = -1;
			break;
		}

		if (query->maybe)
			r->maybe_uids = fresult->uids;
		else
			r->definite_uids = fresult->uids;
		r->scores = fresult->scores;

		e_debug(backend->event,
			"Query complete mailbox=%s %smatches=%d",
			r->box->vname,
			query->maybe ? "maybe_" : "",
			array_count(&fresult->uids));
	}

	if (ret == 0) {
		array_append_zero(&box_results);
		result->box_results = array_idx_modifiable(&box_results, 0);
	}

	fts_flatcurve_xapian_destroy_query(query);

	return ret;
}

static int
fts_backend_flatcurve_lookup(struct fts_backend *_backend, struct mailbox *box,
			     struct mail_search_arg *args,
			     enum fts_lookup_flags flags,
			     struct fts_result *result)
{
	struct mailbox *boxes[2];
	struct fts_multi_result multi_result;
	const struct fts_result *br;
	int ret;

	boxes[0] = box;
	boxes[1] = NULL;

	i_zero(&multi_result);
	multi_result.pool = pool_alloconly_create("results pool", 2048);
	ret = fts_backend_flatcurve_lookup_multi(_backend, boxes, args,
						 flags, &multi_result);

	if ((ret == 0) &&
	    (multi_result.box_results != NULL) &&
	    (multi_result.box_results[0].box != NULL)) {
		br = &multi_result.box_results[0];
		result->box = br->box;
		if (array_is_created(&br->definite_uids))
			array_append_array(&result->definite_uids,
					   &br->definite_uids);
		if (array_is_created(&br->maybe_uids))
			array_append_array(&result->maybe_uids,
					   &br->maybe_uids);
		array_append_array(&result->scores, &br->scores);
		result->scores_sorted = TRUE;
	}
	pool_unref(&multi_result.pool);

	return 0;
}


struct fts_backend fts_backend_flatcurve = {
	.name = "flatcurve",
	.flags = FTS_BACKEND_FLAG_TOKENIZED_INPUT,
	{
		fts_backend_flatcurve_alloc,
		fts_backend_flatcurve_init,
		fts_backend_flatcurve_deinit,
		fts_backend_flatcurve_get_last_uid,
		fts_backend_flatcurve_update_init,
		fts_backend_flatcurve_update_deinit,
		fts_backend_flatcurve_update_set_mailbox,
		fts_backend_flatcurve_update_expunge,
		fts_backend_flatcurve_update_set_build_key,
		fts_backend_flatcurve_update_unset_build_key,
		fts_backend_flatcurve_update_build_more,
		fts_backend_flatcurve_refresh,
		fts_backend_flatcurve_rescan,
		fts_backend_flatcurve_optimize,
		fts_backend_default_can_lookup,
		fts_backend_flatcurve_lookup,
		fts_backend_flatcurve_lookup_multi,
		NULL  /* lookup_done */
	}
};
