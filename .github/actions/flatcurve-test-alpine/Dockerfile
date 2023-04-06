FROM alpine:3.17

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
RUN git clone --depth 1 --branch release-2.3 \
	https://github.com/dovecot/core.git /dovecot/core
RUN cd /dovecot/core && \
	./autogen.sh && \
	PANDOC=false ./configure --with-stemmer --with-textcat --with-icu \
		--enable-hardening=no && \
	make install

RUN git clone --depth 1 https://github.com/slusarz/dovecot-fts-flatcurve.git \
	/dovecot/fts-flatcurve
RUN cd /dovecot/fts-flatcurve && ./autogen.sh && ./configure && make install

RUN git clone --depth 1 --branch release-2.3 \
	https://github.com/dovecot/imaptest.git \
	/dovecot/imaptest-src
RUN cd /dovecot/imaptest-src && ./autogen.sh && ./configure && make install

RUN addgroup dovecot && \
	adduser -DH -G dovecot dovecot && \
	addgroup dovenull && \
	adduser -DH -G dovenull dovenull && \
	addgroup vmail && \
	adduser -DH -G vmail vmail && \
	mkdir -p /dovecot/sdbox && \
	chown -R vmail:vmail /dovecot/sdbox

ADD configs/ /dovecot/configs
RUN chown -R vmail:vmail /dovecot/configs/virtual
ADD imaptest/ /dovecot/imaptest

ADD fts-flatcurve-test.sh /fts-flatcurve-test.sh
RUN chmod +x /fts-flatcurve-test.sh
ENTRYPOINT ["/fts-flatcurve-test.sh"]
