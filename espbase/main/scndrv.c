/*
 * File: scndrv.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/25 12:25:37
 */

#include "screen.h"
#include "drivers.h"            // for I2C_XXX && PIN_XXX

#ifndef CONFIG_BASE_USE_SCN
void scn_initialize() {}
esp_err_t scn_command(scn_cmd_t c, const void *a) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(a);
}
#else // CONFIG_BASE_USE_SCN

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#if defined(WITH_LVGL)
#   include "esp_lvgl_port.h"
#elif defined(WITH_U8G2)
#   include "u8g2.h"
#endif

#ifdef WITH_LVGL
#   define SCREEN_DEPTH CONFIG_LV_COLOR_DEPTH   // bit per pixel
#else
#   define SCREEN_DEPTH 1                       // u8g2 only support monochrome
#endif
#define SCREEN_WIDTH    CONFIG_BASE_SCN_HRES
#define SCREEN_HEIGHT   CONFIG_BASE_SCN_VRES
#define SCREEN_PIXELS   ( SCREEN_WIDTH * SCREEN_HEIGHT )

static const char *TAG = "Screen";

static struct {
    bool probed;
    int axes[7];                // invc, invx, invy, swap, gapx, gapy, rot
    int bus, addr, speed, mode;
    gpio_num_t sda, scl;        // for I2C
    union PACKED {
        struct PACKED {
            int bl, rst;
            int cs, dc;         // for SPI
            int wr, rd;         // for I80
        };
        int pins[6 + SOC_LCD_I80_BUS_WIDTH];
    };
#if defined(WITH_U8G2)
    u8g2_t hdl;
#else
    esp_lcd_panel_handle_t hdl;
    esp_lcd_panel_io_handle_t io;
#   ifdef CONFIG_BASE_SCN_I80
    esp_lcd_i80_bus_handle_t bhdl;
#   endif
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
        if (ctx.cs != GPIO_NUM_NC)  gpio_set_level(ctx.cs, arg); break;
    case U8X8_MSG_GPIO_RESET:
        if (ctx.rst != GPIO_NUM_NC) gpio_set_level(ctx.rst, arg); break;
    case U8X8_MSG_GPIO_I2C_CLOCK:
        if (ctx.scl != GPIO_NUM_NC) gpio_set_level(ctx.scl, arg); break;
    case U8X8_MSG_GPIO_I2C_DATA:
        if (ctx.sda != GPIO_NUM_NC) gpio_set_level(ctx.sda, arg); break;
    case U8X8_MSG_DELAY_MILLI: msleep(arg); break;
    }
    return 0;
}

#   if defined(CONFIG_BASE_SCN_I2C)
static uint8_t u8g2_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr) {
    if (msg == U8X8_MSG_BYTE_SET_DC){
        if (ctx.dc != GPIO_NUM_NC) gpio_set_level(ctx.dc, arg);
    } else if (msg == U8X8_MSG_BYTE_SEND) {
        if (i2c_wtrd(ctx.bus, ctx.addr, ptr, arg, NULL, 0)) return 1;
    }
    return 0;
}
#   elif defined(CONFIG_BASE_SCN_SPI)
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
#   endif // CONFIG_BASE_SCN_I2C

static void scn_u8g2_init() {
#   if defined(CONFIG_BASE_SCN_I2C)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &ctx.hdl, U8G2_R0, u8g2_i2c_cb, u8g2_gpio_cb);
#   elif defined(CONFIG_BASE_SCN_SPI)
    u8g2_Setup_ssd1306_128x64_noname_f(
        &ctx.hdl, U8G2_R0, u8g2_spi_cb, u8g2_gpio_cb);
#   else
    ctx.probed = false;
    return;
#   endif
    u8g2_SetFont(&ctx.hdl, u8g2_font_helvB08_tr);
    u8g2_InitDisplay(&ctx.hdl);
    u8g2_SetPowerSave(&ctx.hdl, 0);
    u8g2_SendBuffer(&ctx.hdl);
}

static esp_err_t u8g2_ui_cmd(scn_cmd_t cmd, const char *arg) {
    if (cmd == SCN_PBAR) {
        static char buf[16];
        static int YS = SCREEN_HEIGHT * 7 / 16, YE = SCREEN_HEIGHT * 9 / 16;
        snprintf(buf, sizeof(buf), "%d %%", MIN(*(uint8_t *)arg, 100));
        int x = SCREEN_WIDTH * CONS(*(int *)arg, 0, 100) / 100;
        int middle = MAX(0, SCREEN_WIDTH - u8g2_GetStrWidth(&ctx.hdl, buf)) / 2;
        u8g2_ClearBuffer(&ctx.hdl);
        u8g2_DrawFrame(&ctx.hdl, 0, YS, SCREEN_WIDTH, YE - YS);
        u8g2_DrawBox(&ctx.hdl, 0, YS, x, YE - YS);
        u8g2_DrawStr(&ctx.hdl, middle, YE + 10, buf);
        u8g2_SendBuffer(&ctx.hdl);
    } else if (cmd != SCN_STAT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

#else // WITH_U8G2

static void scn_lvgl_init() {
    esp_err_t err = ESP_OK;
    const esp_lcd_panel_dev_config_t dev_config = {
        .reset_gpio_num = ctx.rst,
        .bits_per_pixel = SCREEN_DEPTH,
        .color_space    = SCREEN_DEPTH == 1
                            ? ESP_LCD_COLOR_SPACE_MONOCHROME
                            : ESP_LCD_COLOR_SPACE_RGB,
    };
#   if defined(CONFIG_BASE_SCN_I2C)
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr               = ctx.addr,
        .control_phase_bytes    = 1,
        .dc_bit_offset          = 6,
        .lcd_cmd_bits           = 8,
        .lcd_param_bits         = 8,
    };
    esp_lcd_i2c_bus_handle_t _bus = (esp_lcd_i2c_bus_handle_t)ctx.bus;
    if (!err) err = esp_lcd_new_panel_io_i2c(_bus, &io_config, &ctx.io);
    if (!err) err = esp_lcd_new_panel_ssd1306(ctx.io, &dev_config, &ctx.hdl);
    if (!err) err = esp_lcd_panel_mirror(ctx.hdl, true, true);
#   elif defined(CONFIG_BASE_SCN_SPI)
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num        = ctx.dc,
        .cs_gpio_num        = ctx.cs,
        .spi_mode           = ctx.mode,
        .pclk_hz            = ctx.speed,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .trans_queue_depth  = 1,
    };
    esp_lcd_spi_bus_handle_t _bus = (esp_lcd_spi_bus_handle_t)ctx.bus;
    if (!err) err = esp_lcd_new_panel_io_spi(_bus, &io_config, &ctx.io);
    if (!err) err = esp_lcd_new_panel_st7789(ctx.io, &dev_config, &ctx.hdl);
#   elif defined(CONFIG_BASE_SCN_I80)
    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num        = ctx.cs,
        .pclk_hz            = ctx.speed,
        .trans_queue_depth  = 20,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .dc_levels = {
            .dc_idle_level  = 0,
            .dc_cmd_level   = 0,
            .dc_dummy_level = 0,
            .dc_data_level  = 1,
        },
    };
    if (!err) gexp_set_level(ctx.rd, true);
    if (!err) err = esp_lcd_new_panel_io_i80(ctx.bhdl, &io_config, &ctx.io);
    if (!err) err = esp_lcd_new_panel_st7789(ctx.io, &dev_config, &ctx.hdl);
    ctx.axes[0] = ctx.axes[1] = ctx.axes[3] = true; ctx.axes[5] = 35;
#   else
    err = ESP_ERR_NOT_SUPPORTED;
#   endif

    if (!err) err = esp_lcd_panel_reset(ctx.hdl);
    if (!err) err = esp_lcd_panel_init(ctx.hdl);
    if (!err) err = esp_lcd_panel_mirror(ctx.hdl, ctx.axes[1], ctx.axes[2]);
    if (!err) err = esp_lcd_panel_swap_xy(ctx.hdl, ctx.axes[3]);
    if (!err) err = esp_lcd_panel_set_gap(ctx.hdl, ctx.axes[4], ctx.axes[5]);
    if (!err) err = esp_lcd_panel_invert_color(ctx.hdl, ctx.axes[0]);

#   ifdef CONFIG_BASE_SCN_PATCH_ST7789
    if (!err) {
        struct { uint8_t cmd, data[14], len; } patch_st7789[] = {
            { 0x11, { 0 }, 0x80 },
            { 0x3A, { 0x05 }, 1 },
            { 0xB2, { 0x0B, 0x0B, 0x00, 0x33, 0x33 }, 5 },
            { 0xB7, { 0x75 }, 1 },
            { 0xBB, { 0x28 }, 1 },
            { 0xC0, { 0x2C }, 1 },
            { 0xC2, { 0x01 }, 1 },
            { 0xC3, { 0x1F }, 1 },
            { 0xC6, { 0x13 }, 1 },
            { 0xD0, { 0xA7 }, 1 },
            { 0xD0, { 0xA4, 0xA1 }, 2 },
            { 0xD6, { 0xA1 }, 1 },
            { 0xE0, { 0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B,
                      0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32 }, 14 },
            { 0xE1, { 0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B,
                      0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37 }, 14 },
        };
        LOOPN(i, err ? 0 : LEN(patch_st7789)) {
            err = esp_lcd_panel_io_tx_param(
                ctx.io, patch_st7789[i].cmd,
                patch_st7789[i].data, patch_st7789[i].len & 0x7F);
            if (patch_st7789[i].len & 0x80) msleep(120);
        }
    }
#   endif
    if (err) goto exit;

    // we can draw startup logo here before we turn on the screen
#   if SCREEN_DEPTH == 1
    const uint8_t pattern[32] = {
        0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00, // square
        0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00,
        0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81, // cross
        0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
    };
    LOOPN(i, err ? 0 : (SCREEN_WIDTH / 16)) {
        LOOPN(j, err ? 0 : (SCREEN_HEIGHT / 8)) {
            err = esp_lcd_panel_draw_bitmap(
                ctx.hdl, i * 16, j * 8,
                i * 16 + 16, j * 8 + 8,
                pattern + ((i & 1) * 16));
        }
    }
#   else
    uint8_t cbuf[16 * 16 * SCREEN_DEPTH / 8];
    memset(cbuf, 0xFF, sizeof(cbuf));
    LOOPN(i, err ? 0 : (SCREEN_WIDTH / 16)) {
        LOOPN(j, err ? 0 : (SCREEN_HEIGHT / 16)) {
            err = esp_lcd_panel_draw_bitmap(
                ctx.hdl, i * 16, j * 16, (i + 1) * 16, (j + 1) * 16, cbuf);
        }
    }
#   endif

    if (!err) err = esp_lcd_panel_disp_on_off(ctx.hdl, true);
    if (!err) gexp_set_level(ctx.bl, true);

#   ifdef WITH_LVGL
#       ifdef CONFIG_FREERTOS_UNICORE
    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
#       else
    lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_config.task_affinity = 1; // run LVGL task on CPU1 if possible
    lvgl_config.task_stack = 8192; // 8k stack size
#       endif
    const lvgl_port_display_cfg_t disp_config = {
        .io_handle      = ctx.io,
        .panel_handle   = ctx.hdl,
        .double_buffer  = SCREEN_DEPTH != 1,
        .buffer_size    = SCREEN_PIXELS / (SCREEN_DEPTH == 1 ? 1 : 4),
        .monochrome     = SCREEN_DEPTH == 1,
        .hres           = SCREEN_WIDTH,
        .vres           = SCREEN_HEIGHT,
#       if LVGL_VERSION_MAJOR >= 9
        .color_format   = LV_COLOR_FORMAT_RGB565,
#       endif
        .rotation = {           // must be same as esp_lcd_panel_mirror
            .mirror_x   = ctx.axes[1],
            .mirror_y   = ctx.axes[2],
            .swap_xy    = ctx.axes[3],
        },
        .flags = {
            .buff_dma   = true,
#       ifdef CONFIG_PSRAM
            .buff_spiram = true,
#       endif
#       if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = SCREEN_DEPTH == 16,
#       endif
        },
    };
    if (!err && !( err = lvgl_port_init(&lvgl_config) )) {
        if (!( ctx.disp = lvgl_port_add_disp(&disp_config) )) {
            err = ESP_FAIL;
        }
    }
#   endif // WITH_LVGL
exit:
    if (err) {
        ctx.probed = false;
        gexp_set_level(ctx.bl, false);
        gexp_set_level(ctx.rd, false);
        TRYNULL(ctx.hdl, esp_lcd_panel_del);
#   ifdef CONFIG_BASE_SCN_I80
        TRYNULL(ctx.bhdl, esp_lcd_del_i80_bus);
#   endif
#   ifdef WITH_LVGL
        TRYNULL(ctx.disp, lvgl_port_remove_disp);
#   endif
    }
}

int lvgl_ui_cmd(scn_cmd_t cmd, const char *arg); // defined in scnlvgl.c

#endif // WITH_U8G2

void scn_initialize() {
    if (ctx.probed) return;
    const char *names[LEN(ctx.pins)] = {
        "SCN BL", "SCN RST", "SCN CS", "SCN DC", "SCN WR", "SCN RD"
    };
    LOOPN(i, LEN(ctx.pins)) {
        ctx.pins[i] = GPIO_NUM_NC;
        if (i >= 6) names[i] = NULL;
    }
#if CONFIG_BASE_GPIO_SCN_BL >= 0
    ctx.bl = GPIO_NUMBER(CONFIG_BASE_GPIO_SCN_BL);
#endif
#if CONFIG_BASE_GPIO_SCN_RST >= 0
    ctx.rst = GPIO_NUMBER(CONFIG_BASE_GPIO_SCN_RST);
#endif
#if CONFIG_BASE_GPIO_SCN_DC >= 0
    ctx.dc = GPIO_NUMBER(CONFIG_BASE_GPIO_SCN_DC);
#endif
#if defined(CONFIG_BASE_SCN_I80)                // I80 Screen
#   ifdef CONFIG_BASE_SCN_CUSTOM_PINS
    const char *str = CONFIG_BASE_SCN_CUSTOM_PINS;
#   else
    const char *str = CONFIG_BASE_SCN_PINS;
#   endif
    ctx.bus = parse_pin(str, ctx.pins, LEN(ctx.pins), names) - 6;
    ctx.speed = CONFIG_BASE_SCN_I80_SPEED;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num        = ctx.dc,
        .wr_gpio_num        = ctx.wr,
        .clk_src            = LCD_CLK_SRC_PLL160M,
        .bus_width          = ctx.bus,  // 8|16|24
        .max_transfer_bytes = SCREEN_PIXELS * SCREEN_DEPTH / 8,
        .psram_trans_align  = 0,
        .sram_trans_align   = 0,
    };
    if (( ctx.probed = ctx.bus > 0 )) {
        LOOPN(i, ctx.bus) { bus_config.data_gpio_nums[i] = ctx.pins[6 + i]; }
        ctx.probed = esp_lcd_new_i80_bus(&bus_config, &ctx.bhdl) == ESP_OK;
    }
#elif defined(CONFIG_BASE_SCN_SPI)              // SPI Screen
    ctx.speed = CONFIG_BASE_SCN_SPI_SPEED;
    ctx.mode = CONFIG_BASE_SCN_SPI_MODE;
    ctx.bus = NUM_SPI;
    ctx.cs = GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_SDFS);
    ctx.probed = true;
#elif !defined(CONFIG_BASE_SCN_I2C_ALT)         // Screen using default I2C
    ctx.speed = CONFIG_BASE_I2C_SPEED;
    ctx.addr = CONFIG_BASE_SCN_I2C_ADDR;
    ctx.bus = NUM_I2C;  // already initialized
    ctx.sda = PIN_SDA;
    ctx.scl = PIN_SCL;
    ctx.probed = i2c_probe(ctx.bus, ctx.addr) == ESP_OK;
#else                                           // Screen using dedicated I2C
    ctx.speed = CONFIG_BASE_SCN_I2C_SPEED;  // TODO: apply i2c speed
    ctx.addr = CONFIG_BASE_SCN_I2C_ADDR;
    ctx.bus = NUM_I2C == I2C_NUM_0 ? I2C_NUM_1 : I2C_NUM_0;
    ctx.sda = NUM_I2C == I2C_NUM_0 ? PIN_SDA1 : PIN_SDA0;
    ctx.scl = NUM_I2C == I2C_NUM_0 ? PIN_SCL1 : PIN_SDA1;
    ctx.probed = i2c_probe(ctx.bus, ctx.addr) == ESP_OK;
#endif // CONFIG_BASE_SCN_SPI

    if (!ctx.probed) return;
#if defined(WITH_U8G2)
    scn_u8g2_init();
#else
    scn_lvgl_init();
#endif
    if (!ctx.probed) ESP_LOGE(TAG, "Screen initialize failed");
    LOOPN(i, LEN(ctx.pins)) {
        if (ctx.pins[i] == GPIO_NUM_NC) continue;
        if (i < 6) {
            gpio_usage(ctx.pins[i], names[i]);
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "SCN D%u", i - 6);
            gpio_usage(ctx.pins[i], strdup(buf));
        }
    }
}

esp_err_t scn_command(scn_cmd_t cmd, const void *arg) {
    if (!ctx.probed) return ESP_ERR_INVALID_STATE;
    if (cmd == SCN_STAT) {
        printf("Using Screen %dx%d %dbpp "
               "INV:%d|%d|%d SWAP:%d GAP:%d|%d ROT:%d BL:%d RST:%d ",
               SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH,
               ctx.axes[0], ctx.axes[1], ctx.axes[2], ctx.axes[3],
               ctx.axes[4], ctx.axes[5], ctx.axes[6], ctx.bl, ctx.rst);
        bool mhz = ctx.speed > 1000000;
        char unit = "KM"[mhz];
        int speed = ctx.speed / 1000 / (mhz ? 1000 : 1);
#if defined(CONFIG_BASE_SCN_I2C)
        printf("I2C %d-0x%02X %d%cHz SDA:%d SCL:%d",
               ctx.bus, ctx.addr, speed, unit, ctx.sda, ctx.scl);
#elif defined(CONFIG_BASE_SCN_SPI)
        printf("SPI %d %d%cHz CS:%d DC:%d",
               ctx.bus, speed, unit, ctx.cs, ctx.dc);
#elif defined(CONFIG_BASE_SCN_I80)
        printf("I80 %dP %d%cHz CS:%d DC:%d WR:%d RD:%d",
               ctx.bus, speed, unit, ctx.cs, ctx.dc, ctx.wr, ctx.rd);
#endif
#if defined(WITH_U8G2)
        puts(" (U8G2)");
#elif defined(WITH_LVGL)
        puts(" (LVGL)");
#else
        puts(" (ESP_LCD)");
#endif
    }
#if defined(WITH_U8G2)
    return u8g2_ui_cmd(cmd, arg);
#else
    switch (cmd) {
    case SCN_GAP: {
        int val = arg ? *(int *)arg : -1;
        if (val >= 0) {
            ctx.axes[4] = val >> 8;
            ctx.axes[5] = val & 0xFF;
        }
        if (ctx.axes[6] == 1 || ctx.axes[6] == 3) {
            esp_lcd_panel_set_gap(ctx.hdl, ctx.axes[5], ctx.axes[4]);
        } else {
            esp_lcd_panel_set_gap(ctx.hdl, ctx.axes[4], ctx.axes[5]);
        }
    }   break;
    case SCN_ROT:
        ctx.axes[6] = arg ? *(int *)arg : (ctx.axes[6] + 1) % 4;
        if (ctx.axes[4] || ctx.axes[3]) scn_command(SCN_GAP, NULL);
#   ifndef WITH_LVGL
        switch (ctx.axes[6]) {
        case 0:     // 0deg
            esp_lcd_panel_mirror(ctx.hdl, ctx.axes[1], ctx.axes[2]);
            esp_lcd_panel_swap_xy(ctx.hdl, ctx.axes[3]);
            break;
        case 1:     // 90deg
            esp_lcd_panel_mirror(ctx.hdl, ctx.axes[1], !ctx.axes[2]);
            esp_lcd_panel_swap_xy(ctx.hdl, !ctx.axes[3]);
            break;
        case 2:     // 180deg
            esp_lcd_panel_mirror(ctx.hdl, !ctx.axes[1], !ctx.axes[2]);
            esp_lcd_panel_swap_xy(ctx.hdl, ctx.axes[3]);
            break;
        case 3:     // 270deg
            esp_lcd_panel_mirror(ctx.hdl, !ctx.axes[1], ctx.axes[2]);
            esp_lcd_panel_swap_xy(ctx.hdl, !ctx.axes[3]);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }
#   else
        arg = (void *)(ctx.axes + 6);
        FALLTH;
#   endif
    default:
#   if defined(WITH_LVGL)
        if (!ctx.disp) return ESP_ERR_INVALID_STATE;
        if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
        if (cmd == SCN_INIT) arg = ctx.disp;
        esp_err_t err = lvgl_ui_cmd(cmd, arg);
        lvgl_port_unlock();
        return err;
#   else
        if (cmd != SCN_STAT) return ESP_ERR_NOT_SUPPORTED;
#   endif
    }
    return ESP_OK;
#endif // WITH_U8G2
}
#endif // CONFIG_BASE_USE_SCN
