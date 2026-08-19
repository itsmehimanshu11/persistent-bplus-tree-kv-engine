cmake_minimum_required(VERSION 3.16)
project(kv_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(ZLIB REQUIRED)

set(KV_ENGINE_SOURCES
    src/btree/btree.cpp
    src/cli/cli.cpp
    src/flush/flush.cpp
    src/kv_engine.cpp
    src/memtable/memtable.cpp
    src/sstable/sstable.cpp
    src/wal/wal.cpp
    src/benchmark/benchmark.cpp
    src/api/rest_server.cpp
)

add_library(kv_engine STATIC ${KV_ENGINE_SOURCES})
target_include_directories(kv_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(kv_engine PUBLIC ZLIB::ZLIB)

add_executable(kv_engine_main src/main.cpp)
target_link_libraries(kv_engine_main PRIVATE kv_engine)

add_executable(kv_benchmark src/benchmark_main.cpp)
target_link_libraries(kv_benchmark PRIVATE kv_engine)

add_executable(kv_rest_server src/rest_server_main.cpp)
target_link_libraries(kv_rest_server PRIVATE kv_engine)
if(WIN32)
    target_link_libraries(kv_rest_server PRIVATE ws2_32)
endif()

add_executable(test_btree tests/test_btree.cpp)
target_link_libraries(test_btree PRIVATE kv_engine)

add_executable(kv_engine_test tests/test_kv_engine.cpp)
target_link_libraries(kv_engine_test PRIVATE kv_engine)

add_executable(wal_test tests/test_wal.cpp)
target_link_libraries(wal_test PRIVATE kv_engine)

# Tests must remain assertion-enabled even when the project is configured
# as Release. This prevents NDEBUG from silently disabling correctness checks.
if(MSVC)
    target_compile_options(test_btree PRIVATE /UNDEBUG)
    target_compile_options(kv_engine_test PRIVATE /UNDEBUG)
    target_compile_options(wal_test PRIVATE /UNDEBUG)
else()
    target_compile_options(test_btree PRIVATE -UNDEBUG)
    target_compile_options(kv_engine_test PRIVATE -UNDEBUG)
    target_compile_options(wal_test PRIVATE -UNDEBUG)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME btree_test COMMAND test_btree)
    add_test(NAME kv_engine_integration_test COMMAND kv_engine_test)
    add_test(NAME wal_test COMMAND wal_test)
endif()

install(TARGETS kv_engine kv_engine_main kv_benchmark kv_rest_server ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
