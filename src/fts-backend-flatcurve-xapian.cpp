/* Copyright (c) Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifdef XAPIAN_MOVE_SEMANTICS
#  define std_move(x) std::move(x)
#else
#  define std_move(x) x
#endif

#include <xapian.h>
#include <algorithm>
#include <sstream>
#include <string>
#include "fts-flatcurve-config.h"
extern "C" {
#include "lib.h"
#include "array.h"
#include "file-create-locked.h"
#include "hash.h"
#include "hex-binary.h"
#include "mail-storage-private.h"
#include "mail-search.h"
#include "md5.h"
#include "sleep.h"
#include "str.h"
#include "time-util.h"
#include "unichar.h"
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
 * to modify.
 *
 * Data storage: Xapian does not support substring searches by default, so
 * (if substring searching is enabled) we instead need to explicitly store all
 * substrings of the string, up to the point where the substring becomes
 * smaller than min_term_size. */
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
#define FLATCURVE_XAPIAN_BODY_QP        "body"

/* Version database, so that any schema changes can be caught. */
#define FLATCURVE_XAPIAN_DB_KEY_PREFIX "dovecot."
#define FLATCURVE_XAPIAN_DB_VERSION_KEY FLATCURVE_XAPIAN_DB_KEY_PREFIX \
	FTS_FLATCURVE_LABEL
#define FLATCURVE_XAPIAN_DB_VERSION 1

#define FLATCURVE_DBW_LOCK_RETRY_SECS 1
#define FLATCURVE_DBW_LOCK_RETRY_MAX 60
#define FLATCURVE_MANUAL_OPTIMIZE_COMMIT_LIMIT 500

/* Dotlock: needed to ensure we don't run into race conditions when
 * manipulating current directory. */
#define FLATCURVE_XAPIAN_LOCK_FNAME "flatcurve-lock"
#define FLATCURVE_XAPIAN_LOCK_TIMEOUT_SECS 5

#define ENUM_EMPTY(x) ((enum x) 0)


struct flatcurve_xapian_db_path {
	const char *fname;
	const char *path;
};

enum flatcurve_xapian_db_type {
	FLATCURVE_XAPIAN_DB_TYPE_INDEX,
	FLATCURVE_XAPIAN_DB_TYPE_CURRENT,
	FLATCURVE_XAPIAN_DB_TYPE_OPTIMIZE,
	FLATCURVE_XAPIAN_DB_TYPE_LOCK,
	FLATCURVE_XAPIAN_DB_TYPE_UNKNOWN
};

struct flatcurve_xapian_db {
	Xapian::Database *db;
	Xapian::WritableDatabase *dbw;
	struct flatcurve_xapian_db_path *dbpath;
	unsigned int changes;
	enum flatcurve_xapian_db_type type;
};
HASH_TABLE_DEFINE_TYPE(xapian_db, char *, struct flatcurve_xapian_db *);

struct flatcurve_xapian {
	/* Current database objects. */
	struct flatcurve_xapian_db *dbw_current;
	Xapian::Database *db_read;
	HASH_TABLE_TYPE(xapian_db) dbs;
	unsigned int shards;

	/* Locking for current shard manipulation. */
	struct file_lock *lock;
	const char *lock_path;

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

	bool deinit:1;
	bool closing:1;
};

struct flatcurve_fts_query_xapian_maybe {
	Xapian::Query *query;
};

struct flatcurve_fts_query_xapian {
	Xapian::Query *query;
	ARRAY(struct flatcurve_fts_query_xapian_maybe) maybe_queries;

	bool and_search:1;
	bool maybe:1;
	bool start:1;
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
	FLATCURVE_XAPIAN_WDB_CREATE = BIT(0)
};
enum flatcurve_xapian_db_close {
	FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT = BIT(0),
	FLATCURVE_XAPIAN_DB_CLOSE_WDB        = BIT(1),
	FLATCURVE_XAPIAN_DB_CLOSE_DB         = BIT(2),
	FLATCURVE_XAPIAN_DB_CLOSE_ROTATE     = BIT(3),
	FLATCURVE_XAPIAN_DB_CLOSE_MBOX       = BIT(4)
};

/* Externally accessible struct. */
struct fts_flatcurve_xapian_query_iter {
	struct flatcurve_fts_backend *backend;
	struct flatcurve_fts_query *query;
	Xapian::Database *db;
	Xapian::Enquire *enquire;
	Xapian::MSetIterator i;
	Xapian::MSet m;
	struct fts_flatcurve_xapian_query_result *result;

	bool init:1;
	bool main_query:1;
};

static void
fts_flatcurve_xapian_check_db_version(struct flatcurve_fts_backend *backend,
				      struct flatcurve_xapian_db *xdb);
static void
fts_flatcurve_xapian_close_db(struct flatcurve_fts_backend *backend,
			      struct flatcurve_xapian_db *xdb,
			      enum flatcurve_xapian_db_close opts);
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

	x->deinit = TRUE;
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
	x->deinit = FALSE;
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
	(void)fts_backend_flatcurve_delete_dir(
		backend,
		(dbpath == NULL) ? str_c(backend->db_path) : dbpath->path);
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
		e_debug(backend->event, "Cannot open DB (RO); opendir(%s) "
			"failed: %m", str_c(backend->db_path));
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
	} else if (str_begins(d->d_name, FLATCURVE_XAPIAN_LOCK_FNAME)) {
		iter->type = FLATCURVE_XAPIAN_DB_TYPE_LOCK;
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
	unsigned int wait = 0;

	while (xdb->dbw == NULL) {
		try {
			xdb->dbw = new Xapian::WritableDatabase(
					xdb->dbpath->path, db_flags);
		} catch (Xapian::DatabaseLockError &e) {
			e_debug(backend->event, "Waiting for DB (RW; %s) lock",
				xdb->dbpath->fname);
			wait += FLATCURVE_DBW_LOCK_RETRY_SECS;
			if (wait > FLATCURVE_DBW_LOCK_RETRY_MAX)
				i_fatal(FTS_FLATCURVE_DEBUG_PREFIX "Could not "
					"obtain DB lock (RW; %s)",
					xdb->dbpath->fname);
			i_sleep_intr_secs(FLATCURVE_DBW_LOCK_RETRY_SECS);
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
		e_debug(backend->event, "Cannot open DB (RW; %s); %s",
			xdb->dbpath->fname, e.get_description().c_str());
		return NULL;
	}

	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		fts_flatcurve_xapian_check_db_version(backend, xdb);

	e_debug(backend->event, "Opened DB (RW; %s) messages=%u version=%u",
		xdb->dbpath->fname, xdb->dbw->get_doccount(),
		FLATCURVE_XAPIAN_DB_VERSION);

	return xdb;
}

static struct flatcurve_xapian_db_path *
fts_flatcurve_xapian_rename_db(struct flatcurve_fts_backend *backend,
			       struct flatcurve_xapian_db_path *path)
{
	unsigned int i;
	struct flatcurve_xapian_db_path *newpath;
	bool retry = FALSE;

	for (i = 0; i < 3; ++i) {
		std::ostringstream ss;
		std::string new_fname(FLATCURVE_XAPIAN_DB_PREFIX);
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

static bool
fts_flatcurve_xapian_need_optimize(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;

	return ((backend->fuser->set.optimize_limit > 0) &&
		(x->shards >= backend->fuser->set.optimize_limit));
}

static void
fts_flatcurve_xapian_optimize_mailbox(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;

	if (x->deinit || !fts_flatcurve_xapian_need_optimize(backend))
		return;

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
			    enum flatcurve_xapian_db_type type,
			    bool open_wdb)
{
	struct flatcurve_xapian_db_path *newpath;
	struct flatcurve_xapian_db *o, *xdb;
	struct flatcurve_xapian *x = backend->xapian;
	enum flatcurve_xapian_wdb wopts = FLATCURVE_XAPIAN_WDB_CREATE;

	if ((type != FLATCURVE_XAPIAN_DB_TYPE_INDEX) &&
	    (type != FLATCURVE_XAPIAN_DB_TYPE_CURRENT))
		return NULL;

	xdb = p_new(x->pool, struct flatcurve_xapian_db, 1);
	xdb->dbpath = dbpath;
	xdb->type = type;

	if (open_wdb &&
	    (fts_flatcurve_xapian_write_db_get(backend, xdb, wopts) == NULL))
		return NULL;

	hash_table_insert(x->dbs, dbpath->fname, xdb);

	/* If multiple current DBs exist, rename the oldest. */
	if ((type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT) &&
	    (x->dbw_current != NULL)) {
		o = (strcmp(dbpath->fname, x->dbw_current->dbpath->fname) > 0)
			? x->dbw_current : xdb;
		newpath = fts_flatcurve_xapian_rename_db(backend, o->dbpath);
		fts_flatcurve_xapian_close_db(backend, o,
					      FLATCURVE_XAPIAN_DB_CLOSE_WDB);
		hash_table_remove(x->dbs, o->dbpath->fname);
		hash_table_insert(x->dbs, newpath->fname, o);

		o->dbpath = newpath;
		o->type = FLATCURVE_XAPIAN_DB_TYPE_INDEX;
	}

	if (xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT)
		x->dbw_current = xdb;

	return xdb;
}

static int fts_flatcurve_xapian_lock(struct flatcurve_fts_backend *backend)
{
	struct file_create_settings set;
	struct flatcurve_xapian *x = backend->xapian;

	i_zero(&set);
	set.lock_timeout_secs = FLATCURVE_XAPIAN_LOCK_TIMEOUT_SECS;
	set.lock_settings.close_on_free = TRUE;
	set.lock_settings.unlink_on_free = TRUE;
	set.lock_settings.lock_method = backend->parsed_lock_method;

	if (x->lock_path == NULL) {
		if (str_len(backend->volatile_dir) > 0) {
			unsigned char db_path_hash[MD5_RESULTLEN];
			md5_get_digest(str_c(backend->db_path), str_len(backend->db_path),
				db_path_hash);
			x->lock_path = p_strdup_printf(
				x->pool, "%s/" FLATCURVE_XAPIAN_LOCK_FNAME ".%s",
				str_c(backend->volatile_dir),
				binary_to_hex(db_path_hash, sizeof(db_path_hash)));
			set.mkdir_mode = 0700;
		} else {
			x->lock_path = p_strdup_printf(
				x->pool, "%s" FLATCURVE_XAPIAN_LOCK_FNAME,
				str_c(backend->db_path));
		}
	}

	bool created;
	const char *error;
	int ret = file_create_locked(x->lock_path, &set, &x->lock, &created, &error);
	if (ret < 0)
		e_error(backend->event, "file_create_locked(%s) failed: %m",
			x->lock_path);

	return ret;
}

static void fts_flatcurve_xapian_unlock(struct flatcurve_fts_backend *backend)
{
	file_lock_free(&backend->xapian->lock);
}

static bool
fts_flatcurve_xapian_db_read_add(struct flatcurve_fts_backend *backend,
				 struct flatcurve_xapian_db *xdb)
{
	struct flatcurve_xapian *x = backend->xapian;

	if (x->db_read == NULL)
		return TRUE;

	try {
		xdb->db = new Xapian::Database(xdb->dbpath->path);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "Cannot open DB (RO; %s); %s",
			xdb->dbpath->fname, e.get_description().c_str());
		return FALSE;
	}

	fts_flatcurve_xapian_check_db_version(backend, xdb);
	++x->shards;
	x->db_read->add_database(*(xdb->db));

	fts_flatcurve_xapian_optimize_mailbox(backend);

	return TRUE;
}

static bool
fts_flatcurve_xapian_create_current(struct flatcurve_fts_backend *backend,
				    enum flatcurve_xapian_db_close copts)
{
	std::ostringstream s;
	struct flatcurve_xapian_db *xdb;

	/* The current shard has filename of the format PREFIX.timestamp. This
	 * ensures that we will catch any current DB renaming done by another
	 * process (reopen() on the DB will fail, causing the entire DB to be
	 * closed/reopened). */
	s << FLATCURVE_XAPIAN_DB_CURRENT_PREFIX << i_microseconds();
	xdb = fts_flatcurve_xapian_db_add(
		backend,
		fts_flatcurve_xapian_create_db_path(backend, s.str().c_str()),
		FLATCURVE_XAPIAN_DB_TYPE_CURRENT, TRUE);
	if (xdb == NULL || !fts_flatcurve_xapian_db_read_add(backend, xdb))
		return FALSE;

	if (copts)
		fts_flatcurve_xapian_close_db(backend, xdb, copts);

	return TRUE;
}

static bool
fts_flatcurve_xapian_db_populate(struct flatcurve_fts_backend *backend,
				 enum flatcurve_xapian_db_opts opts)
{
	bool dbs_exist, lock, no_create, ret;
	struct flatcurve_xapian_db_iter *iter;
	struct flatcurve_xapian *x = backend->xapian;

	dbs_exist = (hash_table_count(backend->xapian->dbs) > 0);
	no_create = HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT);

	if (dbs_exist && (no_create || (x->dbw_current != NULL)))
		return TRUE;

	if (no_create) {
		struct stat st;
		if (stat(str_c(backend->db_path), &st) == 0)
			lock = S_ISDIR(st.st_mode);
		else if (errno == ENOENT)
			lock = FALSE;
		else {
			e_error(backend->event, "stat(%s) failed: %m",
				str_c(backend->db_path));
		}
	} else {
		if (mailbox_list_mkdir_root(backend->backend.ns->list, str_c(backend->db_path), MAILBOX_LIST_PATH_TYPE_INDEX) < 0) {
			e_error(backend->event, "Cannot create DB (RW); %s",
				str_c(backend->db_path));
			return FALSE;
		}
		lock = TRUE;
	}

	if (lock && (fts_flatcurve_xapian_lock(backend) < 0))
		return FALSE;

	if (!dbs_exist) {
		if ((iter = fts_flatcurve_xapian_db_iter_init(backend, opts)) == NULL) {
			fts_flatcurve_xapian_unlock(backend);
			return FALSE;
		}

		while (fts_flatcurve_xapian_db_iter_next(iter)) {
			(void)fts_flatcurve_xapian_db_add(backend, iter->path,
							  iter->type, FALSE);
		}
		fts_flatcurve_xapian_db_iter_deinit(&iter);
	}

	ret = (!no_create && (x->dbw_current == NULL))
		? fts_flatcurve_xapian_create_current(backend, (enum flatcurve_xapian_db_close)(HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_NOCLOSE_CURRENT) ? 0 : FLATCURVE_XAPIAN_DB_CLOSE_WDB))
		: TRUE;
	fts_flatcurve_xapian_unlock(backend);

	return ret;
}

static struct flatcurve_xapian_db *
fts_flatcurve_xapian_write_db_current(struct flatcurve_fts_backend *backend,
				      enum flatcurve_xapian_db_opts opts)
{
	enum flatcurve_xapian_wdb wopts = ENUM_EMPTY(flatcurve_xapian_wdb);
	struct flatcurve_xapian *x = backend->xapian;

	if ((x->dbw_current != NULL) && (x->dbw_current->dbw != NULL))
		return x->dbw_current;

	opts = (enum flatcurve_xapian_db_opts)
		(opts | FLATCURVE_XAPIAN_DB_NOCLOSE_CURRENT);
	/* dbw_current can be NULL if FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT
	 * is set in opts. */
	if (!fts_flatcurve_xapian_db_populate(backend, opts) ||
	    (x->dbw_current == NULL))
		return NULL;

	return fts_flatcurve_xapian_write_db_get(backend, x->dbw_current,
						 wopts);
}

static Xapian::Database *
fts_flatcurve_xapian_read_db(struct flatcurve_fts_backend *backend,
			     enum flatcurve_xapian_db_opts opts)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	struct fts_flatcurve_xapian_db_stats stats;
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
		if (!fts_flatcurve_xapian_db_read_add(backend, xdb)) {
			/* If we can't open a DB, delete it. */
			fts_flatcurve_xapian_delete(backend, xdb->dbpath);
			hash_table_remove(x->dbs, key);
		}
	}
	hash_table_iterate_deinit(&iter);

	fts_flatcurve_xapian_mailbox_stats(backend, &stats);

	e_debug(backend->event, "Opened DB (RO) messages=%u version=%u "
		"shards=%u", stats.messages, stats.version, stats.shards);

	return x->db_read;
}

void
fts_flatcurve_xapian_mailbox_check(struct flatcurve_fts_backend *backend,
				   struct fts_flatcurve_xapian_db_check *check)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		 (FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		  FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);
	struct flatcurve_xapian *x = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	i_zero(check);

	if (fts_flatcurve_xapian_read_db(backend, opts) == NULL)
		return;

	iter = hash_table_iterate_init(x->dbs);
	while (hash_table_iterate(iter, x->dbs, &key, &val)) {
		xdb = (struct flatcurve_xapian_db *)val;
		try {
			check->errors += Xapian::Database::check(
						std::string(xdb->dbpath->path),
						Xapian::DBCHECK_FIX, NULL);
		} catch (const Xapian::Error &e) {
			e_debug(backend->event, "Check failed; %s",
				e.get_description().c_str());
		}
		++check->shards;
	}
	hash_table_iterate_deinit(&iter);
}

bool fts_flatcurve_xapian_mailbox_rotate(struct flatcurve_fts_backend *backend)
{
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		 (FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		  FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);
	struct flatcurve_xapian_db *xdb;

	if ((xdb = fts_flatcurve_xapian_write_db_current(backend, opts)) == NULL)
		return FALSE;

	fts_flatcurve_xapian_close_db(backend, xdb,
				      FLATCURVE_XAPIAN_DB_CLOSE_ROTATE);

	return TRUE;
}

void
fts_flatcurve_xapian_mailbox_stats(struct flatcurve_fts_backend *backend,
				   struct fts_flatcurve_xapian_db_stats *stats)
{
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		 (FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		  FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);
	struct flatcurve_xapian *x = backend->xapian;

	if ((x->db_read == NULL) &&
	    (fts_flatcurve_xapian_read_db(backend, opts) == NULL)) {
		i_zero(stats);
	} else {
		stats->messages = x->db_read->get_doccount();
		stats->shards = x->shards;
		stats->version = FLATCURVE_XAPIAN_DB_VERSION;
	}
}

static void
fts_flatcurve_xapian_mailbox_terms_do(struct flatcurve_fts_backend *backend,
				      HASH_TABLE_TYPE(term_counter) terms,
				      const char *prefix)
{
	unsigned int counter;
	Xapian::Database *db;
	const char *key, *pkey;
	void *k, *v;
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		 (FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		  FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);
	Xapian::TermIterator t, tend;

	if ((db = fts_flatcurve_xapian_read_db(backend, opts)) == NULL)
		return;

	for (t = db->allterms_begin(prefix), tend = db->allterms_end(prefix); t != tend; ++t) {
		const std::string &term = *t;
		key = term.data();

		if (strlen(prefix) > 0) {
			if (strncmp(key, FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX, 1) != 0)
				continue;
			++key;
		} else if (strncmp(key, FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX, 1) == 0) {
			++key;
		} else if ((strncmp(key, FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX, 1) == 0) ||
			   (strncmp(key, FLATCURVE_XAPIAN_HEADER_PREFIX, 1) == 0)) {
			continue;
		}

		if (hash_table_lookup_full(terms, key, &k, &v)) {
			counter = POINTER_CAST_TO(v, unsigned int);
			pkey = (const char *)k;
		} else {
			counter = 0;
			/* Using backend memory pool, since expectation is
			 * that this will only be called from doveadm. */
			pkey = p_strdup(backend->pool, key);
		}
		counter += t.get_termfreq();
		hash_table_update(terms, pkey, POINTER_CAST(counter));
	}
}

void
fts_flatcurve_xapian_mailbox_terms(struct flatcurve_fts_backend *backend,
				   HASH_TABLE_TYPE(term_counter) terms)
{
	fts_flatcurve_xapian_mailbox_terms_do(backend, terms, "");
}

void
fts_flatcurve_xapian_mailbox_headers(struct flatcurve_fts_backend *backend,
				     HASH_TABLE_TYPE(term_counter) hdrs)
{
	fts_flatcurve_xapian_mailbox_terms_do(
		backend, hdrs, FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX);
}

void fts_flatcurve_xapian_set_mailbox(struct flatcurve_fts_backend *backend)
{
	event_set_append_log_prefix(backend->event, p_strdup_printf(
		backend->xapian->pool, FTS_FLATCURVE_LABEL "(%s): ",
		str_c(backend->boxname)));
}

static void
fts_flatcurve_xapian_check_db_version(struct flatcurve_fts_backend *backend,
				      struct flatcurve_xapian_db *xdb)
{
	Xapian::Database *db = (xdb->dbw == NULL) ? xdb->db : xdb->dbw;
	std::ostringstream ss;
	int v;
	std::string ver;
	enum flatcurve_xapian_wdb wopts = ENUM_EMPTY(flatcurve_xapian_wdb);

	ver = db->get_metadata(FLATCURVE_XAPIAN_DB_VERSION_KEY);
	v = ver.empty() ? 0 : std::atoi(ver.c_str());

	if (v == FLATCURVE_XAPIAN_DB_VERSION)
		return;

	/* If we need to upgrade DB, and this is NOT the write DB, open the
	 * write DB, do the changes there, and reopen the read DB. */
	if (!xdb->dbw) {
		(void)fts_flatcurve_xapian_write_db_get(backend, xdb, wopts);
		fts_flatcurve_xapian_close_db(backend, xdb,
					      FLATCURVE_XAPIAN_DB_CLOSE_WDB);
		(void)xdb->db->reopen();
		return;
        }

	/* 0->1: Added DB version. Always implicity update version when we
	 * upgrade (done at end of this function). */
	if (v == 0)
		++v;

	ss << v;
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
	enum flatcurve_xapian_db_opts opts =
		ENUM_EMPTY(flatcurve_xapian_db_opts);
	enum flatcurve_xapian_wdb wopts = ENUM_EMPTY(flatcurve_xapian_wdb);
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
	    (xdb->dbw->get_doccount() >= fuser->set.rotate_size)) {
		fts_flatcurve_xapian_close_db(
			backend, xdb, FLATCURVE_XAPIAN_DB_CLOSE_ROTATE);
	} else if ((fuser->set.commit_limit > 0) &&
		   (x->doc_updates >= fuser->set.commit_limit)) {
		fts_flatcurve_xapian_close_dbs(
			backend, FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT);
		e_debug(backend->event, "Committing DB as update "
			"limit was reached; limit=%d", fuser->set.commit_limit);
	}
}

static void
fts_flatcurve_xapian_clear_document(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;
	enum flatcurve_xapian_db_opts opts =
		ENUM_EMPTY(flatcurve_xapian_db_opts);
	struct flatcurve_xapian_db *xdb;

	if ((x->doc == NULL) ||
	    ((xdb = fts_flatcurve_xapian_write_db_current(backend, opts)) == NULL))
		return;

	try {
		xdb->dbw->replace_document(x->doc_uid, *x->doc);
	} catch (std::bad_alloc &b) {
		i_fatal(FTS_FLATCURVE_DEBUG_PREFIX "Out of memory "
			"when indexing mail (%s); UID=%d "
			"(Hint: increase indexing process vsz_limit or define "
			"smaller commit_limit value in plugin config)",
			b.what(), x->doc_uid);
	} catch (Xapian::Error &e) {
		e_warning(backend->event, "Could not write message data: "
			  "uid=%u; %s", x->doc_uid,
			  e.get_description().c_str());
	}

	if (x->doc_created)
		delete(x->doc);
	x->doc = NULL;
	x->doc_created = FALSE;
	x->doc_uid = 0;

	fts_flatcurve_xapian_check_commit_limit(backend, xdb);
}

static void
fts_flatcurve_xapian_close_db(struct flatcurve_fts_backend *backend,
			      struct flatcurve_xapian_db *xdb,
			      enum flatcurve_xapian_db_close opts)
{
	bool commit = FALSE, rotate = FALSE;
	unsigned int diff;
	const char *fname;
	struct timeval now, start;
	struct flatcurve_xapian *x = backend->xapian;

	fts_flatcurve_xapian_clear_document(backend);

	if (xdb->dbw != NULL) {
		i_gettimeofday(&start);

		if (HAS_ANY_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_WDB | FLATCURVE_XAPIAN_DB_CLOSE_MBOX)) {
			xdb->dbw->close();
			delete(xdb->dbw);
			xdb->dbw = NULL;
			commit = TRUE;
		} else if (HAS_ANY_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_WDB_COMMIT | FLATCURVE_XAPIAN_DB_CLOSE_ROTATE)) {
			xdb->dbw->commit();
			commit = TRUE;
		}
	}

	if (commit) {
		x->doc_updates = 0;

		i_gettimeofday(&now);
		diff = (unsigned int) timeval_diff_msecs(&now, &start);

		if (xdb->changes > 0)
			e_debug(backend->event, "Committed %u changes to DB "
				"(RW; %s) in %u.%03u secs", xdb->changes,
				xdb->dbpath->fname, diff/1000, diff%1000);

		xdb->changes = 0;

		if ((xdb->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT) &&
		    (HAS_ALL_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_ROTATE) ||
		     (backend->fuser->set.rotate_time > 0) &&
		     (diff > backend->fuser->set.rotate_time)))
			rotate = TRUE;
	}

	if (rotate && (fts_flatcurve_xapian_lock(backend) >= 0)) {
		fname = p_strdup(x->pool, xdb->dbpath->fname);

		if (!fts_flatcurve_xapian_create_current(backend, (enum flatcurve_xapian_db_close)(x->closing ? FLATCURVE_XAPIAN_DB_CLOSE_MBOX : 0)))
			e_debug(backend->event, "Error when rotating DB (%s)",
				xdb->dbpath->fname);
		else
			e_debug(event_create_passthrough(backend->event)->
				set_name("fts_flatcurve_rotate")->
				add_str("mailbox", str_c(backend->boxname))->
				event(),
				"Rotating index (from: %s, to: %s)", fname,
				xdb->dbpath->fname);

		fts_flatcurve_xapian_unlock(backend);
	}

	if ((xdb->db != NULL) &&
	    HAS_ANY_BITS(opts, FLATCURVE_XAPIAN_DB_CLOSE_DB | FLATCURVE_XAPIAN_DB_CLOSE_MBOX)) {
		delete(xdb->db);
		xdb->db = NULL;
	}
}

static void
fts_flatcurve_xapian_close_dbs(struct flatcurve_fts_backend *backend,
			       enum flatcurve_xapian_db_close opts)
{
	struct hash_iterate_context *iter;
	void *key, *val;
	struct flatcurve_xapian *x = backend->xapian;

	iter = hash_table_iterate_init(x->dbs);
	while (hash_table_iterate(iter, x->dbs, &key, &val)) {
		fts_flatcurve_xapian_close_db(
			backend, (struct flatcurve_xapian_db *)val, opts);
	}
	hash_table_iterate_deinit(&iter);
}

void fts_flatcurve_xapian_refresh(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close_dbs(backend, FLATCURVE_XAPIAN_DB_CLOSE_WDB);
}

void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend)
{
	struct flatcurve_xapian *x = backend->xapian;

	x->closing = TRUE;
	fts_flatcurve_xapian_close_dbs(backend, FLATCURVE_XAPIAN_DB_CLOSE_MBOX);
	x->closing = FALSE;

	hash_table_clear(x->dbs, TRUE);

	x->lock_path = NULL;
	x->dbw_current = NULL;
	x->shards = 0;

	if (x->db_read != NULL) {
		x->db_read->close();
		delete(x->db_read);
		x->db_read = NULL;
	}

	p_clear(x->pool);
}

static uint32_t
fts_flatcurve_xapian_get_last_uid_query(struct flatcurve_fts_backend *backend ATTR_UNUSED,
					Xapian::Database *db)
{
	Xapian::Enquire enquire(*db);
	Xapian::MSet m;

	enquire.set_docid_order(Xapian::Enquire::DESCENDING);
	enquire.set_query(Xapian::Query::MatchAll);

	m = enquire.get_mset(0, 1);
	return (m.empty())
		? 0 : m.begin().get_document().get_docid();
}

void fts_flatcurve_xapian_get_last_uid(struct flatcurve_fts_backend *backend,
				       uint32_t *last_uid_r)
{
	Xapian::Database *db;
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		(FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		 FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);

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
		e_debug(backend->event, "Expunge failed uid=%u; UID not found",
			uid);
		return;
	}

	try {
		xdb->dbw->delete_document(uid);
		fts_flatcurve_xapian_check_commit_limit(backend, xdb);
	} catch (Xapian::Error &e) {
		e_debug(backend->event, "update_expunge (%s)",
			e.get_description().c_str());
	}
}

bool
fts_flatcurve_xapian_init_msg(struct flatcurve_fts_backend_update_context *ctx)
{
	enum flatcurve_xapian_db_opts opts =
		ENUM_EMPTY(flatcurve_xapian_db_opts);
	struct flatcurve_xapian *x = ctx->backend->xapian;
	struct flatcurve_xapian_db *xdb;

	if (ctx->uid == x->doc_uid) {
		return TRUE;
	}

	fts_flatcurve_xapian_clear_document(ctx->backend);

	if ((xdb = fts_flatcurve_xapian_write_db_current(ctx->backend, opts)) == NULL)
		return FALSE;

	try {
		(void)xdb->dbw->get_document(ctx->uid);
		return FALSE;
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
	const char *p = (const char *)data;
	struct flatcurve_xapian *x = ctx->backend->xapian;

	if (!fts_flatcurve_xapian_init_msg(ctx))
		return;

	if (str_len(ctx->hdr_name)) {
		h = str_lcase(str_c_modifiable(ctx->hdr_name));
		x->doc->add_boolean_term(
			FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX + h);
	}

	if (ctx->indexed_hdr)
		h = str_ucase(str_c_modifiable(ctx->hdr_name));

	do {
		std::string t (p, size);

		/* Capital ASCII letters at the beginning of a Xapian term are
		 * treated as a "term prefix". Check for a leading ASCII
		 * capital, and lowercase if necessary, to ensure the term
		 * is not confused with a "term prefix". */
		if (i_isupper(t[0]))
			t[0] = i_tolower(t[0]);

		if (ctx->indexed_hdr) {
			x->doc->add_term(
				FLATCURVE_XAPIAN_HEADER_PREFIX + h + t);
		}
		x->doc->add_term(FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX + t);

		unsigned int csize = uni_utf8_char_bytes(*p);
		p += csize;
		size -= csize;
	} while (fuser->set.substring_search &&
		(size >= 0) &&
		(uni_utf8_strlen_n(p, size) >= fuser->set.min_term_size));
}

void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				const unsigned char *data, size_t size)
{
	struct fts_flatcurve_user *fuser = ctx->backend->fuser;
	const char *p = (const char *)data;
	struct flatcurve_xapian *x = ctx->backend->xapian;

	if (!fts_flatcurve_xapian_init_msg(ctx))
		return;

	do {
		std::string t (p, size);

		/* Capital ASCII letters at the beginning of a Xapian term are
		 * treated as a "term prefix". Check for a leading ASCII
		 * capital, and lowercase if necessary, to ensure the term
		 * is not confused with a "term prefix". */
		if (i_isupper(t[0]))
			t[0] = i_tolower(t[0]);

		x->doc->add_term(t);

		unsigned int csize = uni_utf8_char_bytes(*p);
		p += csize;
		size -= csize;
	} while (fuser->set.substring_search &&
		(size >= 0) &&
		(uni_utf8_strlen_n(p, size) >= fuser->set.min_term_size));
}

void fts_flatcurve_xapian_delete_index(struct flatcurve_fts_backend *backend)
{
	fts_flatcurve_xapian_close(backend);
	fts_flatcurve_xapian_delete(backend, NULL);
}

#ifdef XAPIAN_HAS_COMPACT
static bool
fts_flatcurve_xapian_optimize_rebuild(struct flatcurve_fts_backend *backend,
				      Xapian::Database *db,
				      struct flatcurve_xapian_db_path *path)
{
	Xapian::Document doc;
	Xapian::Enquire enquire(*db);
	Xapian::MSetIterator i;
	Xapian::MSet m;
	unsigned int updates = 0;
	struct flatcurve_xapian *x = backend->xapian;
	struct flatcurve_xapian_db *xdb;

	/* Create the optimize shard. */
	xdb = p_new(x->pool, struct flatcurve_xapian_db, 1);
	xdb->dbpath = path;
	xdb->type = FLATCURVE_XAPIAN_DB_TYPE_OPTIMIZE;

	if (fts_flatcurve_xapian_write_db_get(backend, xdb, FLATCURVE_XAPIAN_WDB_CREATE) == NULL)
		return FALSE;

	enquire.set_docid_order(Xapian::Enquire::ASCENDING);
	enquire.set_query(Xapian::Query::MatchAll);
	m = enquire.get_mset(0, db->get_doccount());
	i = m.begin();

	while (i != m.end()) {
		doc = i.get_document();
	        try {
	                xdb->dbw->replace_document(doc.get_docid(), doc);
	        } catch (Xapian::Error &e) {
			return FALSE;
		}
		if (++updates > FLATCURVE_MANUAL_OPTIMIZE_COMMIT_LIMIT) {
			xdb->dbw->commit();
			updates = 0;
		}
		++i;
	}

	fts_flatcurve_xapian_close_db(backend, xdb,
				      FLATCURVE_XAPIAN_DB_CLOSE_WDB);

	return TRUE;
}

// Return value = whether to output debug error message; not success
static bool
fts_flatcurve_xapian_optimize_box_do(struct flatcurve_fts_backend *backend,
				     Xapian::Database *db)
{
	unsigned int diff;
	struct hash_iterate_context *hiter;
	struct flatcurve_xapian_db_iter *iter;
	void *key, *val;
	struct flatcurve_xapian_db_path *n, *o;
	struct timeval now, start;
	enum flatcurve_xapian_db_opts opts =
		ENUM_EMPTY(flatcurve_xapian_db_opts);
	struct flatcurve_xapian *x = backend->xapian;
	enum flatcurve_xapian_wdb wopts = ENUM_EMPTY(flatcurve_xapian_wdb);

	/* We need to lock all of the mailboxes so nothing changes while we
	 * are optimizing. */
	hiter = hash_table_iterate_init(x->dbs);
	while (hash_table_iterate(hiter, x->dbs, &key, &val)) {
		(void)fts_flatcurve_xapian_write_db_get(
			backend, (struct flatcurve_xapian_db *)val, wopts);
	}
	hash_table_iterate_deinit(&hiter);

	/* Create the optimize target. */
	o = fts_flatcurve_xapian_create_db_path(backend,
						FLATCURVE_XAPIAN_DB_OPTIMIZE);
	fts_flatcurve_xapian_delete(backend, o);
	i_gettimeofday(&start);

	try {
		try {
			(void)db->reopen();
			db->compact(o->path, Xapian::DBCOMPACT_NO_RENUMBER |
					     Xapian::DBCOMPACT_MULTIPASS |
					     Xapian::Compactor::FULLER);
		} catch (Xapian::InvalidOperationError &e) {
			/* This exception is not as specific as it could be...
			 * but the likely reason it happens is due to
			 * Xapian::DBCOMPACT_NO_RENUMBER and shards having
			 * disjoint ranges of UIDs (e.g. shard 1 = 1..2, shard
			 * 2 = 2..3). Xapian, as of 1.4.18, cannot handle this
			 * situation. Since we will never be able to compact
			 * this data unless we do something about it, the
			 * options are either 1) delete the index totally and
			 * start fresh (not great for large mailboxes), or to
			 * incrementally build the optimized DB by walking
			 * through all DBs and copying, ignoring duplicate
			 * documents. Let's try to be awesome and do the
			 * latter. */
			e_debug(backend->event, "Native optimize failed, "
				"fallback to manual optimization; %s",
				e.get_description().c_str());
			if (!fts_flatcurve_xapian_optimize_rebuild(backend, db, o))
				throw;
		}
	} catch (Xapian::Error &e) {
		e_error(backend->event, "Optimize failed; %s",
			e.get_description().c_str());
		return TRUE;
	}

	n = p_new(x->pool, struct flatcurve_xapian_db_path, 1);
	n->fname = p_strdup(x->pool, o->fname);
	n->path = p_strdup(x->pool, o->path);

	/* Delete old indexes. */
	if ((iter = fts_flatcurve_xapian_db_iter_init(backend, opts)) == NULL)
		return FALSE;
	while (fts_flatcurve_xapian_db_iter_next(iter)) {
		if ((iter->type == FLATCURVE_XAPIAN_DB_TYPE_INDEX) ||
		    (iter->type == FLATCURVE_XAPIAN_DB_TYPE_CURRENT))
			fts_flatcurve_xapian_delete(backend, iter->path);
	}
	fts_flatcurve_xapian_db_iter_deinit(&iter);

	/* Rename optimize index to an active index. */
	if (fts_flatcurve_xapian_rename_db(backend, n) == NULL) {
		fts_flatcurve_xapian_delete(backend, o);
		return FALSE;
	}

	i_gettimeofday(&now);
	diff = (unsigned int) timeval_diff_msecs(&now, &start);

	e_debug(backend->event, "Optimized DB in %u.%03u secs", diff/1000,
		diff%1000);

	return TRUE;
}
#endif

void fts_flatcurve_xapian_optimize_box(struct flatcurve_fts_backend *backend)
{
#ifdef XAPIAN_HAS_COMPACT
	Xapian::Database *db;
	enum flatcurve_xapian_db_opts opts =
		(enum flatcurve_xapian_db_opts)
		(FLATCURVE_XAPIAN_DB_NOCREATE_CURRENT |
		 FLATCURVE_XAPIAN_DB_IGNORE_EMPTY);

	if ((db = fts_flatcurve_xapian_read_db(backend, opts)) == NULL)
		return;

	if (backend->xapian->deinit &&
	    !fts_flatcurve_xapian_need_optimize(backend)) {
		fts_flatcurve_xapian_close(backend);
		return;
	}

	e_debug(event_create_passthrough(backend->event)->
		set_name("fts_flatcurve_optimize")->
		add_str("mailbox", str_c(backend->boxname))->event(),
		"Optimizing");

	if ((fts_flatcurve_xapian_lock(backend) >= 0) &&
	    (!fts_flatcurve_xapian_optimize_box_do(backend, db)))
		e_error(backend->event, "Optimize failed");

	fts_flatcurve_xapian_close(backend);
	fts_flatcurve_xapian_unlock(backend);
#endif
}

static void
fts_flatcurve_build_query_arg_term(struct flatcurve_fts_query *query,
				   struct mail_search_arg *arg,
				   const char *term)
{
	const char *hdr;
	bool maybe_or = FALSE;
	struct flatcurve_fts_query_xapian_maybe *mquery;
	Xapian::Query::op op = Xapian::Query::OP_INVALID;
	Xapian::Query *oldq, q;
	struct flatcurve_fts_query_xapian *x = query->xapian;

	if (x->start) {
		if (x->and_search) {
			op = Xapian::Query::OP_AND;
			str_append(query->qtext, " AND ");
		} else {
			op = Xapian::Query::OP_OR;
			str_append(query->qtext, " OR ");
		}
	}
	x->start = TRUE;

	if (arg->match_not)
		str_append(query->qtext, "NOT ");

	switch (arg->type) {
	case SEARCH_TEXT:
		q = Xapian::Query(Xapian::Query::OP_OR,
			Xapian::Query(Xapian::Query::OP_WILDCARD,
				t_strdup_printf("%s%s",
					FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX,
					term)),
			Xapian::Query(Xapian::Query::OP_WILDCARD, term));
		str_printfa(query->qtext, "(%s:%s* OR %s:%s*)",
			    FLATCURVE_XAPIAN_ALL_HEADERS_QP, term,
			    FLATCURVE_XAPIAN_BODY_QP, term);
		break;

	case SEARCH_BODY:
		q = Xapian::Query(Xapian::Query::OP_WILDCARD, term);
		str_printfa(query->qtext, "%s:%s*",
			    FLATCURVE_XAPIAN_BODY_QP, term);
		break;

	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		if (strlen(term)) {
			if (fts_header_want_indexed(arg->hdr_field_name)) {
				q = Xapian::Query(
					Xapian::Query::OP_WILDCARD,
					t_strdup_printf("%s%s%s",
						FLATCURVE_XAPIAN_HEADER_PREFIX,
						t_str_ucase(arg->hdr_field_name),
						term));
				str_printfa(query->qtext, "%s%s:%s*",
					    FLATCURVE_XAPIAN_HEADER_QP,
					    t_str_lcase(arg->hdr_field_name),
					    term);
			} else {
				q = Xapian::Query(
					Xapian::Query::OP_WILDCARD,
					t_strdup_printf("%s%s",
						FLATCURVE_XAPIAN_ALL_HEADERS_PREFIX,
						term));
				str_printfa(query->qtext, "%s:%s*",
					    FLATCURVE_XAPIAN_ALL_HEADERS_QP,
					    term);
				/* Non-indexed headers only match if it
				 * appears in the general pool of header
				 * terms for the message, not to a specific
				 * header, so this is only a maybe match. */
				if (x->and_search)
					x->maybe = TRUE;
				else
					maybe_or = TRUE;
			}
		} else {
			hdr = t_str_lcase(arg->hdr_field_name);
			q = Xapian::Query(t_strdup_printf("%s%s",
				FLATCURVE_XAPIAN_BOOLEAN_FIELD_PREFIX, hdr));
			str_printfa(query->qtext, "%s:%s",
				    FLATCURVE_XAPIAN_HEADER_BOOL_QP, hdr);
		}
		break;
	}

	if (arg->match_not)
		q = Xapian::Query(Xapian::Query::OP_AND_NOT,
				  Xapian::Query::MatchAll, q);

	if (maybe_or) {
		/* Maybe searches are not added to the "master search" query if this
		 * is an OR search; they will be run independently. Matches will be
		 * placed in the maybe results array. */
		if (!array_is_created(&x->maybe_queries))
			p_array_init(&x->maybe_queries, query->pool, 4);
		mquery = array_append_space(&x->maybe_queries);
		mquery->query = new Xapian::Query(std_move(q));
	} else if (x->query == NULL) {
		x->query = new Xapian::Query(std_move(q));
	} else {
		oldq = x->query;
		x->query = new Xapian::Query(op, *(x->query), q);
		delete(oldq);
	}
}

static void
fts_flatcurve_build_query_arg(struct flatcurve_fts_query *query,
			      struct mail_search_arg *arg)
{
	const char *term;

	if (arg->no_fts)
		return;

	switch (arg->type) {
	case SEARCH_TEXT:
	case SEARCH_BODY:
	case SEARCH_HEADER:
	case SEARCH_HEADER_ADDRESS:
	case SEARCH_HEADER_COMPRESS_LWSP:
		/* Valid search term. Set match_always, as required by FTS
		 * API, to avoid this argument being looked up later via
		 * regular search code. */
		arg->match_always = TRUE;
		break;

	case SEARCH_MAILBOX:
		/* doveadm will pass this through in 'doveadm search'
		 * commands with a 'mailbox' search argument. The code has
		 * already handled setting the proper mailbox by this point
		 * so just ignore this. */
		return;

	case SEARCH_OR:
	case SEARCH_SUB:
		/* FTS API says to ignore these. */
		return;

	default:
		/* We should never get here - this is a search argument that
		 * we don't understand how to handle that has leaked to this
		 * point. For performance reasons, we will ignore this
		 * argument and err on the side of returning too many
		 * results (rather than falling back to slow, manual
		 * search). */
		return;
	}

	if (strlen(arg->value.str)) {
		/* Prepare search term. Phrase searching is not supported
		 * natively (FTS core provides index terms without positional
		 * context) so we can only do single term searching with
		 * Xapian. Therefore, if we do see a multi-term search, ignore
		 * (since, as of v2.3.19, FTS core will send both the
		 * phrase search and individual search terms separately as
		 * part of the same query. */
		if (strchr(arg->value.str, ' ') != NULL)
			return;

		term = arg->value.str;
	} else {
		/* This is an existence search. */
		term = "";
	}

	fts_flatcurve_build_query_arg_term(query, arg, term);
}

void fts_flatcurve_xapian_build_query(struct flatcurve_fts_query *query)
{
	struct mail_search_arg *args = query->args;
	struct flatcurve_fts_query_xapian *x;

	x = query->xapian = p_new(query->pool,
				  struct flatcurve_fts_query_xapian, 1);

	if (query->match_all) {
		str_append(query->qtext, "[Match All]");
		x->query = new Xapian::Query(Xapian::Query::MatchAll);
		return;
	}

	x->and_search = ((query->flags & FTS_LOOKUP_FLAG_AND_ARGS) != 0);

	for (; args != NULL ; args = args->next) {
		fts_flatcurve_build_query_arg(query, args);
	}
}

struct fts_flatcurve_xapian_query_iter *
fts_flatcurve_xapian_query_iter_init(struct flatcurve_fts_query *query)
{
	struct fts_flatcurve_xapian_query_iter *iter;

	iter = p_new(query->pool, struct fts_flatcurve_xapian_query_iter, 1);
	iter->init = FALSE;
	iter->main_query = TRUE;
	iter->query = query;
	iter->result = p_new(query->pool,
			     struct fts_flatcurve_xapian_query_result, 1);

	return iter;
}

struct fts_flatcurve_xapian_query_result *
fts_flatcurve_xapian_query_iter_next(struct fts_flatcurve_xapian_query_iter *iter)
{
	Xapian::Query maybe, *q = NULL;
	const struct flatcurve_fts_query_xapian_maybe *mquery;
	enum flatcurve_xapian_db_opts opts =
		ENUM_EMPTY(flatcurve_xapian_db_opts);

	if (!iter->init) {
		iter->init = TRUE;

		/* Master query. */
		if (iter->main_query) {
			if (iter->query->xapian->query == NULL)
				iter->main_query = FALSE;
			else
				q = iter->query->xapian->query;
		}

		/* Maybe queries. */
		if (!iter->main_query &&
			array_is_created(&iter->query->xapian->maybe_queries)) {
			maybe = Xapian::Query();
			array_foreach(&iter->query->xapian->maybe_queries, mquery)
				maybe = Xapian::Query(Xapian::Query::OP_OR, maybe, *mquery->query);
			/* Add main query to merge Xapian scores correctly. */
			if (iter->query->xapian->query != NULL)
				maybe = Xapian::Query(Xapian::Query::OP_AND_MAYBE, maybe,
					*iter->query->xapian->query);
			q = &maybe;
		}

		if ((q != NULL) && (iter->db == NULL))
		    iter->db = fts_flatcurve_xapian_read_db(iter->query->backend, opts);

		if ((q == NULL) || (iter->db == NULL))
			return NULL;

		if (iter->enquire == NULL) {
			iter->enquire = new Xapian::Enquire(*iter->db);
			iter->enquire->set_docid_order(Xapian::Enquire::DONT_CARE);
		}
		iter->enquire->set_query(*q);

		try {
			iter->m = iter->enquire->get_mset(0, iter->db->get_doccount());
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

		iter->i = iter->m.begin();
	}

	if (iter->i == iter->m.end()) {
		if (!iter->main_query)
			return NULL;
		iter->main_query = iter->init = FALSE;
		return fts_flatcurve_xapian_query_iter_next(iter);
	}

	iter->result->maybe = !iter->main_query;
	iter->result->score = iter->i.get_weight();
	/* MSet docid can be an "interleaved" docid generated by
	 * Xapian::Database when handling multiple DBs at once. Instead, we
	 * want the "unique docid", which is obtained by looking at the
	 * doc id from the Document object itself. */
	iter->result->uid = iter->i.get_document().get_docid();
	++iter->i;

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
		bool add_score = TRUE;
		if (result->maybe || query->xapian->maybe) {
			add_score = (!seq_range_exists(&r->uids, result->uid) &&
				!seq_range_exists(&r->maybe_uids, result->uid));
			seq_range_array_add(&r->maybe_uids, result->uid);
		} else
			seq_range_array_add(&r->uids, result->uid);
		if (add_score) {
			score = array_append_space(&r->scores);
			score->score = (float)result->score;
			score->uid = result->uid;
		}
	}
	fts_flatcurve_xapian_query_iter_deinit(&iter);
	return TRUE;
}

void fts_flatcurve_xapian_destroy_query(struct flatcurve_fts_query *query)
{
	struct flatcurve_fts_query_xapian_maybe *mquery;

	delete(query->xapian->query);
	if (array_is_created(&query->xapian->maybe_queries)) {
		array_foreach_modifiable(&query->xapian->maybe_queries, mquery) {
			delete(mquery->query);
		}
		array_free(&query->xapian->maybe_queries);
	}
}

const char *fts_flatcurve_xapian_library_version()
{
	return Xapian::version_string();
}
