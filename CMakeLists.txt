cmake_minimum_required(VERSION 3.16)
project(search_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /wd4100 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ----------------------------------------------------
# Shared engine sources (no main, no server entry)
# ----------------------------------------------------
set(ENGINE_SOURCES
    src/indexer.cpp
    src/tokenizer.cpp
    src/bm25.cpp
    src/query_parser.cpp
    src/config.cpp
    src/logger.cpp
    src/metrics.cpp
    src/wal.cpp
    src/util.cpp
    src/varint.cpp
    src/bk_trie.cpp
    src/intern.cpp
)

# ----------------------------------------------------
# CLI binary: search_engine
# ----------------------------------------------------
add_executable(search_engine
    src/main.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(search_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/data
)

# ----------------------------------------------------
# Server binary: search_server
# ----------------------------------------------------
add_executable(search_server
    src/main_server.cpp
    src/api/server.cpp
    src/api/routes.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(search_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/api
    ${CMAKE_SOURCE_DIR}/data
)

# httplib needs _WIN32_WINNT on Windows for socket support
if (WIN32)
    target_compile_definitions(search_server PRIVATE _WIN32_WINNT=0x0A00)
endif()

# ----------------------------------------------------
# Threads
# ----------------------------------------------------
find_package(Threads REQUIRED)
target_link_libraries(search_engine Threads::Threads)
target_link_libraries(search_server Threads::Threads)

# On Windows, httplib needs ws2_32 and wsock32
if (WIN32)
    target_link_libraries(search_server ws2_32 wsock32)
endif()

# Filesystem (GCC < 10)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(search_engine stdc++fs)
    target_link_libraries(search_server stdc++fs)
endif()

# ----------------------------------------------------
# PDF support (optional)
# ----------------------------------------------------
find_program(PDFTOTEXT_EXECUTABLE pdftotext)
if (PDFTOTEXT_EXECUTABLE)
    message(STATUS "pdftotext found: ${PDFTOTEXT_EXECUTABLE}")
    target_compile_definitions(search_engine PRIVATE USE_PDFTOTEXT=1)
    target_compile_definitions(search_server PRIVATE USE_PDFTOTEXT=1)
else()
    message(STATUS "pdftotext not found. PDF indexing disabled.")
endif()

# ----------------------------------------------------
# Output directory
# ----------------------------------------------------
set_target_properties(search_engine search_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
