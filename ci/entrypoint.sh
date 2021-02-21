#!/bin/sh

# Load environment to build esp-idf projects
. "${IDF_PATH}"/export.sh

# Then run the CMD
exec "$@"