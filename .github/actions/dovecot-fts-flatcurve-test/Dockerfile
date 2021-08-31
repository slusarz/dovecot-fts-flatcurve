FROM debian:buster-slim

ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get install -y \
        curl \
        gpg

RUN curl https://repo.dovecot.org/DOVECOT-REPO-GPG | gpg --import && \
        gpg --export ED409DA1 > /etc/apt/trusted.gpg.d/dovecot.gpg && \
        echo "deb https://repo.dovecot.org/ce-2.3-latest/debian/buster buster main" >> /etc/apt/sources.list.d/dovecot.list

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
	libstemmer-dev \
	libexttextcat-dev \
	libicu-dev \
	libxapian-dev \
	dovecot-imaptest

# We need to build Dovecot ourselves, since "standard" Dovecot does not
# come with necessary ICU libraries built-in
RUN mkdir /dovecot
RUN git clone --depth 1 https://github.com/dovecot/core.git /dovecot/core
WORKDIR /dovecot/core
RUN ./autogen.sh && \
	PANDOC=false ./configure --with-stemmer --with-textcat --with-icu && \
	make install

RUN git clone --depth 1 https://github.com/slusarz/dovecot-fts-flatcurve.git /dovecot/fts-flatcurve
WORKDIR /dovecot/fts-flatcurve
RUN ./autogen.sh && ./configure && make install

# Users dovecot and dovenull are created by dovecot-imaptest package
RUN useradd vmail && \
    mkdir -p /dovecot/sdbox && \
    chown -R vmail:vmail /dovecot/sdbox

ADD dovecot.conf /usr/local/etc/dovecot/dovecot.conf
ADD dovecot.conf.no_substring /dovecot
ADD fts-test /dovecot/fts-test
ADD fts-test.no_substring /dovecot/fts-test
ADD fts-test.mbox /dovecot/fts-test.mbox

ADD entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
