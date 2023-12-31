#!/bin/sh

# Build Docker test build environment for dovecot-fts-flatcurve
# Needed to locally create the proper environment to run GitHub tests

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

BUILDDIR=/tmp/dovecot-fts-flatcurve-build

case "$1" in
	"alpine")
		echo "Building alpine test environment ...";;
	"debian")
		echo "Building debian test environment ...";;
	*)
		echo "Usage: $SCRIPT <image_name>"
		exit 1;;
esac

rm -rf $BUILDDIR
mkdir $BUILDDIR

echo "Copying .github/actions/flatcurve-test-$1 ..."
cp -a $SCRIPTPATH/../.github/actions/flatcurve-test-$1/* $BUILDDIR
echo "Copying .github/common ..."
cp -a $SCRIPTPATH/../.github/common/* $BUILDDIR
echo "Copying flatcurve source ..."
cp -a $SCRIPTPATH/../ $BUILDDIR/flatcurve

cd $BUILDDIR
echo "Test build directory at: $BUILDDIR"
