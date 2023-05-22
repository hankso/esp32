## How to use ESP-IDF
To start a new project, create a folder by coping ``get-started`` example project, which contains Makefile configured to use ESD-IDF and CMake build system. It's suggested to make a soft link to the external components folder, in which you can find some libraries for easier usage with file system, like SPIFFS/FATFS.

```bash
cp -av get-started/esp-idf NewProject && cd NewProject
[ -d ../../components ] || ln -s ../../components .
```

## How to use Arduino-ESP32
Arduino core for ESP32 is under ``../arduino-esp32``. It's a package layer based on ESP-IDF, making programing on ESP32 much more easier. Arduino-ESP32 provides plenty of useful functions and tricks like ``millis``, ``lowByte``, ``bitRead``, and etc. External libraries will be stored under ``../arduino-esp32/libraries/``.

```bash
cp -av get-started/arduino NewProject && cd NewProject
```
