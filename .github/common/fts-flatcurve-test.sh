#!/usr/bin/env bash

TESTUSER=user
TESTPASS=pass
TESTBOX=imaptest
DOVECOT_LOG=/var/log/dovecot.log
VALGRIND=0

if [ $VALGRIND -eq 1 ]; then
	DOVEADM_CMD="/usr/bin/valgrind --vgdb=no --num-callers=50 --keep-debuginfo=yes --leak-check=full --trace-children=yes"
else
	DOVEADM_CMD=""
fi

ulimit -c unlimited
rm -f $DOVECOT_LOG

function restart_dovecot() {
	doveadm stop &> /dev/null
	dovecot -c $1
}

function run_imaptest() {
	if ! imaptest user=$TESTUSER pass=$TESTPASS box=$TESTBOX test=$1 ; then
		echo "ERROR: Failed test ($1)!"
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

function run_doveadm() {
	if ! $DOVEADM_CMD doveadm -D $1 &>> $DOVECOT_LOG && [ -z "$2" ] ; then
		echo "ERROR: Failed test ($1)!"
		cat $DOVECOT_LOG
		exit 1
	fi
}

function run_doveadm_ignore_error() {
	run_doveadm "$1" 1
}

function run_not_exists_dir() {
	if [ -d "$1" ]; then
		echo "ERROR: Failed test ($1)!"
		cat $DOVECOT_LOG
		exit 1
	fi
}

function populate_mbox_msg() {
	run_doveadm_ignore_error "mailbox delete -u $TESTUSER ${1}"
	run_doveadm "mailbox create -u $TESTUSER ${1}"
	printf "$2" | run_doveadm "save -u $TESTUSER -m ${1}"
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
unset IMAPTEST_ISSUE_10_GERMAN

run_test "Testing GitHub Issue #11 (DB Rotation/Deletion)" \
	/dovecot/configs/dovecot.conf.issue-11 \
	/dovecot/imaptest/issue-11

run_test "Testing Xapian query keyword parsing" \
	/dovecot/configs/dovecot.conf.xapian-query-keywords \
	/dovecot/imaptest/xapian-query-keywords

run_test "Testing GitHub Issue #27 (BODY phrase search)" \
	/dovecot/configs/dovecot.conf.issue-27 \
	/dovecot/imaptest/issue-27/issue-27

run_test "Testing phrase searching" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/phrase_search/phrase_search

run_test "Testing GitHub Issue #35 (Email searching)" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/issue-35/issue-35

run_test "Testing GitHub Issue #44 (Xapian indexed string too long)" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/issue-44/issue-44

run_test "Testing GitHub Issue #54 (VOLATILEDIR locking)" \
	/dovecot/configs/dovecot.conf.issue-54 \
	/dovecot/imaptest/small_mailbox

TESTBOX=rotatetest
run_test "Testing DB Rotation/Deletion (populating mailbox)" \
	/dovecot/configs/dovecot.conf.issue-11 \
	/dovecot/imaptest/multiple-current-populate
sleep 5
for i in /dovecot/sdbox/user/sdbox/mailboxes/$TESTBOX/dbox-Mails/fts-flatcurve/current.*
do
	runuser -u vmail -- cp -r $i ${i}1
	runuser -u vmail -- cp -r $i ${i}2
done
run_test "Testing DB Rotation/Deletion (multiple current DBs)" \
	/dovecot/configs/dovecot.conf.issue-11 \
	/dovecot/imaptest/multiple-current-test
# DB is now in state where indexes have "disjoint ranges", so a Xapian
# optimize will fail; do manual optimization
run_doveadm "fts optimize -u $TESTUSER"
# If optimization was successful, we should see same results as before
run_test "Testing DB Rotation/Deletion (after optimization)" \
	/dovecot/configs/dovecot.conf.issue-11 \
	/dovecot/imaptest/multiple-current-test2
TESTBOX=imaptest

run_test "Testing optimize_limit" \
	/dovecot/configs/dovecot.conf.optimize_limit \
	/dovecot/imaptest/optimize_limit

run_test "Testing Concurrent Indexing" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/concurrent-index
run_doveadm "index -u $TESTUSER $TESTBOX"
# Need to let indexing complete before we run next test, or else
# indexer-worker won't be killed when we restart Dovecot
sleep 3

run_test "Testing large mailbox" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/large_mailbox

run_test "Testing small mailbox (and large expunge from previous test)" \
	/dovecot/configs/dovecot.conf \
	/dovecot/imaptest/small_mailbox

echo
echo "Testing rescan"
run_doveadm "fts rescan -u $TESTUSER"
echo "Success!"

echo
echo "Testing optimize"
run_doveadm "fts optimize -u $TESTUSER"
echo "Success!"

echo
echo "Testing 'doveadm fts-flatcurve check'"
run_doveadm "fts-flatcurve check -u $TESTUSER rotatetest"
# TODO: Scan for expected input
echo "Success!"

echo
echo "Testing 'doveadm fts-flatcurve dump'"
run_doveadm "fts-flatcurve dump -u $TESTUSER -h rotatetest"
# TODO: Scan for expected input
echo "Success!"

echo
echo "Testing 'doveadm fts-flatcurve rotate'"
run_doveadm "fts-flatcurve rotate -u $TESTUSER rotatetest"
# TODO: Scan for expected input
echo "Success!"

echo
echo "Testing 'doveadm fts-flatcurve stats'"
run_doveadm "fts-flatcurve stats -u $TESTUSER rotatetest"
# TODO: Scan for expected input
echo "Success!"

echo
echo "Testing 'doveadm fts-flatcurve stats -A'"
run_doveadm "fts-flatcurve stats -A rotatetest"
# TODO: Scan for expected input
echo "Success!"

run_test "Testing GitHub Issue #38" \
	/dovecot/configs/dovecot.conf.issue-38 \
	/dovecot/imaptest/issue-38/issue-38

TESTBOX=INBOX
echo
echo "Testing 'doveadm fts-flatcurve remove'"
run_doveadm "fts-flatcurve remove -u $TESTUSER $TESTBOX"
run_not_exists_dir /dovecot/sdbox/user/sdbox/mailboxes/$TESTBOX/dbox-Mails/fts-flatcurve/
echo "Success!"

for m in inbox rotatetest
do
	populate_mbox_msg ${m} "Subject: msg1\n\nbody1\n"
done
for m in imaptest
do
	populate_mbox_msg ${m} "Subject: msg1\n\nbody2\n"
done

run_test "Testing virtual search" \
	/dovecot/configs/dovecot.conf.virtual \
	/dovecot/imaptest/virtual

for u in user user2 user3
do
	TESTUSER=${u}
	for m in inbox test1 test2
	do
		populate_mbox_msg ${m} "Subject: msg1\n\nbody\n"
	done
done

echo
echo "Testing 'doveadm fts optimize -A'"
run_doveadm "fts optimize -A"
# TODO: Scan for expected input
echo "Success!"
