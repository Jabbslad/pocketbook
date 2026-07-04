cmake_minimum_required(VERSION 3.21)

# The Dockerized PocketBook SDK sets CMAKE_TOOLCHAIN_FILE.
include("${CMAKE_TOOLCHAIN_FILE}")
project(rss_reader_mockup C CXX)

string(REGEX REPLACE "[^/]+$" "pbres" PBRES "${CMAKE_C_COMPILER}")

link_directories("${PB_LINK_DIRECTORIES}")
include_directories("${PB_INCLUDE_DIRECTORIES}")
include_directories("${FREETYPE_INCLUDE_DIRS}")

add_executable(rss-reader-mockup main.cpp)
set_target_properties(rss-reader-mockup PROPERTIES OUTPUT_NAME "rss-reader-mockup.app")
target_link_libraries(rss-reader-mockup inkview)
