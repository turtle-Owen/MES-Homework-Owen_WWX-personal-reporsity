cmake_minimum_required(VERSION 3.16)
project(campus-deploy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

function(configure_deployer_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /W3)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endfunction()

if(WIN32)
  add_executable(campus-deploy
    src/main.cpp src/util.cpp src/json.cpp src/cloudflare.cpp src/deploy.cpp src/webconsole.cpp
  )
  target_link_libraries(campus-deploy PRIVATE ws2_32 iphlpapi shell32 bcrypt advapi32 ole32)
  configure_deployer_target(campus-deploy)
  if(NOT MSVC)
    target_compile_definitions(campus-deploy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(campus-deploy PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(campus-deploy PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  set_target_properties(campus-deploy PROPERTIES
    # A generator expression suppresses Visual Studio's extra Release/ suffix.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../../dist-deployer/$<0:>"
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(json-test tests/json_test.cpp src/json.cpp)
  add_executable(security-test tests/security_test.cpp src/json.cpp)
  add_executable(state-test tests/state_test.cpp)
  # Link-time adapters in tunnel_test.cpp deliberately replace all HTTP and file I/O.
  add_executable(tunnel-test tests/tunnel_test.cpp src/cloudflare.cpp src/json.cpp)
  foreach(target json-test security-test state-test tunnel-test)
    configure_deployer_target(${target})
    # Tests use assert: Release builds must execute assertions and their expressions.
    if(MSVC)
      target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
