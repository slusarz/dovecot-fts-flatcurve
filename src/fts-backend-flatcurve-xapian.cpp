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
#include "hash.h"
#include "str.h"
#include "mail-storage-private.h"
#include "mail-search.h"
#include "sleep.h"
#include "time-util.h"
#include "unlink-directory.h"
#include "fts-backend-flatcurve.h"
#include "fts-backend-flatcurve-xapian.h"
#include <dirent.h>
#include <stdio.h>
};

/* How Xapian DBs work in fts-flatcurve: all data lives in under one
 * per-mailbox directory (FTS_FLATCURVE_LABEL) stored at the root of the
 * mailbox indexes directory.
 *
 * There are two different permanent data types within that library:
 * - "index.###": The actual Xapian DB shards. Combined, this comprises the
 *   FTS data for the mailbox. These shards may be directly written to, but
 *   only when deleting messages - new messages are never stored directly to
 *   this DB. Additionally, these DBs are never directly queried; a dummy
 *   object is used to indirectly query them. These indexes may occasionally
 *   be combined into a single index via optimization processes.
 * - "current.###": Xapian DB that contains the index shard where new messages
 *   are stored. Once this index reaches certain (configurable) limits, a new
 *   shard is created and rotated in as the new current index by creating
 *   a shard with a suffix higher than the previous current DB.
 *
 * Within a session, we create a dummy Xapian::Database object, scan the data
 * directory for all indexes, and add each of them to the dummy object. For
 * queries, we then just need to query the dummy object and Xapian handles
 * everything for us. Writes need to be handled separately, as a
 * WritableDatabase object only supports a single on-disk DB at a time; a DB
 * shard, whether "index" or "current", must be directly written to in order
 * to modify. */
#define FLATCURVE_XAPIAN_DB_PREFIX "index."
#define FLATCURVE_XAPIAN_DB_CURRENT_PREFIX "current."

/* These are temporary data types that may appear in the fts directory. They
 * are not intended to perservere between sessions. */
#define FLATCURVE_XAPIAN_DB_OPTIMIZE "optimize"

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

/* Version database, so that any schema changes can be caught. */
#define FLATCURVE_XAPIAN_DB_KEY_PREFIX "dovecot."
#define FLATCURVE_XAPIAN_DB_VERSION_KEY FLATCURVE_XAPIAN_DB_KEY_PREFIX \
	FTS_FLATCURVE_LABEL
#define FLATCURVE_XAPIAN_DB_VERSION 1

#define FLATCURVE_DBW_LOCK_RETRY_SECS 1
#define FLATCURVE_MSET_RANGE 10

struct flatcurve_xapian_db_path {
	const char *fname;
	const char *path;
};

enum flatcurve_xapian_db_type {
	FLATCURVE_XAPIAN_DB_TYPE_INDEX,
	FLATCURVE_XAPIAN_DB_TYPE_CURRENT,
	FLATCURVE_XAPIAN_DB_TYPE_OPTIMIZE,
	FLATCURVE_XAPIAN_DB_TYPE_UNKNOWN
};

struct flatcurve_xapian_db {
	Xapian::Database *db;
	Xapian::WritableDatabase *dbw;
	struct flatcurve_xapian_db_path *dbpath;
	size_t dbw_doccount;
	unsigned int changes;
	enum flatcurve_xapian_db_type type;
};
HASH_TABLE_DEFINE_TYPE(xapian_db, char *, struct flatcurve_xapian_db *);

struct flatcurve_xapian {
	/* Current database objects. */
	struct flatcurve_xapian_db *dbw_current;
	Xapian::Database *db_read;
	HASH_TABLE_TYPE(xapian_db) dbs;

	/* Xapian pool: used for per mailbox DB info, so it can be easily
	 * cleared when switching mailboxes. Not for use with long
	 * lived data (e.g. optimize). */
	pool_t pool;

	/* Current document. */
	Xapian::Document *doc;
	uint32_t doc_uid;
	unsigned int doc_updates;
	bool doc_created:1;

	/* List of mailboxes to optimize at shutdown. */
	HASH_TABLE(char *, char *) optimize;
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

struct flatcurve_xapian_db_iter {
	struct flatcurve_fts_backend *backend;
	DIR *dirp;

	/* These are set every time next() is run. */
	struct flatcurve_xapian_db_path *path;
	enum flatcurve_xapian_db_type type;
};

enum flatcurve_xapian_db_opts {
	FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT = BIT(0),
	FLATCURVE_XAPIAN_DB_IGNORE_EMPTY     = BIT(1),
	FLATCURVE_XAPIAN_DB_NOCLOSE_CURRENT  = BIT(2)
};
enum flatcurve_xapian_wdb {
	FLATCURVE_XAPIAN_WDB_CREATE  = BIT(0),
	FLATCURVE_XAPIAN_WDB_NODEBUG = BIT(1)
};
enum flatcurve_xapian_db_close {
	FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT = BIT(0),
	FLATCURVE_XAPIAN_DB_CLOSE_WDB_CLOSE  = BIT(1),
	FLATCURVE_XAPIAN_DB_CLOSE_DB_CLOSE   = BIT(2),
	FLATCURVE_XAPIAN_DB_CLOSE_ROTATE     = BIT(3)
};

/* Externally accessible struct. */
struct fts_flatcurve_xapian_query_iter {
	struct flatcurve_fts_backend *backend;
	struct flatcurve_fts_query *query;
	Xapian::Database *db;
	Xapian::Enquire *enquire;
	Xapian::MSetIterator i;
	unsigned int offset, size;
	struct fts_flatcurve_xapian_query_result *result;
};

static void
fts_flatcurve_xapian_check_db_version(struct flatcurve_fts_backend *backend,
				      struct flatcurve_xapian_db *xdb);
static void
fts_flatcurve_xapian_close_dbs(struct flatcurve_fts_backend *backend,
			       enum flatcurve_xapian_db_close opts);
static bool
fts_flatcurve_xapian_db_populate(struct flatcurve_fts_backend *backend,
				 enum flatcurve_xapian_db_opts opts);


void fts_flatcurve_xapian_init(struct flatcurve_fts_backend *backend)
{
	backend->xapian = p_new(backend->pool, struct flatcurve_xapian, 1);
	backend->xapian->pool =
		pool_alloconly_create(FTS_FLATCURVE_LABEL " xapian", 2048);
	hash_table_create(&backend->xapian->dbs, backend->xapian->pool,
			  4, str_hash, strcmp);
}

void fts_flatcurve_xapian_deinit(struct flatcurve_fts_backend *backend)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	struct flatcurve_xapian *x = backend->xapian;

	if (hash_table_is_created(x->optimize)) {
		iter = hash_table_iterate_init(x->optimize);
		while (hash_table_iterate(iter, x->optimize, &key, &val)) {
			str_append(backend->boxname, (const char *)key);
			str_append(backend->db_path, (const char *)val);
			fts_flatcurve_xapian_optimize_box(backend);
		}
		hash_table_iterate_deinit(&iter);
		hash_table_destroy(&x->optimize);
	}
	hash_table_destroy(&x->dbs);
	pool_unref(&x->pool);
}

static struct flatcurve_xapian_db_path *
fts_flatcurve_xapian_create_db_path(struct flatcurve_fts_backend *backend,
				    const char *fname)
{
	struct flatcurve_xapian_db_path *dbpath;

	dbpath = p_new(backend->xapian->pool,
		       struct flatcurve_xapian_db_path, 1);
	dbpath->fname = p_strdup(backend->xapian->pool, fname);
	dbpath->path = p_strdup_printf(backend->xapian->pool, "%s%s",
				       str_c(backend->db_path), fname);

	return dbpath;
}

// dbpath = NULL: delete the entire flatcurve index
static void
fts_flatcurve_xapian_delete(struct flatcurve_fts_backend *backend,
			    struct flatcurve_xapian_db_path *dbpath)
{
	const char *error, *path;
	struct stat st;
	enum unlink_directory_flags unlink_flags = UNLINK_DIRECTORY_FLAG_RMDIR;

	path = (dbpath == NULL)
		? str_c(backend->db_path)
		: dbpath->path;

	if (stat(path, &st) < 0)
		return;

	if (S_ISDIR(st.st_mode)) {
		if (unlink_directory(path, unlink_flags, &error) < 0)
			e_debug(backend->event, "Deleting fts data failed "
				"mailbox=%s dir=%s; %s",
				str_c(backend->boxname), path, error);
	} else if (unlink(path) < 0)
		e_debug(backend->event, "Deleting fts data failed mailbox=%s "
			"file=%s", str_c(backend->boxname), path);
}

static struct flatcurve_xapian_db_iter *
fts_flatcurve_xapian_db_iter_init(struct flatcurve_fts_backend *backend,
				  enum flatcurve_xapian_db_opts opts)
{
	DIR *dirp;
	struct flatcurve_xapian_db_iter *iter;

	dirp = opendir(str_c(backend->db_path));
	if ((dirp == NULL) &&
	    HAS_NO_BITS(opts, FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT)) {
		if (errno == ENOENT) {
			if (mailbox_list_mkdir_root(backend->backend.ns->list, str_c(backend->db_path), MAILBOX_LIST_PATH_TYPE_INDEX) < 0) {
				e_debug(backend->event, "Cannot create DB "
					"(RW) mailbox=%s; %s",
					str_c(backend->boxname),
					str_c(backend->db_path));
				return NULL;
			}
			return fts_flatcurve_xapian_db_iter_init(backend, opts);
		}

		e_debug(backend->event, "Cannot open DB (RO) mailbox=%s; "
			"opendir(%s) failed: %m", str_c(backend->boxname),
			str_c(backend->db_path));
		return NULL;
	}

	iter = p_new(backend->xapian->pool, struct flatcurve_xapian_db_iter, 1);
	iter->backend = backend;
	iter->dirp = dirp;

	return iter;
}

static bool
fts_flatcurve_xapian_db_iter_next(struct flatcurve_xapian_db_iter *iter)
{
	struct dirent *d;
	struct stat st;

	if ((iter->dirp == NULL) || (d = readdir(iter->dirp)) == NULL)
		return FALSE;

	if ((strcmp(d->d_name, ".") == 0) || (strcmp(d->d_name, "..") == 0))
		return fts_flatcurve_xapian_db_iter_next(iter);

	iter->path = fts_flatcurve_xapian_create_db_path(iter->backend,
							 d->d_name);
	iter->type = FLATCURVE_XAPIAN_DB_TYPE_UNKNOWN;

	if (str_begins(d->d_name, FLATCURVE_XAPIAN_DB_PREFIX)) {
		if ((stat(iter->path->path, &st) >= 0) && S_ISDIR(st.st_mode))
			iter->type = FLATCURVE_XAPIAN_DB_TYPE_INDEX;
	} else if (str_begins(d->d_name, FLATCURVE_XAPIAN_DB_CURRENT_PREFIX)) {
		if ((stat(iter->path->path, &st) >= 0) && S_ISDIR(st.st_mode))
			iter->type = FLATCURVE_XAPIAN_DB_TYPE_CURRENT;
	} else if (strcmp(d->d_name, FLATCURVE_XAPIAN_DB_OPTIMIZE) == 0) {
		if ((stat(iter->path->path, &st) >= 0) && S_ISDIR(st.st_mode))
			iter->type = FLATCURVE_XAPIAN_DB_TYPE_OPTIMIZE;
	}

	return TRUE;
}

static void
fts_flatcurve_xapian_db_iter_deinit(struct flatcurve_xapian_db_iter **_iter)
{
	struct flatcurve_xapian_db_iter *iter = *_iter;

	*_iter = NULL;

	if (iter->dirp != NULL)
		(void)closedir(iter->dirp);
	p_free(iter->backend->xapian->pool, iter);
}

// throws Exception on error
static struct flatcurve_xapian_db *
fts_flatcurve_xapian_write_db_get_do(struct flatcurve_fts_backend *backend,
				     struct flatcurve_xapian_db *xdb,
				     int db_flags)
{
	enum flatcurve_xapian_db_opts opts =
		FLATCURVE_XAPIAN_DB_NOCLOSE_CURRENT;

	while (xdb->dbw == NULL) {
		try {
			xdb->dbw = new Xapian::WritableDatabase(
					xdb->dbpath->path, db_flags);
		} catch (Xapian::DatabaseLockError &e) {
			e_debug(backend->event, "Waiting for DB (RW) lock "
				"mailbox=%s", str_c(backend->boxname));
			i_sleep_intr_secs(FLATCURVE_DBW_LOCK_RETRY_SECS);
		} catch (Xapian::DatabaseOpeningError &e) {
			if (xdb->type != FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
				throw;
			/* The current DB has changed via another process.
			 * Close all DBs and reload. */
			fts_flatcurve_xapian_close(backend);
			if (!fts_flatcurve_xapian_db_populate(backend, opts))
				throw;
			xdb = backend->xapian->dbw_current;
		}
	}

	return xdb;
}

static struct flatcurve_xapian_db *
fts_flatcurve_xapian_write_db_get(struct flatcurve_fts_backend *backend,
				  struct flatcurve_xapian_db *xdb,
				  enum flatcurve_xapian_wdb wopts)
{
	int db_flags;

	if (xdb->dbw != NULL)
		return xdb;

	db_flags = (HAS_ALL_BITS(wopts, FLATCURVE_XAPIAN_WDB_CREATE)
		? Xapian::DB_CREATE_OR_OPEN : Xapian::DB_OPEN) |
		Xapian::DB_NO_SYNC;

	try {
		fts_flatcurve_xapian_write_db_get_do(backend, xdb, db_flags);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "Cannot open DB (RW) "
			"mailbox=%s; %s", str_c(backend->boxname),
			e.get_msg().c_str());
		return NULL;
	}

	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		xdb->dbw_doccount = xdb->dbw->get_doccount();

	if (HAS_NO_BITS(wopts, FLATCURVE_XAPIAN_WDB_NODEBUG))
		e_debug(backend->event, "Opened DB (RW; %s) mailbox=%s "
			"messages=%u version=%u", xdb->dbpath->fname,
			str_c(backend->boxname), xdb->dbw->get_doccount(),
			FLATCURVE_XAPIAN_DB_VERSION);

	return xdb;
}

static struct flatcurve_xapian_db_path *
fts_flatcurve_xapian_rename_db(struct flatcurve_fts_backend *backend,
			       struct flatcurve_xapian_db_path *path)
{
	unsigned int i;
	std::string new_fname;
	struct flatcurve_xapian_db_path *newpath;
	bool retry = FALSE;
	std::ostringstream ss;

	for (i = 0; i < 5; ++i) {
		new_fname.clear();
		new_fname = FLATCURVE_XAPIAN_DB_PREFIX;
		ss << i_rand_limit(8192);
		new_fname += ss.str();

		newpath = fts_flatcurve_xapian_create_db_path(
				backend, new_fname.c_str());

		if (rename(path->path, newpath->path) < 0) {
			if (retry ||
			    (errno != ENOTEMPTY) && (errno != EEXIST)) {
				p_free(backend->xapian->pool, newpath);
				return NULL;
			}

			/* Looks like a naming conflict; try once again with
			 * a different filename. ss will have additional
			 * randomness added to the original suffix, so it
			 * will almost certainly work the second time. */
			retry = TRUE;
		} else {
			return newpath;
		}
	}

	/* If we still haven't found a valid filename, something is very
	 * wrong. Exit before we enter an infinite loop and consume all the
	 * memory. */
	i_unreached();
}

static void
fts_flatcurve_xapian_optimize_mailbox(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;

	if (!hash_table_is_created(x->optimize))
		hash_table_create(&x->optimize, backend->pool, 0, str_hash,
				  strcmp);
	if (hash_table_lookup(x->optimize, str_c(backend->boxname)) == NULL)
		hash_table_insert(x->optimize,
				  p_strdup(backend->pool, str_c(backend->boxname)),
				  p_strdup(backend->pool, str_c(backend->db_path)));
}

static struct flatcurve_xapian_db *
fts_flatcurve_xapian_db_add(struct flatcurve_fts_backend *backend,
			    struct flatcurve_xapian_db_path *dbpath,
			    enum flatcurve_xapian_db_type type)
{
	struct flatcurve_xapian_db *o, *xdb;
	struct flatcurve_xapian *x = backend->xapian;

	if ((type != FLATCURVE_XAPIAN_DB_TYPE_INDEX) &&
	    (type != FLATCURVE_XAPIAN_DB_TYPE_CURRENT))
		return NULL;

	xdb = p_new(x->pool, struct flatcurve_xapian_db, 1);
	xdb->dbpath = dbpath;
	xdb->type = type;
	hash_table_insert(x->dbs, dbpath->fname, xdb);

	/* If multiple current DBs exist, rename the oldest. */
	if ((type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT) &&
	    (x->dbw_current != NULL)) {
		o = (strcmp(dbpath->fname, x->dbw_current->dbpath->fname) > 0)
			? x->dbw_current : xdb;
		(void)fts_flatcurve_xapian_rename_db(backend, o->dbpath);
		o->type = FLATCURVE_XAPIAN_DB_TYPE_INDEX;
	}

	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		x->dbw_current = xdb;

	return xdb;
}

static void
fts_flatcurve_xapian_close_dbw(struct flatcurve_xapian_db *xdb)
{
	if (xdb->dbw == NULL)
		return;

	xdb->dbw->close();
	delete(xdb->dbw);
	xdb->dbw = NULL;
	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		xdb->dbw_doccount = 0;
}

static bool
fts_flatcurve_xapian_create_current(struct flatcurve_fts_backend *backend,
				    bool keep_open)
{
	struct flatcurve_xapian_db_path *dbpath;
	std::ostringstream s;
	enum flatcurve_xapian_wdb wopts = (enum flatcurve_xapian_wdb)
		(FLATCURVE_XAPIAN_WDB_CREATE | FLATCURVE_XAPIAN_WDB_NODEBUG);
	struct flatcurve_xapian_db *xdb;

	/* The current shard has filename of the format PREFIX.timestamp. This
	 * ensures that we will catch any current DB renaming done by another
	 * process (reopen() on the DB will fail, causing the entire DB to be
	 * closed/reopened). */
	s << FLATCURVE_XAPIAN_DB_CURRENT_PREFIX << i_microseconds();
	dbpath = fts_flatcurve_xapian_create_db_path(backend, s.str().c_str());
	xdb = fts_flatcurve_xapian_db_add(backend, dbpath,
					  FLATCURVE_XAPIAN_DB_TYPE_CURRENT);
	if (fts_flatcurve_xapian_write_db_get(backend, xdb, wopts) == NULL)
		return FALSE;

	if (!keep_open)
		fts_flatcurve_xapian_close_dbw(xdb);

	return TRUE;
}

static bool
fts_flatcurve_xapian_db_populate(struct flatcurve_fts_backend *backend,
				 enum flatcurve_xapian_db_opts opts)
{
	struct flatcurve_xapian_db_iter *iter;
	struct flatcurve_xapian *x = backend->xapian;

	/* Build the DB list, if we haven't already done so. */
	if (hash_table_count(x->dbs) != 0) {
		return TRUE;
	}

	if ((iter = fts_flatcurve_xapian_db_iter_init(backend, opts)) == NULL)
		return FALSE;
	while (fts_flatcurve_xapian_db_iter_next(iter)) {
		if ((iter->type == FLATCURVE_XAPIAN_DB_TYPE_INDEX) ||
		    (iter->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT))
			(void)fts_flatcurve_xapian_db_add(backend, iter->path,
							  iter->type);
	}
	fts_flatcurve_xapian_db_iter_deinit(&iter);

	if ((x->dbw_current == NULL) &&
	    HAS_NO_BITS(opts, FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT) &&
	    !fts_flatcurve_xapian_create_current(backend, HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_NOCLOSE_CURRENT)))
		return FALSE;

	return TRUE;
}

static struct flatcurve_xapian_db *
fts_flatcurve_xapian_write_db_current(struct flatcurve_fts_backend *backend,
				      enum flatcurve_xapian_db_opts opts)
{
	enum flatcurve_xapian_wdb wopts;
	struct flatcurve_xapian *x = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	if ((x->dbw_current != NULL) && (x->dbw_current->dbw != NULL))
		return x->dbw_current;

	if (!fts_flatcurve_xapian_db_populate(backend, opts))
		return NULL;

	xdb = fts_flatcurve_xapian_write_db_get(backend, x->dbw_current, wopts);
	if (xdb == NULL)
		return NULL;

	fts_flatcurve_xapian_check_db_version(backend, xdb);

	return x->dbw_current;
}

static Xapian::Database *
fts_flatcurve_xapian_read_db(struct flatcurve_fts_backend *backend,
			     enum flatcurve_xapian_db_opts opts)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	unsigned int shards = 0;
	struct flatcurve_xapian *x = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	if (x->db_read != NULL) {
		try {
			(void)x->db_read->reopen();
		} catch (Xapian::DatabaseNotFoundError &e) {
			/* This means that the underlying databases have
			 * changed (i.e. DB rotation by another process).
			 * Close all DBs and reopen. */
			 fts_flatcurve_xapian_close(backend);
			 return fts_flatcurve_xapian_read_db(backend, opts);
		}

		return x->db_read;
	}

	if (!fts_flatcurve_xapian_db_populate(backend, opts))
		return NULL;

	if (HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_IGNORE_EMPTY) &&
	    (hash_table_count(x->dbs) == 0))
		return NULL;

	x->db_read = new Xapian::Database();

	iter = hash_table_iterate_init(x->dbs);
	while (hash_table_iterate(iter, x->dbs, &key, &val)) {
		xdb = (struct flatcurve_xapian_db *)val;
		try {
			xdb->db = new Xapian::Database(xdb->dbpath->path);
			fts_flatcurve_xapian_check_db_version(backend, xdb);
			++shards;
			x->db_read->add_database(*(xdb->db));
		} catch (Xapian::Error &e) {
			e_debug(backend->event, "Cannot open DB (RO) "
				"mailbox=%s; %s", str_c(backend->boxname),
				e.get_msg().c_str());
			/* If we can't open a DB, delete it. */
			fts_flatcurve_xapian_delete(backend, xdb->dbpath);
		}
	}
	hash_table_iterate_deinit(&iter);

	e_debug(backend->event, "Opened DB (RO) mailbox=%s messages=%u "
		"version=%u shards=%u", str_c(backend->boxname),
		x->db_read->get_doccount(), FLATCURVE_XAPIAN_DB_VERSION,
		shards);

	if ((backend->fuser->set.optimize_limit > 0) &&
	    (shards >= backend->fuser->set.optimize_limit))
		fts_flatcurve_xapian_optimize_mailbox(backend);

	return x->db_read;
}

static void
fts_flatcurve_xapian_check_db_version(struct flatcurve_fts_backend *backend,
				      struct flatcurve_xapian_db *xdb)
{
	Xapian::Database *db = (xdb->dbw == NULL) ? xdb->db : xdb->dbw;
	std::ostringstream ss;
	int v;
	std::string ver;
	enum flatcurve_xapian_wdb wopts;

	ver = db->get_metadata(FLATCURVE_XAPIAN_DB_VERSION_KEY);
	v = ver.empty() ? 0 : std::atoi(ver.c_str());

	if (v == FLATCURVE_XAPIAN_DB_VERSION)
		return;

	/* If we need to upgrade DB, and this is NOT the write DB, open the
	 * write DB, do the changes there, and reopen the read DB. */
	if (!xdb->dbw) {
		(void)fts_flatcurve_xapian_write_db_get(backend, xdb, wopts);
		fts_flatcurve_xapian_close_dbw(xdb);
		(void)xdb->db->reopen();
		return;
        }

	/* 0->1: Added DB version. Always implicity update version when we
	 * upgrade (done at end of this function). */
	if (v == 0)
		++v;

	ss << FLATCURVE_XAPIAN_DB_VERSION;
	xdb->dbw->set_metadata(FLATCURVE_XAPIAN_DB_VERSION_KEY, ss.str());

	/* Commit the changes now. */
	xdb->dbw->commit();
}

// Function requires read DB to have been opened
static struct flatcurve_xapian_db *
fts_flatcurve_xapian_uid_exists_db(struct flatcurve_fts_backend *backend,
				   uint32_t uid)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	struct flatcurve_xapian_db *ret = NULL, *xdb;

        iter = hash_table_iterate_init(backend->xapian->dbs);
        while ((ret == NULL) &&
	       hash_table_iterate(iter, backend->xapian->dbs, &key, &val)) {
		xdb = (struct flatcurve_xapian_db *)val;
		try {
			(void)xdb->db->get_document(uid);
			ret = xdb;
		} catch (Xapian::DocNotFoundError &e) {}
	}
	hash_table_iterate_deinit(&iter);

	return ret;
}

static struct flatcurve_xapian_db *
fts_flatcurve_xapian_write_db_by_uid(struct flatcurve_fts_backend *backend,
				     uint32_t uid)
{
	enum flatcurve_xapian_db_opts opts;
	enum flatcurve_xapian_wdb wopts;
	struct flatcurve_xapian_db *xdb;

	(void)fts_flatcurve_xapian_read_db(backend, opts);
	xdb = fts_flatcurve_xapian_uid_exists_db(backend, uid);

	return (xdb == NULL)
		? NULL
		: fts_flatcurve_xapian_write_db_get(backend, xdb, wopts);
}

static void
fts_flatcurve_xapian_check_commit_limit(struct flatcurve_fts_backend *backend,
					struct flatcurve_xapian_db *xdb)
{
	struct fts_flatcurve_user *fuser = backend->fuser;
	struct flatcurve_xapian *x = backend->xapian;

	++x->doc_updates;
	++xdb->changes;

	if ((xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT) &&
	    (fuser->set.rotate_size > 0) &&
	    (xdb->dbw_doccount >= fuser->set.rotate_size)) {
		fts_flatcurve_xapian_close_dbs(
			backend, FLATCURVE_XAPIAN_DB_CLOSE_ROTATE);
	} else if ((fuser->set.commit_limit > 0) &&
		   (x->doc_updates >= fuser->set.commit_limit)) {
		fts_flatcurve_xapian_close_dbs(
			backend, FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT);
		e_debug(backend->event, "Committing DB as update "
			"limit was reached; mailbox=%s limit=%d",
			str_c(backend->boxname),
			fuser->set.commit_limit);
	}
}

static void
fts_flatcurve_xapian_clear_document(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;
	enum flatcurve_xapian_db_opts opts;
	struct flatcurve_xapian_db *xdb;

	if ((x->doc == NULL) ||
	    ((xdb = fts_flatcurve_xapian_write_db_current(backend, opts)) == NULL))
		return;

	try {
		xdb->dbw->replace_document(x->doc_uid, *x->doc);
	} catch (std::bad_alloc &b) {
		i_fatal(FTS_FLATCURVE_DEBUG_PREFIX "Out of memory "
			"when indexing mail (%s); mailbox=%s UID=%d "
			"(Hint: increase indexing process vsz_limit or define "
			"smaller commit_limit value in plugin config)",
			b.what(), str_c(backend->boxname), x->doc_uid);
	} catch (Xapian::Error &e) {
		e_warning(backend->event, "Could not write message data: "
			  "mailbox=%s uid=%u; %s", str_c(backend->boxname),
			  x->doc_uid, e.get_msg().c_str());
	}

	if (x->doc_created)
		delete(x->doc);
	x->doc = NULL;
	x->doc_created = FALSE;
	x->doc_uid = 0;

	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		++xdb->dbw_doccount;

	fts_flatcurve_xapian_check_commit_limit(backend, xdb);
}

static bool
fts_flatcurve_xapian_close_dbw_commit(struct flatcurve_fts_backend *backend,
				      struct flatcurve_xapian_db *xdb,
				      const struct timeval *start)
{
	int diff;
	struct timeval now;

	i_gettimeofday(&now);
	diff = timeval_diff_msecs(&now, start);

	e_debug(backend->event, "Committed %u changes to DB (RW; %s) in "
		"%u.%03u secs; mailbox=%s", xdb->changes, xdb->dbpath->fname,
		diff/1000, diff%1000, str_c(backend->boxname));

	xdb->changes = 0;

	return ((xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT) &&
		(backend->fuser->set.rotate_time > 0) &&
		(diff > backend->fuser->set.rotate_time));
}

static void
fts_flatcurve_xapian_rotate(struct flatcurve_fts_backend *backend)
{
	if (!fts_flatcurve_xapian_create_current(backend, TRUE))
		e_debug(backend->event, "Error when rotating DB mailbox=%s",
			str_c(backend->boxname));
	else
		e_debug(event_create_passthrough(backend->event)->
			set_name("fts_flatcurve_rotate")->
			add_str("mailbox", str_c(backend->boxname))->
			event(), "Rotating index mailbox=%s",
			str_c(backend->boxname));
}

static void
fts_flatcurve_xapian_close_dbs(struct flatcurve_fts_backend *backend,
			       enum flatcurve_xapian_db_close opts)
{
	bool commit;
	struct hash_iterate_context *iter;
	void *key, *val;
	struct timeval start;
	struct flatcurve_xapian *x = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	fts_flatcurve_xapian_clear_document(backend);

	iter = hash_table_iterate_init(x->dbs);
	while (hash_table_iterate(iter, x->dbs, &key, &val)) {
		xdb = (struct flatcurve_xapian_db *)val;
		if (xdb->dbw != NULL) {
			i_gettimeofday(&start);
			commit = FALSE;

			if (HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_WDB_CLOSE)) {
				fts_flatcurve_xapian_close_dbw(xdb);
				commit = TRUE;
			} else if (HAS_ANY_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT | FLATCURVE_XAPIAN_DB_CLOSE_ROTATE)) {
				xdb->dbw->commit();
				commit = TRUE;
			}

			if (commit &&
			    (fts_flatcurve_xapian_close_dbw_commit(backend, xdb, &start) ||
			     HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_ROTATE))) {
				fts_flatcurve_xapian_rotate(backend);
				fts_flatcurve_xapian_close_dbw(xdb);
			}

		}
		if ((xdb->db != NULL) &&
		    HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_DB_CLOSE)) {
			delete(xdb->db);
			xdb->db = NULL;
		}
	}
	hash_table_iterate_deinit(&iter);

	x->doc_updates = 0;
}

void fts_flatcurve_xapian_refresh(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close_dbs(backend,
				       FLATCURVE_XAPIAN_DB_CLOSE_WDB_CLOSE);
}

void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;

	fts_flatcurve_xapian_close_dbs(backend,
				       (enum flatcurve_xapian_db_close)
				       (FLATCURVE_XAPIAN_DB_CLOSE_WDB_CLOSE |
					FLATCURVE_XAPIAN_DB_CLOSE_DB_CLOSE));
	hash_table_clear(x->dbs, TRUE);

	x->dbw_current = NULL;

	if (x->db_read != NULL) {
		x->db_read->close();
		delete(x->db_read);
		x->db_read = NULL;
	}

	p_clear(x->pool);
}

static uint32_t
fts_flatcurve_xapian_get_last_uid_query(struct flatcurve_fts_backend *backend,
					Xapian::Database *db)
{
	Xapian::Enquire *enquire;
	Xapian::MSet m;

	enquire = new Xapian::Enquire(*db);
	enquire->set_docid_order(Xapian::Enquire::DESCENDING);
	enquire->set_query(Xapian::Query::MatchAll);

	m = enquire->get_mset(0, 1);
	return (m.empty())
		? 0 : m.begin().get_document().get_docid();
}

void fts_flatcurve_xapian_get_last_uid(struct flatcurve_fts_backend *backend,
				       uint32_t *last_uid_r)
{
	Xapian::Database *db;
	enum flatcurve_xapian_db_opts opts;

	if ((db = fts_flatcurve_xapian_read_db(backend, opts)) != NULL) {
		try {
			/* Optimization: if last used ID still exists in
			 * mailbox, this is a cheap call. */
			*last_uid_r = db->get_document(db->get_lastdocid())
					  .get_docid();
			return;
		} catch (Xapian::DocNotFoundError &e) {
			/* Last used Xapian ID is no longer in the DB. Need
			 * to do a manual search for the last existing ID. */
			*last_uid_r =
				fts_flatcurve_xapian_get_last_uid_query(backend, db);
			return;
		} catch (Xapian::InvalidArgumentError &e) {
			// Document ID is 0
		}
	}

	*last_uid_r = 0;
}

int fts_flatcurve_xapian_uid_exists(struct flatcurve_fts_backend *backend,
				    uint32_t uid)
{
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		(FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		 FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);

	return (fts_flatcurve_xapian_read_db(backend, opts) == NULL)
		? -1
		: (int)(fts_flatcurve_xapian_uid_exists_db(backend, uid) != NULL);
}

void fts_flatcurve_xapian_expunge(struct flatcurve_fts_backend *backend,
				  uint32_t uid)
{
	struct flatcurve_xapian_db *xdb;

	xdb = fts_flatcurve_xapian_write_db_by_uid(backend, uid);
	if (xdb == NULL) {
		e_debug(backend->event, "Expunge failed mailbox=%s uid=%u; "
			"could not open DB to expunge",
			str_c(backend->boxname), uid);
		return;
	}

	try {
		xdb->dbw->delete_document(uid);
		if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
			--xdb->dbw_doccount;
		fts_flatcurve_xapian_check_commit_limit(backend, xdb);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "update_expunge (%s)",
			e.get_msg().c_str());
	}
}

bool
fts_flatcurve_xapian_init_msg(struct flatcurve_fts_backend_update_context *ctx)
{
	Xapian::Document doc;
	struct flatcurve_xapian *x = ctx->backend->xapian;
	enum flatcurve_xapian_db_opts opts;
	struct flatcurve_xapian_db *xdb;

	if (ctx->uid == x->doc_uid) {
		return TRUE;
	}

	fts_flatcurve_xapian_clear_document(ctx->backend);

	if ((xdb = fts_flatcurve_xapian_write_db_current(ctx->backend, opts)) == NULL)
		return FALSE;

	try {
		doc = xdb->dbw->get_document(ctx->uid);
		x->doc = &doc;
	} catch (Xapian::DocNotFoundError &e) {
		x->doc = new Xapian::Document();
		x->doc_created = TRUE;
	} catch (Xapian::Error &e) {
		ctx->ctx.failed = TRUE;
		return FALSE;
	}

	x->doc_uid = ctx->uid;

	return TRUE;
}

void
fts_flatcurve_xapian_index_header(struct flatcurve_fts_backend_update_context *ctx,
				  const unsigned char *data, size_t size)
{
	struct fts_flatcurve_user *fuser = ctx->backend->fuser;
	std::string h;
	int32_t i = 0;
	icu::UnicodeString s, temp;
	struct flatcurve_xapian *x = ctx->backend->xapian;

	if (!fts_flatcurve_xapian_init_msg(ctx))
		return;

	if (str_len(ctx->hdr_name)) {
		h = str_lcase(str_c_modifiable(ctx->hdr_name));
		x->doc->add_boolean_term(
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
			x->doc->add_term(
				FLATCURVE_XAPIAN_HEADER_PREFIX + h + t);
		}
		x->doc->add_term(FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX + t);
	} while (fuser->set.substring_search &&
		 ((unsigned)temp.length() >= fuser->set.min_term_size));
}

void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				const unsigned char *data, size_t size)
{
	struct fts_flatcurve_user *fuser = ctx->backend->fuser;
	int32_t i = 0;
	icu::UnicodeString s, temp;
	struct flatcurve_xapian *x = ctx->backend->xapian;

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

		x->doc->add_term(t);
	} while (fuser->set.substring_search &&
		 ((unsigned)temp.length() >= fuser->set.min_term_size));
}

void fts_flatcurve_xapian_delete_index(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close(backend);
	fts_flatcurve_xapian_delete(backend, NULL);
}

void fts_flatcurve_xapian_optimize_box(struct flatcurve_fts_backend *backend)
{
#ifdef XAPIAN_HAS_COMPACT
	Xapian::Database *db;
	int diff;
	struct flatcurve_xapian_db_iter *iter;
	struct flatcurve_xapian_db_path *n, *o;
	struct timeval now, start;
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		(FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		 FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);

	if ((db = fts_flatcurve_xapian_read_db(backend, opts)) == NULL)
		return;

	e_debug(event_create_passthrough(backend->event)->
		set_name("fts_flatcurve_optimize")->
		add_str("mailbox", str_c(backend->boxname))->event(),
		"Optimizing mailbox=%s", str_c(backend->boxname));

	o = fts_flatcurve_xapian_create_db_path(
		backend, FLATCURVE_XAPIAN_DB_OPTIMIZE);
	fts_flatcurve_xapian_delete(backend, o);
	i_gettimeofday(&start);

	try {
		db->compact(o->path, Xapian::DBCOMPACT_NO_RENUMBER |
				     Xapian::DBCOMPACT_MULTIPASS |
				     Xapian::Compactor::FULLER);
	} catch (Xapian::Error &e) {
		e_error(backend->event, "Optimize failed mailbox=%s; %s",
			str_c(backend->boxname), e.get_msg().c_str());
		return;
	}

	n = t_new(struct flatcurve_xapian_db_path, 1);
	n->fname = t_strdup(o->fname);
	n->path = t_strdup(o->path);

	/* Delete old indexes. */
	fts_flatcurve_xapian_close(backend);
	if ((iter = fts_flatcurve_xapian_db_iter_init(backend, opts)) == NULL) {
		fts_flatcurve_xapian_delete(backend, n);
		e_error(backend->event, "Optimize failed mailbox=%s",
			str_c(backend->boxname));
		return;
	}
	while (fts_flatcurve_xapian_db_iter_next(iter)) {
		if (iter->type != FLATCURVE_XAPIAN_DB_TYPE_OPTIMIZE)
			fts_flatcurve_xapian_delete(backend, iter->path);
	}
	fts_flatcurve_xapian_db_iter_deinit(&iter);

	/* Rename optimize index to an active index. */
	if (fts_flatcurve_xapian_rename_db(backend, n) == NULL) {
		fts_flatcurve_xapian_delete(backend, n);
		e_error(backend->event, "Optimize failed mailbox=%s",
			str_c(backend->boxname));
		return;
	}

	i_gettimeofday(&now);
	diff = timeval_diff_msecs(&now, &start);

	e_debug(backend->event, "Optimized DB in %u.%03u secs; mailbox=%s",
		diff/1000, diff%1000, str_c(backend->boxname));
#endif
}

static bool
fts_flatcurve_build_query_arg(struct flatcurve_fts_query *query,
			      struct mail_search_arg *arg)
{
	struct flatcurve_fts_query_arg *a;
	Xapian::Database *db;
	string_t *hdr, *hdr2;
	enum flatcurve_xapian_db_opts opts;
	std::string t;
	struct flatcurve_fts_query_xapian *x = query->xapian;

	if (arg->no_fts)
		return TRUE;

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
		/* We should never get here - this is a search argument that
		 * we don't understand how to handle that has leaked to this
		 * point. For performance reasons, we will ignore this
		 * argument and err on the side of returning too many
		 * results (rather than falling back to slow, manual
		 * search). */
		array_pop_back(&x->args);
		return TRUE;
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
		if (((db = fts_flatcurve_xapian_read_db(query->backend, opts)) == NULL) ||
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
				/* Non-indexed headers only match if it
				 * appears in the general pool of header
				 * terms for the message, not to a specific
				 * header, so this is only a maybe match. */
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
			/* At the moment, build_query_arg() will never
			 * return FALSE - we will ignore unknown arguments.
			 * Keep the return value though, in case we want to
			 * change this in the future (at which point we
			 * need to uncomment the following two lines. */
			//fts_flatcurve_xapian_build_query_deinit(query);
			//return FALSE;
			i_unreached();
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
			str += " NOT ";
		if (a->is_not || (prev == NULL)) {
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
		e_error(query->backend->event,
			"Parsing query failed: %s (query: %s)",
			e.get_msg().c_str(), str.c_str());
		ret = FALSE;
	}

	fts_flatcurve_xapian_build_query_deinit(query);

	return ret;
}

struct fts_flatcurve_xapian_query_iter *
fts_flatcurve_xapian_query_iter_init(struct flatcurve_fts_query *query)
{
	struct fts_flatcurve_xapian_query_iter *iter;

	iter = p_new(query->pool, struct fts_flatcurve_xapian_query_iter, 1);
	iter->query = query;
	iter->result = p_new(query->pool,
			     struct fts_flatcurve_xapian_query_result, 1);
	iter->size = 0;

	return iter;
}

struct fts_flatcurve_xapian_query_result *
fts_flatcurve_xapian_query_iter_next(struct fts_flatcurve_xapian_query_iter *iter)
{
	Xapian::MSet m;
	enum flatcurve_xapian_db_opts opts;

	if (iter->size == 0) {
		if (iter->enquire == NULL) {
			if ((iter->query->xapian->query == NULL) ||
			    ((iter->db = fts_flatcurve_xapian_read_db(iter->query->backend, opts)) == NULL))
				return NULL;

			iter->enquire = new Xapian::Enquire(*iter->db);
			iter->enquire->set_docid_order(
					Xapian::Enquire::DONT_CARE);
			iter->enquire->set_query(*iter->query->xapian->query);
		}

		try {
			m = iter->enquire->get_mset(iter->offset,
						    FLATCURVE_MSET_RANGE);
		} catch (Xapian::DatabaseModifiedError &e) {
			/* Per documentation, this is only thrown if more than
			 * one change has been made to the database. To
			 * resolve you need to reopen the DB (Xapian can
			 * handle a single snapshot of a modified DB natively,
			 * so this only occurs if there have been multiple
			 * writes). However, we ALWAYS want to use the
			 * most up-to-date version, so we have already
			 * explicitly called reopen() above. Thus, we should
			 * never see this exception. */
			i_unreached();
		}

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
