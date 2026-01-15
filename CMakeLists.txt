cmake_minimum_required(VERSION 3.0)
project(twamp-light)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra -Wall -Werror")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra -Wall -Werror")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra -Wall -O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra -Wall -O0 -g")
# write a compile_commands.json file for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-psabi" )
# set(CMAKE_CXX_FLAGS_RELEASE "-Werror -O2")
set(USE_CUJO_GCC ON)
include(cmake/getversion.cmake)

option(SYSTEMD_UNIT "Include Systemd unit in install" ON)
option(USE_ASAN "Build with address-sanitizer" OFF)
option(RUN_TESTS "Build and run tests" ON)
option(USE_LEGACY_TIMESTAMP_IN_CLIENT "Build without NIC timestamping in the client" OFF)
if (USE_LEGACY_TIMESTAMP_IN_CLIENT)
  add_definitions(-DKERNEL_TIMESTAMP_DISABLED_IN_CLIENT)
endif ()

if (USE_CUJO_GCC)
  # the Wno-error flag is there such that it doesn't exit on the first file where it trips on an analyzer warning. 
  string(REPLACE "-Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-Werror" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  set(CUJO_GCC_FLAGS " -Wall -Wextra -Wdouble-promotion '-Wformat=2' '-Wformat-overflow=2' -Wformat-signedness '-Wformat-truncation=2' '-Wshift-overflow=2' -Wsync-nand '-Wstrict-overflow=2' '-Wsuggest-attribute=format' '-Wsuggest-attribute=noreturn' -Walloc-zero -Walloca '-Warray-bounds=2' '-Wattribute-alias=2' -Wduplicated-branches -Wduplicated-cond -Wtrampolines -Wfloat-equal '-Wshadow=compatible-local' -Wpointer-arith -Wundef -Wunused-macros '-Wcast-align=strict' -Wwrite-strings -Wconversion -Wlogical-op -Wpacked -Wredundant-decls -Wvla -Wno-unknown-pragmas -Wbad-function-cast -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CUJO_GCC_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CUJO_GCC_FLAGS}")
endif ()

if (RUN_TESTS)
  enable_testing()
endif ()

if (USE_ASAN)
  string( TOLOWER "${CMAKE_BUILD_TYPE}" buildtype_lower)
  if (NOT buildtype_lower STREQUAL "debug" )
    message(FATAL_ERROR "USE_ASAN=ON and CMAKE_BUILD_TYPE != Debug")
  endif()
  add_definitions(-fsanitize=address -static-libasan -fno-omit-frame-pointer)
  set(EXTRA_LIBS ${EXTRA_LIBS} -fsanitize=address)
endif ()

find_package (Threads)

add_subdirectory(qoo-c)
add_subdirectory(third_party/CLI11)
add_subdirectory(third_party/json)

# Google Test for unit testing
if (RUN_TESTS)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/googletest)
endif()

set(COMMON_SOURCES
        include/utils.hpp
        src/utils.cpp
        include/packets.h
)

set(CLIENT_TARGET twamp-light-client)
set(SERVER_TARGET twamp-light-server)


include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/include/system
        SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/qoo-c/src
        SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/qoo-c/t-digest-c/src
)

add_executable(${CLIENT_TARGET}
src/client/Client.cpp
include/Client.h
src/client/main_client.cpp
${COMMON_SOURCES}
)

add_executable(${SERVER_TARGET}
src/server/Server.cpp
include/Server.h
src/server/main_server.cpp
${COMMON_SOURCES}
)

target_link_libraries(${CLIENT_TARGET} PRIVATE qoo_static CLI11::CLI11 nlohmann_json::nlohmann_json ${EXTRA_LIBS} Threads::Threads)
target_link_libraries(${SERVER_TARGET} PRIVATE CLI11::CLI11 ${EXTRA_LIBS})
target_include_directories(
        ${CLIENT_TARGET} PRIVATE
)

find_program (BASH_PROGRAM bash)
if (BASH_PROGRAM)
        add_test (NAME test_output 
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test.sh)
        add_test (NAME test_output_ipv6 
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ipv6.sh)        
        add_test (NAME test_memory 
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind_test.sh)
        add_test (NAME test_memory_ipv6 
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind_test_ipv6.sh)
        add_test (NAME test_memory_collator 
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind_test_collator.sh)
        # Integration tests for CLI flags and output formats
        add_test (NAME test_cli_flags
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_cli_flags.sh)
        add_test (NAME test_output_formats
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_output_formats.sh)
        add_test (NAME test_edge_cases
                COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_edge_cases.sh)
endif()

# Unit tests with Google Test
if (RUN_TESTS)
        # Create a library with common sources for testing
        add_library(twamp_common_lib STATIC
                src/utils.cpp
                include/utils.hpp
                include/packets.h
                include/packetlist.h
        )
        target_include_directories(twamp_common_lib PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/include/system
        )

        # Unit test for utilities
        add_executable(test_utils tests/unit/test_utils.cpp)
        target_link_libraries(test_utils PRIVATE twamp_common_lib gtest_main)
        target_include_directories(test_utils PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(NAME test_utils COMMAND test_utils)

        # Unit test for packet structures
        add_executable(test_packets tests/unit/test_packets.cpp)
        target_link_libraries(test_packets PRIVATE twamp_common_lib gtest_main)
        target_include_directories(test_packets PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(NAME test_packets COMMAND test_packets)

        # Unit test for observation list and raw data
        add_executable(test_packetlist tests/unit/test_packetlist.cpp)
        target_link_libraries(test_packetlist PRIVATE twamp_common_lib gtest_main Threads::Threads)
        target_include_directories(test_packetlist PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(NAME test_packetlist COMMAND test_packetlist)
endif()
        
install(TARGETS ${CLIENT_TARGET} ${SERVER_TARGET} 
        RUNTIME DESTINATION bin)

if (SYSTEMD_UNIT) 
	install(FILES systemd/twamp-light-server.service DESTINATION /lib/systemd/system) 
endif()
