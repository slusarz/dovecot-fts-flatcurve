#!/usr/bin/env /bin/sh
# Run as root!
# Usage: podman_cores.sh <podman_user> <podman_container>

mkdir -p /tmp/cores
chmod 1777 /tmp/cores

echo 2 >> /proc/sys/fs/suid_dumpable
echo /tmp/cores/core.%u.%p >> /proc/sys/kernel/core_pattern

runuser -u $1 -- podman run --rm -it --cap-add=ALL -v /tmp/cores:/cores --ulimit core=-1 --entrypoint /bin/bash $2
