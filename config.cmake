
# Default is dev
SET(CODESIGN_URL "https://8v6sf6wg98.execute-api.ap-southeast-2.amazonaws.com/dev/esp32-certificate"
        CACHE INTERNAL "URL for code signing service" FORCE)

IF (BUILD_STAGE MATCHES stg)
    # stg settings
ELSEIF (BUILD_STAGE MATCHES prd)
    # prod settings
ENDIF ()
