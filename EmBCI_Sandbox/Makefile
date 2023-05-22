# Makefile for arduino-core
# See more information at https://github.com/plerup/makeEspArduino

# sketch name is optional
SKETCH := main/EmBCI_ESP32.cpp
CHIP   := esp32

BUILD_ROOT = build

BUILD_EXTRA_FLAGS = \
	# Dont use this to enable vTaskList, because libs in arduino-esp32/tools/sdk/lib are already compiled without vTaskList
	# -DCONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=1 \
	# -DCONFIG_FREERTOS_USE_TRACE_FACILITY=1 \

VERSION_EMBCI = $(shell git -C main tag -l 2>/dev/null | tail -n1)
ifneq (${VERSION_EMBCI}, )
	BUILD_EXTRA_FLAGS += -DCONFIG_VERSION_EMBCI=${VERSION_EMBCI}
endif

USE_CCACHE  = 1
CUSTOM_LIBS ?= ${ESP_PATH}/components

ESP_ROOT ?= ${ESP_PATH}/components/arduino-esp32
include ${ESP_PATH}/tools/makeEspArduino/makeEspArduino.mk
