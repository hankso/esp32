# ESP Base project

### NVS partition

Run `python helper.py genid --pack` to generate nvs partition as `build/nvs.bin`.
Run `python helper.py genid --pack --flash COMx` to flash into chip.

### LED PWM assignment

- LEDC low speed mode
  + Timer 0
    * Channel 0 Mux to PIN_LED
    * 13 bit
    * 5000 Hz
  + Timer 1
    * Channel 1 Mux to PIN_SVOH
    * Channel 2 Mux to PIN_SVOV
    * 10 bit
    * 50 Hz
  + Timer 2
    * Channel 3 Mux to PIN_BUZZ
    * 3 bit

### I2C assignment

- I2C master 0
  + 50 KHz
  + GPIO Expander
    * ADDR 0x20, 0x21, 0x22
  + VLX Sensor
    * ADDR 0x29
  + ALS Sensor
    * ADDR: 0x44, 0x45, 0x46, 0x47
  + GY39 Sensor
    * ADDR 0x5B

- I2C master 1
  + 50 KHz
  + U8G2 Screen
    * ADDR 0x3C

### SPI assignment

- SPI master
  + host 0
    * CS0: SDCard / SDFS
    * CS1: GPIO Expander

### ESP32 GPIO assignment

+-----+--------+-----------+---------+----------+---------+------------------------+
| PIN | ALT0   | ALT1      | ALT2    | ALT3     | ALT4    | Comments               |
+-----+--------+-----------+---------+----------+---------+------------------------+
| VDD_SDIO                                                                         |
+-----+--------+-----------+---------+----------+---------+------------------------+
| 6 * | U1CTS  |           | SPICLK  | HS1_CLK  | SD_CLK  | SPI 0/1 are usually    |
| 7 * | U2RTS  |           | SPIQ    | HS1_DAT0 | SD_DAT0 | connected to the SPI   |
| 8 * | U2CTS  |           | SPID    | HS1_DAT1 | SD_DAT1 | flash and PSRAM        |
| 9 * | U1RXD  |           | SPIHD   | HS1_DAT2 | SD_DAT2 | integrated on the      |
| 10* | U1TXD  |           | SPIWP   | HS1_DAT3 | SD_DAT3 | module and therefore   |
| 11* | U1RTS  |           | SPICS0  | HS1_CMD  | SD_CMD  | should not be used for |
| 16* | U2RXD  | EMAC_COUT |         | HS1_DAT4 |         | other purposes         |
| 17* | U2TXD  | EMAC_COUT |         | HS1_DAT5 |         |                        |
+-----+--------+-----------+---------+----------+---------+------------------------+
| VDD3P3_CPU                                                                       |
+-----+--------+-----------+---------+----------+---------+------------------------+
| 1 * | U0TXD  | EMAC_RXD2 |         | CLK_OUT3 |         |                        |
| 3 * | U0RXD  |           |         | CLK_OUT2 |         |                        |
| 5 * |        | EMAC_RCLK | VSPICS0 | HS1_DAT6 |         |                        |
| 18  |        |           | VSPICLK | HS1_DAT7 |         |                        |
| 19  | U0CTS  | EMAC_TXD0 | VSPIQ   |          |         |                        |
| 21  |        | EMAC_TXEN | VSPIHD  |          |         |                        |
| 22  | U0RTS  | EMAC_TXD1 | VSPIWP  |          |         |                        |
| 23  |        |           | VSPID   | HS1_STRB |         |                        |
+-----+--------+-----------+---------+----------+---------+------------------------+
| VDD3P3_RTC                                              | NOTE | Analog   | RTC  |
+-----+--------+-----------+---------+----------+---------+------+----------+------+
| 0 * | TOUCH1 | EMAC_TCLK |         | CLK_OUT1 |         |      | ADC2_CH1 | IO11 |
| 2 * | TOUCH2 |           | HSPIWQ  | HS2_DAT0 | SD_DAT0 |      | ADC2_CH2 | IO12 |
| 4   | TOUCH0 | EMAC_TXER | HSPIHD  | HS2_DAT1 | SD_DAT1 |      | ADC2_CH0 | IO10 |
| 12* | TOUCH5 | EMAC_TXD3 | HSPIQ   | HS2_DAT2 | SD_DAT2 | MTDI | ADC2_CH5 | IO15 |
| 13* | TOUCH4 | EMAC_RXER | HSPID   | HS2_DAT3 | SD_DAT3 | MTCK | ADC2_CH4 | IO14 |
| 14* | TOUCH6 | EMAC_TXD2 | HSPICLK | HS2_CLK  | SD_CLK  | MTMS | ADC2_CH6 | IO16 |
| 15* | TOUCH3 | EMAC_RXD3 | HSPICS0 | HS2_CMD  | SD_CMD  | MTDO | ADC2_CH3 | IO13 |
| 25  | DAC_1  | EMAC_RXD0 |         |          |         |      | ADC2_CH8 | IO6  |
| 26  | DAC_2  | EMAC_RXD1 |         |          |         |      | ADC2_CH9 | IO7  |
| 27  | TOUCH7 | EMAC_RXDV |         |          |         |      | ADC2_CH7 | IO17 |
| 32  | TOUCH9 | 32K_XP    |         |          |         |      | ADC1_CH4 | IO9  |
| 33  | TOUCH8 | 32K_XN    |         |          |         |      | ADC1_CH5 | IO8  |
| 34  |        | VDET_1    |         |          |         | GPI  | ADC1_CH6 | IO4  |
| 35  |        | VDET_2    |         |          |         | GPI  | ADC1_CH7 | IO5  |
| 36  |        | SENSOR_VP |         |          |         | GPI  | ADC1_CH0 | IO0  |
| 37  |        | SENSOR_CP |         |          |         | GPI  | ADC1_CH1 | IO1  |
| 38  |        | SENSOR_CN |         |          |         | GPI  | ADC1_CH2 | IO2  |
| 39  |        | SENSOR_VN |         |          |         | GPI  | ADC1_CH3 | IO3  |
+-----+--------+-----------+---------+----------+---------+------+----------+------+

1. Strapping pin: GPIO0, GPIO2, GPIO5, GPIO12 (MTDI), and GPIO15 (MTDO) are strapping pins.
2. JTAG: GPIO12-15 are usually used for inline debug.
3. GPI: GPIO34-39 can only be set as input mode and do not have software-enabled pullup or pulldown functions.
4. GPIO1 & GPIO3 are usually used for flashing and debugging.
5. ADC2: ADC2 pins cannot be used when Wi-Fi is used. So, if you are using Wi-Fi and you are having trouble getting the value from an ADC2 GPIO, you may consider using an ADC1 GPIO instead, that should solve your problem.
6. GPIO20 is only available on ESP32-PICO-V3 chip package.

### ESP32-S3 GPIO assignment

+-----+---------+-----------+---------+----------+----------+------+----------+
| PIN | ALT0    | ALT1      | ALT2    | ALT3     | Analog   | RTC  | Comments |
+-----+---------+-----------+---------+----------+----------+------+----------+
| VDD3P3_RTC                                                                  |
+-----+---------+-----------+---------+----------+----------+------+----------+
| 0 * |         |           |         |          |          | IO0  |          |
| 1   | TOUCH1  |           |         |          | ADC1_CH0 | IO1  |          |
| 2   | TOUCH2  |           |         |          | ADC1_CH1 | IO2  |          |
| 3 * | TOUCH3  |           |         |          | ADC1_CH2 | IO3  |          |
| 4   | TOUCH4  |           |         |          | ADC1_CH3 | IO4  |          |
| 5   | TOUCH5  |           |         |          | ADC1_CH4 | IO5  |          |
| 6   | TOUCH6  |           |         |          | ADC1_CH5 | IO6  |          |
| 7   | TOUCH7  |           |         |          | ADC1_CH6 | IO7  |          |
| 8   | TOUCH8  | ASPICS1   |         |          | ADC1_CH7 | IO8  |          |
| 9   | TOUCH9  | ASPIHD    |         | FSPIHD   | ADC1_CH8 | IO9  |          |
| 10  | TOUCH10 | ASPICS0   | FSPIIO4 | FSPICS0  | ADC1_CH9 | IO10 |          |
| 11  | TOUCH11 | ASPID     | FSPIIO5 | FSPID    | ADC2_CH0 | IO11 |          |
| 12  | TOUCH12 | ASPICLK   | FSPIIO6 | FSPICLK  | ADC2_CH1 | IO12 |          |
| 13  | TOUCH13 | ASPIQ     | FSPIIO7 | FSPIQ    | ADC2_CH2 | IO13 |          |
| 14  | TOUCH14 | ASPIWP    | FSPIDQS | FSPIWP   | ADC2_CH3 | IO14 |          |
| 15  | 32K_XP  |           | U0RTS   |          | ADC2_CH4 | IO15 |          |
| 16  | 32K_XN  |           | U0CTS   |          | ADC2_CH5 | IO16 |          |
| 17  |         |           | U1TXD   |          | ADC2_CH6 | IO17 |          |
| 18  |         |           | U1RXD   | CLK_OUT3 | ADC2_CH7 | IO18 |          |
| 19* | USB_DN  |           | U1RTS   | CLK_OUT2 | ADC2_CH8 | IO19 |          |
| 20* | USB_DP  |           | U1CTS   | CLK_OUT1 | ADC2_CH9 | IO20 |          |
| 21  |         |           |         |          |          | IO21 |          |
+-----+---------+-----------+---------+----------+----------+------+----------+
| VDD_SPI                                                                     |
+-----+---------+-----------+---------+----------+----------------------------+
| 26* |         |           | SPICS1  |          | SPI 0/1: GPIO26-32 are     |
| 27* |         |           | SPIHD   |          | usually used for SPI flash |
| 28* |         |           | SPIWP   |          | and PSRAM and not          |
| 29* |         |           | SPICS0  |          | recommended for other uses |
| 30* |         |           | SPICLK  |          |                            |
| 31* |         |           | SPIQ    |          |                            |
| 32* |         |           | SPID    |          |----------------------------|
| 47  |         | ASPICLKN  | SPICLKN |          |                            |
| 48  |         | ASPICLKP  | SPICLKP |          |                            |
+-----+---------+-----------+---------+----------+----------------------------+
| VDD3P3_CPU                                                                  |
+-----+---------+-----------+---------+----------+----------------------------+
| 33* |         | ASPIHD    | SPIIO4  | FSPIHD   | When using Octal Flash or  |
| 34* |         | ASPICS0   | SPIIO5  | FSPICS0  | Octal PSRAM or both,       |
| 35* |         | ASPID     | SPIIO6  | FSPID    | GPIO33-37 are connected to |
| 36* |         | ASPICLK   | SPIIO7  | FSPICLK  | SPIIO4-7 and SPIDQS        |
| 37* |         | ASPIQ     | SPIDQS  | FSPIQ    |----------------------------|
| 38  |         | ASPIWP    |         | FSPIWP   |                            |
| 39  | MTCK    | ASPICS1   |         | CLK_OUT3 |                            |
| 40  | MTDO    |           |         | CLK_OUT2 |                            |
| 41  | MTDI    |           |         | CLK_OUT1 |                            |
| 42  | MTMS    |           |         |          |                            |
| 43  | U0TXD   |           |         | CLK_OUT1 |                            |
| 44  | U0RXD   |           |         | CLK_OUT2 |                            |
| 45* |         |           |         |          |                            |
| 46* |         |           |         |          |                            |
+-----+---------+-----------+---------+----------+----------------------------+

1. Strapping pin: GPIO0, GPIO3, GPIO45 and GPIO46 are strapping pins.
2. USB-JTAG: GPIO19 and GPIO20 are used by USB-JTAG by default.
