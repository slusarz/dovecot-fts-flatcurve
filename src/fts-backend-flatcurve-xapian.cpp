/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include <xapian.h>
extern "C" {
#include "lib.h"
#include "hash.h"
#include "mail-storage-private.h"
#include "mail-search.h"
#include "unlink-directory.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
};

#define FLATCURVE_ALL_HEADERS_QP "allhdrs"
#define FLATCURVE_HEADER_QP "hdr_"
#define FLATCURVE_BODYTEXT_QP "bodytext"

struct flatcurve_xapian {
	Xapian::Database *db_read;
	Xapian::WritableDatabase *db_write;
	Xapian::Document *doc;
	Xapian::TermGenerator *tg;

	uint32_t doc_uid;
	unsigned int doc_updates;
};

struct flatcurve_fts_query_xapian {
	HASH_TABLE(char *, char *) prefixes;
	Xapian::Query *query;
	std::string *str;
};

struct fts_flatcurve_xapian_uid_iterate_context {
	Xapian::Enquire *enquire;
	Xapian::MSetIterator i;
	Xapian::MSet m;
	unsigned int offset;
	bool init:1;
};


struct flatcurve_xapian *fts_flatcurve_xapian_init()
{
	struct flatcurve_xapian *xapian;

	xapian = i_new(struct flatcurve_xapian, 1);

	return xapian;
}

void fts_flatcurve_xapian_deinit(struct flatcurve_xapian *xapian)
{
	i_free(xapian);
}

static void
fts_flatcurve_xapian_periodic_commit(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;

	if (++xapian->doc_updates >= backend->fuser->set.commit_limit) {
		xapian->db_write->commit();
		xapian->doc_updates = 0;
		e_debug(backend->event, "Committing DB as update "
			"limit was reached; mailbox=%s limit=%d",
			backend->box->name,
			backend->fuser->set.commit_limit);
	}
}

static void
fts_flatcurve_xapian_clear_document(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->doc != NULL) {
		xapian->db_write->replace_document(xapian->doc_uid,
						   *xapian->doc);
		fts_flatcurve_xapian_periodic_commit(backend);

		delete(xapian->tg);
		xapian->doc = NULL;
		xapian->doc_uid = 0;
		xapian->tg = NULL;
	}
}

void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_write != NULL) {
		fts_flatcurve_xapian_clear_document(backend);
		xapian->db_write->commit();
		xapian->db_write->close();
		delete(xapian->db_write);
		xapian->db_write = NULL;
		xapian->doc_updates = 0;
	}

	if (xapian->db_read != NULL) {
		xapian->db_read->close();
		delete(xapian->db_read);
		xapian->db_read = NULL;
	}
}

static bool
fts_flatcurve_xapian_open_read(struct flatcurve_fts_backend *backend)
{
	if (backend->xapian->db_read != NULL)
		return TRUE;

	try {
		backend->xapian->db_read = new Xapian::Database(backend->db);
		e_debug(backend->event, "Opened DB (RO) %s (%s)",
			backend->box->name, backend->db);
	} catch (Xapian::Error e) {
		e_debug(backend->event, "Cannot open DB RO (%s) %s; %s",
			backend->box->name, backend->db, e.get_msg().c_str());
		return FALSE;
	}

	return TRUE;
}

static bool
fts_flatcurve_xapian_open_write(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_write != NULL)
		return TRUE;

	try {
		xapian->db_write = new Xapian::WritableDatabase(
			backend->db,
			Xapian::DB_CREATE_OR_OPEN | Xapian::DB_RETRY_LOCK);
		e_debug(backend->event, "Opened DB (RW) %s (%s)",
			backend->box->name, backend->db);
	} catch (Xapian::Error e) {
		e_debug(backend->event, "Can't open DB RW (%s) %s; %s",
			backend->box->name, backend->db, e.get_msg().c_str());
		return FALSE;
	}

	return TRUE;
}

void fts_flatcurve_xapian_get_last_uid(struct flatcurve_fts_backend *backend,
				       uint32_t *last_uid_r)
{
	*last_uid_r = 0;

	if (!fts_flatcurve_xapian_open_read(backend))
		return;

	try {
		*last_uid_r = backend->xapian->db_read->get_lastdocid();
	} catch (Xapian::Error e) {
		e_debug(backend->event, "get_last_uid (%s); %s",
			backend->box->name, e.get_msg().c_str());
	}
}

int fts_flatcurve_xapian_uid_exists(struct flatcurve_fts_backend *backend,
				    uint32_t uid)
{
	if (!fts_flatcurve_xapian_open_read(backend))
		return -1;

	try {
		(void)backend->xapian->db_read->get_document(uid);
	} catch (Xapian::DocNotFoundError e) {
		return 0;
	}

	return 1;
}

void fts_flatcurve_xapian_expunge(struct flatcurve_fts_backend *backend,
				  uint32_t uid)
{
	if (!fts_flatcurve_xapian_open_write(backend))
		return;

	try {
		backend->xapian->db_write->delete_document(uid);
		fts_flatcurve_xapian_periodic_commit(backend);
	} catch (Xapian::Error e) {
		e_debug(backend->event, "update_expunge (%s)",
			e.get_msg().c_str());
	}
}

static bool
fts_flatcurve_xapian_get_document(struct flatcurve_fts_backend_update_context *ctx,
				  struct flatcurve_fts_backend *backend)
{
	Xapian::Document doc;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (ctx->uid == xapian->doc_uid) {
		return TRUE;
	}

	fts_flatcurve_xapian_clear_document(backend);

	if (!fts_flatcurve_xapian_open_write(backend))
		return FALSE;

	try {
		doc = xapian->db_write->get_document(ctx->uid);
		xapian->doc = &doc;
	} catch (Xapian::DocNotFoundError e) {
		xapian->doc = new Xapian::Document();
	} catch (Xapian::Error e) {
		ctx->ctx.failed = TRUE;
		return FALSE;
	}

	xapian->tg = new Xapian::TermGenerator();
	xapian->tg->set_stemming_strategy(Xapian::TermGenerator::STEM_NONE);
	xapian->tg->set_document(*xapian->doc);

	xapian->doc_uid = ctx->uid;

	return TRUE;
}

void
fts_flatcurve_xapian_index_header(struct flatcurve_fts_backend_update_context *ctx,
				  struct flatcurve_fts_backend *backend,
				  const unsigned char *data, size_t size)
{
	std::string p;
	std::string s((char *)data, size);
	struct flatcurve_xapian *xapian = backend->xapian;

	if (!fts_flatcurve_xapian_get_document(ctx, backend))
		return;

	if (ctx->hdr_name != NULL) {
		p += FLATCURVE_HEADER_PREFIX;
		p += str_ucase(ctx->hdr_name);
		if (backend->fuser->set.no_position) {
			xapian->tg->index_text_without_positions(s, 1, p);
		} else {
			xapian->tg->index_text(s, 1, p);
		}
	}

	if (backend->fuser->set.no_position) {
		xapian->tg->index_text_without_positions(s, 1,
							 FLATCURVE_ALL_HEADERS_PREFIX);
	} else {
		xapian->tg->index_text(s, 1, FLATCURVE_ALL_HEADERS_PREFIX);
	}
}

void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				struct flatcurve_fts_backend *backend,
				const unsigned char *data, size_t size)
{
	std::string s((char *)data, size);
	struct flatcurve_xapian *xapian = backend->xapian;

	if (!fts_flatcurve_xapian_get_document(ctx, backend))
		return;

	if (backend->fuser->set.no_position) {
		xapian->tg->index_text_without_positions(s, 1,
							 FLATCURVE_BODYTEXT_PREFIX);
	} else {
		xapian->tg->index_text(s, 1, FLATCURVE_BODYTEXT_PREFIX);
	}
}

static bool
fts_flatcurve_xapian_delete_index_real(struct flatcurve_fts_backend *backend,
				       const char *dir)
{
	const char *error;
	enum unlink_directory_flags unlink_flags = UNLINK_DIRECTORY_FLAG_RMDIR;

	if (unlink_directory(dir, unlink_flags, &error) < 0) {
		e_error(backend->event, "Deleting index (%s) failed: %s",
			dir, error);
		return FALSE;
	}

	return TRUE;
}

bool fts_flatcurve_xapian_delete_index(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close(backend);
	return fts_flatcurve_xapian_delete_index_real(backend, backend->db);
}

void fts_flatcurve_xapian_optimize_box(struct flatcurve_fts_backend *backend)
{
	if (!fts_flatcurve_xapian_open_read(backend))
		return;

	std::string s(backend->db);
	s += FLATCURVE_INDEX_OPTIMIZE_SUFFIX;

	try {
		backend->xapian->db_read->compact(s,
			Xapian::DBCOMPACT_NO_RENUMBER);
	} catch (Xapian::Error e) {
		e_error(backend->event, "Error optimizing DB: %s",
			e.get_msg().c_str());
		return;
	}

	if (fts_flatcurve_xapian_delete_index(backend) &&
	    (rename(s.c_str(), backend->db) < 0)) {
		e_error(backend->event,
			"Activating new index (%s -> %s) failed: %m",
			s.c_str(), backend->db);
		fts_flatcurve_xapian_delete_index_real(backend, s.c_str());
	}
}

static bool
fts_flatcurve_build_query_arg(struct flatcurve_fts_query *query,
			      struct mail_search_arg *arg)
{
	char *hdr, *key;
	struct flatcurve_fts_query_xapian *x = query->xapian;
	std::string *s = x->str;

	switch (arg->type) {
	case SEARCH_TEXT:
	case SEARCH_BODY:
	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		if (s->size() > 0) {
			if (query->and_search) {
				*s += " AND ";
			} else {
				*s += " OR ";
			}
		}
		*s += "(";
		break;
	default:
		return FALSE;
	}

	if (arg->match_not) {
		*s += "NOT ";
	}

	switch (arg->type) {
	case SEARCH_TEXT:
		*s += FLATCURVE_ALL_HEADERS_QP;
		*s += ":\"";
		*s += arg->value.str;
		*s += "\" OR ";
		*s += FLATCURVE_BODYTEXT_QP;
		*s += ":\"";
		*s += arg->value.str;
		*s += "\"";

		hash_table_update(x->prefixes, FLATCURVE_ALL_HEADERS_QP,
				  FLATCURVE_ALL_HEADERS_PREFIX);
		hash_table_update(x->prefixes, FLATCURVE_BODYTEXT_QP,
				  FLATCURVE_BODYTEXT_PREFIX);
		break;
	case SEARCH_BODY:
		*s += FLATCURVE_BODYTEXT_QP;
		*s += ":\"";
		*s += arg->value.str;
		*s += "\"";

		hash_table_update(x->prefixes, FLATCURVE_BODYTEXT_QP,
				  FLATCURVE_BODYTEXT_PREFIX);
		break;
	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		if (!fts_header_want_indexed(arg->hdr_field_name))
			return FALSE;
		hdr = str_lcase(p_strconcat(query->pool, FLATCURVE_HEADER_QP,
				  arg->hdr_field_name));
		*s += hdr;
		*s += ":\"";
		*s += arg->value.str;
		*s += "\"";

		hash_table_update(x->prefixes, hdr,
				  FLATCURVE_HEADER_PREFIX);
		break;
	}

	*s += ")";

	return TRUE;
}

bool fts_flatcurve_xapian_build_query(struct flatcurve_fts_backend *backend,
				      struct flatcurve_fts_query *query)
{
	struct mail_search_arg *args = query->args;
	void *key, *val;
	struct hash_iterate_context *iter;
	Xapian::QueryParser qp;

	query->and_search = (query->flags & FTS_LOOKUP_FLAG_AND_ARGS) != 0;
	query->xapian = p_new(query->pool,
			      struct flatcurve_fts_query_xapian, 1);
	query->xapian->str = new std::string();
	hash_table_create(&query->xapian->prefixes, query->pool, 0,
			  str_hash, strcmp);

	for (; args != NULL ; args = args->next) {
		if (!fts_flatcurve_build_query_arg(query, args))
			return FALSE;
	}

	e_debug(backend->event, "Search query generated: %s",
		query->xapian->str->c_str());

	qp.set_stemming_strategy(Xapian::QueryParser::STEM_NONE);

	iter = hash_table_iterate_init(query->xapian->prefixes);
	while (hash_table_iterate(iter, query->xapian->prefixes, &key, &val)) {
		qp.add_prefix((char *)key, (char *)val);
	}
	hash_table_iterate_deinit(&iter);
	hash_table_destroy(&query->xapian->prefixes);

	try {
		query->xapian->query = new Xapian::Query(
			qp.parse_query(
				*query->xapian->str,
				Xapian::QueryParser::FLAG_BOOLEAN |
				Xapian::QueryParser::FLAG_PHRASE
			)
		);
	} catch (Xapian::QueryParserError e) {
		e_error(backend->event, "Parsing query failed: %s",
			e.get_msg().c_str());
		return FALSE;
	}

	return TRUE;
}

struct fts_flatcurve_xapian_uid_iterate_context
*fts_flatcurve_xapian_uid_iter_init(struct flatcurve_fts_backend *backend,
				    struct flatcurve_fts_query *query)
{
	struct fts_flatcurve_xapian_uid_iterate_context *ctx;

	if (!fts_flatcurve_xapian_open_read(backend))
		return NULL;

	ctx = i_new(struct fts_flatcurve_xapian_uid_iterate_context, 1);
	ctx->enquire = new Xapian::Enquire(*backend->xapian->db_read);
	if (query == NULL) {
		ctx->enquire->set_query(Xapian::Query::MatchAll);
	} else {
		ctx->enquire->set_query(*query->xapian->query);
	}
	ctx->enquire->set_docid_order(Xapian::Enquire::ASCENDING);
	ctx->init = TRUE;

	return ctx;
}

uint32_t fts_flatcurve_xapian_uid_iter_next(
	struct fts_flatcurve_xapian_uid_iterate_context *ctx)
{
	uint32_t uid = 0;

	if (ctx->init || (ctx->i == ctx->m.end())) {
		ctx->m = ctx->enquire->get_mset(ctx->offset, 10);
		if (ctx->m.size() == 0)
			return uid;
		ctx->i = ctx->m.begin();
		ctx->offset += 10;
		ctx->init = FALSE;
	}

	uid = *(ctx->i);
	++ctx->i;

	return uid;
}

void fts_flatcurve_xapian_uid_iter_deinit(
	struct fts_flatcurve_xapian_uid_iterate_context **ctx)
{
	delete((*ctx)->enquire);
	i_free_and_null(*ctx);
}

bool fts_flatcurve_xapian_run_query(struct flatcurve_fts_backend *backend,
				    struct flatcurve_fts_query *query,
				    struct fts_result *r)
{
	struct fts_flatcurve_xapian_uid_iterate_context *iter;
	uint32_t uid;

	if ((iter = fts_flatcurve_xapian_uid_iter_init(backend, query)) == NULL)
		return FALSE;
	while ((uid = fts_flatcurve_xapian_uid_iter_next(iter)) != 0) {
		seq_range_array_add(&r->definite_uids, uid);
	}
	fts_flatcurve_xapian_uid_iter_deinit(&iter);
	return TRUE;
}

void fts_flatcurve_xapian_destroy_query(struct flatcurve_fts_query *query)
{
	delete(query->xapian->query);
}
