FROM alpine:3.18 as base

RUN apk add --no-cache icu libstemmer libexttextcat tini \
	xapian-core openssl1.1-compat ca-certificates tzdata


FROM base as build

ARG DOVECOT_VERSION=2.3.21
ARG PIGEONHOLE_VERSION=0.5.21

# All these commands are run together to minimize size of container;
# we don't want any of the temporary compilation collateral to make
# it into one of the Docker image layers.
RUN apk add --no-cache -t dovecot_temp git autoconf automake libtool wget \
		make gettext-dev bison flex icu-dev rpcgen \
		openssl1.1-compat-dev gcc g++ bash libstemmer-dev \
		libexttextcat-dev xapian-core-dev

RUN mkdir /dovecot

RUN git clone --depth 1 --branch ${DOVECOT_VERSION} \
	https://github.com/dovecot/core.git /dovecot/core
WORKDIR /dovecot/core
RUN ./autogen.sh
RUN PANDOC=false ./configure --with-stemmer --with-textcat --disable-static \
	--with-icu --disable-dependency-tracking --prefix=/usr/local \
	--enable-hardening=no --sysconfdir=/etc --with-rundir=/run/dovecot
RUN make install-strip
RUN make distclean

RUN git clone --depth 1 --branch ${PIGEONHOLE_VERSION} \
	https://github.com/dovecot/pigeonhole.git /dovecot/pigeonhole
WORKDIR /dovecot/pigeonhole
RUN ./autogen.sh
RUN ./configure --with-dovecot=/usr/local/lib/dovecot
RUN make install-strip
RUN make distclean

RUN git clone --depth 1 https://github.com/slusarz/dovecot-fts-flatcurve.git \
	/dovecot/fts-flatcurve
WORKDIR /dovecot/fts-flatcurve
RUN ./autogen.sh
RUN ./configure --with-dovecot=/usr/local/lib/dovecot
RUN make install-strip
RUN make distclean


FROM base

LABEL org.opencontainers.image.authors="Michael Slusarz <slusarz@curecanti.org>"

COPY --from=build /usr/local/ /usr/local/

RUN addgroup -g 1000 vmail && \
	adduser -D -u 1000 -G vmail vmail -h /srv/mail && \
	addgroup -g 1001 dovecot && \
	adduser -DH -u 1001 -G dovecot dovecot && \
	addgroup -g 1002 dovenull && \
	adduser -DH -u 1002 -G dovenull dovenull

ADD docker/dovecot.conf /etc/dovecot/dovecot.conf

EXPOSE 24
EXPOSE 143

VOLUME ["/etc/dovecot", "/srv/mail"]
ENTRYPOINT ["/sbin/tini", "--"]
CMD ["/usr/local/sbin/dovecot", "-F"]
