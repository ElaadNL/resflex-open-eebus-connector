# tests/CMakeLists.txt — EEBus SDK unit tests (Google Test)
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(googletest)

target_compile_options(gtest PRIVATE -Wno-unused-parameter)
target_compile_options(gtest_main PRIVATE -Wno-unused-parameter)
target_compile_options(gmock PRIVATE -Wno-unused-parameter)

add_library(eebus_test_stubs STATIC
  support/stub_mutex.c
  support/stub_timer.c
  support/stub_thread.c
  support/mock_time.c
)
target_include_directories(eebus_test_stubs PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(eebus_test_stubs PRIVATE _GNU_SOURCE=)

add_executable(eebus-unit-tests)

target_sources(eebus-unit-tests PRIVATE
  main.cpp
  common/error_test.cpp
  common/vector_test.cpp
  spine/codec/address_test.cpp
  spine/codec/msg_test.cpp
  spine/codec/datagram_test.cpp
  spine/codec/result_test.cpp
  spine/codec/load_ctrl_test.cpp
  spine/codec/dev_cfg_test.cpp
  spine/codec/dev_diag_test.cpp
  spine/codec/elec_conn_test.cpp
  spine/codec/node_mgmt_test.cpp
  spine/codec/sub_bind_test.cpp
  spine/codec/measurement_test.cpp
  spine/codec/rfe_test.cpp
  spine/sub_bind_test.cpp
  uc/lc_parse_test.cpp
  ship/model_test.cpp
  ship/tls/tls_test.cpp
  ship/connection_test.cpp
  ship/connection_intg_test.cpp
  spine/device_test.cpp
  spine/nm_intg_test.cpp
  spine/router_test.cpp
  spine/dispatch_intg_test.cpp
  uc/lc_cs_test.cpp
  uc/lc_cs_dispatch_test.cpp
  uc/lc_eg_test.cpp
  uc/lc_eg_dispatch_test.cpp
  uc/lc_eg_cache_test.cpp
  log/log_test.cpp

  ${CMAKE_SOURCE_DIR}/src/common/eebus_error.c
  ${CMAKE_SOURCE_DIR}/src/common/eebus_vector.c
  ${CMAKE_SOURCE_DIR}/src/log/eebus_log.c
  ${CMAKE_SOURCE_DIR}/src/ship/tls/ship_tls_openssl.c
  ${CMAKE_SOURCE_DIR}/src/ship/model/ship_model.c
  ${CMAKE_SOURCE_DIR}/src/ship/connection/ship_connection.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_address.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_msg.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_datagram.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_result.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_load_ctrl.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_dev_cfg.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_dev_diag.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_elec_conn.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_node_mgmt.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_sub_bind.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_codec_measurement.c
  ${CMAKE_SOURCE_DIR}/src/spine/codec/spine_rfe.c
  ${CMAKE_SOURCE_DIR}/src/spine/spine_sub_bind.c
  ${CMAKE_SOURCE_DIR}/src/spine/spine_device.c
  ${CMAKE_SOURCE_DIR}/src/spine/spine_node_mgmt.c
  ${CMAKE_SOURCE_DIR}/src/spine/spine_router.c
  ${CMAKE_SOURCE_DIR}/src/uc/uc_lc.c
  ${CMAKE_SOURCE_DIR}/src/uc/uc_lc_cs.c
  ${CMAKE_SOURCE_DIR}/src/uc/uc_lc_eg.c
)

target_include_directories(eebus-unit-tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests/support
)

target_link_options(eebus-unit-tests PRIVATE -Wl,--wrap=time)

target_link_libraries(eebus-unit-tests PRIVATE
  eebus_test_stubs
  gtest
  gtest_main
  pthread
  PkgConfig::CJSON
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_compile_options(eebus-unit-tests PRIVATE
  -Wno-pedantic               # C++ designated-init / compound-literals in C headers
)

if(EEBUS_ENABLE_COVERAGE)
  target_compile_options(eebus-unit-tests PRIVATE --coverage -fprofile-arcs -ftest-coverage)
  target_link_options(eebus-unit-tests PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(eebus-unit-tests)

if(EEBUS_ENABLE_COVERAGE)
  find_program(LCOV lcov)
  find_program(GENHTML genhtml)
  if(LCOV AND GENHTML)
    add_custom_target(coverage
      COMMAND ${CMAKE_COMMAND} -E echo "=== Running tests ==="
      COMMAND $<TARGET_FILE:eebus-unit-tests>
      COMMAND ${CMAKE_COMMAND} -E echo "=== Capturing coverage ==="
      COMMAND ${LCOV} --capture --directory ${CMAKE_BINARY_DIR}
        --output-file ${CMAKE_BINARY_DIR}/coverage.info --rc branch_coverage=1
        --exclude '/usr/*' --exclude '*/googletest/*' --exclude '*/tests/*'
      COMMAND ${CMAKE_COMMAND} -E echo "=== Generating HTML ==="
      COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage.info
        --output-directory ${CMAKE_BINARY_DIR}/coverage --rc branch_coverage=1
        --title "EEBus SDK Unit Tests"
      COMMAND ${CMAKE_COMMAND} -E echo "Report: ${CMAKE_BINARY_DIR}/coverage/index.html"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
  else()
    message(STATUS "lcov/genhtml not found — coverage target disabled (install with: apt install lcov)")
  endif()
endif()
