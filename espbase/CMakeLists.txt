cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/../components
    $ENV{IDF_PATH}/examples/common_components/iperf
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espbase VERSION 1.1.0)

add_compile_definitions(
    PROJECT_NAME=${PROJECT_NAME}
    PROJECT_VERSION=${PROJECT_VERSION}
    PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(CONFIG_USE_FAT STREQUAL "y")
    message(STATUS "Creating FAT FileSystem image on ESP-IDF v5.x")
    fatfs_create_spiflash_image(
        storage
       ${CMAKE_CURRENT_LIST_DIR}/files
       FLASH_IN_PROJECT
    )
else(CONFIG_USE_SPIFFS STREQUAL "y")
    message(STATUS "Creating SPI Flash FileSystem image")
    spiffs_create_partition_image(
        storage
        ${CMAKE_CURRENT_LIST_DIR}/files
        FLASH_IN_PROJECT
    )
endif()
