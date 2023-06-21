---
layout: doc
---

# Testing

::: info Note

[CI testing](https://github.com/slusarz/dovecot-fts-flatcurve/actions/workflows/testing.yml) is done after every push to the GitHub repository.

:::

## Manual Testing

[podman](https://podman.io/) is used for development; [docker](https://docker.com/) should also work.

### Dockerfile Locations

The testing Dockerfile(s) lives in `.github/actions/` directory.

There are two Dockerfiles used for testing:

| Name                    | Description                                        |
| ----------------------- | -------------------------------------------------- |
| `flatcurve-test-alpine` | [Alpine Linux](https://alpinelinux.org/) Container |
| `flatcurve-test-ubuntu` | [Ubuntu](https://ubuntu.com/) Container            |

The containers run a script located at `/fts-flatcurve-test.sh` which performs all the testing.

### Testing Commands

To build image, run (in one of the `actions` directories from above):

```sh
podman build -t dovecot-fts-flatcurve .
```

To run tests:

```sh
podman run dovecot-fts-flatcurve
```

To enter interactive shell without running the tests:

```sh
podman run -it --rm dovecot-fts-flatcurve /bin/bash
```

#### Imaptest

Imaptest is installed in the container to run automated testing.

imaptest example command (run in container):

```sh
imaptest user=foo pass=pass rawlog test=/dovecot/imaptest/src/tests/
```
