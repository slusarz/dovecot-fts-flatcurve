/* Copyright (c) 2020 Michael Slusarz <slusarz@curecanti.org>
 * See the included COPYING file */

#ifndef FTS_BACKEND_FLATCURVE_XAPIAN_H
#define FTS_BACKEND_FLATCURVE_XAPIAN_H

struct flatcurve_xapian *fts_flatcurve_xapian_init();
void fts_flatcurve_xapian_deinit(struct flatcurve_xapian *xapian);
void fts_flatcurve_xapian_commit(struct flatcurve_fts_backend *backend);
void fts_flatcurve_xapian_close(struct flatcurve_fts_backend *backend);
bool fts_flatcurve_xapian_open_read(struct flatcurve_fts_backend *backend,
				    struct fts_flatcurve_user *fuser);
bool fts_flatcurve_xapian_open_write(struct flatcurve_fts_backend *backend,
				     struct fts_flatcurve_user *fuser);
bool fts_flatcurve_xapian_get_last_uid(struct flatcurve_fts_backend *backend,
				       struct fts_flatcurve_user *fuser,
				       uint32_t *last_uid_r);
void fts_flatcurve_xapian_expunge(struct flatcurve_fts_backend *backend,
				  struct fts_flatcurve_user *fuser,
				  uint32_t uid);
void
fts_flatcurve_xapian_index_header(struct flatcurve_fts_backend_update_context *ctx,
				  struct flatcurve_fts_backend *backend,
				  struct fts_flatcurve_user *fuser,
				  const unsigned char *data, size_t size);
void
fts_flatcurve_xapian_index_body(struct flatcurve_fts_backend_update_context *ctx,
				struct flatcurve_fts_backend *backend,
				struct fts_flatcurve_user *fuser,
				const unsigned char *data, size_t size);
void fts_flatcurve_xapian_optimize_box(struct flatcurve_fts_backend *backend,
				       const struct mailbox_info *info);
bool fts_flatcurve_xapian_build_query(struct flatcurve_fts_query *query,
				      struct fts_flatcurve_user *fuser);
bool fts_flatcurve_xapian_run_query(struct flatcurve_fts_backend *backend,
				    struct flatcurve_fts_query *query,
				    struct fts_flatcurve_user *fuser,
				    struct fts_result *r);
void fts_flatcurve_xapian_destroy_query(struct flatcurve_fts_query *query);

#endif
