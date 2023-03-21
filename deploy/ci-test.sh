#!/usr/bin/env bash

SCRIPT_ROOT="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

function exec_tests() {
  local botdir="$1"
  local host="$2"
  local port="$3"

  ${botdir}/EOBot host=${host} port=${port} bots=6 script=${SCRIPT_ROOT}/../src/test/integration/create_accounts.eob
  ${botdir}/EOBot host=${host} port=${port} bots=6,2 script=${SCRIPT_ROOT}/../src/test/integration/change_passwords.eob -- "BotP@ssw0rd" "NewPassword"
  ${botdir}/EOBot host=${host} port=${port} bots=6,2 script=${SCRIPT_ROOT}/../src/test/integration/change_passwords.eob -- "NewPassword" "BotP@ssw0rd"
  ${botdir}/EOBot host=${host} port=${port} bots=6 script=${SCRIPT_ROOT}/../src/test/integration/login_queue_busy.eob
  ${botdir}/EOBot host=${host} port=${port} bots=6 script=${SCRIPT_ROOT}/../src/test/integration/create_delete_char.eob
}

function exec_selfcontained() {
  CONTAINER_NAME="self_contained_etheos_ci_test"

  local botdir="$1"
  local port="$2"
  local docker_setup="$3"
  local docker_teardown="$4"
  local docker_build_local="$5"
  local config_dir="$6"
  local data_dir="$7"
  local image_version="$8"
  local skip_pull="$9"

  if [[ "${docker_setup}" == "true" ]]; then
    echo "Setting up self-contained docker run..."

    if [[ "${docker_build_local}" == "false" ]]; then
      if [[ "${skip_pull}" == "false" ]]; then
        echo "Pulling image..."
        docker pull "darthchungis/etheos:$image_version"
      fi
    else
      echo "Building source (may take a while)..."
      pushd "$SCRIPT_ROOT/.." &> /dev/null
      ./build-linux.sh --mariadb ON --sqlite ON --sqlserver ON &> /dev/null

      echo "Building image..."
      image_version="local"
      docker build -t "darthchungis/etheos:$image_version" .
      popd &> /dev/null
    fi

    local existing=$(docker ps -af "name=$CONTAINER_NAME" -q)
    if [[ ! -z "$existing" ]]; then
      echo "Deleting existing local docker container..."
      docker stop "$existing" &> /dev/null
      docker rm -v "$existing" &> /dev/null
    fi

    docker run --name $CONTAINER_NAME -d \
-e ETHEOS_DBTYPE=sqlite -e ETHEOS_DBHOST=database.sdb -e "ETHEOS_INSTALLSQL=./install.sql" \
-e ETHEOS_MAXCONNECTIONSPERIP=0 -e ETHEOS_IPRECONNECTLIMIT=1s -e ETHEOS_MAXCONNECTIONSPERPC=0 \
-e ETHEOS_LOGINQUEUESIZE=4 -e ETHEOS_THREADPOOLTHREADS=4 -p "$port":"$port" \
-v "$config_dir":/etheos/config_local -v "$data_dir":/etheos/data \
"darthchungis/etheos:$image_version" &> /dev/null
  fi

  python3 $SCRIPT_ROOT/test-connection.py localhost 3 "${port}"

  exec_tests "${botdir}" localhost "${port}"

  if [[ "${docker_teardown}" == "true" ]]; then
    echo "Cleaning up local docker container..."
    docker stop $CONTAINER_NAME &> /dev/null
    docker rm -v $CONTAINER_NAME &> /dev/null
  fi
}

function main() {
  set -e

  local self_contained="false"
  local docker_setup="true"
  local docker_teardown="true"
  local docker_build_local="false"
  local config_dir="$SCRIPT_ROOT/../config_local"
  local data_dir="$SCRIPT_ROOT/../data"
  local image_version="latest"
  local skip_pull="false"

  local host=""
  local port=""

  local botdir=""
  local opt_help="false"

  local option
  while [[ "$#" -gt 0 ]]
  do
    option="$1"
    case "${option}" in
      -s|--self-contained)
        self_contained="true"
        ;;
      -l|--use-local)
        docker_build_local="true"
        ;;
      -c|--configdir)
        config_dir="$2"
        shift
        ;;
      -d|--datadir)
        data_dir="$2"
        shift
        ;;
      -v|--image-version)
        image_version="$2"
        shift
        ;;
      -P|--no-pull)
        skip_pull="true"
        ;;
      --no-setup)
        docker_setup="false"
        ;;
      --no-teardown)
        docker_teardown="false"
        ;;
      --host)
        host="$2"
        shift
        ;;
      --port)
        port="$2"
        shift
        ;;
      -b|--botdir)
        botdir="$2"
        shift
        ;;
      -h|--help)
        opt_help="true"
        ;;
      *)
        echo "Error: unsupported option \"${option}\""
        return 1
        ;;
    esac
    shift
  done

  if [[ "${opt_help}" == "true" ]]; then
    display_usage
    return 0
  fi

  if [[ -z "${botdir}" ]]; then
    echo "EOBot directory is required"
    display_usage
    return 1
  fi

  if [[ "${self_contained}" == "true" ]]; then
    if [[ -z "${port}" ]]; then
      port=8078
    fi

    exec_selfcontained "${botdir}" "${port}" "${docker_setup}" "${docker_teardown}" "${docker_build_local}" "${config_dir}" "${data_dir}" "${image_version}" "${skip_pull}"
  else
    if [[ -z "${host}" ]]; then
      echo "Host is required"
      display_usage
      return 1
    fi

    if [[ -z "${port}" ]]; then
      echo "Port is required"
      display_usage
      return 1
    fi

    exec_tests "${botdir}" "${host}" "${port}"
  fi

  return 0
}

function display_usage() {
  echo "Usage:"
  echo "  ./ci-test.sh -s -b eobot_path [-S -T -p 8078]"
  echo "-or-"
  echo "  ./ci-test.sh -h moffat.io -p 8078 -b eobot_path"
  echo ""
  echo "Options:"
  echo "  -s --self-contained    Run ci tests against auto-setup local docker environment"
  echo "  -l --use-local         Build and use local docker image instead of pulling latest"
  echo "  -c --configdir         Override the default local config_local directory with the specified path"
  echo "                         NOTE: paths must be absolute. Use \$(pwd)/.. for paths relative to the deploy directory"
  echo "  -d --datadir           Override the default local data directory with the speccified path"
  echo "                         NOTE: paths must be absolute. Use \$(pwd)/.. for paths relative to the deploy directory"
  echo "  -v --image-version     Use specified version of docker image (default: latest)"
  echo "  --no-setup             Do not set up docker container, assume already running."
  echo "  --no-teardown          Do not stop/remove docker container after run."
  echo "  --no-pull              Skip 'docker pull' of specified image version. Ignored when --use-local is specified."
  echo "================================================="
  echo "  --host                 Host to connect to"
  echo "  --port                 Port to connect to"
  echo "Global options:"
  echo "  -b --botdir            Root directory of EOBot"
  echo "  -h --help              Show this help"
}

main "$@"
