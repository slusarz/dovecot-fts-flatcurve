FTS Flatcurve plugin for Dovecot
================================

What?
-----

This is a Dovecot FTS plugin to enable message indexing using the Xapian
Open Source Search Engine Library (https://xapian.org/).

The plugin relies on Dovecot to do the necessary stemming. It is intended
to act as a simple interface to the Xapian storage/search query
functionality.

This driver supports phrase searching and match scoring.  This driver does
NOT support fuzzy searches.


Why Flatcurve?
--------------

This plugin was originally written during the 2020 Coronavirus pandemic.

Get it?


Requirements
------------

* Dovecot 2.x+ (tested on Dovecot CE 2.3.10)
  - Flatcurve relies on Dovecot's built-in FTS stemming library.
    - Requires stemmer support (--with-stemmer)
    - Optional libtextcat support (--with-textcat)
    - Optional icu support (--with-icu)
* Xapian 1.2.x+ (tested on Xapian 1.2.22, 1.4.11)
  - 1.4+ is required for optimization support


Compilation
-----------

If you downloaded this package using Git, you will first need to run
`autogen.sh` to generate the configure script and some other files:

```
./autogen.sh
```

The following compilation software/packages must be installed:

 - autoconf
 - automake
 - libtool
 - GNU make

After this script is executed successfully, `configure` needs to be executed
with the following parameters:

 - `--with-dovecot=<path>`

   Path to the dovecot-config file. This can either be a compiled dovecot
   source tree or point to the location where the dovecot-config file is
   installed on your system (typically in the $prefix/lib/dovecot directory).

When these paremeters are omitted, the configure script will try to find thex
local Dovecot installation implicitly.

For example, when compiling against compiled Dovecot sources:

```
./configure --with-dovecot=../dovecot-src
```

Or when compiling against a Dovecot installation:

```
./configure --with-dovecot=/path/to/dovecot
```

As usual, to compile and install, execute the following:

```
make
sudo make install
```

Configuration
-------------

See https://doc.dovecot.org/configuration_manual/fts/ for configuration
information regarding general FTS plugin options.  Note: flatcurve requires
the core Dovecot FTS stemming feature.

Flatcurve provies a single plugin option for configuration: `fts_flatcurve`.

Options for the `fts_flatcurve` plugin setting:

 - `auto_optimize` - The number of database revisions that occur before the
                     index is automatically optimized in the background
                     (integer, set to 0 to disable; DEFAULT: 500)
 - `commit_limit` - Commit database changes after this many documents are
                    updated
		    (integer, set to 0 to disable; DEFAULT: 100)
 - `no_position` - Do not generate positional data (greatly reduces xapian
		   storage, but does not allow phrase searching)
		   (integer [0 for no, 1 for yes]; DEFAULT: 0)

Example:

```
plugin {
  fts_flatcurve = auto_optimize=500 commit_limit=0 no_position=0
}
```


Logging/Events
--------------

This plugin emits events with the category `fts_flatcurve`.


TODOs
-----

- Escape " (and other characters?) when creating search query
- Improve NOT handling for multiple terms
