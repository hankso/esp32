/* 
 * File: console_cmds.cpp
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-13 18:03:04
 *
 * Implemented commands are:
 *  
 */

#include "console.h"
#include "config.h"
#include "update.h"
#include "globals.h"
#include "drivers.h"
#include "filesys.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "rom/uart.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

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
        struct arg_hdr **table = (struct arg_hdr **) argtable;
        int tabindex = 0;
        while (!(table[tabindex]->flag & ARG_TERMINATOR)) { tabindex++; }
        arg_print_errors(stdout, (struct arg_end *) table[tabindex], argv[0]);
        return false;
    }
    return true;
}

static void register_commands(const esp_console_cmd_t * cmds, size_t ncmd) {
    for (size_t i = 0; i < ncmd; i++) {
        ESP_ERROR_CHECK( esp_console_cmd_register(cmds + i) );
    }
}

/******************************************************************************
 * System commands
 */

static int system_restart(int c, char **v) { esp_restart(); return ESP_OK; }

#ifdef CONSOLE_SYSTEM_SLEEP
const char* const wakeup_reason_list[] = {
    "Undefined", "Undefined", "EXT0", "EXT1",
    "Timer", "Touchpad", "ULP", "GPIO", "UART",
};

static struct {
    struct arg_int *tout;
    struct arg_int *pin;
    struct arg_int *lvl;
    struct arg_str *mode;
    struct arg_end *end;
} system_sleep_args = {
    .tout = arg_int0("t", "time", "<t>", "wakeup time, ms"),
    .pin = arg_intn("p", "gpio", "<n>", 0, 8, "Wakeup using specified GPIO"),
    .lvl = arg_intn("l", "level", "<0|1>", 0, 8, "GPIO level to trigger wakeup"),
    .mode = arg_str0(NULL, "method", "<light|deep>", "sleep mode"),
    .end = arg_end(4)
};

static int enable_gpio_light_wakeup() {
    int gpio_count = system_sleep_args.pin->count;
    int level_count = system_sleep_args.lvl->count;
    if (level_count && (gpio_count != level_count)) {
        ESP_LOGE(TAG, "GPIO and level mismatch!");
        return ESP_ERR_INVALID_ARG;
    }
    int gpio, level;
    gpio_int_type_t intr;
    const char *lvls;
    for (int i = 0; i < gpio_count; i++) {
        gpio = system_sleep_args.pin->ival[i];
        level = level_count ? system_sleep_args.lvl->ival[i] : 0;
        lvls = level ? "HIGH" : "LOW";
        intr = level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL;
        fprintf(stderr, "Enable GPIO wakeup, num: %d, level: %s\n", gpio, lvls);
        ESP_ERROR_CHECK( gpio_wakeup_enable((gpio_num_t)gpio, intr) );
    }
    ESP_ERROR_CHECK( esp_sleep_enable_gpio_wakeup() );
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    return ESP_OK;
}

static int enable_gpio_deep_wakeup() {
    int gpio = system_sleep_args.pin->ival[0], level = 0;
    if (system_sleep_args.lvl->count) {
        level = system_sleep_args.lvl->ival[0];
        if (level != 0 && level != 1) {
            ESP_LOGE(TAG, "Invalid wakeup level: %d", level);
            return ESP_ERR_INVALID_ARG;
        }
    }
    const char *lvls = level ? "HIGH" : "LOW";
    esp_sleep_ext1_wakeup_mode_t mode;
    mode = level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ALL_LOW;
    fprintf(stderr, "Enable GPIO wakeup, num: %d, level: %s\n", gpio, lvls);
    ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup(1ULL << gpio, mode) );
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    return ESP_OK;
}

static int system_sleep(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &system_sleep_args))
        return ESP_ERR_INVALID_ARG;
    if (system_sleep_args.tout->count) {
        uint64_t timeout = system_sleep_args.tout->ival[0];
        fprintf(stderr, "Enable timer wakeup, timeout: %llums\n", timeout);
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(timeout * 1000) );
    }
    bool light = true;
    if (system_sleep_args.mode->count) {
        const char *mode = system_sleep_args.mode->sval[0];
        if (strstr(mode, "deep")) {
            light = false;
        } else if (!strstr(mode, "light")) {
            ESP_LOGE(TAG, "Unsupported sleep mode: %s", mode);
            return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t err;
    if (light) {
        if (system_sleep_args.pin->count) {
            if (( err = enable_gpio_light_wakeup() )) return err;
        }
        fprintf(stderr, "Enable UART wakeup, num: %d\n", NUM_UART);
        ESP_ERROR_CHECK( uart_set_wakeup_threshold(NUM_UART, 3) );
        ESP_ERROR_CHECK( esp_sleep_enable_uart_wakeup(NUM_UART) );
    } else {
        if (system_sleep_args.pin->count) {
            if (( err = enable_gpio_deep_wakeup() )) return err;
        }
    }

    fprintf(stderr, "Turn to %s sleep mode\n", light ? "light" : "deep");
    fflush(stderr); uart_tx_wait_idle(NUM_UART);
    if (light) {
        esp_light_sleep_start();
    } else {
        esp_deep_sleep_start();
    }
    fprintf(stderr, "ESP32 is woken up from light sleep mode by %s\n",
            wakeup_reason_list[(int)esp_sleep_get_wakeup_cause()]);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return ESP_OK;
}
#endif // CONSOLE_SYSTEM_SLEEP

static int system_otainfo(int c, char **v) { ota_partition_info(); return ESP_OK; }

#ifdef CONSOLE_SYSTEM_UPDATE
static struct {
    struct arg_str *cmd;
    struct arg_str *part;
    struct arg_str *url;
    struct arg_lit *fetch;
    struct arg_lit *reset;
    struct arg_end *end;
} system_update_args = {
    .cmd = arg_str1(NULL, NULL, "<boot|fetch|reset>", ""),
    .part = arg_str0(NULL, "part", "<label>", "partition to boot from"),
    .url = arg_str0(NULL, "url", "<url>", "specify URL to fetch"),
    .fetch = arg_lit0(NULL, "fetch", "fetch app firmware from URL"),
    .reset = arg_lit0(NULL, "reset", "clear OTA internal states"),
    .end = arg_end(4)
};

static int system_update(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &system_update_args))
        return ESP_ERR_INVALID_ARG;
    const char *subcmd = system_update_args.cmd->sval[0];
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
        const char *url = system_update_args.url->count ? \
                          system_update_args.url->sval[0] : NULL;
        if (!ota_updation_url(url)) {
            printf("Failed to udpate: %s\n", ota_updation_error());
            return ESP_FAIL;
        } else {
            printf("Updation success. Call `restart` to reboot ESP32");
        }
    } else {
        printf("Invalid command: `%s`\n", subcmd);
        return ESP_ERR_INVALID_ARG;
    }
    ota_partition_info();
    return ESP_OK;
}
#endif // CONSOLE_SYSTEM_UPDATE

static void register_system() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_SYSTEM_RESTART
        {
            .command = "restart",
            .help = "Software reset of ESP32",
            .hint = NULL,
            .func = &system_restart,
            .argtable = NULL
        },
#endif
#ifdef CONSOLE_SYSTEM_SLEEP
        {
            .command = "sleep",
            .help = "Turn ESP32 into light/deep sleep mode",
            .hint = NULL,
            .func = &system_sleep,
            .argtable = &system_sleep_args
        },
#endif
#ifdef CONSOLE_SYSTEM_LSOTA
        {
            .command = "lsota",
            .help = "List OTA Updation partition information",
            .hint = NULL,
            .func = &system_otainfo,
            .argtable = NULL,
        },
#endif
#ifdef CONSOLE_SYSTEM_UPDATE
        {
            .command = "update",
            .help = "OTA Updation helper command: boot, reset, fetch",
            .hint = NULL,
            .func = &system_update,
            .argtable = &system_update_args
        },
#endif
    };
    register_commands(cmds, sizeof(cmds) / sizeof(esp_console_cmd_t));
}

/******************************************************************************
 * Configuration commands
 */

#ifdef CONSOLE_CONFIG_IO
static struct {
    struct arg_str *key;
    struct arg_str *val;
    struct arg_lit *load;
    struct arg_lit *save;
    struct arg_lit *stat;
    struct arg_lit *list;
    struct arg_end *end;
} config_io_args = {
    .key = arg_str0(NULL, NULL, "key", "specify config by key"),
    .val = arg_str0(NULL, NULL, "value", "set config value"),
    .load = arg_lit0(NULL, "load", "load from NVS flash"),
    .save = arg_lit0(NULL, "save", "save to NVS flash"),
    .stat = arg_lit0(NULL, "stat", "summary NVS status"),
    .list = arg_lit0(NULL, "list", "list NVS entries"),
    .end = arg_end(6)
};

static int config_io(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &config_io_args))
        return ESP_ERR_INVALID_ARG;
    bool ret = true;
    if (config_io_args.stat->count) {
        config_nvs_stats();
    } else if (config_io_args.list->count) {
        config_nvs_list();
    } else if (config_io_args.load->count) {
        ret = config_nvs_load();
    } else if (config_io_args.save->count) {
        ret = config_nvs_dump();
    } else if (config_io_args.key->count) {
        const char *key = config_io_args.key->sval[0];
        if (config_io_args.val->count) {
            const char *val = config_io_args.val->sval[0];
            printf("Set `%s` to `%s` %s\n", key, val,
                   (ret = config_set(key, val)) ? "done" : "fail");
        } else {
            printf("Get `%s` value `%s`\n", key, config_get(key));
        }
    } else {
        // TODO: list config entries
    }
    return ret ? ESP_OK : ESP_FAIL;
}
#endif // CONSOLE_CONFIG_IO

static void register_config() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_CONFIG_IO
        {
            .command = "config",
            .help = "Set / get / load / save / list configurations",
            .hint = NULL,
            .func = &config_io,
            .argtable = &config_io_args
        },
#endif
    };
    register_commands(cmds, sizeof(cmds) / sizeof(esp_console_cmd_t));
}

/******************************************************************************
 * Driver commands
 */

#ifdef CONSOLE_DRIVER_LED
static struct {
    struct arg_int *idx;
    struct arg_str *cmd;
    struct arg_str *clr;
    struct arg_end *end;
} driver_led_args = {
    .idx = arg_int0("i", "index", "<0-20>", "specify index, default 0"),
    .cmd = arg_str0(NULL, NULL, "<on|off>", "enable/disable LED"),
    .clr = arg_str0("c", "color", "<0xAABBCC>", "specify RGB color"),
    .end = arg_end(3)
};

static int driver_led(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_led_args))
        return ESP_ERR_INVALID_ARG;
    uint8_t idx = driver_led_args.idx->count ? \
                  driver_led_args.idx->ival[0] : 0;
    if (driver_led_args.clr->count) {
        const char *color = driver_led_args.clr->sval[0];
        uint32_t rgb = strtol(color, NULL, 0);
        if (rgb > 0xFFFFFF) {
            printf("Unsupported color: `%s`\n", color);
            return ESP_ERR_INVALID_ARG;
        }
        led_set_color(idx, rgb);
    }
    if (driver_led_args.cmd->count) {
        const char *subcmd = driver_led_args.cmd->sval[0];
        if (strstr(subcmd, "off")) {
            led_set_light(idx, 0);
        } else if (strstr(subcmd, "on")) {
            led_set_light(idx, 1);
        } else {
            printf("Invalid command: `%s`\n", subcmd);
            return ESP_ERR_INVALID_ARG;
        }
        printf("Setting LED %d to %s\n", idx, subcmd);
    }
    printf("LED %d: color 0x%06X, brightness %.2f\n",
            idx, led_get_color(idx), led_get_light(idx));
    return ESP_OK;
}
#endif // CONSOLE_DRIVER_LED

#ifdef CONSOLE_DRIVER_GPIO
static struct {
    struct arg_int *pin;
    struct arg_int *lvl;
    struct arg_lit *i2c;
    struct arg_lit *spi;
    struct arg_end *end;
} driver_gpio_args = {
    .pin = arg_int0(NULL, NULL, "<0-39|100-123|200-215>", "pin number"),
    .lvl = arg_int0(NULL, NULL, "<0|1>", "set pin to LOW / HIGH"),
    .i2c = arg_lit0(NULL, "i2c_ext", "list I2C GPIO Expander"),
    .spi = arg_lit0(NULL, "spi_ext", "list SPI GPIO Expander"),
    .end = arg_end(4)
};

static int driver_gpio(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_gpio_args))
        return ESP_ERR_INVALID_ARG;
    if (!driver_gpio_args.pin->count) {
        gpio_table(driver_gpio_args.i2c->count, driver_gpio_args.spi->count);
        return ESP_OK;
    }
    bool level;
    uint32_t pin_num = driver_gpio_args.pin->ival[0];
    esp_err_t err = ESP_OK;
    if (driver_gpio_args.lvl->count) {
        err = gpioext_set_level(pin_num, driver_gpio_args.lvl->ival[0]);
    } else {
        err = gpioext_get_level(pin_num, &level, true);
    }
    if (err) {
        printf("%s GPIO %d level error: %s\n",
               driver_gpio_args.lvl->count ? "Set" : "Get",
               pin_num, esp_err_to_name(err));
    } else {
        printf("GPIO %d: %s\n", pin_num, level ? "HIGH" : "LOW");
    }
    return ESP_OK;
}
#endif // CONSOLE_DRIVER_GPIO

#ifdef CONSOLE_DRIVER_I2C
static struct {
    struct arg_int *bus;
    struct arg_int *addr;
    struct arg_int *reg;
    struct arg_int *val;
    struct arg_lit *hex;
    struct arg_int *len;
    struct arg_end *end;
} driver_i2c_args = {
    .bus = arg_int1(NULL, NULL, "<0|1>", "I2C bus number"),
    .addr = arg_int0(NULL, NULL, "<0x00-0x7F>", "I2C client 7-bit address"),
    .reg = arg_int0(NULL, NULL, "regaddr", "Register 8-bit address"),
    .val = arg_int0(NULL, NULL, "regval", "Register value"),
    .hex = arg_lit0("w", "word", "R / W in word (16-bit) mode"),
    .len = arg_int0("l", "len", "<num>", "Read specified length of registers"),
    .end = arg_end(6)
};

static int driver_i2c(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_i2c_args))
        return ESP_ERR_INVALID_ARG;
    int bus = driver_i2c_args.bus->ival[0];
    if (0 > bus || bus > 1) {
        printf("Invalid I2C bus number: %d\n", bus);
        return ESP_ERR_INVALID_ARG;
    }
    if (!driver_i2c_args.addr->count) {
        i2c_detect(bus);
        return ESP_OK;
    }
    uint8_t addr = driver_i2c_args.addr->ival[0];
    if (addr > 0x7F) {
        printf("Invalid I2C address: 0x%02X\n", addr);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t reg = driver_i2c_args.reg->count ? \
                  driver_i2c_args.reg->ival[0] : 0;
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
    } else if (!driver_i2c_args.len->count) {
        uint8_t val;
        if (( err = smbus_read_byte(bus, addr, reg, &val) )) return err;
        printf("I2C %d-%02X REG 0x%02X = %02X\n", bus, addr, reg, val);
    } else {
        err = smbus_dump(bus, addr, reg, driver_i2c_args.len->ival[0]);
    }
    return err;
}
#endif // CONSOLE_DRIVER_I2C

#ifdef CONSOLE_DRIVER_ALS
static struct {
    struct arg_int *idx;
    struct arg_str *rlt;
    struct arg_end *end;
} driver_als_args = {
    .idx = arg_int0(NULL, NULL, "<0-4>", "index of ALS chip"),
    .rlt = arg_str0("t", "track", "<0123HVEOA>", "run light tracking"),
    .end = arg_end(2)
};

static int driver_als(int argc, char **argv) {
    static const char *tpl = "Brightness of ALS %d is %.2f lux\n";
    if (!arg_noerror(argc, argv, (void **) &driver_als_args))
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    if (driver_als_args.rlt->count) {
        static const char *methods = "0123HVEOA";
        char *c = strchr(methods, driver_als_args.rlt->sval[0][0]);
        if (!c) {
            printf("Invalid tracking method: %s, select from <%s>\n",
                   driver_als_args.rlt->sval[0], methods);
            return ESP_ERR_INVALID_ARG;
        }
        int hdeg = -1, vdeg = -1;
        als_track_t method = (als_track_t)(c - methods);
        if (!( err = als_tracking(method, &hdeg, &vdeg) ))
            printf("ALS tracked to H: %d, V: %d\n", hdeg, vdeg);
        return err;
    }
    if (!driver_als_args.idx->count) {
        for (int idx = 0; idx < 4; idx++) {
            printf(tpl, idx, als_brightness(idx));
        }
    } else if (driver_als_args.idx->ival[0] < 3) {
        int idx = driver_als_args.idx->ival[0];
        printf(tpl, idx, als_brightness(idx));
    } else {
        gy39_data_t dat;
        if (!( err = gy39_measure(NUM_I2C, &dat) ))
            printf("GY39 %.2f lux, %.2f degC, %.2f Pa, %.2f %%, %.2f m\n",
                    dat.brightness, dat.temperature,
                    dat.atmosphere, dat.humidity, dat.altitude);
    }
    return err;
}
#endif // CONSOLE_DRIVER_ALS

#ifdef CONSOLE_DRIVER_PWM
static struct {
    struct arg_int *hdeg;
    struct arg_int *vdeg;
    struct arg_end *end;
} driver_pwm_args = {
    .hdeg = arg_int1(NULL, NULL, "<0-180>", "Yaw degree"),
    .vdeg = arg_int1(NULL, NULL, "<0-160>", "Pitch degree"),
    .end = arg_end(2)
};

static int driver_pwm(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_pwm_args))
        return ESP_ERR_INVALID_ARG;
    return pwm_degree(driver_pwm_args.hdeg->ival[0], driver_pwm_args.vdeg->ival[0]);
}
#endif // CONSOLE_DRIVER_PWM

static void register_driver() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_DRIVER_LED
        {
            .command = "led",
            .help = "Set / get LED color / brightness",
            .hint = NULL,
            .func= &driver_led,
            .argtable = &driver_led_args
        },
#endif
#ifdef CONSOLE_DRIVER_GPIO
        {
            .command = "gpio",
            .help = "Set / get GPIO pin level",
            .hint = NULL,
            .func = &driver_gpio,
            .argtable = &driver_gpio_args
        },
#endif
#ifdef CONSOLE_DRIVER_I2C
        {
            .command = "i2c",
            .help = "Detect alive I2C slaves on the BUS line",
            .hint = NULL,
            .func = &driver_i2c,
            .argtable = &driver_i2c_args
        },
#endif
#ifdef CONSOLE_DRIVER_ALS
        {
            .command = "als",
            .help = "Get ALS sensor values and do light tracking",
            .hint = NULL,
            .func = &driver_als,
            .argtable = &driver_als_args
        },
#endif
#ifdef CONSOLE_DRIVER_PWM
        {
            .command = "pwm",
            .help = "Control rotation of servo by PWM",
            .hint = NULL,
            .func = &driver_pwm,
            .argtable = &driver_pwm_args
        },
#endif
    };
    register_commands(cmds, sizeof(cmds) / sizeof(esp_console_cmd_t));
}

/******************************************************************************
 * Utilities commands
 */

static int utils_hardware(int c, char **v) { hardware_info(); return ESP_OK; }

static int utils_partinfo(int c, char **v) { partition_info(); return ESP_OK; }

static int utils_taskinfo(int c, char **v) { task_info(); return ESP_OK; }

static int utils_version(int c, char **v) { version_info(); return ESP_OK; }

#ifdef CONSOLE_UTILS_LSMEM
static struct {
    struct arg_lit *verbose;
    struct arg_end *end;
} utils_memory_args = {
    .verbose = arg_litn("v", NULL, 0, 2, "additive option for more output"),
    .end = arg_end(1)
};

static int utils_memory(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &utils_memory_args))
        return ESP_ERR_INVALID_ARG;
    switch (utils_memory_args.verbose->count) {
    case 0:
        memory_info(); break;
    case 2:
        // Too much infomation
        // heap_caps_dump_all(); break;
        heap_caps_print_heap_info(MALLOC_CAP_DMA);
        heap_caps_print_heap_info(MALLOC_CAP_EXEC);
        __attribute__((fallthrough)); // for GCC -Wimplicit-fallthrough
    case 1:
        heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        break;
    }
    return ESP_OK;
}
#endif // CONSOLE_UTILS_LSMEM

#ifdef CONSOLE_UTILS_LSFS
static struct {
    struct arg_str *dir;
    struct arg_str *dev;
    struct arg_end *end;
} utils_listdir_args = {
    .dir = arg_str0(NULL, NULL, "abspath", NULL),
    .dev = arg_str0("d", NULL, "<flash|sdmmc>", "select FS from device"),
    .end = arg_end(2)
};

static int utils_listdir(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &utils_listdir_args))
        return ESP_ERR_INVALID_ARG;
    const char *dev = utils_listdir_args.dev->count ? \
                      utils_listdir_args.dev->sval[0] : "flash";
    const char *dir = utils_listdir_args.dir->count ? \
                      utils_listdir_args.dir->sval[0] : "/";
    if (strstr(dev, "flash")) {
        FFS.list(dir, stdout);
    } else if (strstr(dev, "sdmmc")) {
        SDFS.list(dir, stdout);
    } else {
        printf("Invalid device: `%s`\n", dev);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
#endif // CONSOLE_UTILS_LSFS

#ifdef CONSOLE_UTILS_HIST
static struct {
    struct arg_str *cmd;
    struct arg_str *dst;
    struct arg_end *end;
} utils_history_args = {
    .cmd = arg_str1(NULL, NULL, "<load|save>", ""),
    .dst = arg_str0("f", "file", "history.txt", "relative path to file"),
    .end = arg_end(2)
};

static int utils_history(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &utils_history_args))
        return ESP_ERR_INVALID_ARG;
    const char *subcmd = utils_history_args.cmd->sval[0];
    bool save = false;
    if (strstr(subcmd, "save")) save = true;
    else if (!strstr(subcmd, "load")) {
        printf("Invalid command: `%s`\n", subcmd);
        return ESP_ERR_INVALID_ARG;
    }
    const char *dst = utils_history_args.dst->count ? \
                      utils_history_args.dst->sval[0] : "history.txt";
    size_t plen = strlen(FFS_MP), dlen = strlen(Config.web.DIR_DATA);
    char * fn = (char *)malloc(plen + dlen + strlen(dst) + 1);
    if (!fn) return ESP_ERR_NO_MEM;
    strcpy(fn, FFS_MP); strcat(fn, Config.web.DIR_DATA); strcat(fn, dst);
    if (!FFS.exists(fn + plen) && !save) {
        printf("History file `%s` does not exist\n", fn);
        return ESP_ERR_NOT_FOUND;
    }
    int ret = save ? linenoiseHistorySave(fn) : linenoiseHistoryLoad(fn);
    printf("History file `%s` %s %s\n", fn, subcmd, ret ? "fail" : "done");
    return ret;
}
#endif // CONSOLE_UTILS_HIST

static void register_utils() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_UTILS_VER
        {
            .command = "version",
            .help = "Get version of firmware and SDK",
            .hint = NULL,
            .func = &utils_version,
            .argtable = NULL
        },
#endif
#ifdef CONSOLE_UTILS_LSHW
        {
            .command = "lshw",
            .help = "Display hardware information",
            .hint = NULL,
            .func = &utils_hardware,
            .argtable = NULL
        },
#endif
#ifdef CONSOLE_UTILS_LSPART
        {
            .command = "lspart",
            .help = "Enumerate partitions in flash",
            .hint = NULL,
            .func = &utils_partinfo,
            .argtable = NULL
        },
#endif
#ifdef CONSOLE_UTILS_LSTASK
        {
            .command = "lstask",
            .help = "Enumerate running RTOS tasks",
            .hint = NULL,
            .func = &utils_taskinfo,
            .argtable = NULL
        },
#endif
#ifdef CONSOLE_UTILS_LSMEM
        {
            .command = "lsmem",
            .help = "List avaiable memory blocks with their status",
            .hint = NULL,
            .func = utils_memory,
            .argtable = &utils_memory_args
        },
#endif
#ifdef CONSOLE_UTILS_LSFS
        {
            .command = "lsfs",
            .help = "List directory contents under specified device",
            .hint = NULL,
            .func = &utils_listdir,
            .argtable = &utils_listdir_args
        },
#endif
#ifdef CONSOLE_UTILS_HIST
        {
            .command = "hist",
            .help = "Load from or save console history to a local disk",
            .hint = NULL,
            .func = utils_history,
            .argtable = &utils_history_args
        },
#endif
    };
    register_commands(cmds, sizeof(cmds) / sizeof(esp_console_cmd_t));
}

/******************************************************************************
 * WiFi commands
 */

typedef struct {
    struct arg_str *cmd;
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_end *end;
} wifi_args_t;

#ifdef CONSOLE_WIFI_STA
static wifi_args_t wifi_sta_args = {
    .cmd = arg_str1(NULL, NULL, "<status|scan|connect|disconnect>", ""),
    .ssid = arg_str0("s", NULL, "<SSID>", "SSID of AP"),
    .pass = arg_str0("p", NULL, "<PASS>", "Password of AP"),
    .end = arg_end(3)
};

static int wifi_sta(int argc, char **argv) { // TODO
    if (!arg_noerror(argc, argv, (void **) &wifi_sta_args))
        return ESP_ERR_INVALID_ARG;
    const char * subcmd = wifi_sta_args.cmd->sval[0];
    if (strstr(subcmd, "status")) {
        return ESP_ERR_NOT_FOUND; // wifi_sta_list_ap();
    } else if (strstr(subcmd, "scan")) {
        return ESP_ERR_NOT_FOUND; // wifi_sta_scan();
    } else if (strstr(subcmd, "connect")) {
        return ESP_ERR_NOT_FOUND; // wifi_sta_connect();
    } else if (strstr(subcmd, "disconnect")) {
        return ESP_ERR_NOT_FOUND; // wifi_sta_disconnect();
    }
    printf("Invalid command: `%s`\n", subcmd);
    return ESP_ERR_INVALID_ARG;
}
#endif // CONSOLE_WIFI_STA

#ifdef CONSOLE_WIFI_AP
static wifi_args_t wifi_ap_args = {
    .cmd = arg_str1(NULL, NULL, "<status|start|stop>", ""),
    .ssid = arg_str0("s", NULL, "<SSID>", "SSID of AP"),
    .pass = arg_str0("p", NULL, "<PASS>", "Password of AP"),
    .end = arg_end(3)
};

static int wifi_ap(int argc, char **argv) { // TODO
    if (!arg_noerror(argc, argv, (void **) &wifi_ap_args))
        return ESP_ERR_INVALID_ARG;
    const char * subcmd = wifi_ap_args.cmd->sval[0];
    if (strstr(subcmd, "status")) {
        return ESP_ERR_NOT_FOUND; // wifi_ap_list_sta();
    } else if (strstr(subcmd, "start")) {
        return ESP_ERR_NOT_FOUND; // wifi_ap_start();
    } else if (strstr(subcmd, "stop")) {
        return ESP_ERR_NOT_FOUND; // wifi_ap_stop();
    }
    printf("Invalid command: `%s`\n", subcmd);
    return ESP_ERR_INVALID_ARG;
}
#endif // CONSOLE_WIFI_AP

static void register_wifi() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_WIFI_STA
        {
            .command = "sta",
            .help = "Query / Scan / Connect / Disconnect Access Pointes",
            .hint = NULL,
            .func = &wifi_sta,
            .argtable = &wifi_sta_args
        },
#endif
#ifdef CONSOLE_WIFI_AP
        {
            .command = "ap",
            .help = "Query / Start / Stop Soft Access Point",
            .hint = NULL,
            .func = &wifi_ap,
            .argtable = &wifi_ap_args
        },
#endif
    };
    register_commands(cmds, sizeof(cmds) / sizeof(esp_console_cmd_t));
}

/******************************************************************************
 * Export register commands
 */

extern "C" void console_register_commands() {
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_ERROR_CHECK( esp_console_register_help_command() );
    register_system();
    register_config();
    register_driver();
    register_utils();
    register_wifi();
}
