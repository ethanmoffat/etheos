FROM ubuntu:20.04

COPY scripts/install-deps.sh /tmp
RUN mkdir /etheos
COPY install/ /etheos
RUN rm -rf /etheos/config_local /etheos/test
WORKDIR /etheos

RUN apt-get update && apt-get install -y curl gnupg2 &&\
    /tmp/install-deps.sh --skip-cmake --skip-json &&\
    apt-get clean && rm /tmp/install-deps.sh
EXPOSE 8078

CMD [ "./etheos" ]
