/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#include <xapian.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <unicode/unistr.h>
#include "fts-flatcurve-config.h"
extern "C" {
#include "lib.h"
#include "str.h"
#include "mail-storage-private.h"
#include "mail-search.h"
#include "unlink-directory.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
#include <stdio.h>
};

#define FLATCURVE_ALL_HEADERS_QP "allhdrs"
#define FLATCURVE_HEADER_BOOL_QP "hdr_bool"
#define FLATCURVE_HEADER_QP "hdr_"

#define FLATCURVE_MSET_RANGE 10

struct flatcurve_xapian {
	Xapian::Database *db_read;
	Xapian::WritableDatabase *db_write;
	Xapian::Document *doc;

	uint32_t doc_uid;
	unsigned int db_version, doc_updates, total_updates;
	bool db_version_need_update:1;
	bool doc_created:1;
};

struct flatcurve_fts_query_arg {
	string_t *value;

	bool is_and:1;
	bool is_not:1;
};
ARRAY_DEFINE_TYPE(flatcurve_fts_query_arg, struct flatcurve_fts_query_arg);

struct flatcurve_fts_query_xapian {
	Xapian::Query *query;
	Xapian::QueryParser *qp;
	ARRAY_TYPE(flatcurve_fts_query_arg) args;
};

struct fts_flatcurve_xapian_query_iterate_context {
	Xapian::Enquire *enquire;
	Xapian::MSetIterator i;
	unsigned int offset, size;
	struct fts_flatcurve_xapian_query_result *result;
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
fts_flatcurve_xapian_clear_document(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->doc == NULL)
		return;

	try {
		xapian->db_write->replace_document(xapian->doc_uid,
						   *xapian->doc);
	} catch (std::bad_alloc &b) {
		i_fatal(FTS_FLATCURVE_DEBUG_PREFIX "Out of memory "
			"when indexing mail (%s); mailbox=%s UID=%d "
			"(Hint: increase indexing process vsz_limit or define "
			"smaller commit_limit value in plugin config)",
			b.what(), backend->boxname, xapian->doc_uid);
	} catch (Xapian::Error &e) {
		e_warning(backend->event, "Could not write message data: "
			  "mailbox=%s uid=%u; %s", backend->boxname,
			  xapian->doc_uid, e.get_msg().c_str());
	}

	++xapian->total_updates;

	if ((backend->fuser->set.commit_limit > 0) &&
	    (++xapian->doc_updates >= backend->fuser->set.commit_limit)) {
		xapian->db_write->commit();
		xapian->doc_updates = 0;
		e_debug(backend->event, "Committing DB as update "
			"limit was reached; mailbox=%s limit=%d",
			backend->boxname,
			backend->fuser->set.commit_limit);
	}

	if (xapian->doc_created)
		delete(xapian->doc);
	xapian->doc = NULL;
	xapian->doc_created = FALSE;
	xapian->doc_uid = 0;
}

static bool
fts_flatcurve_xapian_need_optimize(struct flatcurve_fts_backend *backend)
{
#ifdef XAPIAN_HAS_COMPACT
	uint32_t rev;
	struct flatcurve_xapian *xapian = backend->xapian;
	struct fts_flatcurve_user *user = backend->fuser;

	/* Only need to check if db_write was active, as db_read would not
	 * have incremented DB revision or number of messages processed. */
	if (xapian->db_write != NULL) {
		if (user->set.auto_optimize > 0) {
			try {
				rev = xapian->db_write->get_revision();
				if (rev >= user->set.auto_optimize) {
					e_debug(backend->event,
						"Triggering auto optimize; "
						"db_revision=%d", rev);
					return TRUE;
				}
			} catch (Xapian::Error &e) {
				/* Ignore error */
			}
		}

		if ((user->set.auto_optimize_msgs > 0) &&
		    (xapian->total_updates >= user->set.auto_optimize_msgs))
			return TRUE;
	}
#endif
	return FALSE;
}

void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend)
{
	bool optimize = FALSE;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_write != NULL) {
		fts_flatcurve_xapian_clear_document(backend);
		optimize = fts_flatcurve_xapian_need_optimize(backend);

		xapian->db_write->close();
		delete(xapian->db_write);
		xapian->db_write = NULL;
		xapian->doc_updates = xapian->total_updates = 0;
	}

	if (xapian->db_read != NULL) {
		xapian->db_read->close();
		delete(xapian->db_read);
		xapian->db_read = NULL;
	}

	xapian->db_version = 0;
	xapian->db_version_need_update = FALSE;

	if (optimize)
		fts_flatcurve_xapian_optimize_box(backend);
}

static void
fts_flatcurve_xapian_read_db_version(struct flatcurve_xapian *xapian)
{
	std::ostringstream ss;
	std::string ver;

	if (xapian->db_version == 0) {
		ver = (xapian->db_write != NULL)
			? xapian->db_write->get_metadata(
				FTS_BACKEND_FLATCURVE_XAPIAN_DB_VERSION_KEY)
			: xapian->db_read->get_metadata(
				FTS_BACKEND_FLATCURVE_XAPIAN_DB_VERSION_KEY);
		if (ver.empty()) {
			xapian->db_version =
				FTS_BACKEND_FLATCURVE_XAPIAN_DB_VERSION;
			xapian->db_version_need_update = TRUE;
		} else {
			xapian->db_version = std::atoi(ver.c_str());
			/* Once we have more than one version, upgrade
			 * checking will be needed here. For now, there can
			 * only be one version so no need to update. */
		}
	}

	if (xapian->db_version_need_update && (xapian->db_write != NULL)) {
		ss << FTS_BACKEND_FLATCURVE_XAPIAN_DB_VERSION;
		xapian->db_write->set_metadata(
			FTS_BACKEND_FLATCURVE_XAPIAN_DB_VERSION_KEY, ss.str());
		xapian->db_version_need_update = FALSE;
	}
}

static bool
fts_flatcurve_xapian_open_read(struct flatcurve_fts_backend *backend)
{
	if (backend->xapian->db_read != NULL)
		return TRUE;

	try {
		backend->xapian->db_read = new Xapian::Database(backend->db);
		fts_flatcurve_xapian_read_db_version(backend->xapian);
		e_debug(backend->event, "Opened DB (RO) mailbox=%s "
			"version=%u; %s", backend->boxname,
			backend->xapian->db_version, backend->db);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "Cannot open DB RO mailbox=%s; %s",
			backend->boxname, e.get_msg().c_str());
		return FALSE;
	}

	return TRUE;
}

static bool
fts_flatcurve_xapian_open_write(struct flatcurve_fts_backend *backend)
{
	int db_flags =
#ifdef XAPIAN_HAS_RETRY_LOCK
		Xapian::DB_CREATE_OR_OPEN | Xapian::DB_RETRY_LOCK;
#else
		Xapian::DB_CREATE_OR_OPEN;
#endif
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_write != NULL)
		return TRUE;

	try {
		xapian->db_write = new Xapian::WritableDatabase(
			backend->db, db_flags);
		fts_flatcurve_xapian_read_db_version(xapian);
		e_debug(backend->event, "Opened DB (RW) mailbox=%s "
			"version=%u; %s", backend->boxname,
			xapian->db_version, backend->db);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "Cannot open DB RW mailbox=%s; %s",
			backend->boxname, e.get_msg().c_str());
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
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "get_last_uid (%s); %s",
			backend->boxname, e.get_msg().c_str());
	}
}

int fts_flatcurve_xapian_uid_exists(struct flatcurve_fts_backend *backend,
				    uint32_t uid)
{
	if (!fts_flatcurve_xapian_open_read(backend))
		return -1;

	try {
		(void)backend->xapian->db_read->get_document(uid);
	} catch (Xapian::DocNotFoundError &e) {
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
	} catch (Xapian::Error &e) {
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
	} catch (Xapian::DocNotFoundError &e) {
		xapian->doc = new Xapian::Document();
		xapian->doc_created = TRUE;
	} catch (Xapian::Error &e) {
		ctx->ctx.failed = TRUE;
		return FALSE;
	}

	xapian->doc_uid = ctx->uid;

	return TRUE;
}

void
fts_flatcurve_xapian_index_header(struct flatcurve_fts_backend_update_context *ctx,
				  struct flatcurve_fts_backend *backend,
				  const unsigned char *data, size_t size)
{
	std::string h;
	icu::UnicodeString s, temp;
	int32_t i = 0;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (!fts_flatcurve_xapian_get_document(ctx, backend))
		return;

	if (ctx->hdr_name != NULL) {
		h = str_lcase(ctx->hdr_name);
		xapian->doc->add_boolean_term(
			FLATCURVE_BOOLEAN_FIELD_PREFIX + h);
	}

	/* Xapian does not support substring searches by default, so we
	 * instead need to explicitly store all substrings of a string, up
	 * to the point where the substring becomes smaller than
	 * min_term_size. We need to use icu in order to correctly handle
	 * multi-byte characters. */
	s = icu::UnicodeString::fromUTF8(
		icu::StringPiece((const char *)data, size));
	if (ctx->indexed_hdr)
		h = str_ucase(ctx->hdr_name);

	do {
		std::string t;

		temp = s.tempSubString(i++);
		temp.toUTF8String(t);

		if (ctx->indexed_hdr) {
			xapian->doc->add_term(FLATCURVE_HEADER_PREFIX + h + t);
		}
		xapian->doc->add_term(FLATCURVE_ALL_HEADERS_PREFIX + t);
	} while (backend->fuser->set.substring_search &&
		 (temp.length() >= backend->fuser->set.min_term_size));
}

void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				struct flatcurve_fts_backend *backend,
				const unsigned char *data, size_t size)
{
	int32_t i = 0;
	icu::UnicodeString s, temp;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (!fts_flatcurve_xapian_get_document(ctx, backend))
		return;

	/* Xapian does not support substring searches by default, so we
	 * instead need to explicitly store all substrings of a string, up
	 * to the point where the substring becomes smaller than
	 * min_term_size. We need to use icu in order to correctly handle
	 * multi-byte characters. */
	s = icu::UnicodeString::fromUTF8(
		icu::StringPiece((const char *)data, size));

	do {
		std::string t;

		temp = s.tempSubString(i++);
		temp.toUTF8String(t);

		xapian->doc->add_term(t);
	} while (backend->fuser->set.substring_search &&
		 (temp.length() >= backend->fuser->set.min_term_size));
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
#ifdef XAPIAN_HAS_COMPACT
	if (!fts_flatcurve_xapian_open_read(backend))
		return;

	std::string s(backend->db);
	s += FLATCURVE_INDEX_OPTIMIZE_SUFFIX;

	try {
		backend->xapian->db_read->compact(s,
			Xapian::DBCOMPACT_NO_RENUMBER);
	} catch (Xapian::Error &e) {
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
#endif
}

static bool
fts_flatcurve_build_query_arg(struct flatcurve_fts_backend *backend,
			      struct flatcurve_fts_query *query,
			      struct mail_search_arg *arg)
{
	struct flatcurve_fts_query_arg *a;
	string_t *hdr, *hdr2;
	std::string t;
	struct flatcurve_fts_query_xapian *x = query->xapian;

	a = p_new(query->pool, struct flatcurve_fts_query_arg, 1);
	a->value = str_new(query->pool, 64);

	switch (arg->type) {
	case SEARCH_TEXT:
	case SEARCH_BODY:
	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		if (arg->match_not)
			a->is_not = TRUE;
		if ((query->flags & FTS_LOOKUP_FLAG_AND_ARGS) != 0)
			a->is_and = TRUE;
		/* Otherwise, absence of these means an OR search. */
		break;
	case SEARCH_MAILBOX:
		/* doveadm will pass this through in 'doveadm search'
		 * commands with a 'mailbox' search argument. The code has
		 * already handled setting the proper mailbox by this point
		 * so just ignore this. */
		return TRUE;

	case SEARCH_OR:
	case SEARCH_SUB:
		/* FTS API says to ignore these. */
		return TRUE;

	default:
		return FALSE;
	}

	/* Required by FTS API to avoid this argument being looked up via
	 * regular search code. */
	arg->match_always = TRUE;

	/* Prepare search value. Phrases should be surrounding by double
	 * quotes. Single words should not be quoted. Quotes should be
	 * removed from original input. */
	t = arg->value.str;
	t.erase(std::remove(t.begin(), t.end(), '"'), t.end());

	if (t.find_first_of(' ') != std::string::npos) {
		if (!fts_flatcurve_xapian_open_read(backend))
			return TRUE;
		if (!backend->xapian->db_read->has_positions()) {
			/* Phrase searching not available. */
			return TRUE;
		}
		if (t.size() > 0)
			t = "\"" + t + "\"";
	}

	switch (arg->type) {
	case SEARCH_TEXT:
		x->qp->add_prefix(FLATCURVE_ALL_HEADERS_QP,
				  FLATCURVE_ALL_HEADERS_PREFIX);
		str_printfa(a->value, "%s:%s* OR %s*",
			    FLATCURVE_ALL_HEADERS_QP, t.c_str(), t.c_str());
		break;
	case SEARCH_BODY:
		str_append(a->value, t.c_str());
		str_append(a->value, "*");
		break;
	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		if (t.size() > 0) {
			if (fts_header_want_indexed(arg->hdr_field_name)) {
				hdr = str_new(query->pool, 32);
				str_printfa(hdr, "%s%s", FLATCURVE_HEADER_QP,
					    t_str_lcase(arg->hdr_field_name));
				hdr2 = str_new(query->pool, 32);
				str_printfa(hdr2, "%s%s", FLATCURVE_HEADER_PREFIX,
					    t_str_ucase(arg->hdr_field_name));
				x->qp->add_prefix(str_c(hdr), str_c(hdr2));
				str_printfa(a->value, "%s:%s*", str_c(hdr), t.c_str());
			} else {
				x->qp->add_prefix(FLATCURVE_ALL_HEADERS_QP,
						  FLATCURVE_ALL_HEADERS_PREFIX);
				str_printfa(a->value, "%s:%s*",
					    FLATCURVE_ALL_HEADERS_QP,
					    t.c_str());
				/* We can only match if it appears in the pool
				 * of header terms, not to a specific header,
				 * so this is a maybe match. */
				/* FIXME: Add header names to search? */
				query->maybe = TRUE;
			}
		} else {
			x->qp->add_boolean_prefix(FLATCURVE_HEADER_BOOL_QP,
						  FLATCURVE_BOOLEAN_FIELD_PREFIX);
			str_printfa(a->value, "%s:%s", FLATCURVE_HEADER_BOOL_QP,
				    t_str_lcase(arg->hdr_field_name));
		}
		break;
	}

	array_push_back(&x->args, a);

	return TRUE;
}

static void
fts_flatcurve_xapian_build_query_deinit(struct flatcurve_fts_query *query)
{
	array_free(&query->xapian->args);
	delete(query->xapian->qp);
}

bool fts_flatcurve_xapian_build_query(struct flatcurve_fts_backend *backend,
				      struct flatcurve_fts_query *query)
{
	const struct flatcurve_fts_query_arg *a, *prev;
	struct mail_search_arg *args = query->args;
	bool ret = TRUE;
	std::string str;
	struct flatcurve_fts_query_xapian *x;

	x = query->xapian = p_new(query->pool,
				  struct flatcurve_fts_query_xapian, 1);
	array_create(&x->args, query->pool,
		     sizeof(struct flatcurve_fts_query_arg), 4);

	x->qp = new Xapian::QueryParser();
	x->qp->set_stemming_strategy(Xapian::QueryParser::STEM_NONE);

	for (; args != NULL ; args = args->next) {
		if (!fts_flatcurve_build_query_arg(backend, query, args)) {
			fts_flatcurve_xapian_build_query_deinit(query);
			return FALSE;
		}
	}

	/* Empty Query. Optimize by not creating a query and returning no
	 * results when we go through the iteration later. */
	if (array_is_empty(&x->args)) {
		e_debug(backend->event, "Empty search query generated");
		fts_flatcurve_xapian_build_query_deinit(query);
		return TRUE;
	}

	/* Generate the query. */
	prev = NULL;
	array_foreach(&x->args, a) {
		if (a->is_not)
			str += "NOT ";
		if (prev == NULL) {
			str += str_c(a->value);
		} else if (!str_equals(a->value, prev->value)) {
			if (a->is_and)
				str += " AND ";
			else
				str += " OR ";
			str += str_c(a->value);
		}
		prev = a;
	}
	e_debug(backend->event, "Search query generated: %s", str.c_str());

	try {
		x->query = new Xapian::Query(x->qp->parse_query(
			str,
			Xapian::QueryParser::FLAG_BOOLEAN |
			Xapian::QueryParser::FLAG_PHRASE |
			Xapian::QueryParser::FLAG_PURE_NOT |
			Xapian::QueryParser::FLAG_WILDCARD
		));
	} catch (Xapian::QueryParserError &e) {
		e_error(backend->event, "Parsing query failed: %s",
			e.get_msg().c_str());
		ret = FALSE;
	}

	fts_flatcurve_xapian_build_query_deinit(query);

	return ret;
}

struct fts_flatcurve_xapian_query_iterate_context
*fts_flatcurve_xapian_query_iter_init(struct flatcurve_fts_backend *backend,
				      struct flatcurve_fts_query *query)
{
	struct fts_flatcurve_xapian_query_iterate_context *ctx;
	bool empty_query = ((query != NULL) && (query->xapian->query == NULL));

	if (!empty_query && !fts_flatcurve_xapian_open_read(backend))
		return NULL;

	ctx = i_new(struct fts_flatcurve_xapian_query_iterate_context, 1);
	if (!empty_query) {
		ctx->enquire = new Xapian::Enquire(*backend->xapian->db_read);
		ctx->enquire->set_docid_order(Xapian::Enquire::DONT_CARE);
		if (query == NULL) {
			ctx->enquire->set_query(Xapian::Query::MatchAll);
		} else {
			ctx->enquire->set_query(*query->xapian->query);
		}
	}
	ctx->result = i_new(struct fts_flatcurve_xapian_query_result, 1);
	ctx->size = 0;

	return ctx;
}

struct fts_flatcurve_xapian_query_result
*fts_flatcurve_xapian_query_iter_next(struct fts_flatcurve_xapian_query_iterate_context *ctx)
{
	Xapian::MSet m;

	if (ctx->size == 0) {
		if (ctx->enquire == NULL)
			return NULL;
		m = ctx->enquire->get_mset(ctx->offset, FLATCURVE_MSET_RANGE);
		if (m.empty())
			return NULL;
		ctx->i = m.begin();
		ctx->offset += FLATCURVE_MSET_RANGE;
		ctx->size = m.size();
	}

	ctx->result->score = ctx->i.get_weight();
	ctx->result->uid = *(ctx->i);
	++ctx->i;
	--ctx->size;

	return ctx->result;
}

void
fts_flatcurve_xapian_query_iter_deinit(struct fts_flatcurve_xapian_query_iterate_context **ctx)
{
	/* Need to explicitly call dtor, or else MSet doesn't release memory
	 * allocated internally. */
	(*ctx)->i.~MSetIterator();
	delete((*ctx)->enquire);
	i_free((*ctx)->result);
	i_free_and_null(*ctx);
}

bool fts_flatcurve_xapian_run_query(struct flatcurve_fts_backend *backend,
				    struct flatcurve_fts_query *query,
				    struct flatcurve_fts_result *r)
{
	struct fts_flatcurve_xapian_query_iterate_context *iter;
	struct fts_flatcurve_xapian_query_result *result;
	struct fts_score_map *score;

	if ((iter = fts_flatcurve_xapian_query_iter_init(backend, query)) == NULL)
		return FALSE;
	while ((result = fts_flatcurve_xapian_query_iter_next(iter)) != NULL) {
		seq_range_array_add(&r->uids, result->uid);
		score = array_append_space(&r->scores);
		score->score = (float)result->score;
		score->uid = result->uid;
	}
	fts_flatcurve_xapian_query_iter_deinit(&iter);
	return TRUE;
}

void fts_flatcurve_xapian_destroy_query(struct flatcurve_fts_query *query)
{
	delete(query->xapian->query);
}

void fts_flatcurve_xapian_debug_output_version()
{
	i_debug("%sXapian library version: %s", FTS_FLATCURVE_DEBUG_PREFIX,
		Xapian::version_string());
}
