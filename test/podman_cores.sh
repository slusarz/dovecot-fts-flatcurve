#!/usr/bin/env

mkdir /tmp/cores
chmod 1777 /tmp/cores

echo 2 >> /proc/sys/fs/suid_dumpable
echo /tmp/cores/core.%u.%p >> /proc/sys/kernel/core_pattern

podman run --rm -it --cap-add=ALL -v /cores:/cores --ulimit core=-1 --entrypoint /bin/bash $1
