file(GLOB SOURCES *.c *.cpp)
idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS "include")

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.0")
    idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
else()
    idf_component_get_property(tusb_lib leeebo__tinyusb_src COMPONENT_LIB)
endif()

target_include_directories(${tusb_lib} PRIVATE "compat")
# cmake_policy(SET CMP0079 NEW)
# target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
