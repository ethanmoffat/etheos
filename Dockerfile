FROM alpine:3.17.2 AS build

WORKDIR /build
COPY . .

RUN apk add --update --no-cache git bash &&\
    ./scripts/install-deps.sh &&\
    ./build-linux.sh -c --sqlite ON --mariadb ON --sqlserver ON

FROM alpine:3.17.2

WORKDIR /etheos

COPY scripts/install-deps.sh /tmp
COPY --from=build /build/install/ .

# curl/gnupg :: required for package installs in install-deps
# bash :: required to run install-deps
# gcompat :: compatibility layer for glibc in Alpine Linux. See: https://wiki.alpinelinux.org/wiki/Running_glibc_programs
RUN rm -rf config_local test &&\
    apk add --update --no-cache curl gnupg bash gcompat &&\
    /tmp/install-deps.sh --skip-cmake --skip-json &&\
    rm /tmp/install-deps.sh &&\
    addgroup -g 8078 -S etheos && adduser -S -D -u 8078 -h /etheos etheos etheos &&\
    chown -R etheos:etheos /etheos

USER etheos:etheos

VOLUME /etheos/config_local /etheos/data

EXPOSE 8078

CMD [ "./etheos" ]
