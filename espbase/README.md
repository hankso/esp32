# ESP Base project

### NVS partition

Run `python helper.py genid --pack` to generate nvs partition as `build/nvs.bin`.
Run `python helper.py genid --pack --flash COMx` to flash into chip.

+-----+--------+-----------+---------+----------+---------+--------------------------+
| PIN | ALT0   | ALT1      | ALT2    | ALT3     | ALT4    | Comments                 |
+-----+--------+-----------+---------+----------+---------+--------------------------+
| VDD3P3_CPU                                                                         |
+-----+--------+-----------+---------+----------+---------+--------------------------+
| 1 * | U0TXD  | EMAC_RXD2 |         | CLK_OUT3 |         |                          |
| 3 * | U0RXD  |           |         | CLK_OUT2 |         |                          |
| 5 * |        | EMAC_RCLK | VSPICS0 | HS1_DAT6 |         | Strapping                |
| 18  |        |           | VSPICLK | HS1_DAT7 |         |                          |
| 19  | U0CTS  | EMAC_TXD0 | VSPIQ   |          |         |                          |
| 21  |        | EMAC_TXEN | VSPIHD  |          |         |                          |
| 22  | U0RTS  | EMAX_TXD1 | VSPIWP  |          |         |                          |
| 23  |        |           | VSPID   | HS1_STRB |         |                          |
+-----+--------+-----------+---------+----------+---------+--------------------------+
| VDD_SDIO                                                                           |
+-----+--------+-----------+---------+----------+---------+--------------------------+
| 6 * | U1CTS  |           | SPICLK  | HS1_CLK  | SD_CLK  | SPI 0/1 are usually      |
| 7 * | U2RTS  |           | SPIQ    | HS1_DAT0 | SD_DAT0 | connected to the SPI     |
| 8 * | U2CTS  |           | SPID    | HS1_DAT1 | SD_DAT1 | flash and PSRAM          |
| 9 * | U1RXD  |           | SPIHD   | HS1_DAT2 | SD_DAT2 | integrated on the module |
| 10* | U1TXD  |           | SPIWP   | HS1_DAT3 | SD_DAT3 | and therefore should not |
| 11* | U1RTS  |           | SPICS0  | HS1_CMD  | SD_CMD  | be used for other        |
| 16* | U2RXD  | EMAC_COUT |         | HS1_DAT4 |         | purposes.                |
| 17* | U2TXD  | EMAC_COUT |         | HS1_DAT5 |         |                          |
+-----+--------+-----------+---------+----------+---------+------+----------+--------+
| VDD3P3_RTC                                              | NOTE | Analog   | RTC    |
+-----+--------+-----------+---------+----------+---------+------+----------+--------+
| 0 * | TOUCH1 | EMAC_TCLK |         | CLK_OUT1 |         |      | ADC2_CH1 | GPIO11 |
| 2 * | TOUCH2 |           | HSPIWQ  | HS2_DAT0 | SD_DAT0 |      | ADC2_CH2 | GPIO12 |
| 4   | TOUCH0 | EMAC_TXER | HSPIHD  | HS2_DAT1 | SD_DAT1 |      | ADC2_CH0 | GPIO10 |
| 12* | TOUCH5 | EMAC_TXD3 | HSPIQ   | HS2_DAT2 | SD_DAT2 | MTDI | ADC2_CH5 | GPIO15 |
| 13* | TOUCH4 | EMAC_RXER | HSPID   | HS2_DAT3 | SD_DAT3 | MTCK | ADC2_CH4 | GPIO14 |
| 14* | TOUCH6 | EMAC_TXD2 | HSPICLK | HS2_CLK  | SD_CLK  | MTMS | ADC2_CH6 | GPIO16 |
| 15* | TOUCH3 | EMAC_RXD3 | HSPICS0 | HS2_CMD  | SD_CMD  | MTDO | ADC2_CH3 | GPIO13 |
| 25  | DAC_1  | EMAC_RXD0 |         |          |         |      | ADC2_CH8 | GPIO6  |
| 26  | DAC_2  | EMAC_RXD1 |         |          |         |      | ADC2_CH9 | GPIO7  |
| 27  | TOUCH7 | EMAX_RXDV |         |          |         |      | ADC2_CH7 | GPIO17 |
| 32  | TOUCH9 | 32K_XP    |         |          |         |      | ADC1_CH4 | GPIO9  |
| 33  | TOUCH8 | 32K_XN    |         |          |         |      | ADC1_CH5 | GPIO8  |
| 34  |        |           |         |          |         | GPI  | ADC1_CH6 | GPIO4  |
| 35  |        |           |         |          |         | GPI  | ADC1_CH7 | GPIO5  |
| 36  |        |           |         |          |         | GPI  | ADC1_CH0 | GPIO0  |
| 37  |        |           |         |          |         | GPI  | ADC1_CH1 | GPIO1  |
| 38  |        |           |         |          |         | GPI  | ADC1_CH2 | GPIO2  |
| 39  |        |           |         |          |         | GPI  | ADC1_CH3 | GPIO3  |
+-----+--------+-----------+---------+----------+---------+--------------------------+

1. Strapping pin: GPIO0, GPIO2, GPIO5, GPIO12 (MTDI), and GPIO15 (MTDO) are strapping pins.
2. JTAG: GPIO12-15 are usually used for inline debug.
3. GPI: GPIO34-39 can only be set as input mode and do not have software-enabled pullup or pulldown functions.
4. ADC2: ADC2 pins cannot be used when Wi-Fi is used. So, if you are using Wi-Fi and you are having trouble getting the value from an ADC2 GPIO, you may consider using an ADC1 GPIO instead, that should solve your problem.
5. GPIO20 is only available on ESP32-PICO-V3 chip package.
