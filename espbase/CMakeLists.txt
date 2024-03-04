cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/../components
    $ENV{IDF_PATH}/examples/common_components/iperf
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espbase VERSION 1.0.0)

spiffs_create_partition_image(
    storage ${CMAKE_CURRENT_LIST_DIR}/files
    FLASH_IN_PROJECT
)
