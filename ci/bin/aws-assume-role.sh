#!/bin/sh
# Assume the specified role and export the temporary credentials as environment variables
#
# This script must be sourced and not executed. This is because the environment variables are set in the current shell.
# Usage: . ./aws-assume-role.sh <role_arn> <session_name>

#set -e

# Check that we have two command line arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: aws-assume-role.sh <role_arn> <session_name>"
    exit 1
fi

temp_role=$(aws sts assume-role \
                    --role-arn "$1" \
                    --role-session-name "$2")

export AWS_ACCESS_KEY_ID=$(echo "$temp_role" | jq -r .Credentials.AccessKeyId)
export AWS_SECRET_ACCESS_KEY=$(echo "$temp_role" | jq -r .Credentials.SecretAccessKey)
export AWS_SESSION_TOKEN=$(echo "$temp_role" | jq -r .Credentials.SessionToken)
