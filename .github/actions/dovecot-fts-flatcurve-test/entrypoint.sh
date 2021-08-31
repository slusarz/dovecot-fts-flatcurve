#!/usr/bin/env bash
ulimit -c unlimited

echo "Testing RFC Compliant (substring) configuration"
dovecot
if ! imaptest user=foo pass=pass test=/dovecot/imaptest/fts-test ; then
	echo "ERROR: Failed test!"
	exit 1
fi

echo
echo "Testing prefix-only configuration"
doveadm stop
dovecot -c /dovecot/dovecot.conf.no_substring
export IMAPTEST_NO_SUBSTRING=1
if ! imaptest user=foo pass=pass test=/dovecot/imaptest/fts-test ; then
	echo "ERROR: Failed test!"
	exit 1
fi
