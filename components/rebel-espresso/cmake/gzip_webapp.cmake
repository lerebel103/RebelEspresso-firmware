if(NOT DEFINED INPUT_FILE)
  message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

execute_process(
  COMMAND gzip -9 -n -c "${INPUT_FILE}"
  OUTPUT_FILE "${OUTPUT_FILE}"
  RESULT_VARIABLE gzip_result
)

if(NOT gzip_result EQUAL 0)
  message(FATAL_ERROR "gzip failed while generating ${OUTPUT_FILE}")
endif()
