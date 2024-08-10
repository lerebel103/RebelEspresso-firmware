#!/usr/bin/env python3

import json, base64
import subprocess
import sys
import uuid
import os

aws_profile = 'AWSAdministratorAccess-407440998404-dev'
os.environ['AWS_DEFAULT_PROFILE'] = aws_profile

ota_bucket = 'rebelthingsiotstackdevotascaffol-afrotadevadbe8014-howqfypvg1o6'
firmware_key = 'release/garage-door/0.1.157/r1/garage-door-firmware.bin'
signed_firmware_key = 'release/garage-door/0.1.157/r1/signed-firmware-80148f8e-3d03-484e-8a00-d9458d3967cb'
depl_name = 'garage-door'
targets = 'arn:aws:iot:ap-southeast-2:407440998404:thinggroup/garage-door-ota'
role_arn = 'arn:aws:iam::407440998404:role/AfrCreateOtaJobRole'


# Parsed a signed firmware file created by AWS Code Signer
def main():

    # Get the S3 version of the firmware file
    firmware_s3_version = get_s3_firmware_version(ota_bucket, firmware_key)
    # Download signed firmware file to parse it (it's a JSON payload)
    firmware_path = download_signed_firmware(f"s3://{ota_bucket}/{signed_firmware_key}")

    with open(firmware_path, 'r') as f:
        res = json.loads(f.read())

    # Extract signature, encoding and payload
    raw_payload_size = res['rawPayloadSize']
    sig = res['signature']
    signature_algorithm = res['signatureAlgorithm']
    firmware_bin = base64.b64decode(res['payload'])

    # Sanity check
    if raw_payload_size != len(firmware_bin):
        print("Error: rawPayloadSize does not match the length of the payload")
        sys.exit(1)

    print("Firmware and signature parsed successfully")

    # Build the OTA file
    ota_files = [
      {
        'fileName': 'firmware.bin',
        'fileVersion': '1',
        'fileLocation': {
          's3Location': {
            'bucket': "rebelthingsiotstackdevotascaffol-afrotadevadbe8014-howqfypvg1o6",
            'key': firmware_key,
            'version': firmware_s3_version
          }
        },
        'codeSigning': {
          'customCodeSigning': {
            'signature': {
               "inlineDocument": base64.b64encode(sig.encode()).decode('utf-8'),
            },
            "certificateChain": {
              "certificateName": "certificate.pem",
              "inlineDocument": ""
            },
            'hashAlgorithm': signature_algorithm.split('with')[0],
            'signatureAlgorithm': signature_algorithm.split('with')[1]
          }
        }
      }
    ]
    ota_files_json = json.dumps(ota_files)

    cmd = [
      'aws', 'iot', 'create-ota-update',
      '--ota-update-id', f"{depl_name}-{uuid.uuid4()}",
      '--description', 'OTA update Created by CI/CD',
      '--protocols', 'MQTT',
      '--targets', targets,
      '--files', ota_files_json,
      '--role-arn', role_arn
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error creating OTA update: {res}")
        sys.exit(1)
    print(res.stdout)


def download_signed_firmware(aws_signer_output_s3_key):
    print("Downloading signed firmware file")
    firmware_path = '/tmp/signed-firmware.json'
    cmd = [
        'aws', 's3', 'cp', '--quiet', aws_signer_output_s3_key, firmware_path
    ]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"Error downloading signed firmware file: {res}")
        sys.exit(1)
    return firmware_path


def get_s3_firmware_version(bucket, firmware_s3_key):
    print("Getting s3 firmware version for firmware")
    cmd = [
      'aws', 's3api', 'list-object-versions',
      '--bucket', bucket,
      '--prefix', firmware_s3_key,
      '--query', 'Versions[?IsLatest].[VersionId]',
      '--output', 'text'
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or res.stdout.strip() == 'None':
        print(f"Error getting s3 firmware version: {res}")
        sys.exit(1)
    return res.stdout.strip()


# call main function
if __name__ == '__main__':
    main()
