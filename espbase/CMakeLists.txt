cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/version.cmake)

set(COMPONENTS main)
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(SDKCONFIG_BASE "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
set(SDKCONFIG_LOCAL "${SDKCONFIG_BASE}.local")
set(SDKCONFIG_IDFVER "${SDKCONFIG_BASE}.defaults.v${IDF_VERSION_MAJOR}")
set(SDKCONFIG_DEFAULTS "${SDKCONFIG_BASE}.defaults")
if(EXISTS "${SDKCONFIG_LOCAL}")
    list(APPEND SDKCONFIG_DEFAULTS "${SDKCONFIG_LOCAL}")
endif()
if(EXISTS "${SDKCONFIG_IDFVER}")
    list(APPEND SDKCONFIG_DEFAULTS "${SDKCONFIG_IDFVER}")
endif()
if(EXISTS "$ENV{IDF_PATH}/../components")
    set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/../components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_get_property(python PYTHON)
idf_build_set_property(MINIMAL_BUILD ON)

set(helper_py "${python}" "${CMAKE_CURRENT_LIST_DIR}/helper.py" -B "${CMAKE_BINARY_DIR}")
execute_process(COMMAND ${helper_py} prebuild)  # run once for configure

project(espbase VERSION 1.1.0)

add_custom_command(
    TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${helper_py} postbuild              # run for each build
)

idf_build_get_property(target IDF_TARGET)
idf_build_get_property(sdir PROJECT_DIR)
idf_build_get_property(bdir BUILD_DIR)
set(ffs_base "${sdir}/data")

function(elf_build app_name app_path)
    cmake_parse_arguments(arg "" "TYPE" "" "${ARGN}")
    if(NOT arg_TYPE)
        set(cmakefile "${app_path}/CMakeLists.txt")
        if (EXISTS ${cmakefile})
            file(READ ${cmakefile} content)
            if(content MATCHES "project_elf")
                set(arg_TYPE "elf")
            elseif(content MATCHES "project_so")
                set(arg_TYPE "so")
            else()
                message(WARNING "Skip ${app_name} as it is not an ELF or SO project")
                return()
            endif()
        else()
            message(WARNING "Skip ${app_name} as it is not a valid project")
            return()
        endif()
    endif()
    set(elf_app_dir "${bdir}/elf_${app_name}")
    set(elf_dst_dir "${ffs_base}/bin")
    file(MAKE_DIRECTORY "${elf_dst_dir}")
    if (arg_TYPE STREQUAL "elf")
        set(elf_src_bin "${elf_app_dir}/${app_name}.app.elf")
        set(elf_dst_bin "${elf_dst_dir}/${app_name}")
        set(build_target "elf")
    else()
        set(elf_src_bin "${elf_app_dir}/${app_name}.so")
        set(elf_dst_bin "${elf_dst_dir}/${app_name}.so")
        set(build_target "so")
    endif()
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set(build_command ninja ${build_target})
    else()
        set(build_command make ${build_target})
    endif()
    file(REMOVE ${elf_app_dir}/sdkconfig)
    externalproject_add(
        ${app_name}
        SOURCE_DIR ${app_path}
        INSTALL_COMMAND ""
        CMAKE_ARGS
            -DIDF_PATH=$ENV{IDF_PATH}
            -DIDF_TARGET=${target}
            -DSDKCONFIG=${elf_app_dir}/sdkconfig
        BINARY_DIR ${elf_app_dir}
        BUILD_BYPRODUCTS ${elf_src_bin}
        BUILD_ALWAYS 1
        CMAKE_GENERATOR ${CMAKE_GENERATOR}
        BUILD_COMMAND ${build_command}
    )
    add_custom_command(
        OUTPUT ${elf_dst_bin}
        COMMAND ${CMAKE_COMMAND} -E copy ${elf_src_bin} ${elf_dst_bin}
        DEPENDS ${app_name}
        COMMENT "Copy ${elf_src_bin} to ${elf_dst_bin}"
    )
    add_custom_target(elf_${app_name}_bin ALL DEPENDS ${elf_dst_bin})
endfunction()

function(nvs_create_partition_image partition template)
    cmake_parse_arguments(arg "FLASH_IN_PROJECT" "" "DEPENDS" "${ARGN}")
    get_filename_component(template_fullpath ${template} ABSOLUTE)
    partition_table_get_partition_info(size "--partition-name ${partition}" "size")
    partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
    if("${size}" AND "${offset}")
        set(image_file "${bdir}/${partition}.bin")
        add_custom_target(gen_${partition}_bin ALL
            COMMAND ${helper_py} --quiet gencfg
            --tpl "${template_fullpath}"
            --pack "${image_file}"
            --size ${size}
            --offset ${offset}
            DEPENDS ${arg_DEPENDS}
        )
        set_property(
            DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
            ADDITIONAL_MAKE_CLEAN_FILES "${image_file}"
        )
        idf_component_get_property(margs esptool_py FLASH_ARGS)
        idf_component_get_property(sargs esptool_py FLASH_SUB_ARGS)
        esptool_py_flash_target(${partition}-flash "${margs}" "${sargs}" ALWAYS_PLAINTEXT)
        esptool_py_flash_to_partition(${partition}-flash "${partition}" "${image_file}")
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

# CONFIG_BASE_XXX are defined in main/Kconfig.projbuild

if(CONFIG_BASE_FFS_SPI STREQUAL "y")
    message(STATUS "Creating SPI Flash File System image")
    spiffs_create_partition_image(
        ${CONFIG_BASE_FFS_PART}
        "${ffs_base}"
        FLASH_IN_PROJECT
    )
    set(ffs_target "spiffs_${CONFIG_BASE_FFS_PART}_bin")
elseif(IDF_VERION_MAJOR LESS 5)
    message(FATAL_ERROR "Only SPIFFS is supported on ESP-IDF v4")
elseif(CONFIG_BASE_FFS_FAT STREQUAL "y")
    message(STATUS "Creating FAT File System image")
    fatfs_create_spiflash_image(
        ${CONFIG_BASE_FFS_PART}
        "${ffs_base}"
        FLASH_IN_PROJECT PRESERVE_TIME
    )
    set(ffs_target "fatfs_${CONFIG_BASE_FFS_PART}_bin")
elseif(CONFIG_BASE_FFS_LIT STREQUAL "y")
    message(STATUS "Creating Little File System image")
    littlefs_create_partition_image(
        ${CONFIG_BASE_FFS_PART}
        "${ffs_base}"
        FLASH_IN_PROJECT
    )
    set(ffs_target "littlefs_${CONFIG_BASE_FFS_PART}_bin")
endif()

if(TARGET ${ffs_target})                                # guess target name
    set(cmds "${sdir}/cmds")
    file(GLOB childs RELATIVE "${cmds}" "${cmds}/*")    # glob subdirectories
    foreach(child ${childs})
        set(child_path "${cmds}/${child}")
        if(IS_DIRECTORY ${child_path} AND EXISTS ${child_path}/CMakeLists.txt)
            message(STATUS "Registered ELF: ${child}")
            elf_build("${child}" "${child_path}")       # build elf file
            add_dependencies(${ffs_target} elf_${child}_bin)
        endif()
    endforeach()
else()
    message(WARNING "No ${ffs_target} target found: see fatfs/spiffs/littlefs project_include.cmake")
endif()

nvs_create_partition_image(nvs "${sdir}/nvs_flash.csv" FLASH_IN_PROJECT)
