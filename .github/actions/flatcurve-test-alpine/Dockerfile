FROM alpine:3.17

ARG DOVECOT_VERSION=release-2.3
ARG IMAPTEST_VERSION=release-2.3

RUN apk add --no-cache \
	git \
	autoconf \
	automake \
	libtool \
	wget \
	make \
	gettext-dev \
	bison \
	flex \
	valgrind \
	icu-dev \
	openssl1.1-compat-dev \
	gcc \
	g++ \
	bash \
	libstemmer-dev \
	libexttextcat-dev \
	xapian-core-dev \
	runuser

# We need to build Dovecot ourselves, since "standard" Dovecot does not
# come with necessary ICU libraries built-in
RUN mkdir /dovecot
RUN git clone --depth 1 --branch ${DOVECOT_VERSION} \
	https://github.com/dovecot/core.git /dovecot/core
RUN cd /dovecot/core && \
	./autogen.sh && \
	PANDOC=false ./configure --with-stemmer --with-textcat --with-icu \
		--enable-hardening=no && \
	make install

ADD flatcurve/ /dovecot/flatcurve
RUN cd /dovecot/flatcurve && ./autogen.sh && ./configure && make install

RUN git clone --depth 1 --branch ${IMAPTEST_VERSION} \
	https://github.com/dovecot/imaptest.git \
	/dovecot/imaptest-src
RUN cd /dovecot/imaptest-src && ./autogen.sh && ./configure && make install

# vmail needs to come first, as it needs to be the lowest UID in order to
# have correct permissions to access mailbox directory
RUN addgroup vmail && \
	adduser -DH -G vmail vmail && \
	addgroup dovecot && \
	adduser -DH -G dovecot dovecot && \
	addgroup dovenull && \
	adduser -DH -G dovenull dovenull && \
	mkdir -p /dovecot/sdbox && \
	chown -R vmail:vmail /dovecot/sdbox

ADD configs/ /dovecot/configs
RUN chown -R vmail:vmail /dovecot/configs/virtual
ADD imaptest/ /dovecot/imaptest

ADD fts-flatcurve-test.sh /fts-flatcurve-test.sh
RUN chmod +x /fts-flatcurve-test.sh
ENTRYPOINT ["/fts-flatcurve-test.sh"]
