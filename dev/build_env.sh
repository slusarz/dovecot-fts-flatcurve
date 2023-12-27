#!/bin/sh

# Build Docker test build environment for dovecot-fts-flatcurve
# Needed to locally create the proper environment to run GitHub tests

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

case "$1" in
	"alpine")
		echo "Building alpine test environment ...";;
	"ubuntu")
		echo "Building ubuntu test environment ...";;
	*)
		echo "Usage: $SCRIPT <image_name>"
		exit 1;;
esac

rm -rf /tmp/dovecot-fts-flatcurve-build
mkdir /tmp/dovecot-fts-flatcurve-build

echo "Copying .github/actions/flatcurve-test-$1 ..."
cp -a $SCRIPTPATH/../.github/actions/flatcurve-test-$1/* /tmp/dovecot-fts-flatcurve-build
echo "Copying .github/common ..."
cp -a $SCRIPTPATH/../.github/common/* /tmp/dovecot-fts-flatcurve-build
echo "Copying flatcurve source ..."
cp -a $SCRIPTPATH/../ /tmp/dovecot-fts-flatcurve-build/flatcurve

echo "Test build directory at: /tmp/dovecot-fts-flatcurve-build"
