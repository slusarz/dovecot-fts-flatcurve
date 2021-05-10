# Dockerfile for fts-flatcurve development

To build image run:
```
docker build -t dovecot-fts-flatcurve .
```

To run gdb/get core dumps with docker:
```
docker run -it --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
	--rm --privileged -v /proc:/writable_proc \
	-v dovecot-fts-flatcurve-data:/dovecot/sdbox dovecot-fts-flatcurve
```

To access shell:
```
docker exec -it [container_name] /bin/bash
```

imaptest example command:
```
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
