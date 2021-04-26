FROM debian:buster

RUN apt-get update && apt-get upgrade -y && apt-get install -y \
	git \
	automake \
	libtool \
	wget \
	make \
	gettext \
	build-essential \
	bison \
	flex \
	libssl-dev \
	pkg-config \
	liblua5.3-dev \
	sqlite3 \
	libsqlite3-dev \
	libstemmer-dev \
	libexttextcat-dev \
	libicu-dev \
	zlib1g-dev \
	libxapian-dev \
	xapian-tools \
	valgrind

RUN mkdir /dovecot
RUN git clone https://github.com/dovecot/core.git /dovecot/core
WORKDIR /dovecot/core
RUN ./autogen.sh && \
	PANDOC=false ./configure --with-zlib --with-sqlite --with-lua \
	--with-stemmer --with-textcat --with-icu --enable-maintainer-mode \
	--enable-devel-checks && \
	make install

RUN git clone https://github.com/slusarz/dovecot-fts-flatcurve.git /dovecot/fts-flatcurve
WORKDIR /dovecot/fts-flatcurve
RUN ./autogen.sh && ./configure && make install

RUN git clone https://github.com/dovecot/imaptest.git /dovecot/imaptest
WORKDIR /dovecot/imaptest
RUN ./autogen.sh && ./configure && make install

RUN useradd dovenull && \
    useradd vmail && \
    useradd dovecot
COPY dovecot.conf /usr/local/etc/dovecot/dovecot.conf
COPY dovecot.sh /root/dovecot.sh
RUN mkdir -p /dovecot/sdbox && chown -R vmail:vmail /dovecot/sdbox

WORKDIR /
SHELL ["/bin/bash"]
ENTRYPOINT ["/root/dovecot.sh"]
