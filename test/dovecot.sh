#!/usr/bin/env bash
ulimit -c unlimited
dovecot
tail -f /var/log/dovecot.log
