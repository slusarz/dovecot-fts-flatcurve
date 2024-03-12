---
layout: doc
---

# Compilation

If you downloaded this package using Git, you will first need to run
`autogen.sh` to generate the configure script and some other files:

```sh
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
  installed on your system (typically in the `$prefix/lib/dovecot` directory).

When these parameters are omitted, the configure script will try to find the
local Dovecot installation implicitly.

For example, when compiling against compiled Dovecot sources:

```sh
./configure --with-dovecot=../dovecot-src
```

Or when compiling against a Dovecot installation:

```sh
./configure --with-dovecot=/path/to/dovecot
```

To compile and install, execute the following:

```sh
make
sudo make install
```

## Examples

### Ubuntu 20.04/22.04

See: https://github.com/slusarz/dovecot-fts-flatcurve/issues/60#issuecomment-1987879138
