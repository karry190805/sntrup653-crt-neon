cmake_minimum_required(VERSION 3.16)
project(sntrup653_crt_neon C)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  message(FATAL_ERROR "This implementation targets AArch64 NEON; configure with an AArch64 toolchain.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(sntrup653_crt_neon STATIC
  packages/sntrup653-crt-cortex-a76/optimized/crypto_core_multsntrup653.c
)
target_include_directories(sntrup653_crt_neon PUBLIC
  packages/sntrup653-crt-cortex-a76/optimized include)
target_compile_options(sntrup653_crt_neon PRIVATE -O3 -Wall -Wextra)

add_custom_target(target_validation
  COMMAND ${CMAKE_COMMAND} -E echo "Run packages/sntrup653-crt-cortex-a76/run_correctness.sh on the target board"
)
