#!/bin/bash

DIR=`dirname ${BASH_SOURCE[0]}`

make -f ${DIR}/Makefile

cp ${DIR}/build/ota_data_initial.bin ${DIR}/firmware/boot_app0.bin
cp ${DIR}/build/bootloader/bootloader.bin \
   ${DIR}/build/ESP32_Sandbox.ino.bin \
   ${DIR}/build/default.bin \
  ${DIR}/firmware/
