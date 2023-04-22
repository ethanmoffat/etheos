FROM alpine:3.17.2

COPY scripts/install-deps.sh /tmp
COPY install/ /etheos
WORKDIR /etheos

# curl/gnupg :: required for package installs in install-deps
# bash :: required to run install-deps
# gcompat :: compatibility layer for glibc in Alpine Linux. See: https://wiki.alpinelinux.org/wiki/Running_glibc_programs
RUN rm -rf /etheos/config_local /etheos/test &&\
    apk add --update --no-cache curl gnupg bash gcompat &&\
    /tmp/install-deps.sh --skip-cmake --skip-json &&\
    rm /tmp/install-deps.sh &&\
    addgroup -g 8078 -S etheos && adduser -S -D -u 8078 -h /etheos etheos etheos &&\
    chown -R etheos:etheos /etheos

USER etheos:etheos

EXPOSE 8078

CMD [ "/etheos/etheos" ]
