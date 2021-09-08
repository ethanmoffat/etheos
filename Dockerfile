FROM ubuntu:20.04

COPY scripts/install-deps.sh /tmp
RUN mkdir /eoserv
COPY install/*.sql /eoserv
COPY install/*.ini /eoserv
COPY install/LICENSE.txt /eoserv
COPY install/eoserv /eoserv/eoserv

COPY install/upgrade/ /eoserv/upgrade
COPY install/lang/ /eoserv/lang
COPY install/config/ /eoserv/config
WORKDIR /eoserv

RUN apt-get update && apt-get install -y curl gnupg2 &&\
    /tmp/install-deps.sh --skip-cmake --skip-json &&\
    apt-get clean && rm /tmp/install-deps.sh
EXPOSE 8078

CMD [ "./eoserv" ]
