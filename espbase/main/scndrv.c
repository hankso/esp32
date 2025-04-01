/*
 * File: scndrv.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/25 12:25:37
 */

#include "screen.h"
#include "drivers.h"

#ifdef CONFIG_BASE_USE_SCREEN

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#if defined(WITH_LVGL)
#   include "esp_lvgl_port.h"
#elif defined(WITH_U8G2)
#   include "u8g2.h"
#endif

#ifdef CONFIG_BASE_SCREEN_I2C
#   define SCREEN_H_RES    128
#   define SCREEN_V_RES    64
#   define SCREEN_DEPTH    1
#else
#   define SCREEN_H_RES    240
#   define SCREEN_V_RES    240
#   define SCREEN_DEPTH    CONFIG_LV_COLOR_DEPTH
#endif
#define SCREEN_PIXELS   ( SCREEN_H_RES * SCREEN_V_RES )

static const char *TAG = "Screen";

static struct {
    bool probed;
    int bus, addr, speed, mode;
    gpio_num_t sda, scl;        // for I2C
    gpio_num_t cs, dc, rst;     // for SPI
#if defined(WITH_U8G2)
    u8g2_t hdl;
#else
    esp_lcd_panel_handle_t hdl;
    esp_lcd_panel_io_handle_t io;
#   if defined(WITH_LVGL)
    lv_display_t *disp;
#   endif
#endif
} ctx;

#ifdef WITH_U8G2
// HAL callback function as prescribed by the U8G2 library.
static uint8_t u8g2_gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr) {
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT: {
        gpio_config_t conf = {
            .pin_bit_mask   = 0,
            .mode           = GPIO_MODE_OUTPUT,
            .pull_up_en     = GPIO_PULLUP_DISABLE,
            .pull_down_en   = GPIO_PULLDOWN_ENABLE,
            .intr_type      = GPIO_INTR_DISABLE,
        };
        if (ctx.cs != GPIO_NUM_NC)  conf.pin_bit_mask |= BIT(ctx.cs);
        if (ctx.dc != GPIO_NUM_NC)  conf.pin_bit_mask |= BIT(ctx.dc);
        if (ctx.rst != GPIO_NUM_NC) conf.pin_bit_mask |= BIT(ctx.rst);
        if (conf.pin_bit_mask) return gpio_config(&conf);
    }   break;
    case U8X8_MSG_GPIO_CS:
        if (ctx.cs != GPIO_NUM_NC)  { gpio_set_level(ctx.cs, arg); } break;
    case U8X8_MSG_GPIO_RESET:
        if (ctx.rst != GPIO_NUM_NC) { gpio_set_level(ctx.rst, arg); } break;
    case U8X8_MSG_GPIO_I2C_CLOCK:
        if (ctx.scl != GPIO_NUM_NC) { gpio_set_level(ctx.scl, arg); } break;
    case U8X8_MSG_GPIO_I2C_DATA:
        if (ctx.sda != GPIO_NUM_NC) { gpio_set_level(ctx.sda, arg); } break;
    case U8X8_MSG_DELAY_MILLI: msleep(arg); break;
    }
    return 0;
}

#   ifdef CONFIG_BASE_SCREEN_I2C
static uint8_t u8g2_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr) {
    static i2c_cmd_handle_t cmd;
    switch (msg) {
    case U8X8_MSG_BYTE_SET_DC:
        if (ctx.dc != GPIO_NUM_NC) { gpio_set_level(ctx.dc, arg); } break;
    case U8X8_MSG_BYTE_START_TRANSFER: {
        esp_err_t err = i2c_master_start(cmd = i2c_cmd_link_create());
        if (err) return err;
        i2c_master_write_byte(cmd, ctx.addr << 1 | I2C_MASTER_WRITE, true);
    }   break;
    case U8X8_MSG_BYTE_SEND: i2c_master_write(cmd, ptr, arg, true); break;
    case U8X8_MSG_BYTE_END_TRANSFER: {
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(ctx.bus, cmd, TIMEOUT(100));
        i2c_cmd_link_delete(cmd);
        return err;
    }
    }
    return 0;
}
#   else // CONFIG_BASE_SCREEN_I2C
static uint8_t u8g2_spi_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr) {
    static spi_device_handle_t spi_hdl;
    switch (msg) {
    case U8X8_MSG_BYTE_SET_DC:
        if (ctx.dc != GPIO_NUM_NC) { gpio_set_level(ctx.dc, arg); } break;
    case U8X8_MSG_BYTE_INIT: {
        const spi_device_interface_config_t conf = {
            .mode           = ctx.mode,
            .clock_speed_hz = ctx.speed,
            .spics_io_num   = ctx.cs,
            .queue_size     = 100,
        };
        return spi_bus_add_device(ctx.bus, &conf, &spi_hdl);
    }
    case U8X8_MSG_BYTE_SEND: {
        spi_transaction_t trans = { .length = 8 * arg, .tx_buffer = ptr };
        return spi_device_transmit(spi_hdl, &trans);
    }
    }
    return 0;
}
#   endif // CONFIG_BASE_SCREEN_I2C

static void screen_u8g2_init() {
#   ifdef CONFIG_BASE_SCREEN_I2C
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &ctx.hdl, U8G2_R0, u8g2_i2c_cb, u8g2_gpio_cb);
#   else
    u8g2_Setup_ssd1306_128x64_noname_f(
        &ctx.hdl, U8G2_R0, u8g2_spi_cb, u8g2_gpio_cb);
#   endif
    u8g2_SetFont(&ctx.hdl, u8g2_font_helvB08_tr);
    u8g2_InitDisplay(&ctx.hdl);
    u8g2_SetPowerSave(&ctx.hdl, 0);
    u8g2_SendBuffer(&ctx.hdl);
}

static esp_err_t u8g2_ui_cmd(scn_cmd_t cmd, const char *arg) {
    if (cmd == SCN_PBAR) {
        static char buf[16];
        static int ys = SCREEN_V_RES * 7 / 16, ye = SCREEN_V_RES * 9 / 16;
        snprintf(buf, sizeof(buf), "%d %%", MIN(*(uint8_t *)arg, 100));
        int x = SCREEN_H_RES * CONS(*(int *)arg, 0, 100) / 100;
        int middle = MAX(0, SCREEN_H_RES - u8g2_GetStrWidth(&ctx.hdl, buf)) / 2;
        u8g2_ClearBuffer(&ctx.hdl);
        u8g2_DrawFrame(&ctx.hdl, 0, ys, SCREEN_H_RES, ye - ys);
        u8g2_DrawBox(&ctx.hdl, 0, ys, x, ye - ys);
        u8g2_DrawStr(&ctx.hdl, middle, ye + 10, buf);
        u8g2_SendBuffer(&ctx.hdl);
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

#else // WITH_U8G2

static void screen_lvgl_init() {
    esp_err_t err = ESP_OK;
    const esp_lcd_panel_dev_config_t dev_conf = {
        .reset_gpio_num = ctx.rst,
        .bits_per_pixel = SCREEN_DEPTH,
        .color_space    = SCREEN_DEPTH == 1
                            ? ESP_LCD_COLOR_SPACE_MONOCHROME
                            : ESP_LCD_COLOR_SPACE_RGB,
    };
#   ifdef CONFIG_BASE_SCREEN_I2C
    const esp_lcd_panel_io_i2c_config_t io_conf = {
        .dev_addr               = ctx.addr,
        .control_phase_bytes    = 1,
        .dc_bit_offset          = 6,
        .lcd_cmd_bits           = 8,
        .lcd_param_bits         = 8,
    };
    esp_lcd_i2c_bus_handle_t _bus = (esp_lcd_i2c_bus_handle_t)ctx.bus;
    if (!err) err = esp_lcd_new_panel_io_i2c(_bus, &io_conf, &ctx.io);
    if (!err) err = esp_lcd_new_panel_ssd1306(ctx.io, &dev_conf, &ctx.hdl);
#   else
    const esp_lcd_panel_io_spi_config_t io_conf = {
        .dc_gpio_num        = ctx.dc,
        .cs_gpio_num        = ctx.cs,
        .spi_mode           = ctx.mode,
        .pclk_hz            = ctx.speed,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .trans_queue_depth  = 1,
    };
    esp_lcd_spi_bus_handle_t _bus = (esp_lcd_spi_bus_handle_t)ctx.bus;
    if (!err) err = esp_lcd_new_panel_io_spi(_bus, &io_conf, &ctx.io);
    if (!err) err = esp_lcd_new_panel_st7789(ctx.io, &dev_conf, &ctx.hdl);
#   endif
    if (!err) err = esp_lcd_panel_reset(ctx.hdl);
    if (!err) err = esp_lcd_panel_init(ctx.hdl);
    if (!err) err = esp_lcd_panel_mirror(ctx.hdl, true, true);

    // we can draw startup logo here before we turn on the screen
#   if SCREEN_DEPTH == 1
    const uint8_t pattern[32] = {
        0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00, // square
        0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00,
        0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81, // cross
        0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
    };
    LOOPN(i, err ? 0 : (SCREEN_H_RES / 16)) {
        LOOPN(j, err ? 0 : (SCREEN_V_RES / 8)) {
            err = esp_lcd_panel_draw_bitmap(
                ctx.hdl, i * 16, j * 8,
                i * 16 + 16, j * 8 + 8,
                pattern + ((i & 1) * 16));
        }
    }
#   else
    uint8_t cbuf[16 * 16 * SCREEN_DEPTH / 8];
    memset(cbuf, 0xAA, sizeof(cbuf));
    LOOPN(i, err ? 0 : (SCREEN_H_RES / 16)) {
        LOOPN(j, err ? 0 : (SCREEN_V_RES / 16)) {
            err = esp_lcd_panel_draw_bitmap(
                ctx.hdl, i * 16, j * 16, (i + 1) * 16, (j + 1) * 16, cbuf);
        }
    }
#   endif

    if (!err) err = esp_lcd_panel_disp_on_off(ctx.hdl, true);

#   ifdef WITH_LVGL
#       ifdef CONFIG_FREERTOS_UNICORE
    const lvgl_port_cfg_t lvgl_conf = ESP_LVGL_PORT_INIT_CONFIG();
#       else
    lvgl_port_cfg_t lvgl_conf = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_conf.task_affinity = 1; // run LVGL task on CPU1 if possible
    lvgl_conf.task_stack = 8192; // 8k stack size
#       endif
    const lvgl_port_display_cfg_t disp_conf = {
        .io_handle = ctx.io,
        .panel_handle = ctx.hdl,
        .buffer_size = SCREEN_PIXELS / (SCREEN_DEPTH == 1 ? 1 : 4),
        .double_buffer = true,
        .hres = SCREEN_H_RES,
        .vres = SCREEN_V_RES,
        .monochrome = SCREEN_DEPTH == 1,
#       if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#       endif
        .rotation = {           // must be same as esp_lcd_panel_mirror
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
#       if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = SCREEN_DEPTH == 16,
#       endif
        },
    };
    if (!err && !( err = lvgl_port_init(&lvgl_conf) )) {
        if (!( ctx.disp = lvgl_port_add_disp(&disp_conf) )) {
            err = ESP_FAIL;
        } else {
            screen_command(SCN_INIT, ctx.disp);
        }
    }
#   endif // WITH_LVGL
    if (err) {
        ctx.probed = false;
        TRYNULL(ctx.hdl, esp_lcd_panel_del);
#   ifdef WITH_LVGL
        TRYNULL(ctx.disp, lvgl_port_remove_disp);
#   endif
    }
}

int lvgl_ui_cmd(scn_cmd_t cmd, const char *arg); // defined in scnlvgl.c

#endif // WITH_U8G2

void screen_initialize() {
    if (ctx.probed) return;
    ctx.sda = ctx.scl = ctx.cs = ctx.dc = ctx.rst = GPIO_NUM_NC;
#if defined(CONFIG_BASE_SCREEN_SPI)             // SPI Screen
    ctx.speed = CONFIG_BASE_SCREEN_SPI_SPEED;
    ctx.mode = CONFIG_BASE_SCREEN_SPI_MODE;
    ctx.bus = NUM_SPI;
    ctx.rst = PIN_SRST;
    ctx.dc = PIN_SDC;
    ctx.cs = PIN_CS1;
    ctx.probed = true;
#elif !defined(CONFIG_BASE_SCREEN_I2C_ALT)      // Screen using NUM_I2C
    ctx.speed = CONFIG_BASE_I2C_SPEED;
    ctx.addr = CONFIG_BASE_SCREEN_I2C_ADDR;
    ctx.bus = NUM_I2C;  // already initialized
    ctx.sda = PIN_SDA;
    ctx.scl = PIN_SCL;
    ctx.probed = smbus_probe(ctx.bus, ctx.addr) == ESP_OK;
#else                                           // Screen using dedicated I2C
    ctx.speed = CONFIG_BASE_SCREEN_I2C_SPEED;
    ctx.addr = CONFIG_BASE_SCREEN_I2C_ADDR;
    if (NUM_I2C == I2C_NUM_0) {
        ctx.bus = I2C_NUM_1;
        ctx.sda = PIN_SDA1;
        ctx.scl = PIN_SCL1;
    } else {
        ctx.bus = I2C_NUM_0;
        ctx.sda = PIN_SDA0;
        ctx.scl = PIN_SCL0;
    }
    const i2c_config_t i2c_conf = {
        .mode               = I2C_MODE_MASTER,
        .sda_io_num         = ctx.sda,
        .sda_pullup_en      = GPIO_PULLUP_ENABLE,
        .scl_io_num         = ctx.scl,
        .scl_pullup_en      = GPIO_PULLUP_ENABLE,
        .master.clk_speed   = ctx.speed,
    };
    esp_err_t err = i2c_param_config(ctx.bus, &i2c_conf);
    if (!err) err = i2c_driver_install(ctx.bus, I2C_MODE_MASTER, 0, 0, 0);
    if (!err) ctx.probed = smbus_probe(ctx.bus, ctx.addr) == ESP_OK;
#endif // CONFIG_BASE_SCREEN_SPI

    if (!ctx.probed) return;
#ifdef WITH_U8G2
    screen_u8g2_init();
#else
    screen_lvgl_init();
#endif
    if (!ctx.probed) ESP_LOGE(TAG, "Screen initialize failed");
}

void screen_status() {
    if (!ctx.probed) {
        puts("Screen not probed");
        return;
    }
    printf("Using Screen %dx%d %dbpp at ",
           SCREEN_H_RES, SCREEN_V_RES, SCREEN_DEPTH);
#ifdef CONFIG_BASE_SCREEN_I2C
    printf("I2C%d-0x%02X %dKHz SDA:%d SCL:%d",
           ctx.bus, ctx.addr, ctx.speed / 1000, ctx.sda, ctx.scl);
#else
    bool mhz = ctx.speed > 1000000;
    printf("SPI%d %d%cHz CS:%d DC:%d RST:%d",
           ctx.bus, ctx.speed / 1000 / (mhz ? 1000 : 1),
           "KM"[mhz], ctx.cs, ctx.dc, ctx.rst);
#endif
#if defined(WITH_U8G2)
    puts(" (U8G2)");
#elif defined(WITH_LVGL)
    puts(" (LVGL)");
    screen_command(SCN_STAT, NULL);
#else
    puts(" (ESP_LCD)");
#endif
}

esp_err_t screen_command(scn_cmd_t cmd, const void *arg) {
    if (!ctx.probed) return ESP_ERR_NOT_FOUND;
#if defined(WITH_U8G2)
    return u8g2_ui_cmd(cmd, arg);
#elif defined(WITH_LVGL)
    if (!ctx.disp) return ESP_ERR_INVALID_STATE;
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    esp_err_t err = lvgl_ui_cmd(cmd, arg);
    lvgl_port_unlock();
    return err;
#else
    if (cmd == SCN_PBAR) {
#   define XS ( 8 )
#   define XE ( SCREEN_H_RES - 8 )
#   define YS ( SCREEN_V_RES * 7 / 16 )
#   define YE ( SCREEN_V_RES * 9 / 16 )
        esp_err_t err = ESP_OK;
        static uint8_t cbuf[(XE - XS) * (YE - YS) * SCREEN_DEPTH / 8];
        int x = XS + (XE - XS) * CONS(*(int*)arg, 0, 100) / 100;
        if (!err && x > XS) {
            memset(cbuf, 0xFF, sizeof(cbuf)); // foreground
            err = esp_lcd_panel_draw_bitmap(ctx.hdl, XS, YS, x, YE, cbuf);
        }
        if (!err && x < XE) {
            memset(cbuf, 0x00, sizeof(cbuf)); // background
            err = esp_lcd_panel_draw_bitmap(ctx.hdl, x, YS, XE, YE, cbuf);
        }
        return err;
    } else if (cmd == SCN_ROT) {
        switch (*(int *)arg) {
        case 0:     // 0deg
            esp_lcd_panel_swap_xy(ctx.hdl, false);
            esp_lcd_panel_mirror(ctx.hdl, true, true);
            break;
        case 1:     // 90deg
            esp_lcd_panel_swap_xy(ctx.hdl, true);
            esp_lcd_panel_mirror(ctx.hdl, true, false);
            break;
        case 2:     // 180deg
            esp_lcd_panel_swap_xy(ctx.hdl, false);
            esp_lcd_panel_mirror(ctx.hdl, false, false);
            break;
        case 3:     // 270deg
            esp_lcd_panel_swap_xy(ctx.hdl, true);
            esp_lcd_panel_mirror(ctx.hdl, false, true);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
#endif // WITH_U8G2
}
#else // CONFIG_BASE_USE_SCREEN
void screen_initialize() {}
void screen_status() { puts("Screen not enabled"); }
esp_err_t screen_command(scn_cmd_t c, const void *a) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(a);
}
#endif // CONFIG_BASE_USE_SCREEN
