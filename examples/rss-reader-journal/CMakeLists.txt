cmake_minimum_required(VERSION 3.21)

# The Dockerized PocketBook SDK sets CMAKE_TOOLCHAIN_FILE.
include("${CMAKE_TOOLCHAIN_FILE}")
project(rss_reader_journal C CXX)

string(REGEX REPLACE "[^/]+$" "pbres" PBRES "${CMAKE_C_COMPILER}")

link_directories("${PB_LINK_DIRECTORIES}")
include_directories("${PB_INCLUDE_DIRECTORIES}")
include_directories("${FREETYPE_INCLUDE_DIRS}")
include_directories("${CMAKE_FIND_ROOT_PATH}/usr/include/libxml2")

set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # device lacks these .so files
add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)

add_executable(rss-reader-journal main.cpp litehtml_container.cpp)
target_include_directories(rss-reader-journal PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/litehtml/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
set_target_properties(rss-reader-journal PROPERTIES CXX_STANDARD 11)
set_target_properties(rss-reader-journal PROPERTIES OUTPUT_NAME "rss-reader-journal.app")
target_link_libraries(rss-reader-journal inkview litehtml xml2 z lzma sqlite3 dl m)
