/* 
 * File: commands.cpp
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-13 18:03:04
 */

#include "console.h"
#include "config.h"
#include "update.h"
#include "drivers.h"
#include "filesys.h"
#include "network.h"
#include "sensors.h"
#include "usbmode.h"
#include "ledmode.h"
#include "btmode.h"
#include "screen.h"
#include "timesync.h"

#include "esp_sleep.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "rom/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#ifndef CONFIG_BASE_USE_USB
#undef  CONSOLE_DRIVER_USB
#endif
#ifndef CONFIG_BASE_USE_LED
#undef  CONSOLE_DRIVER_LED
#endif
#ifndef CONFIG_BASE_USE_I2C
#undef  CONSOLE_DRIVER_I2C
#endif
#ifndef CONFIG_BASE_USE_SCREEN
#undef  CONSOLE_DRIVER_SCN
#endif
#ifndef CONFIG_BASE_ALS_TRACK
#undef  CONSOLE_DRIVER_ALS
#endif
#ifndef CONFIG_BASE_USE_ADC
#undef  CONSOLE_DRIVER_ADC
#endif
#ifndef CONFIG_BASE_USE_DAC
#undef  CONSOLE_DRIVER_DAC
#endif
#if !defined(CONFIG_BASE_USE_SERVO) && !defined(CONFIG_BASE_USE_BUZZER)
#undef  CONSOLE_DRIVER_PWM
#endif

#if !defined(CONFIG_BASE_USE_FFS) && !defined(CONFIG_BASE_USE_SDFS)
#undef  CONSOLE_UTILS_LSFS
#endif
#if !defined(CONFIG_BASE_USE_USB) \
 && !defined(CONFIG_BASE_USE_BT)  \
 && !defined(CONFIG_BASE_USE_SCREEN)
#undef  CONSOLE_UTILS_HID
#endif

#ifndef CONFIG_BASE_USE_BT
#undef  CONSOLE_NET_BT
#endif
#ifndef CONFIG_BASE_USE_WIFI
#undef  CONSOLE_NET_STA
#undef  CONSOLE_NET_AP
#undef  CONSOLE_NET_FTM
#undef  CONSOLE_NET_MDNS
#undef  CONSOLE_NET_SNTP
#undef  CONSOLE_NET_PING
#undef  CONSOLE_NET_IPERF
#undef  CONSOLE_NET_TSYNC
#endif

static const char * TAG = "Command";

/******************************************************************************
 * Some common utilities
 */

/* @brief   Parse command line arguments into argtable and catch any errors
 * @return
 *          - true  : arguments successfully parsed, no error
 *          - false : error occur
 * */
static bool arg_noerror(int argc, char **argv, void **argtable) {
    if (arg_parse(argc, argv, argtable) != 0) {
        arg_hdr_t **table = (arg_hdr_t **) argtable;
        int tabindex = 0;
        while (!(table[tabindex]->flag & ARG_TERMINATOR)) { tabindex++; }
        arg_print_errors(stdout, (arg_end_t *) table[tabindex], argv[0]);
        return false;
    }
    return true;
}

static void register_commands(const esp_console_cmd_t * cmds, size_t ncmd) {
    LOOPN(i, ncmd) { ESP_ERROR_CHECK( esp_console_cmd_register(cmds + i) ); }
}

#define ARG_STR(p, s)   ((p)->count ? (p)->sval[0] : (s))
#define ARG_INT(p, v)   ((p)->count ? (p)->ival[0] : (v))

#define ARG_PARSE(c, v, t)                                                  \
        do {                                                                \
            if (!arg_noerror((c), (v), (void **)(t)))                       \
                return ESP_ERR_INVALID_ARG;                                 \
        } while (0)

#define ESP_CMD(d, c, h)                                                    \
        {                                                                   \
            .command = #c,                                                  \
            .help = (h),                                                    \
            .hint = NULL,                                                   \
            .func = &(d ## _ ## c),                                         \
            .argtable = NULL                                                \
        }

#define ESP_CMD_ARG(d, c, h)                                                \
        {                                                                   \
            .command = #c,                                                  \
            .help = (h),                                                    \
            .hint = NULL,                                                   \
            .func = &(d ## _ ## c),                                         \
            .argtable = &(d ## _ ## c ## _args)                             \
        }

/******************************************************************************
 * System commands
 */

#ifdef CONSOLE_SYSTEM_RESTART
static struct {
    arg_lit_t *halt;
    arg_lit_t *cxel;
    arg_int_t *tout;
    arg_end_t *end;
} system_restart_args = {
    .halt = arg_lit0("h", "halt", "shutdown instead of reboot"),
    .cxel = arg_lit0("c", "cancel", "cancel pending reboot (if available)"),
    .tout = arg_int0("t", NULL, "0-65535", "reboot timeout in ms"),
    .end  = arg_end(3)
};

static void system_restart_task(void *arg) {
    int tout_ms = arg ? *(int *)arg : 0;
    if (tout_ms) {
        ESP_LOGW(TAG, "Will restart in %ums ...", ABS(tout_ms));
        msleep(ABS(tout_ms));
    }
    if (tout_ms < 0) {
        esp_system_abort("Manually shutdown");
    } else {
        esp_restart();
    }
}

static int system_restart(int argc, char **argv) {
    static TaskHandle_t task = NULL;
    static int end_ms, tout_ms;
    ARG_PARSE(argc, argv, &system_restart_args);
    if (system_restart_args.cxel->count) {
        if (task) puts("Restart cancelled");
        TRYNULL(task, vTaskDelete);
    } else if (task) {
        printf("Restart pending: %.0fms", end_ms - get_timestamp(0) * 1e3);
    } else {
        tout_ms = ARG_INT(system_restart_args.tout, 0);
        end_ms = (int)(get_timestamp(0) * 1e3) + tout_ms;
        if (system_restart_args.halt->count) tout_ms = -tout_ms;
        xTaskCreate(system_restart_task, "restart", 4096, &tout_ms, 99, &task);
        if (!task) system_restart_task(NULL);
    }
    return ESP_OK;
}
#endif

#ifdef CONSOLE_SYSTEM_SLEEP
static const char * const wakeup_reason_list[] = {
    "Undefined", "Undefined", "EXT0", "EXT1",
    "Timer", "Touchpad", "ULP", "GPIO", "UART",
};

static struct {
    arg_str_t *mode;
    arg_int_t *tout;
    arg_int_t *pin;
    arg_int_t *lvl;
    arg_end_t *end;
} system_sleep_args = {
    .mode = arg_str0(NULL, NULL, "light|deep", "sleep mode"),
    .tout = arg_int0("t", NULL, "0-2^31", "wakeup timeout in ms"),
    .pin  = arg_intn("p", NULL, NULL, 0, 8, "wakeup from GPIO[s]"),
    .lvl  = arg_intn("l", NULL, "0|1", 0, 8, "GPIO level[s] to detect"),
    .end  = arg_end(4)
};

static esp_err_t enable_gpio_light_wakeup() {
    int pin_cnt = system_sleep_args.pin->count;
    int lvl_cnt = system_sleep_args.lvl->count;
    if (!pin_cnt) return ESP_OK;
    if (lvl_cnt && (pin_cnt != lvl_cnt)) {
        ESP_LOGE(TAG, "GPIO and level mismatch!");
        return ESP_ERR_INVALID_ARG;
    }
    int lvl;
    const char *lvls;
    gpio_num_t pin;
    gpio_int_type_t intr;
    LOOPN(i, pin_cnt) {
        pin = (gpio_num_t)system_sleep_args.pin->ival[i];
        lvl = lvl_cnt ? system_sleep_args.lvl->ival[i] : 0;
        lvls = lvl ? "HIGH" : "LOW";
        intr = lvl ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL;
        if (esp_sleep_is_valid_wakeup_gpio(pin)) {
            fprintf(stderr, "Use GPIO wakeup, num %d level %s\n", pin, lvls);
            ESP_ERROR_CHECK( gpio_wakeup_enable(pin, intr) );
        } else {
            fprintf(stderr, "Skip GPIO wakeup, num %d level %s\n", pin, lvls);
        }
    }
    ESP_ERROR_CHECK( esp_sleep_enable_gpio_wakeup() );
    return esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
}

static esp_err_t enable_gpio_deep_wakeup() {
    int pin_cnt = system_sleep_args.pin->count;
    if (!pin_cnt) return ESP_OK;
    int lvl = ARG_INT(system_sleep_args.lvl, 0);
    const char *lvls = lvl ? "ANY_HIGH" : "ALL_LOW";
    esp_sleep_ext1_wakeup_mode_t mode = \
        lvl ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ALL_LOW;
    gpio_num_t pin;
    uint64_t mask = 0;
    LOOPN(i, pin_cnt) {
        pin = (gpio_num_t)system_sleep_args.pin->ival[i];
        if (esp_sleep_is_valid_wakeup_gpio(pin)) {
            fprintf(stderr, "Use GPIO wakeup, num %d level %s\n", pin, lvls);
            mask |= 1ULL << pin;
        } else {
            fprintf(stderr, "Skip GPIO wakeup, num %d level %s\n", pin, lvls);
        }
    }
    ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup(mask, mode) );
    return esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}

static int system_sleep(int argc, char **argv) {
    ARG_PARSE(argc, argv, &system_sleep_args);
    const char *mode = ARG_STR(system_sleep_args.mode, "light");
    uint32_t tout_ms = ARG_INT(system_sleep_args.tout, 0);
    if (tout_ms) {
        fprintf(stderr, "Use timer wakeup, timeout: %ums\n", tout_ms);
        uint64_t tout_us = (uint64_t)tout_ms * 1000;
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(tout_us) );
    }
    bool light = true;
    if (strstr(mode, "deep")) {
        light = false;
    } else if (!strstr(mode, "light")) {
        ESP_LOGE(TAG, "Unsupported sleep mode: %s", mode);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err;
    if (light) {
#ifdef CONFIG_BASE_USE_UART
        fprintf(stderr, "Use UART wakeup, num: %d\n", NUM_UART);
        ESP_ERROR_CHECK( uart_set_wakeup_threshold(NUM_UART, 3) );
        ESP_ERROR_CHECK( esp_sleep_enable_uart_wakeup(NUM_UART) );
#endif
        if (( err = enable_gpio_light_wakeup() )) return err;
    } else {
        if (( err = enable_gpio_deep_wakeup() )) return err;
    }

    fprintf(stderr, "Turn to %s sleep mode\n", mode);
    fflush(stderr); fsync(fileno(stderr));
#ifdef CONFIG_BASE_USE_UART
    uart_tx_wait_idle(NUM_UART);
#endif
    if (light) {
        esp_light_sleep_start();
    } else {
        esp_deep_sleep_start(); // no-return (see console_register_commands)
    }
    fprintf(stderr, "Woken up from light sleep mode by %s\n",
            wakeup_reason_list[(int)esp_sleep_get_wakeup_cause()]);
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}
#endif // CONSOLE_SYSTEM_SLEEP

#ifdef CONSOLE_SYSTEM_UPDATE
static struct {
    arg_str_t *cmd;
    arg_str_t *part;
    arg_str_t *url;
    arg_lit_t *fetch;
    arg_lit_t *reset;
    arg_end_t *end;
} system_update_args = {
    .cmd   = arg_str0(NULL, NULL, "boot|fetch|reset", ""),
    .part  = arg_str0("p", NULL, "LABEL", "partition to boot from"),
    .url   = arg_str0("u", NULL, "URL", "specify URL to fetch"),
    .fetch = arg_lit0("f", "fetch", "fetch app firmware from URL"),
    .reset = arg_lit0("r", "reset", "clear OTA internal states"),
    .end   = arg_end(5)
};

static int system_update(int argc, char **argv) {
    ARG_PARSE(argc, argv, &system_update_args);
    const char *subcmd = ARG_STR(system_update_args.cmd, "");
    if (strstr(subcmd, "boot")) {
        if (system_update_args.part->count) {
            const char *label = system_update_args.part->sval[0];
            printf("Boot from %s: ", label);
            if (!ota_updation_partition(label)) {
                puts(ota_updation_error());
                return ESP_FAIL;
            }
            puts("done");
        }
    } else if (strstr(subcmd, "reset")) {
        ota_updation_reset();
        printf("OTA states reset done\n");
    } else if (strstr(subcmd, "fetch")) {
        const char *url = ARG_STR(system_update_args.url, NULL);
        if (!ota_updation_url(url)) {
            printf("Failed to udpate: %s\n", ota_updation_error());
            return ESP_FAIL;
        } else {
            printf("Updation success. Call `restart` to reboot ESP32");
        }
    } else {
        ota_partition_info();
    }
    return ESP_OK;
}
#endif // CONSOLE_SYSTEM_UPDATE

static void register_system() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_SYSTEM_RESTART
        ESP_CMD_ARG(system, restart, "Software reset of ESP32"),
#endif
#ifdef CONSOLE_SYSTEM_SLEEP
        ESP_CMD_ARG(system, sleep, "Turn ESP32 into light/deep sleep mode"),
#endif
#ifdef CONSOLE_SYSTEM_UPDATE
        ESP_CMD_ARG(system, update, "OTA Updation helper command"),
#endif
    };
    register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Driver commands
 */

#ifdef CONSOLE_DRIVER_GPIO
static struct {
    arg_int_t *pin;
    arg_int_t *lvl;
    arg_lit_t *i2c;
    arg_lit_t *spi;
    arg_end_t *end;
} driver_gpio_args = {
    .pin = arg_int0(NULL, NULL, NULL, "gpio number"),
    .lvl = arg_int0(NULL, NULL, "0|1", "set pin to LOW / HIGH"),
    .i2c = arg_lit0(NULL, "i2c", "list pin of I2C GPIO Expander"),
    .spi = arg_lit0(NULL, "spi", "list pin of SPI GPIO Expander"),
    .end = arg_end(4)
};

static int driver_gpio(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_gpio_args);
    esp_err_t err = ESP_OK;
    int pin = ARG_INT(driver_gpio_args.pin, -1);
    int lvl = ARG_INT(driver_gpio_args.lvl, -1);
    if (pin < 0) {
        gpio_table(driver_gpio_args.i2c->count, driver_gpio_args.spi->count);
        return err;
    }
    bool level = lvl;
    if (lvl < 0) {
        err = gexp_get_level(pin, &level, true);
    } else {
        err = gexp_set_level(pin, level);
    }
    if (err) {
        printf("%s GPIO %d level failed: %s\n",
               lvl < 0 ? "Get" : "Set", pin, esp_err_to_name(err));
    } else {
        printf("GPIO %d: %s\n", pin, level ? "HIGH" : "LOW");
    }
    return ESP_OK;
}
#endif // CONSOLE_DRIVER_GPIO

#ifdef CONSOLE_DRIVER_USB
static struct {
    arg_str_t *mode;
    arg_lit_t *now;
    arg_end_t *end;
} driver_usb_args = {
    .mode = arg_str0(NULL, NULL, "0-6|CMH|S", "specify USB mode"),
    .now  = arg_lit0(NULL, "now", "reboot now"),
    .end  = arg_end(2)
};

static int driver_usb(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_usb_args);
    static const char *choices = "CcMmHhS", *c;
    esp_err_t err = ESP_OK;
    bool reboot = driver_usb_args.now->count;
    const char *mode = ARG_STR(driver_usb_args.mode, NULL);
    if (!mode) {
        usbmode_status();
    } else if ('0' <= mode[0] && mode[0] <= '6') {
        err = usbmode_switch((usbmode_t)(mode[0] - '0'), reboot);
    } else if (( c = strchr(choices, mode[0]) )) {
        err = usbmode_switch((usbmode_t)(c - choices), reboot);
    } else {
        printf("Invalid mode to set: `%s`\n", mode);
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}
#endif // CONSOLE_DRIVER_USB

#ifdef CONSOLE_DRIVER_LED
static struct {
    arg_int_t *idx;
    arg_str_t *lgt;
    arg_str_t *clr;
    arg_int_t *blk;
    arg_end_t *end;
} driver_led_args = {
    .idx = arg_int0(NULL, NULL, NULL, "LED index"),
    .lgt = arg_str0("l", NULL, "0-255|on|off", "set lightness"),
    .clr = arg_str0("c", NULL, "0xRRGGBB", "set RGB color"),
    .blk = arg_int0("b", NULL, NULL, "set blink effect"),
    .end = arg_end(4)
};

static int driver_led(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_led_args);
    esp_err_t err = ESP_OK;
    int idx = ARG_INT(driver_led_args.idx, -1);
    int blk = ARG_INT(driver_led_args.blk, LED_BLINK_RESET - 1);
    const char *light = ARG_STR(driver_led_args.lgt, NULL);
    const char *color = ARG_STR(driver_led_args.clr, NULL);
    if (blk >= LED_BLINK_RESET) {
        if (!( err = led_set_blink((led_blink_t)blk) )) {
            if (blk > LED_BLINK_RESET) {
                printf("LED: set blink to %d\n", blk);
            } else {
                puts("LED: stop blink");
            }
        }
        return err;
    }
    char buf[16];
    if (idx < 0) {
        buf[0] = 0;
    } else {
        snprintf(buf, sizeof(buf), " %d", idx);
    }
    if (light) {
        char *ptr;
        uint8_t brightness = strtol(light, &ptr, 0);
        if (strstr(light, "off")) {
            brightness = 0;
        } else if (strstr(light, "on")) {
            brightness = 255;
        } else if (ptr == light) {
            printf("Invalid brightness: `%s`\n", light);
            return ESP_ERR_INVALID_ARG;
        }
        if (( err = led_set_light(idx, brightness) ))
            return err;
        printf("LED%s: set brightness to %d\n", buf, brightness);
    }
    if (color) {
        char *ptr;
        uint32_t rgb = strtol(color, &ptr, 0);
        if (ptr == color || rgb > 0xFFFFFF) {
            printf("Unsupported color: `%s`\n", color);
            return ESP_ERR_INVALID_ARG;
        }
        if (( err = led_set_color(idx, rgb) ))
            return err;
        printf("LED%s: set color to %06X\n", buf, rgb);
    }
    if (idx >= CONFIG_BASE_LED_NUM) {
        printf("Invalid LED index: `%d`\n", idx);
        err = ESP_ERR_INVALID_ARG;
    } else {
        printf("LED%s: color 0x%06X, brightness %d, blink %d\n",
            buf, led_get_color(idx), led_get_light(idx), led_get_blink());
    }
    return err;
}
#endif // CONSOLE_DRIVER_LED

#ifdef CONSOLE_DRIVER_I2C
static struct {
    arg_int_t *bus;
    arg_int_t *addr;
    arg_int_t *reg;
    arg_int_t *val;
    arg_int_t *len;
    arg_lit_t *hex;
    arg_end_t *end;
} driver_i2c_args = {
#   if defined(CONFIG_BASE_USE_I2C0) && defined(CONFIG_BASE_USE_I2C1)
    .bus = arg_int1(NULL, NULL, "0|1", "I2C bus"),
#   else
    .bus = arg_int0(NULL, NULL, STR(CONFIG_BASE_I2C_NUM), "I2C bus"),
#   endif
    .addr = arg_int0(NULL, NULL, "0x00-0x7F", "I2C client 7-bit address"),
    .reg = arg_int0(NULL, NULL, "REG", "register 8-bit address"),
    .val = arg_int0(NULL, NULL, "VAL", "register value"),
    .len = arg_int0("l", NULL, "NUM", "read specified length of regs"),
    .hex = arg_lit0("w", "word", "read/write in word (16-bit) mode"),
    .end = arg_end(6)
};

static int driver_i2c(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_i2c_args);
    int bus = ARG_INT(driver_i2c_args.bus, CONFIG_BASE_I2C_NUM);
    int addr = ARG_INT(driver_i2c_args.addr, -1);
    if (bus < 0 || bus >= I2C_NUM_MAX) {
        printf("Invalid I2C bus number: %d\n", bus);
        return ESP_ERR_INVALID_ARG;
    }
    if (addr > 0x7F) {
        printf("Invalid I2C address: 0x%02X\n", addr);
        return ESP_ERR_INVALID_ARG;
    }
    if (addr < 0) {
        i2c_detect(bus);
        return ESP_OK;
    }
    uint8_t reg = ARG_INT(driver_i2c_args.reg, 0);
    uint8_t len = ARG_INT(driver_i2c_args.len, 0);
    if (driver_i2c_args.val->count) {
        if (driver_i2c_args.hex->count) {
            uint16_t val = driver_i2c_args.val->ival[0];
            return smbus_write_word(bus, addr, reg, val);
        } else {
            uint8_t val = driver_i2c_args.val->ival[0];
            return smbus_write_byte(bus, addr, reg, val);
        }
    }
    esp_err_t err;
    if (driver_i2c_args.hex->count) {
        uint16_t val;
        if (( err = smbus_read_word(bus, addr, reg, &val) )) return err;
        printf("I2C %d-%02X REG 0x%02X = %04X\n", bus, addr, reg, val);
    } else if (!len) {
        uint8_t val;
        if (( err = smbus_read_byte(bus, addr, reg, &val) )) return err;
        printf("I2C %d-%02X REG 0x%02X = %02X\n", bus, addr, reg, val);
    } else {
        err = smbus_dump(bus, addr, reg, len);
    }
    return err;
}
#endif // CONSOLE_DRIVER_I2C

#ifdef CONSOLE_DRIVER_SCN
static struct {
    arg_int_t *btn;
    arg_int_t *bar;
    arg_int_t *rot;
    arg_int_t *fps;
    arg_str_t *font;
    arg_end_t *end;
} driver_scn_args = {
    .btn  = arg_int0(NULL, NULL, "0-6", "trigger virtual button press"),
    .bar  = arg_int0("p", NULL, "0-100", "draw progress bar on screen"),
    .rot  = arg_int0("r", NULL, "0-3", "software rotation of screen"),
    .fps  = arg_int0("f", NULL, "0-100", "set LVGL refresh period in FPS"),
    .font = arg_str0(NULL, "font", "PATH", "load font from file"),
    .end  = arg_end(5)
};

static int driver_scn(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_scn_args);
    int btn = ARG_INT(driver_scn_args.btn, -1);
    int bar = ARG_INT(driver_scn_args.bar, -1);
    int rot = ARG_INT(driver_scn_args.rot, -1);
    int fps = ARG_INT(driver_scn_args.fps, -1);
    const char *font = ARG_STR(driver_scn_args.font, NULL);
    if (bar >= 0) {
        return screen_command(SCN_PBAR, &bar);
    } else if (btn >= 0) {
        return screen_command(SCN_BTN, &btn);
    } else if (rot >= 0) {
        return screen_command(SCN_ROT, &rot);
    } else if (fps >= 0) {
        return screen_command(SCN_FPS, &fps);
    } else if (font) {
        return screen_command(SCN_FONT, font);
    }
    screen_status();
    return ESP_OK;
}
#endif // CONSOLE_DRIVER_SCN

#ifdef CONSOLE_DRIVER_ALS
static struct {
    arg_int_t *idx;
    arg_str_t *rlt;
    arg_end_t *end;
} driver_als_args = {
    .idx = arg_int0(NULL, NULL, "0-3", "index of ALS chip"),
    .rlt = arg_str0("t", NULL, "0-3|HVA", "run light tracking"),
    .end = arg_end(2)
};

static int driver_als(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_als_args);
    static const char *choices = "0123HVA", *c;
    const char *method = ARG_STR(driver_als_args.rlt, NULL);
    int idx = ARG_INT(driver_als_args.idx, -1);
    esp_err_t err = ESP_OK;
    if (method) {
        if (!( c = strchr(choices, method[0]) )) {
            printf("Invalid tracking method: %s\n", method);
            return ESP_ERR_INVALID_ARG;
        }
        int hdeg = -1, vdeg = -1;
        if (!( err = als_tracking((als_track_t)(c - choices), &hdeg, &vdeg) ))
            printf("ALS tracked to H: %d, V: %d\n", hdeg, vdeg);
    } else if (idx < 4) {
        LOOPN(i, 4) {
            if (idx < 0 || i == idx) {
                printf("Brightness of ALS %d is %.2f lux\n",
                        i, als_brightness(i));
            }
        }
    } else {
        gy39_data_t dat;
        if (!( err = gy39_measure(&dat) ))
            printf("GY39 %.2f lux, %.2f degC, %.3f kPa, %.2f %%, %.2f m\n",
                    dat.brightness, dat.temperature,
                    dat.atmosphere, dat.humidity, dat.altitude);
    }
    return err;
}
#endif // CONSOLE_DRIVER_ALS

#ifdef CONSOLE_DRIVER_ADC
static struct {
    arg_int_t *idx;
    arg_lit_t *joy;
    arg_lit_t *hall;
    arg_lit_t *tpad;
    arg_int_t *intv;
    arg_int_t *tout;
    arg_end_t *end;
} driver_adc_args = {
#   ifdef PIN_ADC2
    .idx  = arg_int0(NULL, NULL, "0|1", "index of ADC channel"),
#   else
    .idx  = arg_int0(NULL, NULL, "0", "index of ADC channel"),
#   endif
    .joy  = arg_lit0(NULL, "joy", "read joystick value"),
    .hall = arg_lit0(NULL, "hall", "read hall sensor value"),
    .tpad = arg_lit0(NULL, "tpad", "read touch pad value"),
    .intv = arg_int0("i", NULL, "10-1000", "interval in ms, default 500"),
    .tout = arg_int0("t", NULL, "0-2^31", "loop until timeout in ms"),
    .end  = arg_end(6)
};

static int driver_adc(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_adc_args);
#   ifdef PIN_ADC2
    int idx = ARG_INT(driver_adc_args.idx, -1);
#   else
    int idx = ARG_INT(driver_adc_args.idx, 0);
#   endif
    uint16_t intv_ms = CONS(ARG_INT(driver_adc_args.intv, 500), 10, 1000);
    uint32_t tout_ms = ARG_INT(driver_adc_args.tout, 0);
    do {
        if (driver_adc_args.tpad->count) {
            fprintf(stderr, "\rTouch pad: %4d", tpad_read());
        } else if (driver_adc_args.joy->count) {
            int xy, dx, dy;
            if (( xy = adc_joystick(&dx, &dy) ) == -1) {
                fprintf(stderr, "\rCould not read joystick value");
                break;
            }
            fprintf(stderr, "\rJoystick: x %3d y %3d (%4d %4d)",
                    xy >> 16, xy & 0xFFFF, dx, dy);
        } else if (driver_adc_args.hall->count) {
            fprintf(stderr, "\rADC hall: %4d", adc_hall());
        } else if (idx < 0 || idx > 1) {
            fprintf(stderr, "\rADC: %4dmV %4dmV", adc_read(0), adc_read(1));
        } else {
            fprintf(stderr, "\rADC %d: %4dmV", idx, adc_read(idx));
        }
        if (tout_ms >= intv_ms) {
            fprintf(stderr, " (remain %3ds)", tout_ms / 1000);
            fflush(stderr);
            tout_ms -= intv_ms;
            msleep(intv_ms);
        }
    } while (tout_ms >= intv_ms);
    fputc('\n', stderr);
    return ESP_OK;
}
#endif

#ifdef CONSOLE_DRIVER_DAC
static struct {
    arg_int_t *val;
    arg_str_t *cos;
    arg_int_t *frq;
    arg_int_t *amp;
    arg_end_t *end;
} driver_dac_args = {
    .val = arg_int0(NULL, NULL, "0-255", "output value / offset of wave"),
    .cos = arg_str0(NULL, "cos", "on|off", "cosine wave enable / disable"),
    .frq = arg_int0("f", NULL, "130-55000", "frequency of cosine wave"),
    .amp = arg_int0("a", NULL, "0-3", "amplitude of cosine wave"),
    .end = arg_end(3)
};

static int driver_dac(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_dac_args);
    static uint8_t val, amp;
    static uint16_t freq;
    static bool cwave;
    const char * cos = ARG_STR(driver_dac_args.cos, NULL);
    int v = ARG_INT(driver_dac_args.val, -1);
    int f = ARG_INT(driver_dac_args.frq, -1);
    int a = ARG_INT(driver_dac_args.amp, -1);
    if (v != -1) val = v;
    if (a != -1) {
        if (a < 0 || a > 3) return ESP_ERR_INVALID_ARG;
        amp = a;
    }
    if (f != -1) {
        if (f < 130 || f > 55000) return ESP_ERR_INVALID_ARG;
        freq = f;
    }
    if (cos && cwave != strbool(cos)) {
        if (!freq) f = freq = 130;
        cwave = !cwave;
        v = val;
    }
    if (cwave && (f != -1 || a != -1 || v != -1)) {
        return dac_cwave((freq << 16) | (amp << 8) | val);
    } else if (!cwave && v != -1) {
        return dac_write(val);
    }
    if (cwave) {
        printf("DAC: cosine wave %dHz %d±%dmV\n",
               freq, 3300 * val / 255, 3300 / (1 << amp) / 2);
    } else {
        printf("DAC: output %dmV\n", 3300 * val / 255);
    }
    return ESP_OK;
}
#endif

#ifdef CONSOLE_DRIVER_PWM
static struct {
    arg_int_t *hdeg;
    arg_int_t *vdeg;
    arg_int_t *freq;
    arg_int_t *pcnt;
    arg_end_t *end;
} driver_pwm_args = {
    .hdeg = arg_int0("y", NULL, "0-180", "yaw degree"),
    .vdeg = arg_int0("p", NULL, "0-160", "pitch degree"),
    .freq = arg_int0("f", NULL, "0-5000", "tone frequency"),
    .pcnt = arg_int0("l", NULL, "0-100", "tone loudness (percentage)"),
    .end  = arg_end(4)
};

static int driver_pwm(int argc, char **argv) {
    ARG_PARSE(argc, argv, &driver_pwm_args);
    int hdeg = ARG_INT(driver_pwm_args.hdeg, -1),
        vdeg = ARG_INT(driver_pwm_args.vdeg, -1),
        pcnt = ARG_INT(driver_pwm_args.pcnt, -1),
        freq = ARG_INT(driver_pwm_args.freq, -1);
    if (hdeg >= 0 || vdeg >= 0)
        return pwm_set_degree(hdeg, vdeg);
    if (freq >= 0 || pcnt >= 0)
        return pwm_set_tone(freq, pcnt);
    esp_err_t err = ESP_OK;
    if (!( err = pwm_get_degree(&hdeg, &vdeg) ))
        printf("PWM Degree: %d %d\n", hdeg, vdeg);
    if (!( err = pwm_get_tone(&freq, &pcnt) ))
        printf("PWM Tone: %dHz %d%%\n", freq, pcnt);
    return err;
}
#endif // CONSOLE_DRIVER_PWM

static void register_driver() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_DRIVER_GPIO
        ESP_CMD_ARG(driver, gpio, "Set / get GPIO pin level"),
#endif
#ifdef CONSOLE_DRIVER_USB
        ESP_CMD_ARG(driver, usb, "Set / get USB working mode"),
#endif
#ifdef CONSOLE_DRIVER_LED
        ESP_CMD_ARG(driver, led, "Set / get LED color / brightness"),
#endif
#ifdef CONSOLE_DRIVER_I2C
        ESP_CMD_ARG(driver, i2c, "Detect alive I2C slaves on the bus line"),
#endif
#ifdef CONSOLE_DRIVER_SCN
        ESP_CMD_ARG(driver, scn, "Control screen drawing"),
#endif
#ifdef CONSOLE_DRIVER_ALS
        ESP_CMD_ARG(driver, als, "Get ALS sensor values and light tracking"),
#endif
#ifdef CONSOLE_DRIVER_ADC
        ESP_CMD_ARG(driver, adc, "Read ADC and calculate value in mV"),
#endif
#ifdef CONSOLE_DRIVER_DAC
        ESP_CMD_ARG(driver, dac, "Write DAC and calculate value in mV"),
#endif
#ifdef CONSOLE_DRIVER_PWM
        ESP_CMD_ARG(driver, pwm, "Control rotation of servo by PWM"),
#endif
    };
    register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Utilities commands
 */

#ifdef CONSOLE_UTILS_VERSION
static int utils_version(int c, char **v) { version_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_LSHW
static int utils_lshw(int c, char **v) { hardware_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_LSPART
static int utils_lspart(int c, char **v) { partition_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_LSTASK
static struct {
    arg_int_t *sort;
    arg_end_t *end;
} utils_lstask_args = {
    .sort = arg_int0(NULL, NULL, "0-6", "sort by column index"),
    .end  = arg_end(1)
};

static int utils_lstask(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_lstask_args);
    task_info(ARG_INT(utils_lstask_args.sort, 2));
    return ESP_OK;
}
#endif

#ifdef CONSOLE_UTILS_LSMEM
static struct {
    arg_lit_t *verbose;
    arg_end_t *end;
} utils_lsmem_args = {
    .verbose = arg_litn("v", NULL, 0, 2, "additive option for more output"),
    .end     = arg_end(1)
};

static int utils_lsmem(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_lsmem_args);
    switch (utils_lsmem_args.verbose->count) {
    case 0: memory_info(); break;
    case 2: heap_caps_print_heap_info(MALLOC_CAP_DMA);
            heap_caps_print_heap_info(MALLOC_CAP_EXEC); FALLTH;
    case 1: heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
            heap_caps_print_heap_info(MALLOC_CAP_INTERNAL); break;
    }
    return ESP_OK;
}
#endif // CONSOLE_UTILS_LSMEM

#ifdef CONSOLE_UTILS_LSFS
static struct {
    arg_str_t *dir;
    arg_lit_t *ext;
    arg_end_t *end;
} utils_lsfs_args = {
    .dir = arg_str0(NULL, NULL, "abspath", NULL),
    .ext = arg_lit0("d", "sdcard", "target SDCard instead of Flash"),
    .end = arg_end(2)
};

static int utils_lsfs(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_lsfs_args);
    filesys_listdir(
        utils_lsfs_args.ext->count ? FILESYS_SDCARD : FILESYS_FLASH,
        ARG_STR(utils_lsfs_args.dir, "/"), stdout);
    return ESP_OK;
}
#endif // CONSOLE_UTILS_LSFS

#ifdef CONSOLE_UTILS_HID
static struct {
    arg_str_t *key;
    arg_str_t *mse;
    arg_str_t *dial;
    arg_int_t *tout;
    arg_str_t *tgt;
    arg_end_t *end;
} utils_hid_args = {
    .key  = arg_str0("k", NULL, "CODE", "HID report keypress"),
    .mse  = arg_str0("m", NULL, "B|XYVH", "HID report mouse"),
    .dial = arg_str0("d", NULL, "BLRUD", "HID report S-Dial"),
    .tout = arg_int0("t", NULL, "0-65535", "key/mouse timeout in ms"),
    .tgt  = arg_str0(NULL, "to", "0-2|UBS", "report to USB/BLE/SCN"),
    .end  = arg_end(5)
};

static int utils_hid(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_hid_args);
    esp_err_t err = ESP_OK;
    static const char *choices = "UBS", *c;
    const char *mouse = ARG_STR(utils_hid_args.mse, NULL);
    const char *press = ARG_STR(utils_hid_args.key, NULL);
    const char *dial = ARG_STR(utils_hid_args.dial, NULL);
    const char *tstr = ARG_STR(utils_hid_args.tgt, NULL);
    uint16_t tout_ms = ARG_INT(utils_hid_args.tout, 50);

    hid_target_t to = HID_TARGET_ALL;
    if (tstr) {
        if ('0' <= tstr[0] && tstr[0] <= '2') {
            to = (hid_target_t)(tstr[0] - '0');
        } else if (( c = strchr(choices, tstr[0]) )) {
            to = (hid_target_t)(c - choices);
        } else {
            printf("Invalid target to send: `%s`\n", tstr);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (dial) {
        switch (dial[0]) {
        case 'l': FALLTH; case 'L': hid_report_dial(to, DIAL_L); break;
        case 'r': FALLTH; case 'R': hid_report_dial(to, DIAL_R); break;
        case 'u': FALLTH; case 'U': hid_report_dial(to, DIAL_UP); break;
        case 'd': FALLTH; case 'D': hid_report_dial(to, DIAL_DN); break;
        default: if (strbool(dial)) hid_report_dial_button(to, tout_ms);
        }
    }
    if (mouse) {
        int num, vals[4] = { 0 };
        if (!strcasecmp(mouse, "square")) {
            hid_report_mouse_move(to, 50, 0); msleep(250);
            hid_report_mouse_move(to, 0, 50); msleep(250);
            hid_report_mouse_move(to, -50, 0); msleep(250);
            hid_report_mouse_move(to, 0, -50); msleep(250);
        } else if (!( num = parse_all(mouse, vals, LEN(vals)) )) {
            hid_report_mouse_click(to, mouse, tout_ms);
        } else if (num == 1) {
            hid_report_mouse_button(to, vals[0]);
        } else {
            hid_report_mouse(to, 0, vals[0], vals[1], vals[2], vals[3]);
        }
    }
    if (press) {
        hid_report_keyboard_press(to, press, tout_ms);
    }
    return err;
}
#endif // CONSOLE_UTILS_HID

#ifdef CONSOLE_UTILS_CONFIG
static struct {
    arg_str_t *key;
    arg_str_t *val;
    arg_lit_t *load;
    arg_lit_t *save;
    arg_lit_t *stat;
    arg_lit_t *list;
    arg_lit_t *lall;
    arg_end_t *end;
} utils_config_args = {
    .key  = arg_str0(NULL, NULL, "KEY", "specify config by key"),
    .val  = arg_str0(NULL, NULL, "VAL", "set config value"),
    .load = arg_lit0(NULL, "load", "load from NVS flash"),
    .save = arg_lit0(NULL, "save", "save to NVS flash"),
    .stat = arg_lit0(NULL, "stat", "summary NVS status"),
    .list = arg_lit0(NULL, "list", "list config NVS entries"),
    .lall = arg_lit0(NULL, "list_all", "list all NVS entries"),
    .end  = arg_end(7)
};

static int utils_config(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_config_args);
    bool ret = true;
    const char *key = ARG_STR(utils_config_args.key, NULL);
    const char *val = ARG_STR(utils_config_args.val, NULL);
    if (key) {
        if (val) {
            printf("Set `%s` to `%s` %s\n", key, val,
                   (ret = config_set(key, val)) ? "done" : "fail");
        } else {
            printf("Get `%s` value `%s`\n", key, config_get(key));
        }
    } else if (utils_config_args.load->count) {
        ret = config_nvs_load();
    } else if (utils_config_args.save->count) {
        ret = config_nvs_dump();
    } else if (utils_config_args.stat->count) {
        config_nvs_stats();
    } else if (utils_config_args.list->count) {
        config_nvs_list(false);
    } else if (utils_config_args.lall->count) {
        config_nvs_list(true);
    } else {
        config_list();
    }
    return ret ? ESP_OK : ESP_FAIL;
}
#endif // CONSOLE_UTILS_CONFIG

#ifdef CONSOLE_UTILS_LOGGING
static struct {
    arg_str_t *tag;
    arg_str_t *lvl;
    arg_lit_t *log;
    arg_end_t *end;
} utils_logging_args = {
    .tag = arg_str0(NULL, NULL, "TAG", "specify tag of the log entries"),
    .lvl = arg_str0(NULL, NULL, "0-5|NEWIDV", "set logging level"),
    .log = arg_lit0(NULL, "test", "test logging with specified tag"),
    .end = arg_end(3)
};

static int utils_logging(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_logging_args);
    static const char *choices = "NEWIDV", *c;
    const char *tag = ARG_STR(utils_logging_args.tag, "*");
    const char *lvl = ARG_STR(utils_logging_args.lvl, NULL);
    if (lvl) {
        if (!strcmp(tag, "*")) {
            printf("Invalid tag to set: `%s`\n", tag);
            return ESP_ERR_INVALID_ARG;
        }
        if ('0' <= lvl[0] && lvl[0] <= '5') {
            esp_log_level_set(tag, (esp_log_level_t)(lvl[0] - '0'));
        } else if (( c = strchr(choices, lvl[0]) )) {
            esp_log_level_set(tag, (esp_log_level_t)(c - choices));
        } else {
            printf("Invalid level to set: `%s`\n", lvl);
            return ESP_ERR_INVALID_ARG;
        }
    }
    printf("Logging level of %s is %c\n", tag, choices[esp_log_level_get(tag)]);
    if (utils_logging_args.log->count && strcmp(tag, "*")) {
        for (int i = 1; i < strlen(choices); i++) {
            ESP_LOG_LEVEL(i, tag, "Logging at %c", choices[i]);
        }
    }
    return ESP_OK;
}
#endif // CONSOLE_UTILS_LOGGING

#ifdef CONSOLE_UTILS_HISTORY
static struct {
    arg_str_t *cmd;
    arg_str_t *dst;
    arg_lit_t *ext;
    arg_end_t *end;
} utils_hist_args = {
    .cmd = arg_str1(NULL, NULL, "load|save", ""),
    .dst = arg_str0("f", NULL, "history.txt", "relative path to file"),
    .ext = arg_lit0("d", "sdcard", "target SDCard instead of Flash"),
    .end = arg_end(3)
};

static int utils_hist(int argc, char **argv) {
    ARG_PARSE(argc, argv, &utils_hist_args);
    esp_err_t err = ESP_ERR_INVALID_ARG;
    const char *cmd = utils_hist_args.cmd->sval[0];
    const char *dst = ARG_STR(utils_hist_args.dst, "history.txt");
    bool save = false;
    if (strstr(cmd, "save")) {
        save = true;
    } else if (!strstr(cmd, "load")) {
        printf("Invalid command: `%s`\n", cmd);
        return err;
    }
    char path[strlen(Config.sys.DIR_DATA) + strlen(dst) + 1];
    sprintf(path, "%s%s", Config.sys.DIR_DATA, dst);
    bool exists = filesys_exists(
        utils_lsfs_args.ext->count ? FILESYS_SDCARD : FILESYS_FLASH, path);
    if (!exists && !save) {
        printf("History file `%s` does not exist\n", path);
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = save ? linenoiseHistorySave(path) : linenoiseHistoryLoad(path);
        printf("History file `%s` %s %s\n", path, cmd, err ? "fail" : "done");
    }
    return err;
}
#endif // CONSOLE_UTILS_HISTORY

static void register_utils() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_UTILS_VERSION
        ESP_CMD(utils, version, "Get version of firmware and SDK"),
#endif
#ifdef CONSOLE_UTILS_LSHW
        ESP_CMD(utils, lshw, "Print hardware information"),
#endif
#ifdef CONSOLE_UTILS_LSPART
        ESP_CMD(utils, lspart, "Enumerate partitions in flash"),
#endif
#ifdef CONSOLE_UTILS_LSTASK
        ESP_CMD_ARG(utils, lstask, "Enumerate running RTOS tasks"),
#endif
#ifdef CONSOLE_UTILS_LSMEM
        ESP_CMD_ARG(utils, lsmem, "List memory info"),
#endif
#ifdef CONSOLE_UTILS_LSFS
        ESP_CMD_ARG(utils, lsfs, "List file system directories and files"),
#endif
#ifdef CONSOLE_UTILS_HID
        ESP_CMD_ARG(utils, hid, "Send HID report through USB / BT"),
#endif
#ifdef CONSOLE_UTILS_CONFIG
        ESP_CMD_ARG(utils, config, "Set / get / load / save / list configs"),
#endif
#ifdef CONSOLE_UTILS_LOGGING
        ESP_CMD_ARG(utils, logging, "Set / get ESP logging level"),
#endif
#ifdef CONSOLE_UTILS_HISTORY
        ESP_CMD_ARG(utils, hist, "Dump / load console history from flash"),
#endif
    };
    register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * WiFi commands
 */

#ifdef CONSOLE_NET_STA
static struct {
    arg_str_t *cmd;
    arg_str_t *ssid;
    arg_str_t *pass;
    arg_int_t *tout;
    arg_end_t *end;
} net_sta_args = {
    .cmd  = arg_str0(NULL, NULL, "scan|join|leave", ""),
    .ssid = arg_str0("s", NULL, "SSID", "AP hostname"),
    .pass = arg_str0("p", NULL, "PASS", "AP password"),
    .tout = arg_int0("t", NULL, "0-65535", "scan/join timeout in ms"),
    .end  = arg_end(4)
};

static int net_sta(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_sta_args);
    const char * cmd = ARG_STR(net_sta_args.cmd, "");
    uint16_t tout_ms = ARG_INT(net_sta_args.tout, 0);
    if (strstr(cmd, "scan")) {
        return wifi_sta_scan(ARG_STR(net_sta_args.ssid, NULL), 0, tout_ms, 1);
    } else if (strstr(cmd, "join")) {
        const char *ssid = ARG_STR(net_sta_args.ssid, NULL);
        const char *pass = ARG_STR(net_sta_args.pass, (ssid ? "" : NULL));
        esp_err_t err = wifi_sta_start(ssid, pass, NULL);
        if (!err && tout_ms) err = wifi_sta_wait(tout_ms);
        return err;
    } else if (strstr(cmd, "leave")) {
        return wifi_sta_stop();
    }
    wifi_sta_list_ap();
    return ESP_OK;
}
#endif // CONSOLE_NET_STA

#ifdef CONSOLE_NET_AP
static struct {
    arg_str_t *cmd;
    arg_str_t *ssid;
    arg_str_t *pass;
    arg_end_t *end;
} net_ap_args = {
    .cmd  = arg_str0(NULL, NULL, "start|stop", ""),
    .ssid = arg_str0("s", NULL, "SSID", "AP hostname"),
    .pass = arg_str0("p", NULL, "PASS", "AP password"),
    .end  = arg_end(3)
};

static int net_ap(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_ap_args);
    const char *cmd = ARG_STR(net_ap_args.cmd, "");
    if (strstr(cmd, "start")) {
        const char *ssid = ARG_STR(net_ap_args.ssid, NULL);
        const char *pass = ARG_STR(net_ap_args.pass, (ssid ? "" : NULL));
        return wifi_ap_start(ssid, pass, NULL);
    } else if (strstr(cmd, "stop")) {
        return wifi_ap_stop();
    }
    wifi_ap_list_sta();
    return ESP_OK;
}
#endif // CONSOLE_NET_AP

#ifdef CONSOLE_NET_BT
static struct {
    arg_str_t *mode;
    arg_lit_t *now;
    arg_lit_t *scan;
    arg_int_t *tout;
    arg_int_t *bat;
    arg_str_t *dev;
    arg_end_t *end;
} net_bt_args = {
    .mode = arg_str0(NULL, NULL, "0-2|dDH", "specify BT mode"),
    .now  = arg_lit0(NULL, "now", "reboot now"),
    .scan = arg_lit0(NULL, "scan", "run BT/BLE scan"),
    .tout = arg_int0("t", NULL, "0-65535", "scan timeout in ms"),
    .bat  = arg_int0("b", NULL, "0-100", "BLE report battery level"),
    .dev  = arg_str0("c", NULL, "BDA", "connect to BLE device"),
    .end  = arg_end(6)
};

static int net_bt(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_bt_args);
    static const char *choices = "dDH", *c;
    esp_err_t err = ESP_OK;
    bool reboot = net_bt_args.now->count;
    int bat = ARG_INT(net_bt_args.bat, -1);
    const char *name = ARG_STR(net_bt_args.dev, NULL);
    const char *mode = ARG_STR(net_bt_args.mode, NULL);
    if (net_bt_args.scan->count) {
        err = btmode_scan(ARG_INT(net_bt_args.tout, 0));
    } else if (bat != -1) {
        err = btmode_battery(CONS(bat, 0, 100));
    } else if (name) {
        scan_rst_t *dev = btmode_find_device(name, NULL);
        return dev ? btmode_connect(dev) : ESP_ERR_NOT_FOUND;
    } else if (!mode) {
        btmode_status();
    } else if ('0' <= mode[0] && mode[0] <= '2') {
        err = btmode_switch((btmode_t)(mode[0] - '0'), reboot);
    } else if (( c = strchr(choices, mode[0]) )) {
        err = btmode_switch((btmode_t)(c - choices), reboot);
    } else {
        printf("Invalid mode to set: `%s`\n", mode);
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}
#endif // CONSOLE_NET_BT

#ifdef CONSOLE_NET_FTM
static struct {
    arg_str_t *ssid;
    arg_int_t *npkt;
    arg_lit_t *rep;
    arg_str_t *ctrl;
    arg_int_t *base;
    arg_end_t *end;
} net_ftm_args = {
    .ssid = arg_str0(NULL, NULL, "SSID", "initiator target AP hostname"),
    .npkt = arg_int0("n", NULL, "0-32|64", "initiator frame count"),
    .rep  = arg_lit0(NULL, "resp", "control responder"),
    .ctrl = arg_str0("c", NULL, "on|off", "responder enable / disable"),
    .base = arg_int0("o", NULL, "NUM", "responder T1 offset in cm"),
    .end  = arg_end(5)
};

static int net_ftm(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_ftm_args);
    if (net_ftm_args.rep->count) {
        return ftm_respond(
            ARG_STR(net_ftm_args.ctrl, NULL),
            ARG_INT(net_ftm_args.base, 0));
    } else {
        return ftm_request(
            ARG_STR(net_ftm_args.ssid, NULL),
            ARG_INT(net_ftm_args.npkt, -1));
    }
}
#endif

#ifdef CONSOLE_NET_MDNS
static struct {
    arg_str_t *ctrl;
    arg_str_t *host;
    arg_str_t *serv;
    arg_str_t *prot;
    arg_int_t *tout;
    arg_end_t *end;
} net_mdns_args = {
    .ctrl = arg_str0(NULL, NULL, "on|off", "enable / disable"),
    .host = arg_str0("h", NULL, "HOST", "mDNS hostname to query"),
    .serv = arg_str0("s", NULL, "http|smb", "mDNS service to query"),
    .prot = arg_str0("p", NULL, "tcp|udp", "mDNS protocol to query"),
    .tout = arg_int0("t", NULL, "0-65535", "query timeout in ms"),
    .end  = arg_end(5)
};

static int net_mdns(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_mdns_args);
    return mdns_command(
        ARG_STR(net_mdns_args.ctrl, NULL),
        ARG_STR(net_mdns_args.host, NULL),
        ARG_STR(net_mdns_args.serv, NULL),
        ARG_STR(net_mdns_args.prot, NULL),
        ARG_INT(net_mdns_args.tout, 0)
    );
}
#endif

#ifdef CONSOLE_NET_SNTP
static struct {
    arg_str_t *ctrl;
    arg_str_t *host;
    arg_str_t *mode;
    arg_int_t *intv;
    arg_end_t *end;
} net_sntp_args = {
    .ctrl = arg_str0(NULL, NULL, "on|off", "enable / disable"),
    .host = arg_str0("h", NULL, "HOST", "SNTP server name or address"),
    .mode = arg_str0("m", NULL, "immed|smooth", "SNTP time sync mode"),
    .intv = arg_int0("i", NULL, "0-2^31", "interval between sync in ms"),
    .end  = arg_end(4)
};

static int net_sntp(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_sntp_args);
    return sntp_command(
        ARG_STR(net_sntp_args.ctrl, NULL),
        ARG_STR(net_sntp_args.host, NULL),
        ARG_STR(net_sntp_args.mode, NULL),
        ARG_INT(net_sntp_args.intv, 0)
    );
}
#endif

#ifdef CONSOLE_NET_PING
static struct {
    arg_str_t *host;
    arg_int_t *intv;
    arg_int_t *size;
    arg_int_t *npkt;
    arg_lit_t *stop;
    arg_lit_t *dry;
    arg_end_t *end;
} net_ping_args = {
    .host = arg_str1(NULL, NULL, "HOST", "target hostname or IP address"),
    .intv = arg_int0("i", NULL, "0-65535", "interval between ping in ms"),
    .size = arg_int0("l", NULL, "LEN", "number of data bytes to be sent"),
    .npkt = arg_int0("n", NULL, "NUM", "stop after sending num packets"),
    .stop = arg_lit0(NULL, "stop", "stop currently running ping session"),
    .dry  = arg_lit0(NULL, "dryrun", "print IP address and stop (dryrun)"),
    .end  = arg_end(6)
};

static int net_ping(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_ping_args);
    if (net_ping_args.dry->count)
        return wifi_parse_addr(net_ping_args.host->sval[0], NULL);
    return ping_command(
        net_ping_args.host->sval[0],
        ARG_INT(net_ping_args.intv, 0),
        ARG_INT(net_ping_args.size, 0),
        ARG_INT(net_ping_args.npkt, 0),
        net_ping_args.stop->count
    );
}
#endif

#ifdef CONSOLE_NET_IPERF
static struct {
    arg_lit_t *serv;
    arg_str_t *host;
    arg_int_t *port;
    arg_int_t *size;
    arg_int_t *intv;
    arg_int_t *tout;
    arg_lit_t *udp;
    arg_lit_t *stop;
    arg_end_t *end;
} net_iperf_args = {
    .serv = arg_lit0("s", NULL, "run in server mode"),
    .host = arg_str0("c", NULL, "HOST", "run in client mode"),
    .port = arg_int0("p", NULL, "PORT", "specify port number"),
    .size = arg_int0("l", NULL, "LEN", "read/write buffer size"),
    .intv = arg_int0("i", NULL, "0-255", "time between reports in seconds"),
    .tout = arg_int0("t", NULL, "0-255", "session timeout in seconds"),
    .udp  = arg_lit0("u", "udp", "use UDP rather than TCP"),
    .stop = arg_lit0(NULL, "stop", "stop currently running iperf"),
    .end  = arg_end(8)
};

static int net_iperf(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_iperf_args);
    const char *host = net_iperf_args.serv->count ? NULL : "";
    return iperf_command(
        ARG_STR(net_iperf_args.host, host),
        ARG_INT(net_iperf_args.port, 0),
        ARG_INT(net_iperf_args.size, 0),
        ARG_INT(net_iperf_args.intv, 1),
        ARG_INT(net_iperf_args.tout, 0),
        net_iperf_args.udp->count,
        net_iperf_args.stop->count
    );
}
#endif

#ifdef CONSOLE_NET_TSYNC
static struct {
    arg_lit_t *serv;
    arg_str_t *host;
    arg_int_t *port;
    arg_int_t *tout;
    arg_lit_t *stat;
    arg_lit_t *stop;
    arg_end_t *end;
} net_tsync_args = {
    .serv = arg_lit0("s", NULL, "run in server mode"),
    .host = arg_str0("c", NULL, "HOST", "run in client mode"),
    .port = arg_int0("p", NULL, "PORT", "specify port number"),
    .tout = arg_int0("t", NULL, "0-2^31", "timeout in ms"),
    .stat = arg_lit0(NULL, "stat", "print service summary"),
    .stop = arg_lit0(NULL, "stop", "stop currently running task"),
    .end  = arg_end(6)
};

static int net_tsync(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_tsync_args);
    if (net_tsync_args.stat->count) {
        timesync_server_status();
        return ESP_OK;
    }
    const char *host = net_tsync_args.serv->count ? NULL : "";
    return tsync_command(
        ARG_STR(net_tsync_args.host, host),
        ARG_INT(net_tsync_args.port, 0),
        ARG_INT(net_tsync_args.tout, 0),
        net_tsync_args.stop->count
    );
}
#endif

static void register_network() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_NET_STA
        ESP_CMD_ARG(net, sta, "Query / Scan / Connect / Disconnect APs"),
#endif
#ifdef CONSOLE_NET_AP
        ESP_CMD_ARG(net, ap, "Query / Start / Stop SoftAP"),
#endif
#ifdef CONSOLE_NET_BT
        ESP_CMD_ARG(net, bt, "Set / get BT working mode"),
#endif
#ifdef CONSOLE_NET_FTM
        ESP_CMD_ARG(net, ftm, "RTT Fine Timing Measurement between STA & AP"),
#endif
#ifdef CONSOLE_NET_MDNS
        ESP_CMD_ARG(net, mdns, "Query / Set mDNS hostname and service info"),
#endif
#ifdef CONSOLE_NET_SNTP
        ESP_CMD_ARG(net, sntp, "Query / Set SNTP server and sync status"),
#endif
#ifdef CONSOLE_NET_PING
        ESP_CMD_ARG(net, ping, "Send ICMP ECHO_REQUEST to specified hosts"),
#endif
#ifdef CONSOLE_NET_IPERF
        ESP_CMD_ARG(net, iperf, "Bandwidth test on IP networks"),
#endif
#ifdef CONSOLE_NET_TSYNC
        ESP_CMD_ARG(net, tsync, "TimeSync protocol daemon and client"),
#endif
    };
    register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Export register commands
 */

// Put this variable into RTC memory to maintain the value during deep sleep
RTC_DATA_ATTR static uint32_t boot_count = 0;

static void optimize_datatype() {
#ifdef CONSOLE_SYSTEM_SLEEP
    static char sb[6];
    snprintf(sb, sizeof(sb), "0-%d", GPIO_PIN_COUNT - 1);
    system_sleep_args.pin->hdr.datatype = sb;
#endif
#ifdef CONSOLE_DRIVER_LED
    static char lb[8], bb[8];
    snprintf(lb, sizeof(lb), "0-%d", CONFIG_BASE_LED_NUM - 1);
    snprintf(bb, sizeof(bb), "-1|0-%d", LED_BLINK_MAX - 1);
    driver_led_args.idx->hdr.datatype = lb;
    driver_led_args.blk->hdr.datatype = bb;
#endif
#ifdef CONSOLE_DRIVER_GPIO
    static char gb[22];
    size_t gl = sizeof(gb), gs = snprintf(gb, gl, "0-%d", GPIO_PIN_COUNT - 1);
#   ifdef CONFIG_BASE_GPIOEXP_I2C
    gs += snprintf(gb + gs, gl - gs, "|%d-%d", PIN_I2C_BASE, PIN_I2C_MAX - 1);
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_SPI
    gs += snprintf(gb + gs, gl - gs, "|%d-%d", PIN_SPI_BASE, PIN_SPI_MAX - 1);
#   endif
    driver_gpio_args.pin->hdr.datatype = gb; NOTUSED(gs);
#endif
}

extern "C" void console_register_commands() {
    if (boot_count++) fputs("Woken up from deep sleep mode", stderr);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    const esp_console_cmd_t clear = {
        .command = "clear",
        .help = "Clean screen",
        .hint = NULL,
        .func = [] (int c, char **v) -> int {
            linenoiseClearScreen();
            return ESP_OK;
        },
        .argtable = NULL
    };
    ESP_ERROR_CHECK( esp_console_register_help_command() );
    ESP_ERROR_CHECK( esp_console_cmd_register(&clear) );
    optimize_datatype();
    register_network();
    register_driver();
    register_system();
    register_utils();
}
