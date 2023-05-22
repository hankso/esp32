/*************************************************************************
File: spi_max6675.h
Author: Hankso
Webpage: http://github.com/hankso
Time: Mon 27 May 2019 15:47:10 CST
************************************************************************/

#ifndef _spi_max6675_h_
#define _spi_max6675_h_

#include "globals.h"
#include "soc/spi_pins.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"

/*
 * HSPI Pin Number
 *  MISO - GPIO12
 *  MOSI - GPIO13 (not used)
 *  SCLK - GPIO14
 *  CS1  - GPIO15
 *  CS2  - GPIO33
 *  CS3  - GPIO32
 */
spi_bus_config_t hspi_buscfg = {
    .mosi_io_num = -1,
    .miso_io_num = HSPI_IOMUX_PIN_NUM_MISO,
    .sclk_io_num = HSPI_IOMUX_PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 0,
    .flags = SPICOMMON_BUSFLAG_MASTER,
};

uint8_t HSPI_IOMUX_PIN_NUM_CSn[3] = {15, 25, 32};

/*
 * VSPI Pin Number
 *  MISO - GPIO19
 *  MOSI - GPIO23 (not used)
 *  SCLK - GPIO18
 *  CS4  - GPIO5
 *  CS5  - GPIO17
 *  CS6  - GPIO23
 */
spi_bus_config_t vspi_buscfg = {
    .mosi_io_num = -1,
    .miso_io_num = VSPI_IOMUX_PIN_NUM_MISO,
    .sclk_io_num = VSPI_IOMUX_PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 0,
    .flags = SPICOMMON_BUSFLAG_MASTER,
};

uint8_t VSPI_IOMUX_PIN_NUM_CSn[3] = {5, 17, 23};


spi_device_interface_config_t devcfg = {
    .command_bits = 0,
    .address_bits = 0,
    .dummy_bits = 0,
    .mode = 0,  // SPI Mode: CPOL=0, CPHA=0
    .duty_cycle_pos = 128,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = 4000000,  // SPI Freq: 4MHz
    .input_delay_ns = 0,
    .spics_io_num = 0,  // not allocated yet
    .flags = 0,
    .queue_size = 7,  // max: 6 devices + 1
    .pre_cb = NULL,
    .post_cb = NULL,
};

spi_device_handle_t _hdlrs[6];
spi_transaction_t _trans;

void spi_max6675_init() {
    spi_bus_initialize(HSPI_HOST, &hspi_buscfg, 1);
    spi_bus_initialize(VSPI_HOST, &vspi_buscfg, 2);
    for (int i = 0; i < 3; i++) {
        devcfg.spics_io_num = HSPI_IOMUX_PIN_NUM_CSn[i];
        spi_bus_add_device(HSPI_HOST, &devcfg, &_hdlrs[i]);
        devcfg.spics_io_num = VSPI_IOMUX_PIN_NUM_CSn[i];
        spi_bus_add_device(VSPI_HOST, &devcfg, &_hdlrs[i + 3]);
    }
    memset(&_trans, 0, sizeof(_trans));
    _trans.flags = SPI_TRANS_USE_RXDATA;
    _trans.length = 16;  // sizeof(recv_buff) * sizeof(recv_buff[0]);
}

/*
 * Data received from MAX6675
 *
 * +-----------+-------------+----------+-----------+--------+
 * | Dummy Bit | 12bits TEMP | T-Input  | Device ID | State  |
 * +-----------+-------------+----------+-----------+--------+
 * | D15 = 0   | D14 - D3    | D2 = 1/0 | D1 = 0    | D0 = ? |
 * +-----------+-------------+----------+-----------+--------+
 *
 */
void spi_max6675_read() {
    uint16_t temp;
    for (int i = 0; i < 6; i++) {
        spi_device_transmit(_hdlrs[i], &_trans);
        temp = ((uint16_t)_trans.rx_data[0] << 5) | (_trans.rx_data[1] >> 3);
        temp_value[i] = (temp & 0xfff) / 4;
    }
}

#endif // _spi_max6675_h_
