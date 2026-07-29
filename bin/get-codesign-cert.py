#!/usr/bin/env python3
# This script is used to download the code signing certificate from the server and save it to a file.
#
# A codesign cert will allow the device to validate new firmware images recieved vi OTA updates.

import sys
import os
import json
import requests

if len(sys.argv) != 3:
    print("Usage: get-codesign-cert.py <url> <output-file>")
    sys.exit(-1)

url = sys.argv[1]
cert_file = sys.argv[2]
r = requests.get(url)
certificate = json.loads(r.text.replace('\n', '\\n'))['certificate']

dir_path = os.path.dirname(cert_file)
if not os.path.exists(dir_path):
    os.makedirs(dir_path)
    
with open(cert_file, 'w') as f:
    f.write(certificate)

print("Certificate saved to " + cert_file + ".")
