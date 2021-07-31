#!/usr/bin/env bash
ulimit -c unlimited
dovecot
imaptest user=foo pass=pass test=/dovecot/imaptest/src/tests/
