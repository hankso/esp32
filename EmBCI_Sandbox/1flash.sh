esptool.py \
    --chip esp32 \
    --port /dev/ttyUSB0 \
    --baud 921600 \
    --before default_reset \
    --after hard_reset \
    write_flash -z \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size detect \
    0x1000 ./build/bootloader/bootloader.bin \
    0x8000 ./build/default.bin \
    0xe000 ./build/ota_data_initial.bin \
    0x10000 ./build/ESP32_Sandbox.ino.bin
