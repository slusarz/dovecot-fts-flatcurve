# Dockerfile for fts-flatcurve development

To build image run:

```
docker build -t dovecot-fts-flatcurve .
docker run -it dovecot-fts-flatcurve

# To run gdb/get core dumps with docker, this needs to be run instead:
docker run -it dovecot-fts-flatcurve \
	--cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
	--rm --privileged -v /proc:/writable_proc
# In the container (via shell):
ulimit -c unlimited
echo 2 >> /writable_proc/sys/fs/suid_dumpable
echo /tmp/core >> /writable_proc/sys/kernel/core_pattern
(restart Dovecot)

# To access shell:
docker exec -it [container_name] /bin/bash
```

imaptest example command:
```
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
