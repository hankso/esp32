cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

set(EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/../components
    $ENV{IDF_PATH}/examples/common_components/iperf
)

include($ENV{IDF_PATH}/tools/cmake/version.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(helper_py python ${CMAKE_CURRENT_LIST_DIR}/helper.py)
execute_process(COMMAND ${helper_py} prebuild)

project(espbase VERSION 1.1.0)

# CONFIG_BASE_XXX are defined in main/Kconfig.projbuild

if(CONFIG_BASE_FFS_FAT STREQUAL "y")
    if (IDF_VERSION_MAJOR LESS 5)
        message(FATAL_ERROR "FAT FS is only supported on ESP-IDF v5.x. "
                            "Current version is ESP-IDF ${IDF_VERSION}.")
    endif()
    message(STATUS "Creating FAT File System image")
    fatfs_create_spiflash_image(
        ${CONFIG_BASE_FFS_PART}
        ${CMAKE_CURRENT_LIST_DIR}/files
        FLASH_IN_PROJECT
    )
elseif(CONFIG_BASE_FFS_SPI STREQUAL "y")
    message(STATUS "Creating SPI Flash File System image")
    spiffs_create_partition_image(
        ${CONFIG_BASE_FFS_PART}
        ${CMAKE_CURRENT_LIST_DIR}/files
        FLASH_IN_PROJECT
    )
endif()

function(nvs_create_partition_image partition template)
    set(options FLASH_IN_PROJECT)
    set(multi DEPENDS)
    cmake_parse_arguments(arg "${options}" "" "${multi}" "${ARGN}")
    get_filename_component(template_fullpath ${template} ABSOLUTE)
    partition_table_get_partition_info(
        size "--partition-name ${partition}" "size")
    partition_table_get_partition_info(
        offset "--partition-name ${partition}" "offset")
    if("${size}" AND "${offset}")
        set(image_file ${CMAKE_BINARY_DIR}/${partition}.bin)
        add_custom_target(gen_${partition}_bin ALL
            COMMAND ${helper_py} --quiet gencfg
            --tpl ${template_fullpath}
            --pack ${image_file}
            --size ${size}
            --offset ${offset}
            DEPENDS ${arg_DEPENDS}
            )
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
            ADDITIONAL_MAKE_CLEAN_FILES ${image_file})
        idf_component_get_property(margs esptool_py FLASH_ARGS)
        idf_component_get_property(sargs esptool_py FLASH_SUB_ARGS)
        esptool_py_flash_target(
            ${partition}-flash "${margs}" "${sargs}" ALWAYS_PLAINTEXT)
        esptool_py_flash_to_partition(
            ${partition}-flash "${partition}" "${image_file}")
        add_dependencies(${partition}-flash gen_${partition}_bin)
        if(arg_FLASH_IN_PROJECT)
            esptool_py_flash_to_partition(flash "${partition}" "${image_file}")
            add_dependencies(flash gen_${partition}_bin)
        endif()
    else()
        set(message "Failed to generate NVS flash image. "
                    "Check if using the correct partition table file.")
        fail_at_built_time(nvs_${partition}_bin "${message}")
    endif()
endfunction()

nvs_create_partition_image(
    nvs
    ${CMAKE_CURRENT_LIST_DIR}/nvs_flash.csv
    FLASH_IN_PROJECT
)
