#!/usr/bin/env bash

TESTUSER=user
TESTPASS=pass
DOVECOT_LOG=/var/log/dovecot.log

ulimit -c unlimited

function restart_dovecot() {
	doveadm stop &> /dev/null
	rm -f $DOVECOT_LOG
	dovecot -c $1
}

function run_imaptest() {
	if ! imaptest user=$TESTUSER pass=$TESTPASS test=$1 ; then
		echo "ERROR: Failed test!"
		cat $DOVECOT_LOG
		exit 1
	fi
}

function run_test() {
	echo
	echo $1
	restart_dovecot $2
	run_imaptest $3
}

run_test "Testing RFC Compliant (substring) configuration" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/fts-test

export IMAPTEST_NO_SUBSTRING=1
run_test "Testing prefix-only configuration" \
	/dovecot/configs/dovecot.conf.no_substring \
	/dovecot/imaptest/fts-test
unset IMAPTEST_NO_SUBSTRING

run_test "Testing GitHub Issue #9" \
	/dovecot/configs/dovecot.conf.issue-9 \
	/dovecot/imaptest/issue-9
