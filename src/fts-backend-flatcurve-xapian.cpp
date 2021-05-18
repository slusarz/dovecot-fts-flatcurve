/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
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
#include <dirent.h>
#include <stdio.h>
};

#define FLATCURVE_XAPIAN_DB_PREFIX "index."
#define FLATCURVE_XAPIAN_DB_CURRENT_WRITE_SUFFIX "current"
#define FLATCURVE_XAPIAN_DB_OPTIMIZE_PREFIX "optimize"
#define FLATCURVE_XAPIAN_CURRENT_DBW \
	FLATCURVE_XAPIAN_DB_PREFIX FLATCURVE_XAPIAN_DB_CURRENT_WRITE_SUFFIX

/* Xapian "recommendations" are that you begin your local prefix identifier
 * with "X" for data that doesn't match with a data type listed as a Xapian
 * "convention". However, this recommendation is for maintaining
 * compatability with the search front-end (Omega) that they provide. We don't
 * care about compatability, so save storage space by using single letter
 * prefixes. Bodytext is stored without prefixes, as it is expected to be the
 * single largest storage pool. */
#define FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX   "A"
#define FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX "B"
#define FLATCURVE_XAPIAN_HEADER_PREFIX        "H"

#define FLATCURVE_XAPIAN_ALL_HEADERS_QP "allhdrs"
#define FLATCURVE_XAPIAN_HEADER_BOOL_QP "hdr_bool"
#define FLATCURVE_XAPIAN_HEADER_QP      "hdr_"

/* Version database, so that any schema changes can be caught.
 * 1 = Initial version */
#define FLATCURVE_XAPIAN_DB_VERSION_KEY "dovecot." FTS_FLATCURVE_LABEL
#define FLATCURVE_XAPIAN_DB_VERSION 1

#define FLATCURVE_MSET_RANGE 10

struct flatcurve_xapian_db {
	Xapian::Database *db;
	Xapian::WritableDatabase *dbw;
	char *path;
};
ARRAY_DEFINE_TYPE(xapian_database, struct flatcurve_xapian_db);

struct flatcurve_xapian {
	Xapian::Database *db_read;
	Xapian::WritableDatabase *db_write;
	Xapian::Document *doc;

	ARRAY_TYPE(xapian_database) read_dbs;

	struct fts_flatcurve_xapian_db_iter *db_iter;

	string_t *dbw_path;
	uint32_t doc_uid;
	unsigned int doc_updates;
	size_t dbw_doccount;
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

struct fts_flatcurve_xapian_query_iter {
	struct flatcurve_fts_query *query;
	Xapian::Enquire *enquire;
	Xapian::MSetIterator i;
	unsigned int offset, size;
	struct fts_flatcurve_xapian_query_result *result;
};

enum fts_flatcurve_xapian_db_iter_options {
	FLATCURVE_XAPIAN_DB_ITER_NO_OPTIONS,
	FLATCURVE_XAPIAN_DB_ITER_IGNORE_OPTIMIZE = 0x01
};

struct fts_flatcurve_xapian_db_iter {
	struct flatcurve_fts_backend *backend;
	DIR *dirp;
	const char *ignore;
	enum fts_flatcurve_xapian_db_iter_options options;
	string_t *path;
	size_t path_len;
};


static Xapian::WritableDatabase *
fts_flatcurve_xapian_write_db(struct flatcurve_fts_backend *backend);
static void
fts_flatcurve_xapian_close_write_db(struct flatcurve_fts_backend *backend);


void fts_flatcurve_xapian_init(struct flatcurve_fts_backend *backend)
{
	backend->xapian = p_new(backend->pool, struct flatcurve_xapian, 1);
	p_array_init(&backend->xapian->read_dbs, backend->pool, 4);
	backend->xapian->dbw_path = str_new(backend->pool, 256);
}

static void
fts_flatcurve_xapian_delete_db_dir(struct flatcurve_fts_backend *backend,
			           const char *dir)
{
	const char *error;
	enum unlink_directory_flags unlink_flags = UNLINK_DIRECTORY_FLAG_RMDIR;

	if (unlink_directory(dir, unlink_flags, &error) < 0)
		e_debug(backend->event, "Deleting index failed mailbox=%s; %s",
			str_c(backend->boxname), error);
}

static struct fts_flatcurve_xapian_db_iter *
fts_flatcurve_xapian_db_iter_init(struct flatcurve_fts_backend *backend,
				  enum fts_flatcurve_xapian_db_iter_options opts)
{
	DIR *dirp;
	struct fts_flatcurve_xapian_db_iter *iter;

	dirp = opendir(str_c(backend->db_path));
	if (dirp == NULL) {
		if (errno != ENOENT)
			e_debug(backend->event, "Cannot open DB RO "
				"mailbox=%s; opendir(%s) failed: %m",
				str_c(backend->boxname),
				str_c(backend->db_path));
		return NULL;
	}

	if (backend->xapian->db_iter == NULL) {
		iter = p_new(backend->pool,
			     struct fts_flatcurve_xapian_db_iter, 1);
		iter->backend = backend;
		iter->path = str_new(backend->pool, 128);
		backend->xapian->db_iter = iter;
	} else {
		iter = backend->xapian->db_iter;
	}

	iter->dirp = dirp;
	iter->ignore = NULL;
	iter->options = opts;

	str_truncate(iter->path, 0);
	str_append_str(iter->path, backend->db_path);
	iter->path_len = str_len(iter->path);

	return iter;
}

static int
fts_flatcurve_xapian_uid_exists_db(Xapian::Database *db, uint32_t uid)
{
	try {
		(void)db->get_document(uid);
	} catch (Xapian::DocNotFoundError &e) {
		return 0;
	}

	return 1;
}

static bool fts_flatcurve_xapian_dir_exists(const char *path)
{
	struct stat st;

	return (stat(path, &st) >= 0) && S_ISDIR(st.st_mode);
}

static const char *
fts_flatcurve_xapian_db_iter_next(struct fts_flatcurve_xapian_db_iter *iter)
{
	struct dirent *d;

	while ((d = readdir(iter->dirp)) != NULL) {
		str_truncate(iter->path, iter->path_len);
		str_append(iter->path, d->d_name);

		/* Check for ignored file. */
		if ((iter->ignore != NULL) &&
		    (strcmp(str_c(iter->path), iter->ignore) == 0))
			continue;

		/* Ignore all files in this directory other than directories
		 * that begin with FLATCURVE_XAPIAN_DB_PREFIX. */
		if (str_begins(d->d_name, FLATCURVE_XAPIAN_DB_PREFIX) &&
		    fts_flatcurve_xapian_dir_exists(str_c(iter->path)))
			return str_c(iter->path);

		/* If we find remnants of optimization, delete it now. */
		if (((iter->options & FLATCURVE_XAPIAN_DB_ITER_IGNORE_OPTIMIZE) == 0) &&
		    (str_begins(d->d_name, FLATCURVE_XAPIAN_DB_OPTIMIZE_PREFIX)))
			fts_flatcurve_xapian_delete_db_dir(iter->backend,
							   str_c(iter->path));
	}

	return NULL;
}

static void
fts_flatcurve_xapian_db_iter_deinit(struct fts_flatcurve_xapian_db_iter **_iter)
{
	struct fts_flatcurve_xapian_db_iter *iter = *_iter;

	*_iter = NULL;
	(void)closedir(iter->dirp);
}

static void
fts_flatcurve_xapian_check_db_version(struct flatcurve_fts_backend *backend,
				      Xapian::Database *db, bool write)
{
	Xapian::WritableDatabase *dbw;
	std::ostringstream ss;
	std::string ver;
	bool write_ver = FALSE;
	struct flatcurve_xapian *xapian = backend->xapian;

	ver = db->get_metadata(FLATCURVE_XAPIAN_DB_VERSION_KEY);
	if (ver.empty()) {
		/* Upgrade from 0 to 1: store the DB version. */
		write_ver = TRUE;

	} else {
		/* At the present time, we only have one DB version. Once we
		 * have more than one version, we will need to compare the
		 * version information whenever we open the DB - it is
		 * possible we will need to upgrade schema before we can
		 * begin working with the DB. For now, no need to do any of
		 * this. */
	}

	if (write_ver &&
	    ((dbw = fts_flatcurve_xapian_write_db(backend)) != NULL)) {
		ss << FLATCURVE_XAPIAN_DB_VERSION;
		dbw->set_metadata(FLATCURVE_XAPIAN_DB_VERSION_KEY, ss.str());
		if (!write)
			fts_flatcurve_xapian_close_write_db(backend);
	}
}

static void
fts_flatcurve_xapian_read_db_add(struct flatcurve_fts_backend *backend,
				 const char *path)
{
	struct flatcurve_xapian_db *xdb;

	if (backend->xapian->db_read == NULL)
		return;

	try {
		xdb = array_append_space(&backend->xapian->read_dbs);
		xdb->db = new Xapian::Database(path);
		xdb->path = p_strdup(backend->pool, path);
		fts_flatcurve_xapian_check_db_version(backend, xdb->db,
						      FALSE);
		backend->xapian->db_read->add_database(*(xdb->db));
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "Cannot open DB RO mailbox=%s; %s",
			str_c(backend->boxname), e.get_msg().c_str());
	}
}

static bool
fts_flatcurve_xapian_rename_db(struct flatcurve_fts_backend *backend,
			       const char *orig_path, std::string &new_path)
{
	bool retry = FALSE;
	std::ostringstream ss;

	for (;;) {
		new_path.clear();
		new_path = str_c(backend->db_path);
		new_path += FLATCURVE_XAPIAN_DB_PREFIX;
		ss << i_rand_limit(8192);
		new_path += ss.str();

		if (rename(orig_path, new_path.c_str()) < 0) {
			if (retry ||
			    (errno != ENOTEMPTY) && (errno != EEXIST)) {
				new_path.clear();
				return FALSE;
			}

			/* Looks like a naming conflict; try once again with
			 * a different filename. ss will have additional
			 * randomness added to the original suffix, so it
			 * will almost certainly work the second time. */
			retry = TRUE;
		} else {
			return TRUE;
		}
	}
}

static Xapian::Database *
fts_flatcurve_xapian_read_db(struct flatcurve_fts_backend *backend)
{
	struct fts_flatcurve_xapian_db_iter *iter;
	const char *path;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_read != NULL)
		return xapian->db_read;

	/* How Xapian DBs work in fts-flatcurve: generally, on init, there
	 * should be 2 on-disk databases: one that contains the "old"
	 * read-only optimized indexes and the "current" database that is
	 * written to. Once the "current" database reaches a certain size,
	 * it is swapped out for a new database. If, at the end of a
	 * session, there are multiple "old" databases, we will combine and
	 * optimize them into a single database. However, we cannot guarantee
	 * that this optimization will occur successfully, so we always assume
	 * there may be multiple read-only indexes at all times.
	 *
	 * Within a session, we create a dummy Xapian::Database object,
	 * scan the data directory for all databases, and add each of them
	 * to the dummy object. For queries, we then just need to query the
	 * dummy object and Xapian handles everything for us. Writes need
	 * to be handled separately, as a WritableDatabase object only
	 * supports a single on-disk DB at a time. */

	if ((iter = fts_flatcurve_xapian_db_iter_init(backend, FLATCURVE_XAPIAN_DB_ITER_NO_OPTIONS)) == NULL)
		return NULL;

	xapian->db_read = new Xapian::Database;

	while ((path = fts_flatcurve_xapian_db_iter_next(iter)) != NULL) {
		fts_flatcurve_xapian_read_db_add(backend, path);
	}

	fts_flatcurve_xapian_db_iter_deinit(&iter);

	e_debug(backend->event, "Opened DB (RO) mailbox=%s messages=%u "
		"version=%u; %s", str_c(backend->boxname),
		xapian->db_read->get_doccount(), FLATCURVE_XAPIAN_DB_VERSION,
		str_c(backend->db_path));

	return xapian->db_read;
}

static Xapian::WritableDatabase *
fts_flatcurve_xapian_write_db_open(struct flatcurve_fts_backend *backend,
				   const char *path, int db_flags,
				   std::string &error)
{
	Xapian::WritableDatabase *dbw;

#ifdef XAPIAN_HAS_RETRY_LOCK
	db_flags |= Xapian::DB_RETRY_LOCK;
#endif

	try {
		dbw = new Xapian::WritableDatabase(path, db_flags);
		e_debug(backend->event, "Opened DB (RW) mailbox=%s "
			"messages=%u version=%u; %s", str_c(backend->boxname),
			dbw->get_doccount(), FLATCURVE_XAPIAN_DB_VERSION, path);
		return dbw;
	} catch (Xapian::Error &e) {
		error = e.get_msg();
		return NULL;
	}
}

static Xapian::WritableDatabase *
fts_flatcurve_xapian_write_db(struct flatcurve_fts_backend *backend)
{
	bool dbw_created = FALSE;
	std::string error;
	struct flatcurve_xapian *xapian = backend->xapian;

	if (xapian->db_write != NULL)
		return xapian->db_write;

	str_append_str(xapian->dbw_path, backend->db_path);
	str_append(xapian->dbw_path, FLATCURVE_XAPIAN_CURRENT_DBW);

	/* Check and see if write DB exists. */
	if (!fts_flatcurve_xapian_dir_exists(str_c(xapian->dbw_path))) {
		dbw_created = TRUE;
		if (mailbox_list_mkdir_root(backend->backend.ns->list,
		    str_c(backend->db_path), MAILBOX_LIST_PATH_TYPE_INDEX) < 0) {
			e_debug(backend->event, "Cannot create DB "
				"mailbox=%s; %s", str_c(backend->boxname),
				str_c(backend->db_path));
			return NULL;
		}
	}

	xapian->db_write = fts_flatcurve_xapian_write_db_open(
				backend, str_c(xapian->dbw_path),
				Xapian::DB_CREATE_OR_OPEN, error);
	if (xapian->db_write == NULL) {
		e_debug(backend->event, "Cannot open DB RW mailbox=%s; %s",
			str_c(backend->boxname), error.c_str());
		return NULL;
	}

	fts_flatcurve_xapian_check_db_version(backend, xapian->db_write, TRUE);

	xapian->dbw_doccount = xapian->db_write->get_doccount();

	if (dbw_created)
		fts_flatcurve_xapian_read_db_add(backend,
						 str_c(xapian->dbw_path));

	return xapian->db_write;
}

static Xapian::WritableDatabase *
fts_flatcurve_xapian_write_db_by_uid(struct flatcurve_fts_backend *backend,
				     uint32_t uid)
{
	Xapian::WritableDatabase *dbw;
	std::string error;
	unsigned int i;
	struct flatcurve_xapian_db *xdb;

	/* Need to figure out which DB the UID lives in. Always look in the
	 * "current" DB first, since more recent messages tend to be the
	 * ones that are accessed most often (and many mailboxes will only
	 * ever have a single DB anyway. */
	if ((dbw = fts_flatcurve_xapian_write_db(backend)) == NULL)
		return NULL;

	if (fts_flatcurve_xapian_uid_exists_db(dbw, uid) > 0)
		return dbw;

	(void)fts_flatcurve_xapian_read_db(backend);
	for (i = 0; i < array_count(&backend->xapian->read_dbs); i++) {
		xdb = array_idx_modifiable(&backend->xapian->read_dbs, i);
		if (fts_flatcurve_xapian_uid_exists_db(xdb->db, uid) > 0) {
			if (xdb->dbw == NULL)
				xdb->dbw = fts_flatcurve_xapian_write_db_open(
					backend, xdb->path, Xapian::DB_OPEN,
					error);
			if (xdb->dbw != NULL)
				return xdb->dbw;
		}
	}

	return NULL;
}

static void
fts_flatcurve_xapian_clear_document(struct flatcurve_fts_backend *backend)
{
	Xapian::WritableDatabase *dbw;
	std::string new_path, old_path;
	struct flatcurve_xapian *xapian = backend->xapian;

	if ((xapian->doc == NULL) ||
	    ((dbw = fts_flatcurve_xapian_write_db(backend)) == NULL))
		return;

	try {
		dbw->replace_document(xapian->doc_uid, *xapian->doc);
	} catch (std::bad_alloc &b) {
		i_fatal(FTS_FLATCURVE_DEBUG_PREFIX "Out of memory "
			"when indexing mail (%s); mailbox=%s UID=%d "
			"(Hint: increase indexing process vsz_limit or define "
			"smaller commit_limit value in plugin config)",
			b.what(), str_c(backend->boxname), xapian->doc_uid);
	} catch (Xapian::Error &e) {
		e_warning(backend->event, "Could not write message data: "
			  "mailbox=%s uid=%u; %s", str_c(backend->boxname),
			  xapian->doc_uid, e.get_msg().c_str());
	}

	if ((backend->fuser->set.commit_limit > 0) &&
	    (++xapian->doc_updates >= backend->fuser->set.commit_limit)) {
		dbw->commit();
		xapian->doc_updates = 0;
		e_debug(backend->event, "Committing DB as update "
			"limit was reached; mailbox=%s limit=%d",
			str_c(backend->boxname),
			backend->fuser->set.commit_limit);
	}

	if (xapian->doc_created)
		delete(xapian->doc);
	xapian->doc = NULL;
	xapian->doc_created = FALSE;
	xapian->doc_uid = 0;

	if ((backend->fuser->set.rotate_size > 0) &&
	    (++xapian->dbw_doccount >= backend->fuser->set.rotate_size)) {
		/* We've hit the rotate limit. Close all DBs and move the
		 * current write DB to an "old" DB path. */
		old_path = str_c(xapian->dbw_path);
		fts_flatcurve_xapian_close(backend);
		if (fts_flatcurve_xapian_rename_db(backend, old_path.c_str(), new_path)) {
			e_debug(event_create_passthrough(backend->event)->
				set_name("fts_flatcurve_rotate")->
				add_str("mailbox", str_c(backend->boxname))->
				event(), "Rotating index mailbox=%s",
				str_c(backend->boxname));
		} else {
			e_debug(backend->event, "Error when rotating DBs "
				"mailbox=%s; Falling back to optimizing DB",
				str_c(backend->boxname));
			fts_flatcurve_xapian_optimize_box(backend);
		}
	}
}

void fts_flatcurve_xapian_refresh(struct flatcurve_fts_backend *backend)
{
	Xapian::Database *db;

	fts_flatcurve_xapian_close_write_db(backend);
	if (backend->xapian->db_read &&
	    ((db = fts_flatcurve_xapian_read_db(backend)) != NULL))
		(void)db->reopen();
}

static void
fts_flatcurve_xapian_close_write_db(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	if (xapian->db_write == NULL)
		return;

	fts_flatcurve_xapian_clear_document(backend);

	xapian->db_write->close();
	delete(xapian->db_write);
	xapian->db_write = NULL;
	str_truncate(xapian->dbw_path, 0);
	xapian->dbw_doccount = xapian->doc_updates = 0;

	/* Close any other DBs that were also opened for writing (e.g.
	 * expunge) */
	array_foreach_modifiable(&xapian->read_dbs, xdb) {
		if (xdb->dbw != NULL) {
			xdb->dbw->close();
			delete(xdb->dbw);
		}
	}
}

void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *xapian = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	fts_flatcurve_xapian_close_write_db(backend);

	if (xapian->db_read == NULL)
		return;

	array_foreach_modifiable(&xapian->read_dbs, xdb) {
		delete(xdb->db);
		p_free(backend->pool, xdb->path);
	}
	array_clear(&xapian->read_dbs);
	xapian->db_read->close();
	delete(xapian->db_read);
	xapian->db_read = NULL;
}

void fts_flatcurve_xapian_get_last_uid(struct flatcurve_fts_backend *backend,
				       uint32_t *last_uid_r)
{
	Xapian::Database *db;
	*last_uid_r = 0;

	if ((db = fts_flatcurve_xapian_read_db(backend)) == NULL)
		return;

	try {
		*last_uid_r =
			db->get_document(db->get_lastdocid()).get_docid();
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "get_last_uid (%s); %s",
			str_c(backend->boxname), e.get_msg().c_str());
	}
}

int fts_flatcurve_xapian_uid_exists(struct flatcurve_fts_backend *backend,
				    uint32_t uid)
{
	Xapian::Database *db;

	if ((db = fts_flatcurve_xapian_read_db(backend)) == NULL)
		return -1;

	return fts_flatcurve_xapian_uid_exists_db(db, uid);
}

void fts_flatcurve_xapian_expunge(struct flatcurve_fts_backend *backend,
				  uint32_t uid)
{
	Xapian::WritableDatabase *dbw;

	dbw = fts_flatcurve_xapian_write_db_by_uid(backend, uid);
	if (dbw == NULL) {
		e_debug(backend->event, "Expunge failed mailbox=%s uid=%u; "
			"could not open DB to expunge",
			str_c(backend->boxname), uid);
		return;
	}

	try {
		dbw->delete_document(uid);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "update_expunge (%s)",
			e.get_msg().c_str());
	}
}

bool
fts_flatcurve_xapian_init_msg(struct flatcurve_fts_backend_update_context *ctx)
{
	Xapian::WritableDatabase *dbw;
	Xapian::Document doc;
	struct flatcurve_xapian *xapian = ctx->backend->xapian;

	if (ctx->uid == xapian->doc_uid) {
		return TRUE;
	}

	fts_flatcurve_xapian_clear_document(ctx->backend);

	if ((dbw = fts_flatcurve_xapian_write_db(ctx->backend)) == NULL)
		return FALSE;

	try {
		doc = dbw->get_document(ctx->uid);
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
				  const unsigned char *data, size_t size)
{
	std::string h;
	icu::UnicodeString s, temp;
	int32_t i = 0;
	struct flatcurve_xapian *xapian = ctx->backend->xapian;

	if (!fts_flatcurve_xapian_init_msg(ctx))
		return;

	if (str_len(ctx->hdr_name)) {
		h = str_lcase(str_c_modifiable(ctx->hdr_name));
		xapian->doc->add_boolean_term(
			FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX + h);
	}

	/* Xapian does not support substring searches by default, so we
	 * instead need to explicitly store all substrings of a string, up
	 * to the point where the substring becomes smaller than
	 * min_term_size. We need to use icu in order to correctly handle
	 * multi-byte characters. */
	s = icu::UnicodeString::fromUTF8(
		icu::StringPiece((const char *)data, size));
	if (ctx->indexed_hdr)
		h = str_ucase(str_c_modifiable(ctx->hdr_name));

	do {
		std::string t;

		temp = s.tempSubString(i++);
		temp.toUTF8String(t);

		if (ctx->indexed_hdr) {
			xapian->doc->add_term(
				FLATCURVE_XAPIAN_HEADER_PREFIX + h + t);
		}
		xapian->doc->add_term(FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX + t);
	} while (ctx->backend->fuser->set.substring_search &&
		 (temp.length() >= ctx->backend->fuser->set.min_term_size));
}

void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				const unsigned char *data, size_t size)
{
	int32_t i = 0;
	icu::UnicodeString s, temp;
	struct flatcurve_xapian *xapian = ctx->backend->xapian;

	if (!fts_flatcurve_xapian_init_msg(ctx))
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
	} while (ctx->backend->fuser->set.substring_search &&
		 (temp.length() >= ctx->backend->fuser->set.min_term_size));
}

void fts_flatcurve_xapian_delete_index(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close(backend);
	fts_flatcurve_xapian_delete_db_dir(backend, str_c(backend->db_path));
}

void fts_flatcurve_xapian_optimize_box(struct flatcurve_fts_backend *backend)
{
#ifdef XAPIAN_HAS_COMPACT
	Xapian::Database *db;
	struct fts_flatcurve_xapian_db_iter *iter;
	std::string n, o;
	const char *path;

	if ((db = fts_flatcurve_xapian_read_db(backend)) == NULL)
		return;

	o = str_c(backend->db_path);
	o += FLATCURVE_XAPIAN_DB_OPTIMIZE_PREFIX;

	try {
		db->compact(o, Xapian::DBCOMPACT_NO_RENUMBER |
			       Xapian::DBCOMPACT_MULTIPASS |
			       Xapian::Compactor::FULLER);
	} catch (Xapian::Error &e) {
		e_error(backend->event, "Error optimizing DB mailbox=%s; %s",
			str_c(backend->boxname), e.get_msg().c_str());
		return;
	}

	if (!fts_flatcurve_xapian_rename_db(backend, o.c_str(), n)) {
		e_error(backend->event, "Activating new index failed "
			"mailbox=%s", str_c(backend->boxname));
		fts_flatcurve_xapian_delete_db_dir(backend, o.c_str());
		return;
	}

	/* Delete old indexes except for new DB. */
	fts_flatcurve_xapian_close(backend);
	if ((iter = fts_flatcurve_xapian_db_iter_init(backend, FLATCURVE_XAPIAN_DB_ITER_IGNORE_OPTIMIZE)) == NULL) {
		e_error(backend->event, "Activating new index (%s -> %s) "
			"failed mailbox=%s; %m", o.c_str(), n.c_str(),
			str_c(backend->boxname));
		fts_flatcurve_xapian_delete_db_dir(backend, n.c_str());
		return;
	}

	iter->ignore = n.c_str();
	while ((path = fts_flatcurve_xapian_db_iter_next(iter)) != NULL) {
		fts_flatcurve_xapian_delete_db_dir(backend, path);
	}
	fts_flatcurve_xapian_db_iter_deinit(&iter);

	e_debug(backend->event, "Optimized DB; mailbox=%s",
		str_c(backend->boxname));
#endif
}

static bool
fts_flatcurve_build_query_arg(struct flatcurve_fts_query *query,
			      struct mail_search_arg *arg)
{
	struct flatcurve_fts_query_arg *a;
	Xapian::Database *db;
	string_t *hdr, *hdr2;
	std::string t;
	struct flatcurve_fts_query_xapian *x = query->xapian;

	a = array_append_space(&x->args);
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
		array_pop_back(&x->args);
		return TRUE;

	case SEARCH_OR:
	case SEARCH_SUB:
		/* FTS API says to ignore these. */
		array_pop_back(&x->args);
		return TRUE;

	default:
		array_pop_back(&x->args);
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
		if (((db = fts_flatcurve_xapian_read_db(query->backend)) == NULL) ||
		    !db->has_positions()) {
			/* Phrase searching not available. */
			array_pop_back(&x->args);
			return TRUE;
		}

		if (t.size() > 0)
			t = "\"" + t + "\"";
	}

	switch (arg->type) {
	case SEARCH_TEXT:
		x->qp->add_prefix(FLATCURVE_XAPIAN_ALL_HEADERS_QP,
				  FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX);
		str_printfa(a->value, "%s:%s* OR %s*",
			    FLATCURVE_XAPIAN_ALL_HEADERS_QP, t.c_str(),
			    t.c_str());
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
				str_printfa(hdr, "%s%s",
					    FLATCURVE_XAPIAN_HEADER_QP,
					    t_str_lcase(arg->hdr_field_name));
				hdr2 = str_new(query->pool, 32);
				str_printfa(hdr2, "%s%s",
					    FLATCURVE_XAPIAN_HEADER_PREFIX,
					    t_str_ucase(arg->hdr_field_name));
				x->qp->add_prefix(str_c(hdr), str_c(hdr2));
				str_printfa(a->value, "%s:%s*", str_c(hdr), t.c_str());
			} else {
				x->qp->add_prefix(
					FLATCURVE_XAPIAN_ALL_HEADERS_QP,
					FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX);
				str_printfa(a->value, "%s:%s*",
					    FLATCURVE_XAPIAN_ALL_HEADERS_QP,
					    t.c_str());
				/* We can only match if it appears in the pool
				 * of header terms, not to a specific header,
				 * so this is a maybe match. */
				/* FIXME: Add header names to search? */
				query->maybe = TRUE;
			}
		} else {
			x->qp->add_boolean_prefix(
				FLATCURVE_XAPIAN_HEADER_BOOL_QP,
				FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX);
			str_printfa(a->value, "%s:%s",
				    FLATCURVE_XAPIAN_HEADER_BOOL_QP,
				    t_str_lcase(arg->hdr_field_name));
		}
		break;
	}

	return TRUE;
}

static void
fts_flatcurve_xapian_build_query_deinit(struct flatcurve_fts_query *query)
{
	array_free(&query->xapian->args);
	delete(query->xapian->qp);
}

bool fts_flatcurve_xapian_build_query(struct flatcurve_fts_query *query)
{
	const struct flatcurve_fts_query_arg *a, *prev;
	struct mail_search_arg *args = query->args;
	bool ret = TRUE;
	std::string str;
	struct flatcurve_fts_query_xapian *x;

	x = query->xapian = p_new(query->pool,
				  struct flatcurve_fts_query_xapian, 1);
	if (query->match_all) {
		query->qtext = str_new_const(query->pool, "[Match All]", 11);
		x->query = new Xapian::Query(Xapian::Query::MatchAll);
		return TRUE;
	}

	p_array_init(&x->args, query->pool, 4);

	x->qp = new Xapian::QueryParser();
	x->qp->set_stemming_strategy(Xapian::QueryParser::STEM_NONE);

	for (; args != NULL ; args = args->next) {
		if (!fts_flatcurve_build_query_arg(query, args)) {
			fts_flatcurve_xapian_build_query_deinit(query);
			return FALSE;
		}
	}

	/* Empty Query. Optimize by not creating a query and returning no
	 * results when we go through the iteration later. */
	if (array_is_empty(&x->args)) {
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

	query->qtext = str_new(query->pool, 64);
	str_append(query->qtext, str.c_str());

	try {
		x->query = new Xapian::Query(x->qp->parse_query(
			str,
			Xapian::QueryParser::FLAG_BOOLEAN |
			Xapian::QueryParser::FLAG_PHRASE |
			Xapian::QueryParser::FLAG_PURE_NOT |
			Xapian::QueryParser::FLAG_WILDCARD
		));
	} catch (Xapian::QueryParserError &e) {
		e_error(query->backend->event, "Parsing query failed: %s",
			e.get_msg().c_str());
		ret = FALSE;
	}

	fts_flatcurve_xapian_build_query_deinit(query);

	return ret;
}

struct fts_flatcurve_xapian_query_iter *
fts_flatcurve_xapian_query_iter_init(struct flatcurve_fts_query *query)
{
	Xapian::Database *db;
	struct fts_flatcurve_xapian_query_iter *iter;

	bool empty_query = (query->xapian->query == NULL);

	if (!empty_query &&
	    ((db = fts_flatcurve_xapian_read_db(query->backend)) == NULL))
		return NULL;

	iter = p_new(query->pool, struct fts_flatcurve_xapian_query_iter, 1);
	iter->query = query;
	if (!empty_query) {
		iter->enquire = new Xapian::Enquire(*db);
		iter->enquire->set_docid_order(Xapian::Enquire::DONT_CARE);
		iter->enquire->set_query(*query->xapian->query);
	}
	iter->result = p_new(query->pool,
			     struct fts_flatcurve_xapian_query_result, 1);
	iter->size = 0;

	return iter;
}

struct fts_flatcurve_xapian_query_result *
fts_flatcurve_xapian_query_iter_next(struct fts_flatcurve_xapian_query_iter *iter)
{
	Xapian::MSet m;

	if (iter->size == 0) {
		if (iter->enquire == NULL)
			return NULL;
		m = iter->enquire->get_mset(iter->offset, FLATCURVE_MSET_RANGE);
		if (m.empty())
			return NULL;
		iter->i = m.begin();
		iter->offset += FLATCURVE_MSET_RANGE;
		iter->size = m.size();
	}

	iter->result->score = iter->i.get_weight();
	/* MSet docid can be an "interleaved" docid generated by
	 * Xapian::Database when handling multiple DBs at once. Instead, we
	 * want the "unique docid", which is obtained by looking at the
	 * doc id from the Document object itself. */
	iter->result->uid = iter->i.get_document().get_docid();
	++iter->i;
	--iter->size;

	return iter->result;
}

void
fts_flatcurve_xapian_query_iter_deinit(struct fts_flatcurve_xapian_query_iter **_iter)
{
	struct fts_flatcurve_xapian_query_iter *iter = *_iter;

	/* Need to explicitly call dtor, or else MSet doesn't release memory
	 * allocated internally. */
	*_iter = NULL;
	iter->i.~MSetIterator();
	delete(iter->enquire);
	p_free(iter->query->pool, iter->result);
	p_free(iter->query->pool, iter);
}

bool fts_flatcurve_xapian_run_query(struct flatcurve_fts_query *query,
				    struct flatcurve_fts_result *r)
{
	struct fts_flatcurve_xapian_query_iter *iter;
	struct fts_flatcurve_xapian_query_result *result;
	struct fts_score_map *score;

	if ((iter = fts_flatcurve_xapian_query_iter_init(query)) == NULL)
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

const char *fts_flatcurve_xapian_library_version()
{
	return Xapian::version_string();
}
