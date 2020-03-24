AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_STORAGE_INCLUDE) \
	$(LIBDOVECOT_FTS_INCLUDE) \
	$(XAPIAN_INCLUDE)

AM_CXXFLAGS = \
	$(XAPIAN_LIBS)

lib21_fts_flatcurve_plugin_la_LDFLAGS = -module -avoid-version

module_LTLIBRARIES = \
	lib21_fts_flatcurve_plugin.la

lib21_fts_flatcurve_plugin_la_LIBADD = \
	$(XAPIAN_LIBS)

lib21_fts_flatcurve_plugin_la_SOURCES = \
	fts-flatcurve-plugin.c \
	fts-backend-flatcurve.c \
	fts-backend-flatcurve-xapian.cpp

noinst_HEADERS = \
	fts-flatcurve-plugin.h \
	fts-backend-flatcurve.h \
	fts-backend-flatcurve-xapian.h
