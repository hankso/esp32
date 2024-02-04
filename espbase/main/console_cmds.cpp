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
#include "network.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "rom/uart.h"
#include "sys/param.h"
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

#define ARG_STR(p, s)   ((p)->count ? (p)->sval[0] : (s))
#define ARG_INT(p, v)   ((p)->count ? (p)->ival[0] : (v))

/******************************************************************************
 * System commands
 */

#ifdef CONSOLE_SYSTEM_RESTART
static int system_restart(int c, char **v) { esp_restart(); return ESP_OK; }
#endif

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
    .tout = arg_int0(NULL, "time", "<t>", "wakeup time, ms"),
    .pin  = arg_intn(NULL, "gpio", "<n>", 0, 8, "Wakeup using specified GPIO"),
    .lvl  = arg_intn(NULL, "level", "<0|1>", 0, 8, "GPIO level to trigger wakeup"),
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

#ifdef CONSOLE_SYSTEM_UPDATE
static struct {
    struct arg_str *cmd;
    struct arg_str *part;
    struct arg_str *url;
    struct arg_lit *fetch;
    struct arg_lit *reset;
    struct arg_end *end;
} system_update_args = {
    .cmd   = arg_str0(NULL, NULL, "<boot|fetch|reset>", ""),
    .part  = arg_str0(NULL, "part", "<label>", "partition to boot from"),
    .url   = arg_str0(NULL, "url", "<url>", "specify URL to fetch"),
    .fetch = arg_lit0("f", "fetch", "fetch app firmware from URL"),
    .reset = arg_lit0("r", "reset", "clear OTA internal states"),
    .end = arg_end(5)
};

static int system_update(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &system_update_args))
        return ESP_ERR_INVALID_ARG;
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
    const char *key = ARG_STR(config_io_args.key, NULL);
    const char *val = ARG_STR(config_io_args.val, NULL);
    if (key) {
        if (val) {
            printf("Set `%s` to `%s` %s\n", key, val,
                   (ret = config_set(key, val)) ? "done" : "fail");
        } else {
            printf("Get `%s` value `%s`\n", key, config_get(key));
        }
    } else if (config_io_args.load->count) {
        ret = config_nvs_load();
    } else if (config_io_args.save->count) {
        ret = config_nvs_dump();
    } else if (config_io_args.list->count) {
        config_nvs_list();
    } else if (config_io_args.stat->count) {
        config_nvs_stats();
    } else {
        config_list();
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
    struct arg_str *lgt;
    struct arg_str *clr;
    struct arg_int *blk;
    struct arg_end *end;
} driver_led_args = {
    .idx = arg_int0(NULL, NULL, "<0-" STR(CONFIG_LED_NUM) ">", "LED index"),
    .lgt = arg_str0(NULL, "light", "<0-255|on|off>", "set brightness"),
    .clr = arg_str0(NULL, "color", "<0xAABBCC>", "set RGB color"),
    .blk = arg_int0(NULL, "blink", "<-1-7>", "set blink effect"),
    .end = arg_end(4)
};

static int driver_led(int argc, char **argv) {
    static char buf[16];
    if (!arg_noerror(argc, argv, (void **) &driver_led_args))
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    int idx = ARG_INT(driver_led_args.idx, -1);
    int blk = ARG_INT(driver_led_args.blk, -2);
    const char *light = ARG_STR(driver_led_args.lgt, NULL);
    const char *color = ARG_STR(driver_led_args.clr, NULL);
    if (blk > -2) {
        if (!( err = led_set_blink(blk) )) {
            if (blk >= 0)
                printf("LED: set blink to %d\n", blk);
            else
                puts("LED: stop blink");
        }
        return err;
    }
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
    if (idx >= CONFIG_LED_NUM) {
        printf("Invalid LED index: `%d`\n", idx);
        err = ESP_ERR_INVALID_ARG;
    } else {
        printf("LED%s: color 0x%06X, brightness %d\n",
            buf, led_get_color(idx), led_get_light(idx));
    }
    return err;
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
    .pin = arg_int0(NULL, NULL, "<0-39|40-79|80-99>", "pin number"),
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
    .reg = arg_int0(NULL, NULL, "regaddr", "register 8-bit address"),
    .val = arg_int0(NULL, NULL, "regval", "register value"),
    .hex = arg_lit0("w", "word", "read/write in word (16-bit) mode"),
    .len = arg_int0(NULL, "len", "<num>", "read specified length of registers"),
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
    uint8_t reg = ARG_INT(driver_i2c_args.reg, 0);
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
    .rlt = arg_str0(NULL, "track", "<0123HVEOA>", "run light tracking"),
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

#ifdef CONSOLE_DRIVER_ADC
static struct {
    struct arg_int *tdly;
    struct arg_int *tout;
    struct arg_end *end;
} driver_adc_args = {
    .tdly = arg_int0("d", NULL, "<10-1000>", "delay in ms, default 500"),
    .tout = arg_int0("t", NULL, "<0-65535>", "loop until timeout in sec"),
    .end = arg_end(2)
};

static int driver_adc(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_adc_args))
        return ESP_ERR_INVALID_ARG;
    if (!driver_adc_args.tout->count) {
        printf("ADC value: %4umV\n", adc_read());
    } else {
        uint16_t delay_ms = ARG_INT(driver_adc_args.tdly, 500);
        uint32_t timeout_ms = driver_adc_args.tout->ival[0] * 1000;
        delay_ms = MAX(10, MIN(delay_ms, 1000));
        while (timeout_ms >= delay_ms) {
            fprintf(stderr, "\rADC value: %4umV", adc_read()); fflush(stderr);
            msleep(delay_ms);
            timeout_ms -= delay_ms;
        }
        fputc('\n', stderr);
        fputc('\n', stdout);
    }
    return ESP_OK;
}
#endif

#ifdef CONSOLE_DRIVER_PWM
static struct {
    struct arg_int *hdeg;
    struct arg_int *vdeg;
    struct arg_int *freq;
    struct arg_int *pcent;
    struct arg_end *end;
} driver_pwm_args = {
    .hdeg = arg_int0("y", NULL, "<0-180>", "yaw degree"),
    .vdeg = arg_int0("p", NULL, "<0-160>", "pitch degree"),
    .freq = arg_int0("f", NULL, "<1-20000>", "tone frequency"),
    .pcent = arg_int0("l", NULL, "<0-100>", "tone loudness (percentage)"),
    .end = arg_end(4)
};

static int driver_pwm(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &driver_pwm_args))
        return ESP_ERR_INVALID_ARG;
    int hdeg = ARG_INT(driver_pwm_args.hdeg, -1),
        vdeg = ARG_INT(driver_pwm_args.vdeg, -1),
        pcent = ARG_INT(driver_pwm_args.pcent, -1);
    uint32_t freq = ARG_INT(driver_pwm_args.freq, -1);
    if (hdeg >= 0 || vdeg >= 0)
        return pwm_set_degree(hdeg, vdeg);
    if (freq != 0xFFFFFFFF || pcent >= 0)
        return pwm_set_tone(freq, pcent);
    esp_err_t err = ESP_OK;
    if (!( err = pwm_get_degree(&hdeg, &vdeg) ))
        printf("PWM Degree: %d %d\n", hdeg, vdeg);
    if (!( err = pwm_get_tone(&freq, &pcent) ))
        printf("PWM Tone: %dHz %d%%\n", freq, pcent);
    return err;
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
#ifdef CONSOLE_DRIVER_ADC
        {
            .command = "adc",
            .help = "Read ADC and calculate value in mV",
            .hint = NULL,
            .func = &driver_adc,
            .argtable = &driver_adc_args
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

#ifdef CONSOLE_UTILS_LSHW
static int utils_hardware(int c, char **v) { hardware_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_LSPART
static int utils_partinfo(int c, char **v) { partition_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_LSTASK
static int utils_taskinfo(int c, char **v) { task_info(); return ESP_OK; }
#endif

#ifdef CONSOLE_UTILS_VER
static int utils_version(int c, char **v) { version_info(); return ESP_OK; }
#endif

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
    const char *dev = ARG_STR(utils_listdir_args.dev, "flash");
    const char *dir = ARG_STR(utils_listdir_args.dir, "/");
    if (strstr(dev, "flash")) {
#ifdef CONFIG_FFS_MP
        FFS.list(dir, stdout);
#else
        ESP_LOGW(TAG, "Flash File System not enabled");
#endif
    } else if (strstr(dev, "sdmmc")) {
#ifdef CONFIG_SDFS_MP
        SDFS.list(dir, stdout);
#else
        ESP_LOGW(TAG, "SDMMC File System not enabled");
#endif
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
    struct arg_str *dev;
    struct arg_str *dst;
    struct arg_end *end;
} utils_history_args = {
    .cmd = arg_str1(NULL, NULL, "<load|save>", ""),
    .dev = arg_str0("d", NULL, "<flash|sdmmc>", "select FS from device"),
    .dst = arg_str0("f", NULL, "history.txt", "relative path to file"),
    .end = arg_end(3)
};

static int utils_history(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &utils_history_args))
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    const char *subcmd = utils_history_args.cmd->sval[0];
    bool save = false, exists = false;
    if (strstr(subcmd, "save")) {
        save = true;
    } else if (!strstr(subcmd, "load")) {
        printf("Invalid command: `%s`\n", subcmd);
        return err;
    }
    const char *dev = ARG_STR(utils_history_args.dev, "flash");
    const char *dst = ARG_STR(utils_history_args.dst, "history.txt");
    static char fullpath[CONFIG_SPIFFS_OBJ_NAME_LEN];
    if (strstr(dev, "flash")) {
#ifdef CONFIG_FFS_MP
        snprintf(fullpath, sizeof(fullpath), "%s%s%s",
                CONFIG_FFS_MP, Config.web.DIR_DATA, dst);
        exists = FFS.exists(fullpath + strlen(CONFIG_FFS_MP));
#else
        ESP_LOGW(TAG, "Flash File System not enabled");
#endif // CONFIG_FFS_MP
    } else if (strstr(dev, "sdmmc")) {
#ifdef CONFIG_SDFS_MP
        snprintf(fullpath, sizeof(fullpath), "%s%s%s",
                CONFIG_SDFS_MP, Config.web.DIR_DATA, dst);
        exists = SDFS.exists(fullpath + strlen(CONFIG_SDFS_MP));
#else
        ESP_LOGW(TAG, "SDMMC File System not enabled");
#endif // CONFIG_SDFS_MP
    } else {
        printf("Invalid device: `%s`\n", dev);
        return err;
    }
    if (!exists && !save) {
        printf("History file `%s` does not exist\n", fullpath);
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = save ? linenoiseHistorySave(fullpath) : linenoiseHistoryLoad(fullpath);
        printf("History file `%s` %s %s\n",
                fullpath, subcmd, err ? "fail" : "done");
    }
    return err;
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

#ifdef CONSOLE_NET_STA
static struct {
    struct arg_str *cmd;
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_int *tout;
    struct arg_end *end;
} net_sta_args = {
    .cmd = arg_str0(NULL, NULL, "<scan|join|leave>", ""),
    .ssid = arg_str0("s", NULL, "<SSID>", "AP hostname"),
    .pass = arg_str0("p", NULL, "<PASS>", "AP password"),
    .tout = arg_int0("t", NULL, "<msec>", "timeout to wait"),
    .end = arg_end(4)
};

static int net_sta(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &net_sta_args))
        return ESP_ERR_INVALID_ARG;
    const char * subcmd = ARG_STR(net_sta_args.cmd, "");
    if (strstr(subcmd, "scan")) {
        const char *ssid = ARG_STR(net_sta_args.ssid, NULL);
        uint16_t timeout_ms = ARG_INT(net_sta_args.tout, 0);
        return wifi_sta_scan(ssid, 0, timeout_ms);
    } else if (strstr(subcmd, "join")) {
        const char *ssid = ARG_STR(net_sta_args.ssid, NULL);
        const char *pass = ARG_STR(net_sta_args.pass, (ssid ? "" : NULL));
        esp_err_t err = wifi_sta_start(ssid, pass, NULL);
        if (!err && net_sta_args.tout->count)
            err = wifi_sta_wait(net_sta_args.tout->ival[0]);
        return err;
    } else if (strstr(subcmd, "leave")) {
        return wifi_sta_stop();
    }
    return wifi_sta_list_ap();
}
#endif // CONSOLE_NET_STA

#ifdef CONSOLE_NET_AP
static struct {
    struct arg_str *cmd;
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_end *end;
} net_ap_args = {
    .cmd = arg_str0(NULL, NULL, "<start|stop>", ""),
    .ssid = arg_str0("s", NULL, "<SSID>", "AP hostname"),
    .pass = arg_str0("p", NULL, "<PASS>", "AP password"),
    .end = arg_end(3)
};

static int net_ap(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &net_ap_args))
        return ESP_ERR_INVALID_ARG;
    const char * subcmd = ARG_STR(net_ap_args.cmd, "");
    if (strstr(subcmd, "start")) {
        const char *ssid = ARG_STR(net_ap_args.ssid, NULL);
        const char *pass = ARG_STR(net_ap_args.pass, (ssid ? "" : NULL));
        return wifi_ap_start(ssid, pass, NULL);
    } else if (strstr(subcmd, "stop")) {
        return wifi_ap_stop();
    }
    return wifi_ap_list_sta();
}
#endif // CONSOLE_NET_AP

#ifdef CONSOLE_NET_IPERF
static struct {
    struct arg_str *host;
    struct arg_int *port;
    struct arg_int *size;
    struct arg_int *intv;
    struct arg_int *tout;
    struct arg_lit *stop;
    struct arg_lit *udp;
    struct arg_end *end;
} net_iperf_args = {
    .host = arg_str0("c", NULL, "<host>", "run in client mode"),
    .port = arg_int0("p", NULL, "<port>", "specify port number"),
    .size = arg_int0("l", NULL, "<bytes>", "read/write buffer size"),
    .intv = arg_int0("i", NULL, "<sec>", "time between bandwidth reports"),
    .tout = arg_int0("t", NULL, "<sec>", "time to transmit for"),
    .stop = arg_lit0(NULL, "stop", "stop currently running iperf"),
    .udp = arg_lit0("u", "udp", "use UDP rather than TCP"),
    .end = arg_end(7)
};

static int net_iperf(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &net_iperf_args))
        return ESP_ERR_INVALID_ARG;
    return iperf_command(
        ARG_STR(net_iperf_args.host, NULL),
        ARG_INT(net_iperf_args.port, 0), ARG_INT(net_iperf_args.size, 0),
        ARG_INT(net_iperf_args.intv, 0), ARG_INT(net_iperf_args.tout, 0),
        net_iperf_args.stop->count, net_iperf_args.udp->count
    );
}
#endif

#ifdef CONSOLE_NET_PING
static struct {
    struct arg_str *host;
    struct arg_int *tout;
    struct arg_int *size;
    struct arg_int *npkt;
    struct arg_end *end;
} net_ping_args = {
    .host = arg_str1(NULL, NULL, "<host>", "target IP address"),
    .tout = arg_int0("t", NULL, "<msec>", "time to wait for a response"),
    .size = arg_int0("s", NULL, "<byte>", "number of data bytes to be sent"),
    .npkt = arg_int0("c", NULL, "<num>", "stop after sending num packets"),
    .end = arg_end(4)
};

static int net_ping(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &net_ping_args))
        return ESP_ERR_INVALID_ARG;
    return ping_command(
        net_ping_args.host->sval[0], ARG_INT(net_ping_args.tout, 0),
        ARG_INT(net_ping_args.size, 0), ARG_INT(net_ping_args.npkt, 0)
    );
}
#endif

#ifdef CONSOLE_NET_FTM
static struct {
    struct arg_str *cmd;
    struct arg_str *ssid;
    struct arg_int *npkt;
    struct arg_int *tout;
    struct arg_int *base;
    struct arg_str *ctrl;
    struct arg_end *end;
} net_ftm_args = {
    .cmd = arg_str1(NULL, NULL, "<REP|REQ>", "run as responder | initiator"),
    .ssid = arg_str0(NULL, NULL, "<SSID>", "for initiator: target AP hostname"),
    .npkt = arg_int0("c", NULL, "<0:8:32|64>", "for initiator: frame count"),
    .tout = arg_int0("t", NULL, "<msec>", "for initiator: timeout in ms"),
    .base = arg_int0("o", NULL, "<cm>", "for responder: T1 offset in cm"),
    .ctrl = arg_str0("a", NULL, "<on|off>", "for responder: enable / disable"),
    .end = arg_end(6)
};

static int net_ftm(int argc, char **argv) {
    if (!arg_noerror(argc, argv, (void **) &net_ftm_args))
        return ESP_ERR_INVALID_ARG;
    const char *subcmd = net_ftm_args.cmd->sval[0];
    if (strstr(subcmd, "REP")) {
        int16_t base = net_ftm_args.base->ival[0];
        return ftm_responder(
            ARG_STR(net_ftm_args.ctrl, NULL),
            net_ftm_args.base->count ? &base : NULL);
    } else if (strstr(subcmd, "REQ")) {
        uint8_t npkt = net_ftm_args.npkt->ival[0];
        return ftm_initiator(
            ARG_STR(net_ftm_args.ssid, NULL),
            ARG_INT(net_ftm_args.tout, 0),
            net_ftm_args.npkt->count ? &npkt : NULL);
    } else {
        printf("Invalid command: `%s`\n", subcmd);
        return ESP_ERR_INVALID_ARG;
    }
}
#endif

static void register_network() {
    const esp_console_cmd_t cmds[] = {
#ifdef CONSOLE_NET_STA
        {
            .command = "sta",
            .help = "Query / Scan / Connect / Disconnect Access Pointes",
            .hint = NULL,
            .func = &net_sta,
            .argtable = &net_sta_args
        },
#endif
#ifdef CONSOLE_NET_AP
        {
            .command = "ap",
            .help = "Query / Start / Stop Soft Access Point",
            .hint = NULL,
            .func = &net_ap,
            .argtable = &net_ap_args
        },
#endif
#ifdef CONSOLE_NET_IPERF
        {
            .command = "iperf",
            .help = "Bandwidth test on IP networks",
            .hint = NULL,
            .func = &net_iperf,
            .argtable = &net_iperf_args
        },
#endif
#ifdef CONSOLE_NET_PING
        {
            .command = "ping",
            .help = "Send ICMP ECHO_REQUEST to specified hosts",
            .hint = NULL,
            .func = &net_ping,
            .argtable = &net_ping_args
        },
#endif
#ifdef CONSOLE_NET_FTM
        {
            .command = "ftm",
            .help = "Fine Timing Measurement between STA and AP using RTT",
            .hint = NULL,
            .func = &net_ftm,
            .argtable = &net_ftm_args
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
    register_network();
}
