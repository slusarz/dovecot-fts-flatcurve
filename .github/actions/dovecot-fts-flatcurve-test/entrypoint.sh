#!/usr/bin/env bash

TESTUSER=user
TESTPASS=pass
TESTBOX=imaptest
DOVECOT_LOG=/var/log/dovecot.log

ulimit -c unlimited

function restart_dovecot() {
	doveadm stop &> /dev/null
	rm -f $DOVECOT_LOG
	dovecot -c $1
}

function run_imaptest() {
	if ! imaptest user=$TESTUSER pass=$TESTPASS box=$TESTBOX test=$1 ; then
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

TESTBOX=inbox
run_test "Testing GitHub Issue #9 (1st pass)" \
	/dovecot/configs/dovecot.conf.issue-9 \
	/dovecot/imaptest/issue-9
run_test "Testing GitHub Issue #9 (2nd pass; crash)" \
	/dovecot/configs/dovecot.conf.issue-9 \
	/dovecot/imaptest/issue-9

TESTBOX=imaptest
run_test "Testing GitHub Issue #10 (English)" \
	/dovecot/configs/dovecot.conf.issue-10 \
	/dovecot/imaptest/issue-10/issue-10
export IMAPTEST_ISSUE_10_GERMAN=1
run_test "Testing GitHub Issue #10 (German; fails)" \
	/dovecot/configs/dovecot.conf.issue-10 \
	/dovecot/imaptest/issue-10/issue-10
unset IMAPTEST_NO_SUBSTRING

run_test "Testing GitHub Issue #11 (DB Rotation/Deletion)" \
        /dovecot/configs/dovecot.conf.issue-11 \
        /dovecot/imaptest/issue-11

TESTBOX=rotatetest
restart_dovecot /dovecot/configs/dovecot.conf.issue-11
doveadm mailbox delete -u $TESTUSER $TESTBOX &> /dev/null
doveadm mailbox create -u $TESTUSER $TESTBOX
echo "Subject: foo" | doveadm save -u $TESTUSER -m $TESTBOX
sleep 2
for i in /dovecot/sdbox/user/sdbox/mailboxes/$TESTBOX/dbox-Mails/fts-flatcurve/current.*
do
	runuser -u vmail -- cp -r $i ${i}1
done
echo "Subject: foo" | doveadm save -u $TESTUSER -m $TESTBOX
echo "Subject: foo" | doveadm save -u $TESTUSER -m $TESTBOX
sleep 5
run_test "Testing DB Rotation/Deletion (multiple current DBs)" \
	/dovecot/configs/dovecot.conf.issue-11 \
	/dovecot/imaptest/multiple-current-db
TESTBOX=imaptest

run_test "Testing optimize_limit" \
        /dovecot/configs/dovecot.conf.optimize_limit \
        /dovecot/imaptest/optimize_limit

run_test "Testing Concurrent Indexing" \
        /dovecot/configs/dovecot.conf \
        /dovecot/imaptest/concurrent-index
doveadm index -u $TESTUSER $TESTBOX
# Need to let indexing complete before we run next test, or else
# indexer-worker won't be killed when we restart Dovecot
sleep 3

run_test "Testing large mailbox" \
        /dovecot/configs/dovecot.conf \
        /dovecot/imaptest/large_mailbox

run_test "Testing small mailbox (and large expunge from previous test)" \
        /dovecot/configs/dovecot.conf \
        /dovecot/imaptest/small_mailbox
