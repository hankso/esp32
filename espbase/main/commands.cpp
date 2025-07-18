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
#include "avcmode.h"
#include "ledmode.h"
#include "usbmode.h"
#include "btmode.h"
#include "screen.h"
#include "timesync.h"

#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "rom/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#ifndef CONFIG_BASE_USE_ELF
#   undef CONSOLE_SYS_EXEC
#endif

#ifndef CONFIG_BASE_USE_USB
#   undef CONSOLE_DRV_USB
#endif
#ifndef CONFIG_BASE_USE_LED
#   undef CONSOLE_DRV_LED
#endif
#ifndef CONFIG_BASE_USE_I2C
#   undef CONSOLE_DRV_I2C
#endif
#ifndef CONFIG_BASE_USE_ADC
#   undef CONSOLE_DRV_ADC
#endif
#ifndef CONFIG_BASE_USE_DAC
#   undef CONSOLE_DRV_DAC
#endif
#if !defined(CONFIG_BASE_USE_SERVO) && !defined(CONFIG_BASE_USE_BUZZER)
#   undef CONSOLE_DRV_PWM
#endif

#if !defined(CONFIG_BASE_USE_FFS) && !defined(CONFIG_BASE_USE_SDFS)
#   undef CONSOLE_UTIL_LSFS
#   undef CONSOLE_UTIL_HISTORY
#endif

#ifndef CONFIG_BASE_USE_BT
#   undef  CONSOLE_NET_BT
#endif
#ifndef CONFIG_BASE_USE_WIFI
#   undef  CONSOLE_NET_STA
#   undef  CONSOLE_NET_AP
#   undef  CONSOLE_NET_FTM
#   undef  CONSOLE_NET_MDNS
#   undef  CONSOLE_NET_SNTP
#   undef  CONSOLE_NET_PING
#   undef  CONSOLE_NET_IPERF
#   undef  CONSOLE_NET_TSYNC
#endif
#ifndef CONFIG_ESP_WIFI_FTM_ENABLE
#   undef CONSOLE_NET_FTM
#endif

#if !defined(CONFIG_BASE_USE_USB) \
 && !defined(CONFIG_BASE_USE_BT)  \
 && !defined(CONFIG_BASE_USE_SCREEN)
#   undef CONSOLE_APP_HID
#endif
#ifndef CONFIG_BASE_USE_SCREEN
#   undef CONSOLE_APP_SCN
#endif
#ifndef CONFIG_BASE_ALS_TRACK
#   undef CONSOLE_APP_ALS
#endif

static const char * TAG = "Command";

/******************************************************************************
 * Some common utilities
 */

static bool parse_noerror(int argc, char **argv, void **argtable) {
    LOOPN(i, argc) {
        if (!strcmp(argv[i], "--help")) {
            printf("Usage: %s", argv[0]);
            arg_print_syntax(stdout, argtable, "\n");
            arg_print_glossary(stdout, argtable, "  %-20s %s\n");
            return false;
        }
    }
    if (arg_parse(argc, argv, argtable) != 0) {
        arg_hdr_t **table = (arg_hdr_t **)argtable;
        int tabindex = 0;
        while (!(table[tabindex]->flag & ARG_TERMINATOR)) { tabindex++; }
        arg_print_errors(stdout, (arg_end_t *)table[tabindex], argv[0]);
        printf("Try '%s --help' for more information\n", argv[0]);
        return false;
    }
    return true;
}

#define ARG_PARSE(c, v, t)                                                  \
        do {                                                                \
            if (!parse_noerror((c), (v), (void **)(t)))                     \
                return ESP_ERR_CONSOLE_ARGPARSE;                            \
        } while (0)

static esp_err_t register_commands(const esp_console_cmd_t *cmds, size_t num) {
    esp_err_t err = ESP_OK;
    LOOPN(i, num) { if (!err) err = esp_console_cmd_register(cmds + i); }
    return err;
}

#define ARG_STR(p, v)   ((p)->count ? (p)->sval[0] : (v))
#define ARG_INT(p, v)   ((p)->count ? (p)->ival[0] : (v))
#define ARG_DBL(p, v)   ((p)->count ? (p)->dval[0] : (v))

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

#ifdef CONSOLE_SYS_RESTART
static struct {
    arg_lit_t *halt;
    arg_lit_t *cxel;
    arg_int_t *tout;
    arg_end_t *end;
} sys_restart_args = {
    .halt = arg_lit0("h", "halt", "shutdown instead of reboot"),
    .cxel = arg_lit0("c", "cancel", "cancel pending reboot (if available)"),
    .tout = arg_int0("t", NULL, "0-65535", "reboot timeout in ms"),
    .end  = arg_end(3)
};

static void sys_restart_task(void *arg) {
    uint32_t tout_ms = arg ? *(uint32_t *)arg : 0;
    if (tout_ms && tout_ms != 0xDEADBEEF) {
        ESP_LOGW(TAG, "Will restart in %ums ...", tout_ms);
        msleep(tout_ms);
        tout_ms = 0;
    }
    if (tout_ms) {
        esp_system_abort("Manually shutdown");
    } else {
        esp_restart();
    }
}

static int sys_restart(int argc, char **argv) {
    ARG_PARSE(argc, argv, &sys_restart_args);
    TaskHandle_t task = xTaskGetHandle("restart");
    static uint32_t end_ms, tout_ms;
    if (sys_restart_args.cxel->count && task) {
        puts("Restart cancelled");
        vTaskDelete(task);
    } else if (task) {
        printf("Restart pending: %.0fms", end_ms - get_timestamp(0) * 1e3);
    } else {
        tout_ms = ABS(ARG_INT(sys_restart_args.tout, 0));
        end_ms = (uint32_t)(get_timestamp(0) * 1e3) + tout_ms;
        if (sys_restart_args.halt->count) tout_ms = 0xDEADBEEF;
        xTaskCreate(sys_restart_task, "restart", 4096, &tout_ms, 99, &task);
        if (!task) sys_restart_task(NULL);
    }
    return ESP_OK;
}
#endif

#ifdef CONSOLE_SYS_SLEEP
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
} sys_sleep_args = {
    .mode = arg_str0(NULL, NULL, "light|deep", "sleep mode [default light]"),
    .tout = arg_int0("t", NULL, "0-2^31", "wakeup timeout in ms [default 0]"),
    .pin  = arg_intn("p", NULL, NULL, 0, 8, "wakeup from GPIO[s]"),
    .lvl  = arg_intn("l", NULL, "0|1", 0, 8, "GPIO level[s] to detect"),
    .end  = arg_end(4)
};

static esp_err_t enable_gpio_light_wakeup() {
    int pin_cnt = sys_sleep_args.pin->count;
    int lvl_cnt = sys_sleep_args.lvl->count;
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
        pin = (gpio_num_t)sys_sleep_args.pin->ival[i];
        lvl = lvl_cnt ? sys_sleep_args.lvl->ival[i] : 0;
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
    int pin_cnt = sys_sleep_args.pin->count;
    if (!pin_cnt) return ESP_OK;
    int lvl = ARG_INT(sys_sleep_args.lvl, 0);
    const char *lvls = lvl ? "ANY_HIGH" : "ALL_LOW";
    esp_sleep_ext1_wakeup_mode_t mode = \
        lvl ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ALL_LOW;
    gpio_num_t pin;
    uint64_t mask = 0;
    LOOPN(i, pin_cnt) {
        pin = (gpio_num_t)sys_sleep_args.pin->ival[i];
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

static int sys_sleep(int argc, char **argv) {
    ARG_PARSE(argc, argv, &sys_sleep_args);
    const char *mode = ARG_STR(sys_sleep_args.mode, "light");
    uint32_t tout_ms = ARG_INT(sys_sleep_args.tout, 0);
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
#   ifdef CONFIG_BASE_USE_UART
        fprintf(stderr, "Use UART wakeup, num: %d\n", NUM_UART);
        ESP_ERROR_CHECK( uart_set_wakeup_threshold(NUM_UART, 3) );
        ESP_ERROR_CHECK( esp_sleep_enable_uart_wakeup(NUM_UART) );
#   endif
        if (( err = enable_gpio_light_wakeup() )) return err;
    } else {
        if (( err = enable_gpio_deep_wakeup() )) return err;
    }

    fprintf(stderr, "Turn to %s sleep mode\n", mode);
    fflush(stderr); fsync(fileno(stderr));
#   ifdef CONFIG_BASE_USE_UART
    uart_tx_wait_idle(NUM_UART);
#   endif
    if (light) {
        esp_light_sleep_start();
    } else {
        esp_deep_sleep_start(); // no-return (see console_register_commands)
    }
    fprintf(stderr, "Woken up from light sleep mode by %s\n",
            wakeup_reason_list[(int)esp_sleep_get_wakeup_cause()]);
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}
#endif // CONSOLE_SYS_SLEEP

#ifdef CONSOLE_SYS_UPDATE
static struct {
    arg_str_t *cmd;
    arg_str_t *part;
    arg_str_t *url;
    arg_lit_t *fce;
    arg_end_t *end;
} sys_update_args = {
    .cmd  = arg_str0(NULL, NULL, "boot|fetch|reset", ""),
    .part = arg_str0("p", NULL, "LABEL", "partition to boot from"),
    .url  = arg_str0("u", NULL, "URL", "specify URL to fetch"),
    .fce  = arg_lit0("f", NULL, "skip version verification"),
    .end  = arg_end(4)
};

static int sys_update(int argc, char **argv) {
    ARG_PARSE(argc, argv, &sys_update_args);
    const char *subcmd = ARG_STR(sys_update_args.cmd, "");
    if (strstr(subcmd, "boot")) {
        if (sys_update_args.part->count) {
            const char *label = sys_update_args.part->sval[0];
            printf("Boot from %s: ", label);
            if (!ota_updation_boot(label)) {
                puts(ota_updation_error());
                return ESP_FAIL;
            }
            puts("done");
        } else {
            ota_updation_info();
        }
    } else if (strstr(subcmd, "reset")) {
        ota_updation_reset();
        printf("OTA states reset done\n");
    } else if (strstr(subcmd, "fetch")) {
        const char *url = ARG_STR(sys_update_args.url, NULL);
        if (!ota_updation_url(url, sys_update_args.fce->count)) {
            printf("Failed to udpate: %s\n", ota_updation_error());
            return ESP_FAIL;
        }
        printf("Updation success. Call `restart` to reboot ESP32");
    } else {
        ota_updation_info();
    }
    return ESP_OK;
}
#endif // CONSOLE_SYS_UPDATE

#ifdef CONSOLE_SYS_EXEC
static struct {
    arg_lit_t *ext;
    arg_lit_t *hdr;
    arg_str_t *path;
    arg_lit_t *sep;
    arg_str_t *argv;
    arg_end_t *end;
} sys_exec_args = {
    .ext  = arg_lit0("d", "sdcard", "target SDCard instead of Flash"),
    .hdr  = arg_litn("h", "header", 0, 4, "print ELF header and exit"),
    .path = arg_str1(NULL, NULL, "path", "ELF file to run"),
    .sep  = arg_lit0(NULL, "", NULL), // add '--' seperator to arg_print_syntax
    .argv = arg_strn(NULL, NULL, "argv", 0, 10, "args MUST be after '--'"),
    .end  = arg_end(5)
};

static esp_err_t sys_exec(int argc, char **argv) {
    ARG_PARSE(argc, argv, &sys_exec_args);
    int eargc = sys_exec_args.argv->count + 1;
    char **eargv = NULL;
    const char *path = ARG_STR(sys_exec_args.path, NULL);
    filesys_type_t type = FILESYS_TYPE(sys_exec_args.ext->count);
    esp_err_t err = ESP_OK;
    if (sys_exec_args.hdr->count) {
        filesys_readelf(type, path, sys_exec_args.hdr->count);
    } else if (!( err = ECALLOC(eargv, eargc, sizeof(char *)) )) {
        char *basename = strrchr(path, '/');
        LOOPN(i, eargc) {
            eargv[i] = i ? strdup(sys_exec_args.argv->sval[i - 1]) :
                           strdup(basename ? basename + 1 : path);
            if (!eargv[i]) { err = ESP_ERR_NO_MEM; break; }
        }
        if (!err) err = filesys_execute(type, path, eargc, eargv);
        LOOPN(i, eargc) { TRYFREE(eargv[i]); }
        free(eargv);
    }
    return err;
}
#endif // CONSOLE_SYS_EXEC

static esp_err_t register_sys() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_SYS_RESTART
        ESP_CMD_ARG(sys, restart, "Software reset of ESP32"),
#endif
#ifdef CONSOLE_SYS_UPDATE
        ESP_CMD_ARG(sys, update, "OTA Updation helper command"),
#endif
#ifdef CONSOLE_SYS_SLEEP
        ESP_CMD_ARG(sys, sleep, "Turn ESP32 into light/deep sleep mode"),
#endif
#ifdef CONSOLE_SYS_EXEC
        ESP_CMD_ARG(sys, exec, "Load and execute ELF files"),
#endif
    };
    return register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Driver commands
 */

#ifdef CONSOLE_DRV_GPIO
static struct {
    arg_int_t *pin;
    arg_int_t *lvl;
    arg_lit_t *i2c;
    arg_lit_t *spi;
    arg_end_t *end;
} drv_gpio_args = {
    .pin = arg_int0(NULL, NULL, NULL, "gpio number"),
    .lvl = arg_int0(NULL, NULL, "0|1", "set pin to LOW / HIGH"),
    .i2c = arg_lit0(NULL, "i2c", "list pin of I2C GPIO Expander"),
    .spi = arg_lit0(NULL, "spi", "list pin of SPI GPIO Expander"),
    .end = arg_end(4)
};

static int drv_gpio(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_gpio_args);
    esp_err_t err = ESP_OK;
    int pin = ARG_INT(drv_gpio_args.pin, -1);
    int lvl = ARG_INT(drv_gpio_args.lvl, -1);
    if (pin < 0) {
        gpio_table(drv_gpio_args.i2c->count, drv_gpio_args.spi->count);
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
#endif // CONSOLE_DRV_GPIO

#ifdef CONSOLE_DRV_USB
static struct {
    arg_str_t *mode;
    arg_lit_t *now;
    arg_end_t *end;
} drv_usb_args = {
    .mode = arg_str0(NULL, NULL, "0-6|CMH|S", "specify USB mode"),
    .now  = arg_lit0(NULL, "now", "reboot right now if needed"),
    .end  = arg_end(2)
};

static int drv_usb(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_usb_args);
    const char *mode = ARG_STR(drv_usb_args.mode, NULL);
    int idx = stridx(mode, "CcMmHhS");
    esp_err_t err = ESP_OK;
    if (!mode) {
        usbmode_status();
    } else if (idx >= 0) {
        err = usbmode_switch((usbmode_t)idx, drv_usb_args.now->count);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}
#endif // CONSOLE_DRV_USB

#ifdef CONSOLE_DRV_LED
static struct {
    arg_int_t *idx;
    arg_str_t *lgt;
    arg_str_t *clr;
    arg_int_t *blk;
    arg_end_t *end;
} drv_led_args = {
    .idx = arg_int0(NULL, NULL, NULL, "LED index"),
    .lgt = arg_str0("l", NULL, "0-255|on|off", "set lightness"),
    .clr = arg_str0("c", NULL, "0xRRGGBB", "set RGB color"),
    .blk = arg_int0("b", NULL, NULL, "set blink effect"),
    .end = arg_end(4)
};

static int drv_led(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_led_args);
    esp_err_t err = ESP_OK;
    int idx = ARG_INT(drv_led_args.idx, -1);
    int blk = ARG_INT(drv_led_args.blk, LED_BLINK_RESET - 1);
    const char *light = ARG_STR(drv_led_args.lgt, NULL);
    const char *color = ARG_STR(drv_led_args.clr, NULL);
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
        int bval = 0;
        if (strstr(light, "off")) {
            bval = 0;
        } else if (strstr(light, "on")) {
            bval = 255;
        } else if (!parse_int(light, &bval) || bval < 0 || bval > 255) {
            printf("Invalid brightness: `%s`\n", light);
            return ESP_ERR_INVALID_ARG;
        }
        if (( err = led_set_light(idx, bval) ))
            return err;
        printf("LED%s: set brightness to %d\n", buf, bval);
    }
    if (color) {
        int rgb = 0;
        if (!parse_int(color, &rgb) || rgb < 0 || rgb > 0xFFFFFF) {
            printf("Unsupported color: `%s`\n", color);
            return ESP_ERR_INVALID_ARG;
        }
        if (( err = led_set_color(idx, rgb) ))
            return err;
        printf("LED%s: set color to 0x%06X\n", buf, rgb);
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
#endif // CONSOLE_DRV_LED

#ifdef CONSOLE_DRV_I2C
static struct {
    arg_int_t *bus;
    arg_int_t *addr;
    arg_int_t *reg;
    arg_int_t *val;
    arg_int_t *len;
    arg_lit_t *hex;
    arg_end_t *end;
} drv_i2c_args = {
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

static int drv_i2c(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_i2c_args);
    int bus = ARG_INT(drv_i2c_args.bus, CONFIG_BASE_I2C_NUM);
    int addr = ARG_INT(drv_i2c_args.addr, -1);
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
    esp_err_t err;
    uint8_t word = drv_i2c_args.hex->count ? 4 : 2;
    uint16_t len = ARG_INT(drv_i2c_args.len, 0);
    uint16_t reg = ARG_INT(drv_i2c_args.reg, 0);
    uint16_t val = ARG_INT(drv_i2c_args.val, 0);
    if (drv_i2c_args.val->count) {
        if (word == 4) {
            err = smbus_write_word(bus, addr, reg, val);
        } else {
            err = smbus_write_byte(bus, addr, reg, val);
        }
    } else if (!len) {
        if (word == 4) {
            err = smbus_read_word(bus, addr, reg, &val);
        } else {
            uint8_t tmp;
            err = smbus_read_byte(bus, addr, reg, &tmp);
            val = tmp;
        }
        if (!err) {
            printf("I2C %d-%02X REG 0x%0*X = 0x%0*X\n",
                   bus, addr, word, reg, word, val);
        }
    } else {
        err = smbus_dump(bus, addr, reg, len);
    }
    return err;
}
#endif // CONSOLE_DRV_I2C

#ifdef CONSOLE_DRV_ADC
static struct {
    arg_int_t *idx;
    arg_lit_t *joy;
    arg_lit_t *hall;
    arg_int_t *intv;
    arg_int_t *tout;
    arg_end_t *end;
} drv_adc_args = {
#   ifdef PIN_ADC2
    .idx  = arg_int0(NULL, NULL, "0|1", "index of ADC channel"),
#   else
    .idx  = arg_int0(NULL, NULL, "0", "index of ADC channel"),
#   endif
    .joy  = arg_lit0(NULL, "joy", "read joystick value"),
    .hall = arg_lit0(NULL, "hall", "read hall sensor value"),
    .intv = arg_int0("i", NULL, "10-1000", "interval in ms, default 500"),
    .tout = arg_int0("t", NULL, "0-2^31", "loop until timeout in ms"),
    .end  = arg_end(6)
};

static int drv_adc(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_adc_args);
#   ifdef PIN_ADC2
    int idx = ARG_INT(drv_adc_args.idx, -1);
#   else
    int idx = ARG_INT(drv_adc_args.idx, 0);
#   endif
    uint16_t intv_ms = CONS(ARG_INT(drv_adc_args.intv, 500), 10, 1000);
    uint32_t tout_ms = ARG_INT(drv_adc_args.tout, 0);
    uint64_t state = asleep(intv_ms, 0);
    do {
        if (drv_adc_args.joy->count) {
            int xy, dx, dy;
            if (( xy = adc_joystick(&dx, &dy) ) == -1) {
                fprintf(stderr, "\rCould not read joystick value");
                break;
            }
            fprintf(stderr, "\rJoystick: x %3d y %3d (%4d %4d)",
                    xy >> 16, xy & 0xFFFF, dx, dy);
        } else if (drv_adc_args.hall->count) {
            fprintf(stderr, "\rADC hall: %4d", adc_hall());
        } else if (idx < 0 || idx > 1) {
            fprintf(stderr, "\rADC: %4dmV %4dmV", adc_read(0), adc_read(1));
        } else {
            fprintf(stderr, "\rADC %d: %4dmV", idx, adc_read(idx));
        }
        if (tout_ms >= intv_ms) {
            fprintf(stderr, " (remain %3ds)", tout_ms / 1000); fflush(stderr);
            state = asleep(intv_ms, state);
            tout_ms -= intv_ms;
        } else break;
    } while (1);
    fputc('\n', stderr);
    return ESP_OK;
}
#endif

#ifdef CONSOLE_DRV_DAC
static struct {
    arg_int_t *val;
    arg_str_t *cos;
    arg_int_t *frq;
    arg_int_t *amp;
    arg_end_t *end;
} drv_dac_args = {
    .val = arg_int0(NULL, NULL, "0-255", "output value / offset of wave"),
    .cos = arg_str0(NULL, "cos", "on|off", "cosine wave enable / disable"),
    .frq = arg_int0("f", NULL, "130-55000", "frequency of cosine wave"),
    .amp = arg_int0("a", NULL, "0-3", "amplitude of cosine wave"),
    .end = arg_end(3)
};

static int drv_dac(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_dac_args);
    static uint8_t val, amp;
    static uint16_t freq;
    static bool cwave;
    const char * cos = ARG_STR(drv_dac_args.cos, NULL);
    int v = ARG_INT(drv_dac_args.val, -1),
        f = ARG_INT(drv_dac_args.frq, -1),
        a = ARG_INT(drv_dac_args.amp, -1);
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

#ifdef CONSOLE_DRV_PWM
static struct {
    arg_int_t *hdeg;
    arg_int_t *vdeg;
    arg_int_t *freq;
    arg_int_t *pcnt;
    arg_end_t *end;
} drv_pwm_args = {
    .hdeg = arg_int0("y", NULL, "0-180", "yaw degree"),
    .vdeg = arg_int0("p", NULL, "0-160", "pitch degree"),
    .freq = arg_int0("f", NULL, "0-5000", "tone frequency"),
    .pcnt = arg_int0("l", NULL, "0-100", "tone loudness (percentage)"),
    .end  = arg_end(4)
};

static int drv_pwm(int argc, char **argv) {
    ARG_PARSE(argc, argv, &drv_pwm_args);
    int hdeg = ARG_INT(drv_pwm_args.hdeg, -1),
        vdeg = ARG_INT(drv_pwm_args.vdeg, -1),
        pcnt = ARG_INT(drv_pwm_args.pcnt, -1),
        freq = ARG_INT(drv_pwm_args.freq, -1);
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
#endif // CONSOLE_DRV_PWM

static esp_err_t register_drv() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_DRV_GPIO
        ESP_CMD_ARG(drv, gpio, "Set / get GPIO pin level"),
#endif
#ifdef CONSOLE_DRV_USB
        ESP_CMD_ARG(drv, usb, "Set / get USB working mode"),
#endif
#ifdef CONSOLE_DRV_LED
        ESP_CMD_ARG(drv, led, "Set / get LED color / brightness"),
#endif
#ifdef CONSOLE_DRV_I2C
        ESP_CMD_ARG(drv, i2c, "Detect alive I2C slaves on the bus line"),
#endif
#ifdef CONSOLE_DRV_ADC
        ESP_CMD_ARG(drv, adc, "Read ADC and calculate value in mV"),
#endif
#ifdef CONSOLE_DRV_DAC
        ESP_CMD_ARG(drv, dac, "Write DAC and calculate value in mV"),
#endif
#ifdef CONSOLE_DRV_PWM
        ESP_CMD_ARG(drv, pwm, "Control rotation of servo by PWM"),
#endif
    };
    return register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Utilities commands
 */

#ifdef CONSOLE_UTIL_VERSION
static int util_version(int c, char **v) { version_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTIL_LSHW
static int util_lshw(int c, char **v) { hardware_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTIL_LSPART
static int util_lspart(int c, char **v) { partition_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTIL_LSTASK
static struct {
    arg_int_t *sort;
    arg_lit_t *lvl;
    arg_end_t *end;
} util_lstask_args = {
    .sort = arg_int0(NULL, NULL, "0-6", "sort by column index"),
    .lvl  = arg_litn("v", NULL, 0, 2, "additive option for more output"),
    .end  = arg_end(2)
};

static int util_lstask(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_lstask_args);
    switch (util_lstask_args.lvl->count) {
    case 2: esp_event_dump(stdout); putchar('\n'); FALLTH;
    case 1: esp_timer_dump(stdout); putchar('\n'); FALLTH;
    default: task_info(ARG_INT(util_lstask_args.sort, 2)); break;
    }
    return ESP_OK;
}
#endif

#ifdef CONSOLE_UTIL_LSMEM
static struct {
    arg_lit_t *lvl;
    arg_lit_t *chk;
    arg_end_t *end;
} util_lsmem_args = {
    .lvl = arg_litn("v", NULL, 0, 2, "additive option for more output"),
    .chk = arg_litn("c", NULL, 0, 3, "check heap memory integrity"),
    .end = arg_end(2)
};

static int util_lsmem(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_lsmem_args);
    switch (util_lsmem_args.lvl->count) {
    case 2: heap_caps_print_heap_info(MALLOC_CAP_DMA);
            heap_caps_print_heap_info(MALLOC_CAP_EXEC); FALLTH;
    case 1: heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
            heap_caps_print_heap_info(MALLOC_CAP_INTERNAL); break;
    default: memory_info(); break;
    }
    switch (util_lsmem_args.chk->count) {
    case 3: heap_caps_check_integrity_all(true); break;
    case 2: heap_caps_check_integrity(MALLOC_CAP_DMA, true);
            heap_caps_check_integrity(MALLOC_CAP_EXEC, true); FALLTH;
    case 1: heap_caps_check_integrity(MALLOC_CAP_DEFAULT, true);
            heap_caps_check_integrity(MALLOC_CAP_INTERNAL, true); break;
    default: break;
    }
    return ESP_OK;
}
#endif // CONSOLE_UTIL_LSMEM

#ifdef CONSOLE_UTIL_LSFS
static struct {
    arg_str_t *dir;
    arg_lit_t *stat;
    arg_lit_t *info;
    arg_lit_t *ext;
    arg_end_t *end;
} util_lsfs_args = {
    .dir  = arg_str0(NULL, NULL, "path", NULL),
    .stat = arg_lit0("s", "stat", "print result of stat"),
    .info = arg_lit0("i", "info", "print file system information"),
    .ext  = arg_lit0("d", "sdcard", "target SDCard instead of Flash"),
    .end  = arg_end(3)
};

static int util_lsfs(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_lsfs_args);
    const char *path = ARG_STR(util_lsfs_args.dir, "/");
    filesys_type_t type = FILESYS_TYPE(util_lsfs_args.ext->count);
    if (util_lsfs_args.info->count) {
        filesys_print_info(type);
    } else if (util_lsfs_args.stat->count) {
        filesys_pstat(type, path);
    } else {
        filesys_listdir(type, path, stdout);
    }
    return ESP_OK;
}
#endif // CONSOLE_UTIL_LSFS

#ifdef CONSOLE_UTIL_CONFIG
static struct {
    arg_str_t *key;
    arg_str_t *val;
    arg_lit_t *load;
    arg_lit_t *save;
    arg_lit_t *stat;
    arg_lit_t *list;
    arg_lit_t *lall;
    arg_end_t *end;
} util_config_args = {
    .key  = arg_str0(NULL, NULL, "KEY", "specify config by key"),
    .val  = arg_str0(NULL, NULL, "VAL", "set config value"),
    .load = arg_lit0(NULL, "load", "load from NVS flash"),
    .save = arg_lit0(NULL, "save", "save to NVS flash"),
    .stat = arg_lit0(NULL, "stat", "summary NVS status"),
    .list = arg_lit0(NULL, "list", "list config NVS entries"),
    .lall = arg_lit0(NULL, "list_all", "list all NVS entries"),
    .end  = arg_end(7)
};

static int util_config(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_config_args);
    bool ret = true;
    const char *key = ARG_STR(util_config_args.key, NULL);
    const char *val = ARG_STR(util_config_args.val, NULL);
    if (key) {
        if (val) {
            printf("Set `%s` to `%s` %s\n", key, val,
                   (ret = config_set(key, val)) ? "done" : "fail");
        } else {
            printf("Get `%s` value `%s`\n", key, config_get(key));
        }
    } else if (util_config_args.load->count) {
        ret = config_nvs_load();
    } else if (util_config_args.save->count) {
        ret = config_nvs_dump();
    } else if (util_config_args.stat->count) {
        config_nvs_stats();
    } else if (util_config_args.list->count) {
        config_nvs_list(false);
    } else if (util_config_args.lall->count) {
        config_nvs_list(true);
    } else {
        config_stats();
    }
    return ret ? ESP_OK : ESP_FAIL;
}
#endif // CONSOLE_UTIL_CONFIG

#ifdef CONSOLE_UTIL_LOGGING
static struct {
    arg_str_t *tag;
    arg_str_t *lvl;
    arg_lit_t *log;
    arg_end_t *end;
} util_logging_args = {
    .tag = arg_str0(NULL, NULL, "TAG", "tag of the log entries [default *]"),
    .lvl = arg_str0(NULL, NULL, "0-5|NEWIDV", "set logging level"),
    .log = arg_lit0(NULL, "test", "test logging with specified tag"),
    .end = arg_end(3)
};

static int util_logging(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_logging_args);
    const char *lvls = "NEWIDV";
    const char *tag = ARG_STR(util_logging_args.tag, "*");
    const char *lvl = ARG_STR(util_logging_args.lvl, NULL);
    if (lvl) {
        int idx = stridx(lvl, lvls);
        if (idx >= 0) esp_log_level_set(tag, (esp_log_level_t)idx);
    }
    if (strlen(tag) > 16) {
        printf("Logging tag too long to test: %s\n", tag);
        return ESP_OK;
    }
    /* Hotfix for logging: tags passed to esp_log_level_set/get will be
     * cached as pointer for faster access. Even the content pointed by
     * tags have changed, they still hit the cache. E.g.:
     *
     *      char tag[10] = "FOO";
     *      esp_log_level_set(tag, ESP_LOG_ERROR);
     *
     *      int lvl = esp_log_level_get(tag);
     *      printf("level of %s: %d", tag, lvl);    // -> level of FOO: 1
     *
     *      strcpy(tag, "BAR");
     *      lvl = esp_log_level_get(tag);
     *      printf("level of %s: %d", tag, lvl);    // -> level of BAR: 1
     *
     *      lvl = esp_log_level_get("BAR");
     *      printf("level of %s: %d", tag, lvl);    // -> level of BAR: 3
     *
     * Default TAG_CACHE_SIZE is 31 in esp-idf-v4.4/components/log/log.c
     */
    static char *skip_cache[32];
    static uint8_t idx = 0, val;
    if (!strcmp(tag, "*")) {
        val = esp_log_level_get("*");
    } else {
        char *dup = strdup(tag);
        if (!dup) return ESP_ERR_NO_MEM;
        TRYFREE(skip_cache[idx]);
        tag = skip_cache[idx] = dup;
        idx = (idx + 1) % LEN(skip_cache);
        val = esp_log_level_get(tag);
        if (util_logging_args.log->count) {
            LOOP(i, 1, strlen(lvls)) {
                ESP_LOG_LEVEL(i, tag, "Logging at %c", lvls[i]);
            }
        }
    }
    printf("Logging level of %s is %c\n", tag, lvls[val]);
    return ESP_OK;
}
#endif // CONSOLE_UTIL_LOGGING

#ifdef CONSOLE_UTIL_HISTORY
static struct {
    arg_str_t *cmd;
    arg_str_t *dst;
    arg_lit_t *ext;
    arg_end_t *end;
} util_hist_args = {
    .cmd = arg_str1(NULL, NULL, "load|save", ""),
    .dst = arg_str0("f", NULL, "PATH", "history file [default history.txt]"),
    .ext = arg_lit0("d", "sdcard", "target SDCard instead of Flash"),
    .end = arg_end(3)
};

static int util_hist(int argc, char **argv) {
    ARG_PARSE(argc, argv, &util_hist_args);
    esp_err_t err = ESP_ERR_INVALID_ARG;
    const char *cmd = util_hist_args.cmd->sval[0];
    const char *dst = ARG_STR(util_hist_args.dst, "history.txt");
    bool save = false;
    if (strstr(cmd, "save")) {
        save = true;
    } else if (!strstr(cmd, "load")) {
        printf("Invalid command: `%s`\n", cmd);
        return err;
    }
    filesys_type_t type = FILESYS_TYPE(util_hist_args.ext->count);
    const char *path = filesys_join(type, 2, Config.sys.DIR_DATA, dst);
    if (!save && !filesys_exists(type, path)) {
        printf("History file `%s` does not exist\n", path);
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = save ? linenoiseHistorySave(path) : linenoiseHistoryLoad(path);
        printf("History file `%s` %s %s\n", path, cmd, err ? "fail" : "done");
    }
    return err;
}
#endif // CONSOLE_UTIL_HISTORY

static esp_err_t register_util() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_UTIL_VERSION
        ESP_CMD(util, version, "Get version of firmware and SDK"),
#endif
#ifdef CONSOLE_UTIL_LSHW
        ESP_CMD(util, lshw, "Print hardware information"),
#endif
#ifdef CONSOLE_UTIL_LSPART
        ESP_CMD(util, lspart, "Enumerate partitions in flash"),
#endif
#ifdef CONSOLE_UTIL_LSTASK
        ESP_CMD_ARG(util, lstask, "Enumerate running RTOS tasks"),
#endif
#ifdef CONSOLE_UTIL_LSMEM
        ESP_CMD_ARG(util, lsmem, "List memory info"),
#endif
#ifdef CONSOLE_UTIL_LSFS
        ESP_CMD_ARG(util, lsfs, "List file system directories and files"),
#endif
#ifdef CONSOLE_UTIL_CONFIG
        ESP_CMD_ARG(util, config, "Set / get / load / save / list configs"),
#endif
#ifdef CONSOLE_UTIL_LOGGING
        ESP_CMD_ARG(util, logging, "Set / get ESP logging level"),
#endif
#ifdef CONSOLE_UTIL_HISTORY
        ESP_CMD_ARG(util, hist, "Dump / load console history from flash"),
#endif
    };
    return register_commands(cmds, LEN(cmds));
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
    .now  = arg_lit0(NULL, "now", "reboot right now if needed"),
    .scan = arg_lit0(NULL, "scan", "run BT/BLE scan"),
    .tout = arg_int0("t", NULL, "0-65535", "scan timeout in ms"),
    .bat  = arg_int0("b", NULL, "0-100", "BLE report battery level"),
    .dev  = arg_str0("c", NULL, "BDA", "connect to BLE device"),
    .end  = arg_end(6)
};

static int net_bt(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_bt_args);
    const char *name = ARG_STR(net_bt_args.dev, NULL);
    const char *mode = ARG_STR(net_bt_args.mode, NULL);
    int bat = ARG_INT(net_bt_args.bat, -1);
    int idx = stridx(mode, "dDH");
    esp_err_t err = ESP_OK;
    if (net_bt_args.scan->count) {
        err = btmode_scan(ARG_INT(net_bt_args.tout, 0));
    } else if (bat != -1) {
        err = btmode_battery(CONS(bat, 0, 100));
    } else if (name) {
        err = btmode_connect(name, NULL);
    } else if (!mode) {
        btmode_status();
    } else if (idx >= 0) {
        err = btmode_switch((btmode_t)idx, net_bt_args.now->count);
    } else {
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
    .dry  = arg_lit0(NULL, "dryrun", "print IP address and stop"),
    .end  = arg_end(6)
};

static int net_ping(int argc, char **argv) {
    ARG_PARSE(argc, argv, &net_ping_args);
    if (net_ping_args.dry->count)
        return network_parse_addr(net_ping_args.host->sval[0], NULL);
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
    .tout = arg_int0("t", NULL, "0-2^31", "task timeout in ms"),
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

static esp_err_t register_net() {
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
    return register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Application commands
 */

#ifdef CONSOLE_APP_HID
static struct {
    arg_str_t *key;
    arg_str_t *str;
    arg_str_t *mse;
    arg_str_t *pad;
    arg_str_t *ctrl;
    arg_str_t *dial;
    arg_int_t *tout;
    arg_dbl_t *tevt;
    arg_str_t *tgt;
    arg_end_t *end;
} app_hid_args = {
    .key  = arg_str0("k", NULL, "CODE", "report keypress"),
    .str  = arg_str0("s", NULL, "STR", "report type in"),
    .mse  = arg_str0("m", NULL, "B|XYVH", "report mouse"),
    .pad  = arg_str0("p", NULL, "BTXYXY", "report gamepad"),
    .ctrl = arg_str0("c", NULL, "1-15", "report system control"),
    .dial = arg_str0("d", NULL, "LRUD", "report S-Dial"),
    .tout = arg_int0("t", NULL, "0-65535", "event timeout in ms"),
    .tevt = arg_dbl0(NULL, "ts", "MSEC", "event unix timestamp in ms"),
    .tgt  = arg_str0(NULL, "to", "0-2|UBS", "report to USB/BT/SCN"),
    .end  = arg_end(9)
};

static int app_hid(int argc, char **argv) {
    ARG_PARSE(argc, argv, &app_hid_args);
    const char *typein = ARG_STR(app_hid_args.str, NULL);
    const char *keybd = ARG_STR(app_hid_args.key, NULL);
    const char *mouse = ARG_STR(app_hid_args.mse, NULL);
    const char *gmpad = ARG_STR(app_hid_args.pad, NULL);
    const char *sctrl = ARG_STR(app_hid_args.ctrl, NULL);
    const char *sdial = ARG_STR(app_hid_args.dial, NULL);
    const char *tstr = ARG_STR(app_hid_args.tgt, NULL);
    uint16_t tout_ms = ARG_INT(app_hid_args.tout, 50);
    double tevt_ms = ARG_DBL(app_hid_args.tevt, 0);
    int idx = stridx(tstr, "UBS");
    esp_err_t err = ESP_OK;

    hid_target_t to = idx >= 0 ? (hid_target_t)idx : HID_TARGET_ALL;
    if (tstr && idx < 0) return ESP_ERR_INVALID_ARG;

    if (keybd) {
        hid_report_keybd_press(to, keybd, tout_ms);
    } else if (typein) {
        char buf[2] = { 0, 0 };
        tout_ms = MAX(50, tout_ms) / 2;
        LOOPN(i, strlen(typein)) {
            buf[0] = typein[i];
            hid_report_keybd_press(to, buf, tout_ms); msleep(tout_ms);
        }
    } else if (mouse) {
        int vals[4];
        switch (parse_all(mouse, vals, LEN(vals))) {
        case 0: hid_report_mouse_click(to, mouse, tout_ms); break;
        case 1: hid_report_mouse_button(to, vals[0]); break;
        case 2: hid_report_mouse_move(to, vals[0], vals[1]); break;
        default: hid_report_mouse(to, 0, vals[0], vals[1], vals[2], vals[3]);
        }
    } else if (gmpad) {
        int vals[4];
        switch (parse_all(gmpad, vals, LEN(vals))) {
        case 0: hid_report_gmpad_click(to, gmpad, tout_ms); break;
        case 1: idx = stridx(gmpad, "DATS"); if (idx < 0) break;
                hid_report_gmpad_button(to, vals[0], idx); break;
        case 2: hid_report_gmpad_trig(to, vals[0], vals[1]); break;
        default: hid_report_gmpad_joyst(to, vals[0], vals[1], vals[2], vals[3]);
        }
    } else if (sctrl) {
        const char *tpl = "|Pwdn|Sleep|Wake|mCtx|mMain|mApp|mHelp|meXit|msEl"
                          "|mRt|mLt|mUp|mDn|rcOld|rwarM";
        if (( idx = stridx(sctrl, tpl) ) >= 0) {
            hid_report_sctrl(to, (hid_sctrl_keycode_t)idx);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (sdial) {
        switch (sdial[0]) {
        case 'u': FALLTH; case 'U': hid_report_sdial(to, SDIAL_U); break;
        case 'd': FALLTH; case 'D': hid_report_sdial(to, SDIAL_D); break;
        case 'r': FALLTH; case 'R': hid_report_sdial(to, SDIAL_R); break;
        case 'l': FALLTH; case 'L': hid_report_sdial(to, SDIAL_L); break;
        default: if (strbool(sdial)) hid_report_sdial_click(to, tout_ms);
        }
    }

    if (tevt_ms) {
        double curr_ms = get_timestamp_us(0) * 1e3;
        if (curr_ms > tevt_ms)
            ESP_LOGD(TAG, "event latency: %.3fms", curr_ms - tevt_ms);
    }
    return err;
}
#endif // CONSOLE_APP_HID

#ifdef CONSOLE_APP_SCN
static struct {
    arg_int_t *btn;
    arg_int_t *bar;
    arg_int_t *rot;
    arg_int_t *fps;
    arg_str_t *font;
    arg_end_t *end;
} app_scn_args = {
    .btn  = arg_int0(NULL, NULL, "0-6", "trigger virtual button press"),
    .bar  = arg_int0("p", NULL, "0-100", "draw progress bar on screen"),
    .rot  = arg_int0("r", NULL, "0-3", "software rotation of screen"),
    .fps  = arg_int0("f", NULL, "0-100", "set LVGL refresh period in FPS"),
    .font = arg_str0(NULL, "font", "PATH", "load font from file"),
    .end  = arg_end(5)
};

static int app_scn(int argc, char **argv) {
    ARG_PARSE(argc, argv, &app_scn_args);
    int btn = ARG_INT(app_scn_args.btn, -1);
    int bar = ARG_INT(app_scn_args.bar, -1);
    int rot = ARG_INT(app_scn_args.rot, -1);
    int fps = ARG_INT(app_scn_args.fps, -1);
    const char *font = ARG_STR(app_scn_args.font, NULL);
    if (bar >= 0) return screen_command(SCN_PBAR, &bar);
    if (btn >= 0) return screen_command(SCN_BTN, &btn);
    if (rot >= 0) return screen_command(SCN_ROT, &rot);
    if (fps >= 0) return screen_command(SCN_FPS, &fps);
    if (font) return screen_command(SCN_FONT, font);
    screen_status();
    return ESP_OK;
}
#endif // CONSOLE_APP_SCN

#ifdef CONSOLE_APP_ALS
static struct {
    arg_int_t *idx;
    arg_str_t *rlt;
    arg_end_t *end;
} app_als_args = {
    .idx = arg_int0(NULL, NULL, "0-3", "index of ALS chip"),
    .rlt = arg_str0("t", NULL, "0-3|HVA", "run light tracking"),
    .end = arg_end(2)
};

static int app_als(int argc, char **argv) {
    ARG_PARSE(argc, argv, &app_als_args);
    const char *rlt = ARG_STR(app_als_args.rlt, NULL);
    int idx = ARG_INT(app_als_args.idx, -1);
    esp_err_t err = ESP_OK;
    if (rlt) {
        int track = stridx(rlt, "0123HVA"), hdeg = -1, vdeg = -1;
        if (track < 0) {
            err = ESP_ERR_INVALID_ARG;
        } else if (!( err = als_tracking((als_track_t)track, &hdeg, &vdeg) )) {
            printf("ALS tracked to H: %d, V: %d\n", hdeg, vdeg);
        }
    } else if (idx < ALS_NUM) {
        LOOPN(i, ALS_NUM) {
            if (idx < 0 || i == idx) {
                printf("Brightness of ALS %d is %.2f lux\n",
                        i, als_brightness(i));
            }
        }
    } else {
        printf("Invalid index %d\n", idx);
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}
#endif // CONSOLE_APP_ALS

#ifdef CONSOLE_APP_AVC
static struct {
    arg_str_t *tgt;
    arg_str_t *ctrl;
    arg_lit_t *cam;
    arg_lit_t *viz;
    arg_int_t *tout;
    arg_end_t *end;
} app_avc_args = {
    .tgt  = arg_str0(NULL, NULL, "1-3", "audio|video|all [default all]"),
    .ctrl = arg_str0(NULL, NULL, "on|off", "enable / disable"),
    .cam  = arg_lit0(NULL, "cam", "get or set camera config"),
    .viz  = arg_lit0("v", "viz", "print audio volume / video frame info"),
    .tout = arg_int0("t", NULL, "0-2^31", "capture task timeout in ms"),
    .end  = arg_end(5)
};

static int app_avc(int argc, char **argv) {
    ARG_PARSE(argc, argv, &app_avc_args);
    const char *target = ARG_STR(app_avc_args.tgt, "3");
    const char *itpl = app_avc_args.tgt->hdr.glossary;
    const char *istr = strstr(itpl, target);
    uint8_t index = istr ? strcnt(itpl, "|", istr - itpl) : target[0] - '0';
    if (app_avc_args.cam->count) {
        if (app_avc_args.tgt->count) return CAMERA_LOADS(target);
        return CAMERA_PRINT(stdout);
    }
    return avc_command(
        ARG_STR(app_avc_args.ctrl, NULL),
        index ? MIN(index, 3) : 3,
        ARG_INT(app_avc_args.tout, 0),
        app_avc_args.viz->count ? stderr : NULL
    );
}
#endif // CONSOLE_APP_AVC

#ifdef CONSOLE_APP_SEN
static struct {
    arg_str_t *sen;
    arg_int_t *intv;
    arg_int_t *tout;
    arg_end_t *end;
} app_sen_args = {
    .sen  = arg_str1(NULL, NULL, "0-5", "temp|tpad|tscn|dist|gy39|pwr"),
    .intv = arg_int0("i", NULL, "10-1000", "interval in ms, default 500"),
    .tout = arg_int0("t", NULL, "0-2^31", "loop until timeout in ms"),
    .end  = arg_end(3)
};

static int app_sen(int argc, char **argv) {
    ARG_PARSE(argc, argv, &app_sen_args);
    const char *sensor = ARG_STR(app_sen_args.sen, "0");
    const char *itpl = app_sen_args.sen->hdr.glossary;
    const char *istr = strstr(itpl, sensor);
    uint8_t index = istr ? strcnt(itpl, "|", istr - itpl) : sensor[0] - '0';
    uint16_t intv_ms = CONS(ARG_INT(app_sen_args.intv, 500), 10, 1000);
    uint32_t tout_ms = ARG_INT(app_sen_args.tout, 0);
    uint64_t state = asleep(intv_ms, 0);
    do {
        if (index == 0) {
            float val = temp_celsius();
            if (!val) goto error;
            fprintf(stderr, "\rTemp: %.2f degC", val);
        } else if (index == 1) {
            uint16_t val = tpad_read();
            if (!val) goto error;
            fprintf(stderr, "\rTouch pad: %4d", val);
        } else if (index == 2) {
            tscn_data_t dat;
            if (tscn_probe(&dat)) goto error;
            fprintf(stderr, "\rTouch screen:");
            LOOPN(i, dat.num) {
                fprintf(stderr, " ID %d, EVT %c, X %3d, Y %3d",
                    dat.pts[i].id, "PRC-"[dat.pts[i].evt],
                    dat.pts[i].x, dat.pts[i].y);
            }
            const char *gstr = NULL;
            switch (dat.ges) {
            case GES_MOVE_UP: gstr = "Move Up";     break;
            case GES_MOVE_RT: gstr = "Move Right";  break;
            case GES_MOVE_DN: gstr = "Move Down";   break;
            case GES_MOVE_LT: gstr = "Move Left";   break;
            case GES_ZOOM_IN: gstr = "Zoom In";     break;
            case GES_ZOOM_OT: gstr = "Zoom Out";    break;
            }
            if (gstr) fprintf(stderr, " Gesture %s", gstr);
            if (!dat.num && !dat.ges) fprintf(stderr, " not touched");
        } else if (index == 3) {
            uint16_t val = vlx_probe();
            if (val == UINT16_MAX) goto error;
            fprintf(stderr, "\rDistance: range ");
            if (val > 1000) {
                fprintf(stderr, "%.3fm", val / 1e3);
            } else {
                fprintf(stderr, "%4dmm", val);
            }
        } else if (index == 4) {
            gy39_data_t dat;
            if (gy39_measure(&dat)) goto error;
            fprintf(stderr, "\rGY39: %.2flux %.2fdegC %.3fkPa %.2f%% %.2fm",
                dat.brightness, dat.temperature,
                dat.atmosphere, dat.humidity, dat.altitude);
        } else if (index == 5) {
            pwr_status();
        } else {
            fprintf(stderr, "Nothing to do"); break;
        }
        if (tout_ms >= intv_ms) {
            fprintf(stderr, " (remain %3ds)", tout_ms / 1000); fflush(stderr);
            state = asleep(intv_ms, state);
            tout_ms -= intv_ms;
        } else break;
    } while (1);
    fputc('\n', stderr);
    return ESP_OK;
error:
    fputs("Measurement failed\n", stderr);
    return ESP_FAIL;
}
#endif // CONSOLE_APP_SEN

static esp_err_t register_app() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_APP_HID
        ESP_CMD_ARG(app, hid, "Send HID report through USB / BT"),
#endif
#ifdef CONSOLE_APP_SCN
        ESP_CMD_ARG(app, scn, "Control screen drawing"),
#endif
#ifdef CONSOLE_APP_ALS
        ESP_CMD_ARG(app, als, "Get ALS brightness and run light tracking"),
#endif
#ifdef CONSOLE_APP_AVC
        ESP_CMD_ARG(app, avc, "Control audio/video capturing"),
#endif
#ifdef CONSOLE_APP_SEN
        ESP_CMD_ARG(app, sen, "Get sensor values (TEMP, TPAD etc.)"),
#endif
    };
    return register_commands(cmds, LEN(cmds));
}

/******************************************************************************
 * Register commands
 */

static int cli_cls(int c, char **v) { linenoiseClearScreen(); return ESP_OK; }

static int cli_ctx(int c, char **v) {
    console_register_prompt(NULL, c > 1 ? v[1] : "");
    return ESP_OK;
}

static esp_err_t register_cli() {
    const esp_console_cmd_t cmds[] = {
        ESP_CMD(cli, cls, "Clean screen"),
        ESP_CMD(cli, ctx, "Command prefix context"),
    };
    return register_commands(cmds, LEN(cmds));
}

extern "C" void console_register_commands() {
#ifdef CONSOLE_SYS_SLEEP
    static char sb[6];
    snprintf(sb, sizeof(sb), "0-%d", GPIO_PIN_COUNT - 1);
    sys_sleep_args.pin->hdr.datatype = sb;
#endif
#ifdef CONSOLE_DRV_LED
    static char lb[8], bb[8];
    snprintf(lb, sizeof(lb), "0-%d", CONFIG_BASE_LED_NUM - 1);
    snprintf(bb, sizeof(bb), "-1|0-%d", LED_BLINK_MAX - 1);
    drv_led_args.idx->hdr.datatype = lb;
    drv_led_args.blk->hdr.datatype = bb;
#endif
#ifdef CONSOLE_DRV_GPIO
    static char gb[22];
    size_t gl = sizeof(gb), gs = snprintf(gb, gl, "0-%d", GPIO_PIN_COUNT - 1);
#   ifdef CONFIG_BASE_GPIOEXP_I2C
    gs += snprintf(gb + gs, gl - gs, "|%d-%d", PIN_I2C_BASE, PIN_I2C_MAX - 1);
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_SPI
    gs += snprintf(gb + gs, gl - gs, "|%d-%d", PIN_SPI_BASE, PIN_SPI_MAX - 1);
#   endif
    drv_gpio_args.pin->hdr.datatype = gb; NOTUSED(gs);
#endif
    ESP_ERROR_CHECK( esp_console_register_help_command() );
    ESP_ERROR_CHECK( register_cli() );
    ESP_ERROR_CHECK( register_sys() );
    ESP_ERROR_CHECK( register_util() );
    ESP_ERROR_CHECK( register_drv() );
    ESP_ERROR_CHECK( register_net() );
    ESP_ERROR_CHECK( register_app() );
}
