#!/usr/bin/env /bin/bash
# Updates a currently running podman container with the contents of the
# current local dovecot-fts-flatcurve git repository.
# Usage: podman_deploy.sh <podman_container>

get_script_dir () {
     SOURCE="${BASH_SOURCE[0]}"
     while [ -h "$SOURCE" ]; do
          DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
          SOURCE="$( readlink "$SOURCE" )"
          [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
     done
     $( cd -P "$( dirname "$SOURCE" )" )
     pwd
}

podman exec $1 rm -rf /dovecot/fts-flatcurve
podman exec $1 rm -rf /dovecot/configs
podman exec $1 rm -rf /dovecot/imaptest
podman exec $1 rm -rf /dovecot/sdbox/user
podman cp "$(get_script_dir)/.." $1:/dovecot/fts-flatcurve
podman exec $1 /bin/bash -c " \
	cd /dovecot/fts-flatcurve ; \
	make distclean ; \
	./autogen.sh ; \
	./configure ; \
	make install "
podman exec $1 cp -a /dovecot/fts-flatcurve/.github/actions/dovecot-fts-flatcurve-test/configs/ /dovecot/configs
podman exec $1 chown -R vmail:vmail /dovecot/configs/virtual
podman exec $1 cp -a /dovecot/fts-flatcurve/.github/actions/dovecot-fts-flatcurve-test/imaptest/ /dovecot/imaptest
podman exec $1 cp /dovecot/fts-flatcurve/.github/actions/dovecot-fts-flatcurve-test/fts-flatcurve-test.sh /
