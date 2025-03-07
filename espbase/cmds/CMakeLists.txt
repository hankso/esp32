cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_elf VERSION 0.1.0)

include(elf_loader)
project_elf(test_elf)
