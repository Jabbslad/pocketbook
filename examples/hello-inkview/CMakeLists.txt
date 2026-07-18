cmake_minimum_required(VERSION 3.21)

# The Dockerized PocketBook SDK sets CMAKE_TOOLCHAIN_FILE.
include("${CMAKE_TOOLCHAIN_FILE}")
project(hello_inkview C CXX)

link_directories("${PB_LINK_DIRECTORIES}")
include_directories("${PB_INCLUDE_DIRECTORIES}")
include_directories("${FREETYPE_INCLUDE_DIRS}")

add_executable(hello-inkview main.cpp)
set_target_properties(hello-inkview PROPERTIES OUTPUT_NAME "hello-inkview.app")
target_link_libraries(hello-inkview inkview)
