# Dockerfile for fts-flatcurve development

Dockerfile lives in .github/actions/dovecot-fts-flatcurve-test directory.

"podman" is used for development; "docker" should also work.

To build image run:
```
podman build -t dovecot-fts-flatcurve .
```

To access shell:
```
podman exec -it [container_name] /bin/bash
```

-or- to run gdb/get core dumps with podman, run the ``podman_cores.sh``
script. in this directory


imaptest example command (run in container):
```
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
