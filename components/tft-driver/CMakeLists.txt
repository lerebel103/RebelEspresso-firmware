set(srcs
	"ili9340.c"
	"fontx.c"
	"decode_png.c"
	"pngle.c"
	)

# tjpgd library does not exist in ESP32-S2 ROM.
if(IDF_TARGET STREQUAL "esp32")
	list(APPEND srcs "decode_jpeg.c")
endif()

if(IDF_TARGET STREQUAL "esp32c3")
	list(APPEND srcs "decode_jpeg.c")
endif()

idf_component_register(
		SRCS ${srcs}
		INCLUDE_DIRS "."
		REQUIRES spiffs driver esp_driver_spi esp_driver_gpio esp_driver_ledc)

if (HARDWARE_REVISION_MAJOR STREQUAL "2")
	# Create a SPIFFS image from the contents of the 'font' directory
	# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
	# the generated image should be flashed when the entire project is flashed to
	# the target with 'idf.py -p PORT flash
	spiffs_create_partition_image(storage font FLASH_IN_PROJECT)
endif()
