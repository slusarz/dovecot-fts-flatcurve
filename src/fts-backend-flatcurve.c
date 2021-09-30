/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include "lib.h"
#include "array.h"
#include "str.h"
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
	.name = FTS_FLATCURVE_LABEL,
	.parent = &event_category_fts
};

static struct fts_backend *fts_backend_flatcurve_alloc(void)
{
	struct flatcurve_fts_backend *backend;
	pool_t pool;

	pool = pool_alloconly_create(FTS_FLATCURVE_LABEL " pool", 4096);

	backend = i_new(struct flatcurve_fts_backend, 1);
	backend->backend = fts_backend_flatcurve;
	backend->pool = pool;

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
		*error_r = FTS_FLATCURVE_DEBUG_PREFIX "Invalid settings";
		return -1;
	}

	backend->boxname = str_new(backend->pool, 128);
	backend->db_path = str_new(backend->pool, 256);
	backend->fuser = fuser;

	fts_flatcurve_xapian_init(backend);

	backend->event = event_create(_backend->ns->user->event);
	event_add_category(backend->event, &event_category_fts_flatcurve);
	event_set_append_log_prefix(backend->event, FTS_FLATCURVE_DEBUG_PREFIX);

        e_debug(backend->event, "Xapian library version: %s",
		fts_flatcurve_xapian_library_version());

	return 0;
}

static void
fts_backend_flatcurve_close_box(struct flatcurve_fts_backend *backend)
{
	if (str_len(backend->boxname)) {
		fts_flatcurve_xapian_close(backend);

		str_truncate(backend->boxname, 0);
		str_truncate(backend->db_path, 0);
	}
}

static int fts_backend_flatcurve_refresh(struct fts_backend * _backend)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	fts_flatcurve_xapian_refresh(backend);

	return 0;
}

static void fts_backend_flatcurve_deinit(struct fts_backend *_backend)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	fts_backend_flatcurve_close_box(backend);
	fts_flatcurve_xapian_deinit(backend);

	event_unref(&backend->event);
	pool_unref(&backend->pool);

	i_free(backend);
}

static void
fts_backend_flatcurve_open_box(struct flatcurve_fts_backend *backend,
			       const char *name, const char *path)
{
	str_append(backend->boxname, name);
	str_printfa(backend->db_path, "%s/%s/", path, FTS_FLATCURVE_LABEL);
}

static void
fts_backend_flatcurve_set_mailbox(struct flatcurve_fts_backend *backend,
				  struct mailbox *box)
{
	const char *path;

	if (str_len(backend->boxname) &&
	    (strcasecmp(box->vname, str_c(backend->boxname)) == 0))
		return;

	fts_backend_flatcurve_close_box(backend);

	if (mailbox_get_path_to(box, MAILBOX_LIST_PATH_TYPE_INDEX, &path) <= 0)
		i_unreached(); /* fts already checked this */

	fts_backend_flatcurve_open_box(backend, box->vname, path);
}

static int
fts_backend_flatcurve_get_last_uid(struct fts_backend *_backend,
				   struct mailbox *box, uint32_t *last_uid_r)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;

	fts_backend_flatcurve_set_mailbox(backend, box);

	fts_flatcurve_xapian_get_last_uid(backend, last_uid_r);

	e_debug(event_create_passthrough(backend->event)->
		set_name("fts_flatcurve_last_uid")->
		add_str("mailbox", str_c(backend->boxname))->
		add_int("uid", *last_uid_r)->event(),
		"Last UID mailbox=%s uid=%d", str_c(backend->boxname),
		*last_uid_r);

	return 0;
}

static struct fts_backend_update_context
*fts_backend_flatcurve_update_init(struct fts_backend *_backend)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;
	struct flatcurve_fts_backend_update_context *ctx;

	ctx = p_new(backend->pool,
		    struct flatcurve_fts_backend_update_context, 1);
	ctx->ctx.backend = _backend;
	ctx->backend = backend;
	ctx->hdr_name = str_new(backend->pool, 128);

	return &ctx->ctx;
}

static int
fts_backend_flatcurve_update_deinit(struct fts_backend_update_context *_ctx)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;
	int ret = _ctx->failed ? -1 : 0;

	str_free(&ctx->hdr_name);
	p_free(ctx->backend->pool, ctx);

	return ret;
}

static void
fts_backend_flatcurve_update_set_mailbox(struct fts_backend_update_context *_ctx,
					 struct mailbox *box)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;

	if (box == NULL) {
		fts_backend_flatcurve_close_box(ctx->backend);
	} else {
		fts_backend_flatcurve_set_mailbox(ctx->backend, box);
	}
}

static void
fts_backend_flatcurve_update_expunge(struct fts_backend_update_context *_ctx,
				     uint32_t uid)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;

	e_debug(event_create_passthrough(ctx->backend->event)->
		set_name("fts_flatcurve_expunge")->
		add_str("mailbox", str_c(ctx->backend->boxname))->
		add_int("uid", uid)->event(),
		"Expunge mailbox=%s uid=%d", str_c(ctx->backend->boxname),
		uid);

	fts_flatcurve_xapian_expunge(ctx->backend, uid);
}

static bool
fts_backend_flatcurve_update_set_build_key(struct fts_backend_update_context *_ctx,
					   const struct fts_backend_build_key *key)
{
	bool changed = FALSE;
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;

	i_assert(str_len(ctx->backend->boxname));

	if (_ctx->failed)
		return FALSE;

	if (ctx->uid != key->uid) {
		e_debug(event_create_passthrough(ctx->backend->event)->
			set_name("fts_flatcurve_index")->
			add_str("mailbox", str_c(ctx->backend->boxname))->
			add_int("uid", key->uid)->event(),
			"Indexing mailbox=%s uid=%d",
			str_c(ctx->backend->boxname), key->uid);
		changed = TRUE;
	}

	ctx->type = key->type;
	ctx->uid = key->uid;

	/* Specifically init message, as there is a chance that there
	 * is no valid search info in a message so the message will
	 * not be saved to DB after processing. */
	if (changed)
		fts_flatcurve_xapian_init_msg(ctx);

	switch (key->type) {
	case FTS_BACKEND_BUILD_KEY_HDR:
		i_assert(key->hdr_name != NULL);
		str_append(ctx->hdr_name, key->hdr_name);
		ctx->indexed_hdr = fts_header_want_indexed(key->hdr_name);
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

	str_truncate(ctx->hdr_name, 0);
}

static int
fts_backend_flatcurve_update_build_more(struct fts_backend_update_context *_ctx,
					const unsigned char *data, size_t size)
{
	struct flatcurve_fts_backend_update_context *ctx =
		(struct flatcurve_fts_backend_update_context *)_ctx;

	i_assert(ctx->uid != 0);

	if (_ctx->failed)
		return -1;

	if (size < ctx->backend->fuser->set.min_term_size)
		return 0;

	/* Xapian has a hard limit of "245 bytes", at least with the glass
	 * and chert backends.  However, it is highly doubtful that people
	 * are realistically going to search with more than 10s of
	 * characters. Therefore, limit term size (via a configurable
	 * value). */
	size = I_MIN(size, ctx->backend->fuser->set.max_term_size);

	switch (ctx->type) {
	case FTS_BACKEND_BUILD_KEY_HDR:
	case FTS_BACKEND_BUILD_KEY_MIME_HDR:
		fts_flatcurve_xapian_index_header(ctx, data, size);
		break;
	case FTS_BACKEND_BUILD_KEY_BODY_PART:
		fts_flatcurve_xapian_index_body(ctx, data, size);
		break;
	default:
		i_unreached();
	}

	return (_ctx->failed) ? -1 : 0;
}

static string_t
*fts_backend_flatcurve_seq_range_string(ARRAY_TYPE(seq_range) *uids,
					pool_t pool)
{
	unsigned int count, i;
	const struct seq_range *range;
	string_t *ret;

	ret = str_new(pool, 256);

	range = array_get(uids, &count);
	for (i = 0; i < count; i++) {
		if (i != 0)
			str_append(ret, ",");
		str_printfa(ret, "%u", range[i].seq1);
		if (range[i].seq1 != range[i].seq2)
			str_printfa(ret, ":%u", range[i].seq2);
	}

	return ret;
}

static void
fts_backend_flatcurve_rescan_box(struct flatcurve_fts_backend *backend,
				 struct mailbox *box,
				 pool_t pool)
{
	bool dbexist = TRUE;
	struct event_passthrough *e;
	struct fts_flatcurve_xapian_query_iter *iter;
	struct seq_range_iter iter2;
	uint32_t low_uid;
	struct mail *mail;
	ARRAY_TYPE(seq_range) expunged, missing, uids;
	struct flatcurve_fts_query *query;
	struct fts_flatcurve_xapian_query_result *result;
	int ret;
	struct mail_search_args *search_args;
	struct mail_search_context *search_ctx;
	struct mailbox_transaction_context *trans;
	const char *u, *u2;

	/* Check for non-indexed mails. */
	if (mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ) < 0)
		return;

	trans = mailbox_transaction_begin(box, 0, __func__);
	search_args = mail_search_build_init();
	mail_search_build_add_all(search_args);

	p_array_init(&missing, pool, 32);
	p_array_init(&uids, pool, 256);

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
			break;
		default:
			break;
		}
	}
end:
	(void)mailbox_search_deinit(&search_ctx);

	mail_search_args_unref(&search_args);
	(void)mailbox_transaction_commit(&trans);

	if (!dbexist)
		return;

	e = event_create_passthrough(backend->event)->
				     set_name("fts_flatcurve_rescan")->
				     add_str("mailbox", box->name);

	if (!array_is_empty(&missing)) {
		/* There does not seem to be an easy way via FTS API (as of
		 * 2.3.15) to indicate what specific uids need to be indexed.
		 * Instead, delete all messages above the lowest, non-indexed
		 * UID and recreate the index the next time the mailbox
		 * is accessed. */
		seq_range_array_iter_init(&iter2, &missing);
		ret = seq_range_array_iter_nth(&iter2, 0, &low_uid);
		i_assert(ret);
	}

	query = p_new(pool, struct flatcurve_fts_query, 1);
	query->backend = backend;
	query->match_all = TRUE;
	query->pool = pool;
	fts_flatcurve_xapian_build_query(query);

	p_array_init(&expunged, pool, 256);

	if ((iter = fts_flatcurve_xapian_query_iter_init(query)) != NULL) {
		while ((result = fts_flatcurve_xapian_query_iter_next(iter)) != NULL) {
			if (((low_uid > 0) && (result->uid >= low_uid)) ||
			    ((low_uid == 0) && (!seq_range_exists(&uids, result->uid)))) {
				fts_flatcurve_xapian_expunge(backend,
							     result->uid);
				seq_range_array_add(&expunged, result->uid);
			}
		}
		fts_flatcurve_xapian_query_iter_deinit(&iter);
	}

	fts_flatcurve_xapian_destroy_query(query);

	if (array_is_empty(&expunged)) {
		e_debug(e->add_str("status", "ok")->event(),
			"Rescan: no issues found mailbox=%s",
			box->name);
	} else {
		u = str_c(fts_backend_flatcurve_seq_range_string(&expunged,
								 pool));
		e->add_str("expunged", u);

		if (low_uid > 0) {
			u2 = str_c(fts_backend_flatcurve_seq_range_string(
					&missing, pool));
			e_debug(e->add_str("status", "missing_msgs")->
				add_str("uids", u2)->event(),
				"Rescan: missing messages mailbox=%s uids=%s "
				"expunged=%s", box->name, u2, u);
		} else {
			e_debug(e->add_str("status", "expunge_msgs")->event(),
				"Rescan: expunge non-existent messages "
				"mailbox=%s expunged=%s", box->name, u);
		}
	}
}

static int
fts_backend_flatcurve_iterate_ns(struct fts_backend *_backend,
				 enum fts_backend_flatcurve_action act)
{
	struct flatcurve_fts_backend *backend =
		(struct flatcurve_fts_backend *)_backend;
	struct mailbox *box;
	const struct mailbox_info *info;
	struct mailbox_list_iterate_context *iter;
	const enum mailbox_list_iter_flags iter_flags =
		MAILBOX_LIST_ITER_NO_AUTO_BOXES |
		MAILBOX_LIST_ITER_RETURN_NO_FLAGS;
	enum mailbox_flags mbox_flags = 0;
	pool_t pool;
	bool pool_alloc = FALSE;

	iter = mailbox_list_iter_init(_backend->ns->list, "*", iter_flags);
	while ((info = mailbox_list_iter_next(iter)) != NULL) {
		box = mailbox_alloc(backend->backend.ns->list, info->vname,
				    mbox_flags);
		fts_backend_flatcurve_set_mailbox(backend, box);

		switch (act) {
		case FTS_BACKEND_FLATCURVE_ACTION_OPTIMIZE:
			fts_flatcurve_xapian_optimize_box(backend);
			break;
		case FTS_BACKEND_FLATCURVE_ACTION_RESCAN:
			if (!pool_alloc) {
				pool = pool_alloconly_create(
					FTS_FLATCURVE_LABEL " rescan pool",
					4096);
				pool_alloc = TRUE;
			}
			fts_backend_flatcurve_rescan_box(backend, box, pool);
			p_clear(pool);
			break;
		}

		mailbox_free(&box);
	}
	(void)mailbox_list_iter_deinit(&iter);

	if (pool_alloc)
		pool_unref(&pool);

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
	const char *u;

	/* Create query */
	query = p_new(result->pool, struct flatcurve_fts_query, 1);
	query->args = args;
	query->backend = backend;
	query->flags = flags;
	query->pool = result->pool;
	if (!fts_flatcurve_xapian_build_query(query)) {
		fts_flatcurve_xapian_destroy_query(query);
		return -1;
	}

	p_array_init(&box_results, result->pool, 8);
	for (i = 0; boxes[i] != NULL; i++) {
		r = array_append_space(&box_results);
		r->box = boxes[i];

		fresult = p_new(result->pool, struct flatcurve_fts_result, 1);
		p_array_init(&fresult->scores, result->pool, 32);
		p_array_init(&fresult->uids, result->pool, 32);

		fts_backend_flatcurve_set_mailbox(backend, r->box);

		if (!fts_flatcurve_xapian_run_query(query, fresult)) {
			ret = -1;
			break;
		}

		if ((query->maybe) ||
		    ((flags & FTS_LOOKUP_FLAG_NO_AUTO_FUZZY) != 0))
			r->maybe_uids = fresult->uids;
		else
			r->definite_uids = fresult->uids;
		r->scores = fresult->scores;

		/* This was an empty query - skip output of debug info. */
		if (query->qtext == NULL)
			continue;

		u = str_c(fts_backend_flatcurve_seq_range_string(&fresult->uids,
								 query->pool));
		e_debug(event_create_passthrough(backend->event)->
			set_name("fts_flatcurve_query")->
			add_int("count", array_count(&fresult->uids))->
			add_str("mailbox", r->box->vname)->
			add_str("maybe", query->maybe ? "yes" : "no")->
			add_str("query", str_c(query->qtext))->
			add_str("uids", u)->event(), "Query (%s) "
			"mailbox=%s %smatches=%d uids=%s", str_c(query->qtext),
			r->box->vname, query->maybe ? "maybe_" : "",
			array_count(&fresult->uids), u);
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
	multi_result.pool = pool_alloconly_create(FTS_FLATCURVE_LABEL
						  " results pool", 4096);
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

	return ret;
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
