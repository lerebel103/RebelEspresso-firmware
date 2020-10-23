#!/bin/bash
#
# description: Run locally on a dev machine or in CloudBuild to rebuild the Docker container for builds.
# author: Will Castelnau
#
set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
source "$DIR"/shell.env

# Create docker image to build TI's stuff with
docker build -f "${DIR}"/Dockerfile --build-arg MD5="$MD5" "$DIR" -t "$REPOSITORY_NAME":latest

# Tag for GCR now
GCR_IMAGE=gcr.io/${REPOSITORY_NAME}
docker tag "$REPOSITORY_NAME":latest "$GCR_IMAGE":latest
docker tag "$REPOSITORY_NAME":latest "$GCR_IMAGE":"$MD5"

echo ""
echo "Image ready, to push run:"
echo "aws ecr get-login-password --region ap-southeast-2 | docker login --username AWS --password-stdin $GCR_ENDPOINT"
echo "docker push $GCR_IMAGE:latest"
echo "docker push $GCR_IMAGE:$MD5"
