# Dockerfile for fts-flatcurve development

To build image run:

```
docker build -t dovecot-fts-flatcurve .
docker run --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
	-it dovecot-fts-flatcurve

# To access shell:
docker exec -it [container_name] /bin/bash
```

imaptest example command:
```
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
