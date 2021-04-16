#!/usr/bin/env bash
doveadm stop
ulimit -c unlimited
echo 2 >> /writable_proc/sys/fs/suid_dumpable
echo /tmp/core >> /writable_proc/sys/kernel/core_pattern
dovecot
tail -f /var/log/dovecot.log
