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

  if [[ "${docker_setup}" == "true" ]]; then
    echo "Setting up self-contained docker run..."
    echo "Pulling image..."
    docker pull darthchungis/etheos &> /dev/null

    local existing=$(docker ps -af "name=$CONTAINER_NAME" -q)
    if [[ ! -z "$existing" ]]; then
      echo "Deleting existing local docker container..."
      docker stop "$existing" &> /dev/null
      docker rm -v "$existing" &> /dev/null
    fi

    docker run --name $CONTAINER_NAME -d \
-e ETHEOS_DBTYPE=sqlite -e ETHEOS_DBHOST=database.sdb -e "ETHEOS_INSTALLSQL=./install.sql" -p "$port":"$port" \
-v $SCRIPT_ROOT/../config_local:/etheos/config_local -v $SCRIPT_ROOT/../data:/etheos/data \
darthchungis/etheos &> /dev/null
  fi

  python3 test-connection.py localhost 5 "${port}"

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
      -S|--no-setup)
        docker_setup="false"
        ;;
      -T|--no-teardown)
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

    exec_selfcontained "${botdir}" "${port}" "${docker_setup}" "${docker_teardown}"
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
  echo "  -S --no-setup          Do not set up docker containers, assume already running"
  echo "  -T --no-teardown       Set up docker containers, but leave them running"
  echo "================================================="
  echo "  --host              Host to connect to"
  echo "  --port              Port to connect to"
  echo "Global options:"
  echo "  -b --botdir            Root directory of EOBot"
  echo "  -h --help              Show this help"
}

main "$@"
