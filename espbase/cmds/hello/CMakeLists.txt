cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello)

# e.g. set(ELF_COMPONENTS "log" "lwip")

include(elf_loader)
project_elf(hello)
