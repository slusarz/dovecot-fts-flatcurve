# Dockerfile for fts-flatcurve development

Dockerfile lives in .github/ directory.

To build image run:
```
docker build -t dovecot-fts-flatcurve .
```

To run gdb/get core dumps with docker:
```
docker run -it --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
	--rm --privileged -v /proc:/writable_proc \
	-v dovecot-fts-flatcurve-data:/dovecot/sdbox dovecot-fts-flatcurve

# Run these commands in the container
ulimit -c unlimited
echo 2 >> /writable_proc/sys/fs/suid_dumpable
echo /tmp/core >> /writable_proc/sys/kernel/core_pattern
```

To access shell:
```
docker exec -it [container_name] /bin/bash
```

imaptest example command:
```
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
