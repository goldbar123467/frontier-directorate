cmake_minimum_required(VERSION 3.25)

project(FrontierDirectorate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FD_WARNINGS_AS_ERRORS "Treat Frontier Directorate warnings as errors" ON)
option(FD_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FD_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(FD_ENABLE_COVERAGE "Enable compiler coverage instrumentation" OFF)
option(FD_BUILD_TESTS "Build CTest targets" ON)
option(FD_BUILD_BENCHMARKS "Build Update 01 benchmarks" ON)
option(FD_BUILD_FUZZERS "Build standalone/fuzzer harnesses" OFF)
option(FD_BUILD_WARDEN_TESTS "Enable long-running Update 01 evidence campaigns" OFF)

if(FD_ENABLE_ASAN_UBSAN AND FD_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must use separate build trees")
endif()

function(fd_apply_warnings target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
     CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    if(FD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(fd_apply_instrumentation target)
  if(FD_ENABLE_ASAN_UBSAN)
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
  elseif(FD_ENABLE_TSAN)
    target_compile_options(${target} PRIVATE
      -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE
      -fsanitize=thread -fno-omit-frame-pointer)
  endif()
  if(FD_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE --coverage)
      target_link_options(${target} PRIVATE --coverage)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
      target_link_options(${target} PRIVATE -fprofile-instr-generate)
    endif()
  endif()
endfunction()

set(FD_CORE_SOURCES
  src/core/fd_core.c
  src/core/fd_generate.c
  src/core/fd_replay.c
  src/core/fd_rng.c
  src/core/fd_serialize.c
  src/core/fd_sha256.c
  src/core/fd_validate.c)
add_library(frontier_core STATIC ${FD_CORE_SOURCES})
target_include_directories(frontier_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE src/core)
set_target_properties(frontier_core PROPERTIES
  C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
fd_apply_warnings(frontier_core)
fd_apply_instrumentation(frontier_core)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/fd.cpp")
  add_library(frontier_cpp STATIC
    src/cpp/fd.cpp
    src/cpp/result.cpp)
  target_include_directories(frontier_cpp PUBLIC
    include
    src/cpp)
  target_link_libraries(frontier_cpp PUBLIC frontier_core)
  set_target_properties(frontier_cpp PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
  fd_apply_warnings(frontier_cpp)
  fd_apply_instrumentation(frontier_cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/ascii_renderer.cpp")
  add_library(frontier_ascii STATIC src/terminal/ascii_renderer.cpp)
  target_include_directories(frontier_ascii PUBLIC include src/cpp src/terminal)
  if(TARGET frontier_cpp)
    target_link_libraries(frontier_ascii PUBLIC frontier_cpp)
  else()
    target_link_libraries(frontier_ascii PUBLIC frontier_core)
  endif()
  set_target_properties(frontier_ascii PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
  fd_apply_warnings(frontier_ascii)
  fd_apply_instrumentation(frontier_ascii)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/main.cpp")
  add_executable(frontier_terminal src/terminal/main.cpp)
  if(TARGET frontier_ascii)
    target_link_libraries(frontier_terminal PRIVATE frontier_ascii)
  elseif(TARGET frontier_cpp)
    target_link_libraries(frontier_terminal PRIVATE frontier_cpp)
  else()
    target_link_libraries(frontier_terminal PRIVATE frontier_core)
  endif()
  fd_apply_warnings(frontier_terminal)
  fd_apply_instrumentation(frontier_terminal)
endif()

if(FD_BUILD_TESTS)
  include(CTest)
  enable_testing()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/test_core.c")
    add_executable(test_core tests/core/test_core.c)
    target_include_directories(test_core PRIVATE src/core)
    target_link_libraries(test_core PRIVATE frontier_core)
    fd_apply_warnings(test_core)
    fd_apply_instrumentation(test_core)
    add_test(NAME test_core COMMAND test_core)
    if(FD_BUILD_WARDEN_TESTS)
      add_test(NAME test_core_warden
        COMMAND test_core --seeds 10000 --roundtrips 1000)
      set_tests_properties(test_core_warden PROPERTIES
        LABELS "campaign;warden"
        TIMEOUT 3600)
    endif()
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/test_headless.c")
    add_executable(test_headless tests/core/test_headless.c)
    target_link_libraries(test_headless PRIVATE frontier_core)
    fd_apply_warnings(test_headless)
    fd_apply_instrumentation(test_headless)
    add_test(NAME test_headless COMMAND test_headless)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/test_public_api.c")
    add_executable(test_public_api tests/core/test_public_api.c)
    target_link_libraries(test_public_api PRIVATE frontier_core)
    fd_apply_warnings(test_public_api)
    fd_apply_instrumentation(test_public_api)
    add_test(NAME test_public_api COMMAND test_public_api)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/test_threads.c")
    find_package(Threads REQUIRED)
    add_executable(test_threads tests/core/test_threads.c)
    target_link_libraries(test_threads PRIVATE frontier_core Threads::Threads)
    fd_apply_warnings(test_threads)
    fd_apply_instrumentation(test_threads)
    add_test(NAME test_threads COMMAND test_threads)
  endif()
  # The allocation-failure harnesses replace global new/delete, which conflicts
  # with Clang TSan's own new/delete interceptors. They run in the normal and
  # ASan/UBSan configurations; the TSan configuration focuses on concurrency.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpp.cpp" AND
     TARGET frontier_cpp AND TARGET frontier_ascii AND NOT FD_ENABLE_TSAN)
    add_executable(test_cpp tests/test_cpp.cpp)
    target_link_libraries(test_cpp PRIVATE frontier_ascii)
    fd_apply_warnings(test_cpp)
    fd_apply_instrumentation(test_cpp)
    add_test(NAME test_cpp COMMAND test_cpp)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpp_headless.cpp" AND
     TARGET frontier_cpp)
    add_executable(test_cpp_headless tests/test_cpp_headless.cpp)
    target_link_libraries(test_cpp_headless PRIVATE frontier_cpp)
    fd_apply_warnings(test_cpp_headless)
    fd_apply_instrumentation(test_cpp_headless)
    add_test(NAME test_cpp_headless COMMAND test_cpp_headless)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ascii_exception.cpp" AND
     TARGET frontier_ascii AND NOT FD_ENABLE_TSAN)
    add_executable(test_ascii_exception tests/test_ascii_exception.cpp)
    target_link_libraries(test_ascii_exception PRIVATE frontier_ascii)
    fd_apply_warnings(test_ascii_exception)
    fd_apply_instrumentation(test_ascii_exception)
    add_test(NAME test_ascii_exception COMMAND test_ascii_exception)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ascii_c.c" AND
     TARGET frontier_ascii)
    add_executable(test_ascii_c tests/test_ascii_c.c)
    set_target_properties(test_ascii_c PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(test_ascii_c PRIVATE frontier_ascii)
    fd_apply_warnings(test_ascii_c)
    fd_apply_instrumentation(test_ascii_c)
    add_test(NAME test_ascii_c COMMAND test_ascii_c)
  endif()
endif()

if(FD_BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/core/bench_u01.c")
  find_package(Threads REQUIRED)
  add_executable(bench_u01 bench/core/bench_u01.c)
  target_include_directories(bench_u01 PRIVATE src/core)
  target_link_libraries(bench_u01 PRIVATE frontier_core Threads::Threads m)
  fd_apply_warnings(bench_u01)
  fd_apply_instrumentation(bench_u01)
endif()

if(FD_BUILD_BENCHMARKS AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_ascii.cpp" AND
   TARGET frontier_ascii)
  add_executable(benchmark_ascii tests/benchmark_ascii.cpp)
  target_link_libraries(benchmark_ascii PRIVATE frontier_ascii)
  fd_apply_warnings(benchmark_ascii)
  fd_apply_instrumentation(benchmark_ascii)
endif()

if(FD_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "FD_BUILD_FUZZERS requires Clang/libFuzzer")
  endif()
  add_library(frontier_core_fuzz STATIC ${FD_CORE_SOURCES})
  target_include_directories(frontier_core_fuzz PUBLIC include PRIVATE src/core)
  set_target_properties(frontier_core_fuzz PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
  fd_apply_warnings(frontier_core_fuzz)
  target_compile_options(frontier_core_fuzz PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  foreach(fuzz_target IN ITEMS fuzz_config fuzz_save fuzz_replay fuzz_action fuzz_api)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/${fuzz_target}.c")
      add_executable(${fuzz_target} tests/fuzz/${fuzz_target}.c)
      target_include_directories(${fuzz_target} PRIVATE src/core)
      target_link_libraries(${fuzz_target} PRIVATE frontier_core_fuzz)
      fd_apply_warnings(${fuzz_target})
      fd_apply_instrumentation(${fuzz_target})
      target_compile_options(${fuzz_target} PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
      target_link_options(${fuzz_target} PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    endif()
  endforeach()
endif()
