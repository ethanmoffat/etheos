#!/usr/bin/env bash

function main() {
  set -e

  local SCRIPT_ROOT="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

  local service_principal=""
  local service_principal_password=""
  local environment_name=""
  local resource_group="etheos"
  local template_file="${SCRIPT_ROOT}/etheos-container.json"
  local parameter_file=""
  local opt_help="false"

  local option
  while [[ "$#" -gt 0 ]]
  do
    option="$1"
    case "${option}" in
      -u|--user)
        service_principal="$2"
        shift
        ;;
      -p|--password)
        service_principal_password="$2"
        shift
        ;;
      -e|--environment-name)
        environment_name="$2"
        shift
        ;;
      -g|--resource-group)
        resource_group="$2"
        shift
        ;;
      --template-file)
        template_file="$2"
        shift
        ;;
      --parameter-file)
        parameter_file="$2"
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

  if [[ -z "${service_principal}" ]]; then
    echo "Error: service principal must be specified with -u|--user"
    return 1
  fi

  if [[ -z "${service_principal_password}" ]]; then
    echo "Error: service principal password must be specified with -p|--password"
    return 1
  fi

  case "${environment_name}" in
    dev|test|ci-test|prod) ;;
    *)
      echo "Error: environment must be one of dev/test/ci-test/prod"
      return 1
      ;;
  esac

  if [[ -z "${parameter_file}" ]]; then
    parameter_file="${SCRIPT_ROOT}/params-${environment_name}.json"
  fi

  echo ""
  echo "Service principal: ${service_principal}"
  echo "Environment name: ${environment_name}"
  echo "Resource group: ${resource_group}"
  echo "Template file: ${template_file}"
  echo "Parameter file: ${parameter_file}"

  local container_name="etheos-${environment_name}"
  echo "Container name: ${container_name}"

  local dns_name="${environment_name}.etheos"
  echo "DNS name: ${dns_name}"

  echo ""
  echo "******Logging in to moffat.io tenant as ${service_principal}******"
  az login --service-principal -u "${service_principal}" -p "${service_principal_password}" --tenant moffat.io > /dev/null

  echo ""
  echo "******Deleting existing container (if exists)******"
  az container show -n "${container_name}" -g "${resource_group}" > /dev/null && az container delete -n "${container_name}" -g "${resource_group}" -y > /dev/null

  # Delete the ci test database container group if it exists
  #
  if [[ "${environment_name}" == "ci-test" ]]; then
    az container show -n "${container_name}-db" -g "${resource_group}" > /dev/null && az container delete -n "${container_name}-db" -g "${resource_group}" -y > /dev/null
  fi

  echo ""
  echo "******Creating container******"
  az deployment group create --resource-group "${resource_group}" --template-file "${template_file}" --parameters "${parameter_file}"

  echo ""
  echo "******Deleting existing DNS A record for ${environment_name}******"
  az network dns record-set a show -n "${dns_name}" -g moffat.io -z moffat.io > /dev/null && az network dns record-set a delete -n "${dns_name}" -g moffat.io -z moffat.io -y

  echo ""
  echo "******Creating new DNS A record for ${environment_name}******"
  ipAddr=$(az container show -g "${resource_group}" -n "${container_name}" | jq -r .ipAddress.ip)
  az network dns record-set a add-record -a "${ipAddr}" -n "${dns_name}" -g moffat.io -z moffat.io > /dev/null

  return 0
}

function display_usage() {
  echo "Usage:"
  echo "  deploy.sh [options]"
  echo ""
  echo "Options:"
  echo "  -u --user              User (azure service principal to authenticate as)"
  echo "  -p --password          Password for the service principal"
  echo "  -e --environment-name  Environment (one of: dev/test/ci-test/prod)"
  echo "  -g --resource-group    (optional) Resource group to deploy to"
  echo "  --template-file        (optional) Path to the template file to use for deployment"
  echo "  --parameter-file       (optional) Path to parameters for deployment. Defaults to deploy/params-{environment}.json"
  echo "  -h --help              Show this help"
}

main "$@"
