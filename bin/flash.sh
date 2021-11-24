#!/bin/bash

set -e

# Working dir of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

out_dir=/tmp
esp_tool=esptool.py
nvs_gen="${DIR}/../components/esp-homekit-sdk/tools/factory_nvs_gen/factory_nvs_gen.py"

# offset at which we are flashing the factory nvs partition
nvs_factory_offset=0x005b2000
# For r2.0
nvs_factory_offset=0x00684000

# Google IoT related
gcp_namespace="giot"
project_id="rebelthings"
location_id="asia-east1"
registry_id="RebelEspresso"


# Get thing id
mac=$(${esp_tool} flash_id | grep MAC | sed -e "s/MAC: //g")
thing_id=m${mac//:/}
thing_id="re-2.0-0000001"

echo "Generating Elliptic Curve keys for ${thing_id}"
private_key=$(openssl ecparam -genkey -name prime256v1 -noout)
public_key=$(echo "${private_key}" | openssl ec -pubout - )


echo "Generating and flashing factory nvs partition"
echo -e \
"key,type,encoding,value
${gcp_namespace},namespace,,
project_id,data,string,${project_id}
location_id,data,string,${location_id}
registry_id,data,string,${registry_id}
private_key,data,binary,\"${private_key}\"" > ${out_dir}/app_data.csv

${nvs_gen} --outdir=${out_dir} --infile ${out_dir}/app_data.csv 11122333 ES32 "${out_dir}/${thing_id}_factory_nvs"
${esp_tool} write_flash ${nvs_factory_offset} "${out_dir}/${thing_id}_factory_nvs.bin"

# Cleanup
echo "Cleaning up"
rm "${out_dir}/app_data.csv"
rm "${out_dir}/${thing_id}_factory_nvs.csv"
rm "${out_dir}/${thing_id}_factory_nvs.bin"

# Now echo the device public key
echo "Device public key is:"
echo "${public_key}"
