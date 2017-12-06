set(smf_platform_headers
  ${PROJECT_SOURCE_DIR}/src/platform/macros.h
  ${PROJECT_SOURCE_DIR}/src/platform/log.h
  )
install(FILES ${smf_platform_headers} DESTINATION include/smf/platform)
