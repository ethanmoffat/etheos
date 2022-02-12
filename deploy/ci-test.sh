#!/usr/bin/env bash

function main() {
  set -e

  local SCRIPT_ROOT="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

  local host=""
  local port=""
  local botdir=""
  local opt_help="false"

  local option
  while [[ "$#" -gt 0 ]]
  do
    option="$1"
    case "${option}" in
      -h|--host)
        host="$2"
        shift
        ;;
      -p|--port)
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

  if [[ -z "${botdir}" ]]; then
    echo "EOBot directory is required"
    display_usage
    return 1
  fi

  ${botdir}/EOBot host=${host} port=${port} bots=10 script=${SCRIPT_ROOT}/../src/test/integration/create_accounts.eob
  ${botdir}/EOBot host=${host} port=${port} bots=10 script=${SCRIPT_ROOT}/../src/test/integration/change_passwords.eob -- "BotP@ssw0rd" "NewPassword"
  ${botdir}/EOBot host=${host} port=${port} bots=10 script=${SCRIPT_ROOT}/../src/test/integration/change_passwords.eob -- "NewPassword" "BotP@ssw0rd"
  ${botdir}/EOBot host=${host} port=${port} bots=10 script=${SCRIPT_ROOT}/../src/test/integration/login_queue_busy.eob

  return 0
}

function display_usage() {
  echo "Usage:"
  echo "  ci-test.sh -h host -p port"
  echo ""
  echo "Options:"
  echo "  -h --host              Host to connect to"
  echo "  -p --port              Port to connect to"
  echo "  -b --botdir            Root directory of EOBot"
  echo "  -h --help              Show this help"
}

main "$@"
