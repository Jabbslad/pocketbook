cmake_minimum_required(VERSION 3.21)

# The Dockerized PocketBook SDK sets CMAKE_TOOLCHAIN_FILE.
include("${CMAKE_TOOLCHAIN_FILE}")
project(rss_reader_journal C CXX)

string(REGEX REPLACE "[^/]+$" "pbres" PBRES "${CMAKE_C_COMPILER}")

link_directories("${PB_LINK_DIRECTORIES}")
include_directories("${PB_INCLUDE_DIRECTORIES}")
include_directories("${FREETYPE_INCLUDE_DIRS}")
include_directories("${CMAKE_FIND_ROOT_PATH}/usr/include/libxml2")

add_executable(rss-reader-journal main.cpp)
set_target_properties(rss-reader-journal PROPERTIES OUTPUT_NAME "rss-reader-journal.app")
target_link_libraries(rss-reader-journal inkview xml2 z lzma dl m)
