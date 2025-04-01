/* 
 * File: drivers.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:54:28
 */

#include "drivers.h"
#include "config.h"
#include "ledmode.h"
#include "hidtool.h"
#include "timesync.h"

#include "esp_attr.h"
#include "esp_camera.h"
#include "esp_intr_alloc.h"
#include "soc/soc_caps.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_BASE_USE_BTN
#   include "iot_button.h"
#endif

#ifdef CONFIG_BASE_USE_KNOB
#   include "iot_knob.h"
#endif

static const char *TAG = "Driver";

/******************************************************************************
 * UART with custom pin
 */

static void uart_initialize() {
    // esp_vfs_dev_uart_register is called on startup code to use /dev/uart0
    fflush(stdout); fsync(fileno(stdout));

#ifdef CONFIG_BASE_USE_UART
    // UART driver configuration
    uart_config_t uart_conf = {
#   ifdef CONFIG_BASE_USE_CONSOLE
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
#   else
        .baud_rate = 115200,
#   endif
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
#   if SOC_UART_SUPPORT_REF_TICK
        .source_clk = UART_SCLK_REF_TICK
#   elif SOC_UART_SUPPORT_XTAL_CLK
        .source_clk = UART_SCLK_XTAL
#   else
#       error "No UART clock source is aware of DFS"
#   endif
    };
    uart_param_config(NUM_UART, &uart_conf);
    uart_set_pin(NUM_UART, PIN_TXD, PIN_RXD, PIN_RTS, PIN_CTS);
    ESP_ERROR_CHECK( uart_driver_install(NUM_UART, 256, 0, 0, NULL, 0) );
#endif
}

/******************************************************************************
 * ADC analog in
 */

#ifdef CONFIG_BASE_USE_ADC
#include "soc/adc_periph.h"

#ifdef TARGET_IDF_5
#   include "esp_adc/adc_oneshot.h"
#   include "esp_adc/adc_cali.h"
#   include "esp_adc/adc_cali_scheme.h"
#else
#   include "driver/adc.h"
#   include "esp_adc_cal.h"
#endif

static struct {
#ifdef TARGET_IDF_5
    adc_cali_handle_t cali;
    adc_oneshot_unit_handle_t oneshot;
#else
    esp_adc_cal_characteristics_t chars;
#endif
    adc_channel_t chans[2];
    gpio_num_t pins[2];
    const adc_unit_t unit;
    const adc_atten_t atten;
    const adc_bits_width_t width;
} adc = {
#ifdef TARGET_IDF_5
    .cali = { 0 },
    .oneshot = { 0 },
#else
    .chars = { 0 },
#endif
    .pins = {
#ifdef PIN_ADC1
        PIN_ADC1,
#else
        PIN_UNUSED,
#endif
#ifdef PIN_ADC2
        PIN_ADC2,
#else
        PIN_UNUSED,
#endif
    },
    .chans = { ADC_CHANNEL_MAX, ADC_CHANNEL_MAX },
    .unit = ADC_UNIT_1, // only use ADC1
    .atten = ADC_ATTEN_DB_12,
#ifdef CONFIG_BASE_ADC_HALL_SENSOR
    .width = ADC_WIDTH_BIT_12,
#else
    .width = ADC_WIDTH_BIT_DEFAULT,
#endif
};

static adc_channel_t gpio2adc(gpio_num_t pin) {
    LOOPN(j, SOC_ADC_MAX_CHANNEL_NUM) {
        if (adc_channel_io_map[0][j] == pin) return j;
    }
    return ADC_CHANNEL_MAX;
}

static void adc_initialize() {
    LOOPN(i, LEN(adc.chans)) {
        if (adc.pins[i] == PIN_UNUSED) continue;
        if (( adc.chans[i] = gpio2adc(adc.pins[i]) ) == ADC_CHANNEL_MAX) {
            ESP_LOGE(TAG, "ADC: invalid pin %d", adc.pins[i]);
            return;
        }
    }
    esp_err_t err = ESP_OK;
#ifdef TARGET_IDF_5
    adc_oneshot_unit_init_cfg_t init_conf = { .unit_id = adc.unit };
    if (!( err = adc_oneshot_new_unit(&init_conf, &adc.oneshot) )) {
        adc_oneshot_chan_cfg_t chan_conf = {
            .bitwidth = adc.width,
            .atten = adc.atten,
        };
        ITERV(chan, adc.chans) {
            if (chan == ADC_CHANNEL_MAX) continue;
            err = adc_oneshot_config_channel(adc.oneshot, chan, &chan_conf);
            if (err) break;
        }
    }
    if (!err) err = adc_calibration_init(adc.unit, adc.atten, &adc.cali);
#else
    ITERV(chan, adc.chans) {
        if (chan == ADC_CHANNEL_MAX) continue;
        if (( err = adc1_config_channel_atten(chan, adc.atten) )) break;
    }
    if (!err) err = adc1_config_width(adc.width);
#   ifdef CONFIG_IDF_TARGET_ESP32
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF)) {
        ESP_LOGI(TAG, "ADC: eFuse VRef not supported");
    } else {
        ESP_LOGD(TAG, "ADC: eFuse VRef supported");
    }
#   endif
#   if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONIFG_IDF_TARGET_ESP32S3)
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP)) {
        ESP_LOGI(TAG, "ADC: eFuse Two Point not supported");
    } else {
        ESP_LOGD(TAG, "ADC: eFuse Two Point supported");
    }
#   endif
    memset(&adc.chars, 0, sizeof(adc.chars));
    esp_adc_cal_value_t vtype = esp_adc_cal_characterize(
        adc.unit, adc.atten, adc.width, 1100, &adc.chars);
    if (vtype == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGD(TAG, "ADC: characterized using Two Point Value");
    } else if (vtype == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGD(TAG, "ADC: characterized using eFuse VRef");
    } else {
        ESP_LOGD(TAG, "ADC: characterized using Default VRef");
    }
#endif // TARGET_IDF_5
    if (err) {
        ESP_LOGE(TAG, "ADC initialize failed: %s", esp_err_to_name(err));
        LOOPN(i, LEN(adc.chans)) { adc.chans[i] = ADC_CHANNEL_MAX; }
    }
}

int adc_hall() {
#if defined(CONFIG_BASE_ADC_HALL_SENSOR) && defined(TARGET_IDF_4)
    int raw = 0;
    adc_power_acquire();
    LOOPN(i, CONFIG_BASE_ADC_MULTISAMPLING) {
        usleep(10);
        raw += hall_sensor_read();
    }
    adc_power_release();
    ITERV(chan, adc.chans) {
        if (chan != ADC_CHANNEL_MAX) adc1_config_channel_atten(chan, adc.atten);
    }
    return raw / CONFIG_BASE_ADC_MULTISAMPLING;
#else
    return 0;
#endif
}

int adc_read(uint8_t idx) {
    if (idx >= LEN(adc.chans) || adc.chans[idx] == ADC_CHANNEL_MAX)
        return -1;
    int raw, cum = 0, cnt = 0;
    LOOPN(i, CONFIG_BASE_ADC_MULTISAMPLING) {
        usleep(10);
#ifdef TARGET_IDF_5
        adc_oneshot_read(adc.oneshot, adc.chans[idx], &raw);
#else
        raw = adc1_get_raw(adc.chans[idx]);
#endif
        if (raw == -1) return -1;
        cum += raw;
        cnt++;
    }
#ifdef TARGET_IDF_5
    if (cnt) adc_cali_raw_to_voltage(adc.oneshot, cum / cnt, &raw);
    return cnt ? raw : -1;
#else
    return cnt ? esp_adc_cal_raw_to_voltage(cum / cnt, &adc.chars) : -1;
#endif
}

int adc_joystick(int *dx, int *dy) {
#ifdef CONFIG_BASE_ADC_JOYSTICK
    static int px, py;
    int x = adc_read(0);
    int y = adc_read(1);
    if (x == -1 || y == -1) return -1;
    if (dx) *dx = x - px ?: x;
    if (dy) *dy = y - py ?: y;
    px = x; py = y;
    return x << 16 | y;
#else
    return -1; NOTUSED(dx); NOTUSED(dy);
#endif
}

#else // CONFIG_BASE_USE_ADC
int adc_hall() { return 0; }
int adc_read(uint8_t i) { return -1; NOTUSED(i); }
int adc_joystick(int *x, int *y) { return -1; NOTUSED(x); NOTUSED(y); }
#endif // CONFIG_BASE_USE_ADC

/******************************************************************************
 * DAC analog out
 */

#ifdef CONFIG_BASE_USE_DAC
#   include "soc/dac_periph.h"
#   include "driver/dac.h"

static dac_channel_t dac_chan = DAC_CHANNEL_MAX;

static dac_channel_t gpio2dac(gpio_num_t pin) {
    LOOPN(i, SOC_DAC_PERIPH_NUM) {
        if (dac_periph_signal.dac_channel_io_num[i] == pin) return i;
    }
    return DAC_CHANNEL_MAX;
}

static void dac_initialize() {
    if (( dac_chan = gpio2dac(PIN_DAC) ) == DAC_CHANNEL_MAX) {
        ESP_LOGE(TAG, "DAC: invalid pin %d", PIN_DAC);
        return;
    }
    dac_output_enable(dac_chan);
}

esp_err_t dac_write(uint8_t val) {
    if (dac_chan == DAC_CHANNEL_MAX) return ESP_ERR_INVALID_STATE;
    dac_cw_generator_disable();
    return dac_output_voltage(dac_chan, val);
}

esp_err_t dac_cwave(uint32_t val) {
    if (dac_chan == DAC_CHANNEL_MAX) return ESP_ERR_INVALID_STATE;
    dac_cw_config_t conf = {
        .en_ch = dac_chan,
        .scale = MIN((val >> 8) & 0xFF, DAC_CW_SCALE_8),
        .freq = CONS(val >> 16, 130, 55000),
        .offset = (val & 0xFF) - 128, // -128 ~ 127
    };
    dac_cw_generator_enable();
    return dac_cw_generator_config(&conf);
}
#else
esp_err_t dac_write(uint8_t v) { return ESP_ERR_NOT_SUPPORTED; NOTUSED(v); }
esp_err_t dac_cwave(uint32_t v) { return ESP_ERR_NOT_SUPPORTED; NOTUSED(v); }
#endif

/******************************************************************************
 * PWM by hardware LEDC
 */

// LEDC_TIMER_0 and LEDC_CHANNEL_0 is for LED

#define SPEED_MODE  LEDC_LOW_SPEED_MODE

#define BUZZER_TMR  LEDC_TIMER_1
#define BUZZER_RES  LEDC_TIMER_10_BIT
#define BUZZER_CH   LEDC_CHANNEL_1

#define SERVO_TMR   LEDC_TIMER_2
#define SERVO_RES   LEDC_TIMER_10_BIT
#define SERVO_CHH   LEDC_CHANNEL_2
#define SERVO_CHV   LEDC_CHANNEL_3

// LEDC_TIMER_3 and LEDC_CHANNEL_4 is for Camera XCLK

static void pwm_initialize() {
#ifdef CONFIG_BASE_USE_SERVO
    ledc_timer_config_t servo_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = SERVO_TMR,
        .duty_resolution    = SERVO_RES,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&servo_conf) );
    ledc_channel_config_t hor_conf = {
        .gpio_num           = PIN_SVOH,
        .speed_mode         = servo_conf.speed_mode,
        .channel            = SERVO_CHH,
        .timer_sel          = servo_conf.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ledc_channel_config_t ver_conf = {
        .gpio_num           = PIN_SVOV,
        .speed_mode         = servo_conf.speed_mode,
        .channel            = SERVO_CHV,
        .timer_sel          = servo_conf.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&hor_conf) );
    ESP_ERROR_CHECK( ledc_channel_config(&ver_conf) );
#endif
#ifdef CONFIG_BASE_USE_BUZZER
    ledc_timer_config_t buzzer_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = BUZZER_TMR,
        .duty_resolution    = BUZZER_RES,
        .freq_hz            = 5000, // 0-5kHz is commonlly used
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&buzzer_conf) );
    ledc_channel_config_t chan_conf = {
        .gpio_num           = PIN_BUZZ,
        .speed_mode         = buzzer_conf.speed_mode,
        .channel            = BUZZER_CH,
        .timer_sel          = buzzer_conf.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&chan_conf) );
#endif
}

static esp_err_t UNUSED pwm_set_duty(int channel, int duty) {
    esp_err_t err = ledc_set_duty(SPEED_MODE, channel, duty);
    if (!err) err = ledc_update_duty(SPEED_MODE, channel);
    return err;
}

#ifdef CONFIG_BASE_USE_SERVO
// mapping 0-180 deg to 0.5-2.5 ms
static const float servo_offset = 0.5 / 20 * ((1 << SERVO_RES) - 1);
static const float servo_scale  = 2.0 / 20 * ((1 << SERVO_RES) - 1) / 180;

esp_err_t pwm_set_degree(int hdeg, int vdeg) {
    esp_err_t err = ESP_OK;
    if (!err && hdeg >= 0) {
        hdeg = MIN(180, 166 * hdeg / 180 + 14);
        err = pwm_set_duty(SERVO_CHH, hdeg * servo_scale + servo_offset);
    }
    if (!err && vdeg >= 0) {
        vdeg = MIN(160, vdeg);
        err = pwm_set_duty(SERVO_CHV, vdeg * servo_scale + servo_offset);
    }
    return err;
}

esp_err_t pwm_get_degree(int *hdeg, int *vdeg) {
    int hduty = ledc_get_duty(SPEED_MODE, SERVO_CHH),
        vduty = ledc_get_duty(SPEED_MODE, SERVO_CHV);
    *hdeg = (int)((hduty - servo_offset) / servo_scale);
    *vdeg = (int)((vduty - servo_offset) / servo_scale);
    return ESP_OK;
}
#else
esp_err_t pwm_set_degree(int h, int v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h); NOTUSED(v);
}
esp_err_t pwm_get_degree(int *h, int *v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h); NOTUSED(v);
}
#endif

#ifdef CONFIG_BASE_USE_BUZZER
// mapping 0-100 percent to 0%-50% of duty
static const float buzzer_scale = ((1 << BUZZER_RES) - 1) / 200;

esp_err_t pwm_set_tone(int freq, int pcnt) {
    esp_err_t err = freq > 20000 ? ESP_ERR_INVALID_ARG : ESP_OK;
    if (freq == 0) pcnt = 0;
    if (!err && freq > 0)  err = ledc_set_freq(SPEED_MODE, BUZZER_TMR, freq);
    if (!err && pcnt >= 0) err = pwm_set_duty(BUZZER_CH, pcnt * buzzer_scale);
    return err;
}

esp_err_t pwm_get_tone(int *freq, int *pcnt) {
    *freq = ledc_get_freq(SPEED_MODE, BUZZER_TMR);
    *pcnt = (int)(ledc_get_duty(SPEED_MODE, BUZZER_CH) / buzzer_scale);
    return ESP_OK;
}
#else
esp_err_t pwm_set_tone(int f, int p) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(f); NOTUSED(p);
}
esp_err_t pwm_get_tone(int *f, int *p) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(f); NOTUSED(p);
}
#endif

#undef SPEED_MODE
#undef SERVO_TMR
#undef SERVO_RES
#undef SERVO_CHH
#undef SERVO_CHV
#undef BUZZER_TMR
#undef BUZZER_RES
#undef BUZZER_CH

/******************************************************************************
 * SPI Master interface
 */

#ifdef CONFIG_BASE_USE_SPI
static void spi_initialize() {
    spi_bus_config_t buf_conf = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        // FIXME: .max_transfer_sz = 81960,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    esp_err_t err = spi_bus_initialize(NUM_SPI, &buf_conf, SPI_DMA_CH_AUTO);
    if (err && err != ESP_ERR_INVALID_STATE)
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
}
#endif

/******************************************************************************
 * I2C Master interface
 */

#ifdef CONFIG_BASE_USE_I2C
static void i2c_initialize() {
    i2c_config_t i2c_conf = {
        .mode               = I2C_MODE_MASTER,
        .sda_io_num         = PIN_SDA,
        .sda_pullup_en      = GPIO_PULLUP_ENABLE,
        .scl_io_num         = PIN_SCL,
        .scl_pullup_en      = GPIO_PULLUP_ENABLE,
        .master.clk_speed   = CONFIG_BASE_I2C_SPEED,
    };
    ESP_ERROR_CHECK( i2c_param_config(NUM_I2C, &i2c_conf) );
    ESP_ERROR_CHECK( i2c_driver_install(NUM_I2C, i2c_conf.mode, 0, 0, 0) );
}

esp_err_t smbus_probe(int bus, uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, TIMEOUT(20));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t smbus_wregs(
    int bus, uint8_t addr, uint8_t reg, uint8_t *val, size_t len
) {
    // SMBus Write protocol:
    //      S | (ADDR | W) | ACK | REG | ACK | (DATA | ACK) * n | P
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, val, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, TIMEOUT(20 * len));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t smbus_rregs(
    int bus, uint8_t addr, uint8_t reg, uint8_t *val, size_t len
) {
    // SMBus Read protocol:
    //      S | (ADDR | W) | ACK | REG | ACK |
    //      S | (ADDR | R) | ACK | (DATA | A) * n - 1 | (DATA | N) | P
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, val, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, val + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, TIMEOUT(20 * len));
    i2c_cmd_link_delete(cmd);
    return err;
}
#else // CONFIG_BASE_USE_I2C
esp_err_t smbus_probe(int b, uint8_t a) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(b); NOTUSED(a);
}
esp_err_t smbus_wregs(int b, uint8_t a, uint8_t r, uint8_t *v, size_t l) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(b); NOTUSED(a); NOTUSED(r); NOTUSED(v); NOTUSED(l);
}
esp_err_t smbus_rregs(int b, uint8_t a, uint8_t r, uint8_t *v, size_t l) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(b); NOTUSED(a); NOTUSED(r); NOTUSED(v); NOTUSED(l);
}
#endif // CONFIG_BASE_USE_I2C

esp_err_t smbus_write_byte(int bus, uint8_t addr, uint8_t reg, uint8_t val) {
    return smbus_wregs(bus, addr, reg, &val, 1);
}

esp_err_t smbus_read_byte(int bus, uint8_t addr, uint8_t reg, uint8_t *val) {
    return smbus_rregs(bus, addr, reg, val, 1);
}

esp_err_t smbus_write_word(int bus, uint8_t addr, uint8_t reg, uint16_t val) {
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    return smbus_wregs(bus, addr, reg, buf, 2);
}

esp_err_t smbus_read_word(int bus, uint8_t addr, uint8_t reg, uint16_t *val) {
    uint8_t buf[2] = { 0, 0 };
    esp_err_t err = smbus_rregs(bus, addr, reg, buf, 2);
    if (!err) *val = buf[0] << 8 | buf[1];
    return err;
}

esp_err_t smbus_dump(int bus, uint8_t addr, uint8_t reg, uint8_t num) {
#define LENGTH 16
    uint8_t *buf = NULL;
    esp_err_t err = EMALLOC(buf, num);
    if (!err) err = smbus_rregs(bus, addr, reg, buf, num);
    if (!err && num) {
        printf("I2C %d-%02X register table 0x%02X - 0x%02X\n  ",
               bus, addr, reg, reg + num);
        LOOPN(i, LENGTH) { printf(" %02X", i); }
        if (reg % LENGTH) // pad start spaces
            printf("\n%02X%*s", reg - (reg % LENGTH), 3 * (reg % LENGTH), "");
        LOOPN(i, num) {
            if ((i + reg) % LENGTH == 0) printf("\n%02X", i + reg); // newline
            printf(" %02X", buf[i]);
        }
        putchar('\n');
    }
    TRYFREE(buf);
    return err;
#undef LENGTH
}

esp_err_t smbus_toggle(int bus, uint8_t addr, uint8_t reg, uint8_t bit) {
    uint8_t val = 0, mask = BIT(bit);
    esp_err_t err = smbus_read_byte(bus, addr, reg, &val);
    if (val & mask) {
        val &= ~mask;
    } else {
        val |= mask;
    }
    return err ?: smbus_write_byte(bus, addr, reg, val);
}

static bool i2c_master_inited(int bus) {
    esp_err_t err = smbus_probe(bus, 0);
    return err != ESP_ERR_INVALID_ARG && err != ESP_ERR_INVALID_STATE;
}

void i2c_detect(int bus) {
    if (!i2c_master_inited(bus)) return;
    LOOPN(i, 0x10) {
        if (!i) printf("  ");
        printf(" %02X", i);
    }
    LOOPN(addr, 0x7F) {
        if (addr % 0x10 == 0)
            printf("\n%02X", addr);
        if (!addr) {
            printf("   ");
            continue;
        }
        switch (smbus_probe(bus, addr)) {
        case ESP_OK:            printf(" %02X", addr); break;
        case ESP_ERR_TIMEOUT:   printf(" UU"); break;
        default:                printf(" --");
        }
        fflush(stdout);
    }
}

/******************************************************************************
 * GPIO Expander
 */

#ifdef CONFIG_BASE_GPIOEXP_I2C
static const uint8_t i2c_pin_addr[3] = { 0b0100000, 0b0100001, 0b0100010 };
static uint8_t i2c_pin_data[3] = { 0, 0, 0 }, i2c_pin_probed[3];

static esp_err_t i2c_gexp_set_level(gexp_num_t pin, bool level) {
    if (!PIN_IS_I2CEXP(pin)) return ESP_ERR_INVALID_ARG;
    uint8_t num = pin - PIN_I2C_BASE, idx = num >> 3, mask = BIT(num & 0x7);
    if (!i2c_pin_probed[idx]) return ESP_ERR_NOT_FOUND;
    uint8_t addr = i2c_pin_addr[idx], *datp = i2c_pin_data + idx;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return i2c_master_write_to_device(NUM_I2C, addr, datp, 1, 20);
}

static esp_err_t i2c_gexp_get_level(gexp_num_t pin, bool *level, bool sync) {
    if (!PIN_IS_I2CEXP(pin)) return ESP_ERR_INVALID_ARG;
    uint8_t num = pin - PIN_I2C_BASE, idx = num >> 3, mask = BIT(num & 0x7);
    if (!i2c_pin_probed[idx]) return ESP_ERR_NOT_FOUND;
    uint8_t addr = i2c_pin_addr[idx], *datp = i2c_pin_data + idx;
    esp_err_t err = ESP_OK;
    if (sync) err = i2c_master_read_from_device(NUM_I2C, addr, datp, 1, 20);
    if (!err) *level = *datp & mask;
    return err;
}
#endif

#ifdef CONFIG_BASE_GPIOEXP_SPI
static spi_device_handle_t spi_pin_hdl = NULL;
static spi_transaction_t spi_pin_trans;
static uint8_t *spi_pin_data;

static esp_err_t spi_gexp_set_level(gexp_num_t pin, bool level) {
    if (!spi_pin_hdl) return ESP_ERR_INVALID_STATE;
    if (!PIN_IS_SPIEXP(pin)) return ESP_ERR_INVALID_ARG;
    int num = pin - PIN_SPI_BASE;
    uint8_t idx = num >> 3, mask = BIT(num & 0x7), *datp = spi_pin_data + idx;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return spi_device_polling_transmit(spi_pin_hdl, &spi_pin_trans);
}

static esp_err_t spi_gexp_get_level(gexp_num_t pin, bool *level, bool sync) {
    if (!spi_pin_hdl) return ESP_ERR_INVALID_STATE;
    if (!PIN_IS_SPIEXP(pin)) return ESP_ERR_INVALID_ARG;
    int num = pin - PIN_SPI_BASE;
    uint8_t idx = num >> 3, mask = BIT(num & 0x7), *datp = spi_pin_data + idx;
    esp_err_t err = ESP_OK;
    if (sync) err = spi_device_polling_transmit(spi_pin_hdl, &spi_pin_trans);
    if (!err) *level = *datp & mask;
    return err;
}
#endif

#ifdef CONFIG_BASE_USE_GPIOEXP
static void IRAM_ATTR UNUSED gexp_isr(void *arg) {
#   ifdef CONFIG_BASE_GPIOEXP_INT
    ets_printf("PIN_INT %s\n", gpio_get_level(PIN_INT) ? "RISE" : "FALL");
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_I2C
    LOOPN(i, LEN(i2c_pin_data)) {
        if (i2c_gexp_get_level(PIN_I2C_BASE + i * 8, NULL, true)) continue;
        ets_printf("I2C GPIOExp: %s\n", format_binary(i2c_pin_data[i], 1));
    }
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_SPI
    if (spi_gexp_get_level(PIN_SPI_BASE, NULL, true)) return;
    LOOPN(i, PIN_SPI_COUNT / 8) {
        ets_printf("SPI GPIOExp: %s\n", format_binary(spi_pin_data[i], 1));
    }
#   endif
}

static void gexp_initialize() {
    esp_err_t err;
#   ifdef CONFIG_BASE_GPIOEXP_I2C
    if (PIN_I2C_COUNT > (LEN(i2c_pin_data) * 8) || PIN_I2C_COUNT <= 0) {
        ESP_LOGE(TAG, "Invalid I2C GPIOExp pin count: %d", PIN_I2C_COUNT);
        return;
    }
    uint8_t found = 0;
    LOOPN(i, LEN(i2c_pin_data)) {
        err = smbus_probe(NUM_I2C, i2c_pin_addr[i]);
        if (err == ESP_ERR_INVALID_STATE) break; // driver not installed
        found += (i2c_pin_probed[i] = err == ESP_OK);
    }
    if (!found) {
        err = ESP_ERR_NOT_FOUND;
        ESP_LOGE(TAG, "I2C GPIOExp init error: %s", esp_err_to_name(err));
    }
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_SPI
    // If the transmitted data is 32bits or less, it is preferred to use
    // tx_data in spi_transaction_t. Each expander chip uses 8bits.
    if (PIN_SPI_COUNT % 8 || PIN_SPI_COUNT <= 0) {
        ESP_LOGE(TAG, "Invalid SPI GPIOExp pin count: %d", PIN_SPI_COUNT);
        return;
    }
    if (PIN_SPI_COUNT <= 32) {
        spi_pin_data = spi_pin_trans.tx_data;
        spi_pin_trans.flags = SPI_TRANS_USE_TXDATA;
        spi_pin_trans.length = PIN_SPI_COUNT;
    } else if (ECALLOC(spi_pin_data, 1, PIN_SPI_COUNT / 8)) {
        ESP_LOGE(TAG, "Could not allocate memory for SPI GPIOExp");
        return;
    } else {
        spi_pin_trans.length = PIN_SPI_COUNT;
        spi_pin_trans.tx_buffer = spi_pin_data;
    }
    spi_device_interface_config_t dev_conf = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0b10,                       // CPOL = 1, CPHA = 0
        .duty_cycle_pos = 128,              // 128/256 = 50% (Tlow = Thigh)
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = SPI_MASTER_FREQ_8M,
        .input_delay_ns = 0,
        .spics_io_num = PIN_CS2,
        .flags = 0,
        .queue_size = 1,                    // only one transaction allowed
        .pre_cb = NULL,
        .post_cb = NULL
    };
    err = spi_bus_add_device(NUM_SPI, &dev_conf, &spi_pin_hdl);
    if (err) ESP_LOGE(TAG, "SPI GPIOExp init error: %s", esp_err_to_name(err));
#   endif
#   ifdef CONFIG_BASE_GPIOEXP_INT
    gpio_config_t int_conf = {
        .pin_bit_mask = BIT64(PIN_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    if (!strcasecmp(Config.sys.INT_EDGE, "HIGH")) {
        int_conf.intr_type = GPIO_INTR_HIGH_LEVEL;
        int_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "LOW")) {
        int_conf.intr_type = GPIO_INTR_LOW_LEVEL;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "POS")) {
        int_conf.intr_type = GPIO_INTR_POSEDGE;
        int_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "NEG")) {
        int_conf.intr_type = GPIO_INTR_NEGEDGE;
    }
    ESP_ERROR_CHECK( gpio_config(&int_conf) );
    ESP_ERROR_CHECK( gpio_install_isr_service(0) );
    ESP_ERROR_CHECK( gpio_isr_handler_add(PIN_INT, gexp_isr, NULL) );
#   endif
}
#endif

/******************************************************************************
 * GPIO Interrupt (inc. button & knob)
 */

static const char * gpio_default_usage[GPIO_PIN_COUNT] = {
#if defined(CONFIG_IDF_TARGET_ESP32)
    [0]         = "Strapping PU",
    [2]         = "Strapping PD",
    [5]         = "Strapping PU",
    [6]         = "Flash SPICLK",
    [7]         = "Flash SPIQ (PICO-D4)",
    [8]         = "Flash SPID (PICO-D4)",
    [9]         = "Flash SPIHD (PICO-V3-02)",
    [10]        = "Flash SPIWP (PICO-V3-02)",
    [11]        = "Flash SPICS0",
    [16]        = "Flash D2WD",
    [17]        = "Flash D2WD",
    [20]        = "ESP32-PICO-V3",
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    [0]         = "Strapping PU",
    [3]         = "Strapping Float",
    [19]        = "USB DN",
    [20]        = "USB DP",
    [26]        = "Flash SPICS1",
    [27]        = "Flash SPIHD",
    [28]        = "Flash SPIWP",
    [29]        = "Flash SPICS0",
    [30]        = "Flash SPICLK",
    [31]        = "Flash SPIQ",
    [32]        = "Flash SPID",
    [45]        = "Strapping PD",
    [46]        = "Strapping PD",
#endif
#ifdef CONFIG_BASE_USE_LED
    [PIN_LED]   = "LED",
#endif
#ifdef CONFIG_BASE_USE_UART
    [PIN_TXD]   = "UART TXD" STR(CONFIG_BASE_UART_NUM),
    [PIN_RXD]   = "UART RXD" STR(CONFIG_BASE_UART_NUM),
#endif
#ifdef CONFIG_BASE_USE_I2S
    [PIN_CLK]   = "I2S CLK",
    [PIN_DAT]   = "I2S DAT",
#endif
#ifdef CONFIG_BASE_USE_I2C0
    [PIN_SDA0]  = "I2C SDA0",
    [PIN_SCL0]  = "I2C SCL0",
#endif
#ifdef CONFIG_BASE_USE_I2C1
    [PIN_SDA1]  = "I2C SDA1",
    [PIN_SCL1]  = "I2C SCL1",
#endif
#ifdef CONFIG_BASE_USE_SPI
    [PIN_MISO]  = "SPI MISO",
    [PIN_MOSI]  = "SPI MOSI",
    [PIN_SCLK]  = "SPI SCLK",
#endif
#ifdef CONFIG_BASE_USE_SDFS
    [PIN_CS0]   = "SPI CS0 (SDCard)",
#endif
#ifdef CONFIG_BASE_SCREEN_SPI
    [PIN_CS1]   = "SPI CS1 (Screen)",
    [PIN_SDC]   = "SPI Screen D/C",
#   if PIN_SRST != GPIO_NUM_NC
    [PIN_SRST]  = "SPI Screen RESET",
#   endif
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
    [PIN_CS2]   = "SPI CS2 (GPIOExp)",
#endif
#ifdef CONFIG_BASE_GPIOEXP_INT
    [PIN_INT]   = "GEXP INT",
#endif
#if defined(CONFIG_BASE_ADC_HALL_SENSOR)
    [PIN_ADC1]  = "HALL Sensor P",
    [PIN_ADC2]  = "HALL Sensor N",
#elif defined(CONFIG_BASE_ADC_JOYSTICK)
    [PIN_ADC1]  = "Joystick X",
    [PIN_ADC2]  = "Joystick Y",
#else
#   ifdef PIN_ADC1
    [PIN_ADC1]  = "ADC1",
#   endif
#   ifdef PIN_ADC2
    [PIN_ADC2]  = "ADC2",
#   endif
#endif
#ifdef CONFIG_BASE_USE_DAC
    [PIN_DAC]   = "DAC",
#endif
#ifdef CONFIG_BASE_USE_TPAD
    [PIN_TPAD]  = "Touch",
#endif
#ifdef CONFIG_BASE_BTN_INPUT
    [PIN_BTN]   = "Button",
#endif
#ifdef CONFIG_BASE_USE_KNOB
    [PIN_ENCA]  = "Knob Encoder A",
    [PIN_ENCB]  = "Knob Encoder B",
#endif
#ifdef CONFIG_BASE_USE_SERVO
    [PIN_SVOH]  = "Servo Yaw",
    [PIN_SVOV]  = "Servo Pitch",
#endif
#ifdef CONFIG_BASE_USE_BUZZER
    [PIN_BUZZ]  = "Buzzer",
#endif
};

static bool gpio_usable(gpio_num_t pin) {
    if (pin > GPIO_PIN_COUNT) return false;
    if (!gpio_default_usage[pin]) return true;
    return startswith(gpio_default_usage[pin], "Strapping");
}

#ifdef CONFIG_BASE_USE_BTN
static button_handle_t btn[2];

static void cb_button(void *arg, void *data) {
    static const char *BTAG = "button";
    int pin = (int)data;
    switch (iot_button_get_event(arg)) {
    case BUTTON_PRESS_DOWN:
        ESP_LOGI(BTAG, "%d press", pin);
        if (pin == PIN_BTN) hid_report_dial(HID_TARGET_ALL, DIAL_DN);
        break;
    case BUTTON_PRESS_UP:
        ESP_LOGI(BTAG, "%d release[%d]", pin, iot_button_get_ticks_time(arg));
        if (pin == PIN_BTN) hid_report_dial(HID_TARGET_ALL, DIAL_UP);
        break;
    case BUTTON_SINGLE_CLICK:
        ESP_LOGI(BTAG, "%d single click", pin);
        break;
    case BUTTON_DOUBLE_CLICK:
        ESP_LOGI(BTAG, "%d double click", pin);
        break;
    case BUTTON_MULTIPLE_CLICK:
        ESP_LOGI(BTAG, "%d click %d times", pin, iot_button_get_repeat(arg));
        break;
    case BUTTON_LONG_PRESS_HOLD:
        ESP_LOGI(BTAG, "%d long press %d",
                 pin, iot_button_get_long_press_hold_cnt(arg));
        break;
    default: break;
    }
}

#   ifdef CONFIG_BASE_ADC_JOYSTICK
static button_handle_t jstk[4];

static void cb_joystick(void *arg, void *data) {
    button_event_t event = iot_button_get_event(arg);
    if (event != BUTTON_PRESS_DOWN && event != BUTTON_LONG_PRESS_HOLD) return;
    int x = adc_read(0), y = adc_read(1);
    if (x == -1 || y == -1) return;
    x = x > 1900 ? (x - 1900) : x < 1400 ? (x - 1400) : 0;
    y = y > 1900 ? (y - 1900) : y < 1400 ? (y - 1400) : 0;
    if (x && y) hid_report_mouse_move(HID_TARGET_ALL, x / 28, y / 28); // ±50
}
#   endif

static button_handle_t button_init(
    button_config_t *conf, gpio_num_t pin, button_cb_t cb
) {
    button_handle_t hdl = iot_button_create(conf);
    if (!hdl) {
        ESP_LOGE(TAG, "bind to GPIO%d failed", pin);
    } else LOOPN(event, BUTTON_EVENT_MAX) {
        if (event == BUTTON_MULTIPLE_CLICK) {
            button_event_config_t evt = {
                .event = event,
                .event_data.multiple_clicks.clicks = 3,
            };
            iot_button_register_event_cb(hdl, evt, cb, (void *)pin);
        } else {
            iot_button_register_cb(hdl, event, cb, (void *)pin);
        }
    }
    return hdl;
}
#endif // CONFIG_BASE_USE_BTN

#ifdef CONFIG_BASE_USE_KNOB
static knob_handle_t knob;
static const char *KTAG = "Knob";

static void cb_knob(void *arg, void *data) {
    switch (iot_knob_get_event(arg)) {
    case KNOB_LEFT:
        ESP_LOGD(KTAG, "left rotate %d", iot_knob_get_count_value(arg));
        hid_report_dial(HID_TARGET_ALL, DIAL_L);
        break;
    case KNOB_RIGHT:
        ESP_LOGD(KTAG, "right rotate %d", iot_knob_get_count_value(arg));
        hid_report_dial(HID_TARGET_ALL, DIAL_R);
        break;
    default: break;
    }
}
#endif

static void gpio_initialize() {
#ifdef CONFIG_BASE_USE_KNOB
    knob_event_t events[] = { KNOB_LEFT, KNOB_RIGHT };
    knob_config_t knob_conf = {
        .default_direction = 0,     // 0:positive; 1:negative
        .gpio_encoder_a = PIN_ENCA,
        .gpio_encoder_b = PIN_ENCB
    };
    if (!( knob = iot_knob_create(&knob_conf) )) {
        ESP_LOGE(KTAG, "bind to GPIO%d & %d failed", PIN_ENCA, PIN_ENCB);
    } else ITERV(event, events) {
        iot_knob_register_cb(knob, event, cb_knob, NULL);
    }
#endif
#ifdef CONFIG_BASE_USE_BTN
#   ifdef CONFIG_BASE_BTN_INPUT
    button_config_t btn_conf = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = PIN_BTN,
            .active_level = strbool(Config.sys.BTN_HIGH),
        }
    };
    btn[0] = button_init(&btn_conf, PIN_BTN, cb_button);
#   endif
#   ifdef CONFIG_BASE_BTN_GPIO0
    if (gpio_usable(GPIO_NUM_0)) {
        button_config_t io0_conf = {
            .type = BUTTON_TYPE_GPIO,
            .gpio_button_config = { .gpio_num = GPIO_NUM_0 }
        };
        btn[1] = button_init(&io0_conf, GPIO_NUM_0, cb_button);
    }
#   endif
#   ifdef CONFIG_BASE_ADC_JOYSTICK
    btn_conf.type = BUTTON_TYPE_ADC;
    btn_conf.long_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS + 20;
#   ifdef TARGET_IDF_5
    btn_conf.adc_button_config.adc_handle = adc.oneshot;
#   endif
    LOOPN(i, LEN(adc.chans)) {
        if (adc.chans[i] == ADC_CHANNEL_MAX) continue;
        btn_conf.adc_button_config.adc_channel = adc.chans[i];
        btn_conf.adc_button_config.button_index = 0;
        btn_conf.adc_button_config.min = 0;
        btn_conf.adc_button_config.max = 1400; // 0.0-1.4V
        jstk[2 * i + 0] = button_init(&btn_conf, adc.pins[i], cb_joystick);
        btn_conf.adc_button_config.button_index = 1;
        btn_conf.adc_button_config.min = 1900;
        btn_conf.adc_button_config.max = 3300; // 1.9-3.3V
        jstk[2 * i + 1] = button_init(&btn_conf, adc.pins[i], cb_joystick);
    }
#   endif
#endif
}

esp_err_t gexp_set_level(int pin, bool level) {
    if (GPIO_IS_VALID_GPIO(pin)) return gpio_set_level(pin, level);
#ifdef CONFIG_BASE_GPIOEXP_I2C
    if (PIN_IS_I2CEXP(pin))      return i2c_gexp_set_level(pin, level);
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
    if (PIN_IS_SPIEXP(pin))      return spi_gexp_set_level(pin, level);
#endif
    return ESP_ERR_INVALID_ARG;
}

esp_err_t gexp_get_level(int pin, bool *level, bool sync) {
    if (GPIO_IS_VALID_GPIO(pin)) {
        *level = gpio_get_level(pin);
        return ESP_OK;
    }
#ifdef CONFIG_BASE_GPIOEXP_I2C
    if (PIN_IS_I2CEXP(pin)) return i2c_gexp_get_level(pin, level, sync);
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
    if (PIN_IS_SPIEXP(pin)) return spi_gexp_get_level(pin, level, sync);
#endif
    return ESP_ERR_INVALID_ARG;
}

void gpio_table(bool i2c, bool spi) {
    const char *value;
    printf("Native GPIO %d-%d\nPIN Value Usage\n", 0, GPIO_PIN_COUNT - 1);
    LOOPN(pin, GPIO_PIN_COUNT) {
        if (!GPIO_IS_VALID_GPIO(pin)) continue;
        value = gpio_get_level(pin) ? "HIGH" : "LOW";
        printf("%-3d %5s %s\n", pin, value, gpio_default_usage[pin] ?: "");
    }
#ifdef CONFIG_BASE_GPIOEXP_I2C
    if (i2c) {
        printf("\nI2C GPIOExp %d-%d\nPIN Value\n",
               PIN_I2C_BASE, PIN_I2C_MAX - 1);
        bool level;
        esp_err_t err;
        LOOP(pin, PIN_I2C_BASE, PIN_I2C_MAX) {
            if (!( err = i2c_gexp_get_level(pin, &level, false) )) {
                value = level ? "HIGH" : "LOW";
            } else if (err == ESP_ERR_NOT_FOUND) {
                value = "";
            } else {
                value = esp_err_to_name(err);
            }
            printf("%-3d %5s\n", pin, value);
        }
    }
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
    if (spi) {
        printf("\nSPI GPIOExp %d-%d\nPIN Value\n",
               PIN_SPI_BASE, PIN_SPI_MAX - 1);
        bool level;
        esp_err_t err;
        LOOP(pin, PIN_SPI_BASE, PIN_SPI_MAX) {
            if (!( err = spi_gexp_get_level(pin, &level, false) )) {
                value = level ? "HIGH" : "LOW";
            } else if (err == ESP_ERR_INVALID_STATE) {
                value = "";
            } else {
                value = esp_err_to_name(err);
            }
            printf("%-3d %5s\n", pin, value);
        }
    }
#endif
}

/******************************************************************************
 * Audio/Video sensors (I2S PDM MIC and SCCB/SMBus Camera)
 */

ESP_EVENT_DEFINE_BASE(AVC_EVENT);

#define AVC_POST(id, data, len, tout) \
    esp_event_post(AVC_EVENT, id, data, len, TIMEOUT(tout))

static UNUSED bool audio_run, video_run;
static UNUSED esp_event_handler_instance_t aud_shdl, vid_shdl;

#ifdef CONFIG_BASE_USE_I2S

#ifdef TARGET_IDF_5
static i2s_chan_handle_t i2s_handle;
#   define I2S_ACQUIRE()    i2c_channel_enable(i2s_handle)
#   define I2S_RELEASE()    i2c_channel_disable(i2s_handle)
#   define I2S_READ(...)    i2c_channel_read(i2s_handle, __VA_ARGS__)
#   define PDM_SHZ          CONFIG_BASE_PDM_SAMPLE_RATE
#   define PDM_BPC          ( I2S_DATA_BIT_WIDTH_16BIT / 8 )
#   define PDM_TYPE         int16_t
#   ifdef CONFIG_BASE_PDM_STEREO
#       define PDM_NCH      I2S_SLOT_MODE_STEREO
#   else
#       define PDM_NCH      I2S_SLOT_MODE_MONO
#   endif

static void i2s_initialize() {
#   define DEFAULT(n, ...)  I2S_##n##_DEFAULT_CONFIG(__VA_ARGS__)
    i2s_chan_config_t chan_conf = DEFAULT(CHANNEL, NUM_I2S, I2S_ROLE_MASTER);
    i2s_pdm_rx_config_t pdm_conf = {
        .clk_cfg  = DEFAULT(PDM_RX_CLK, PDM_SHZ),
        .slot_cfg = DEFAULT(PDM_RX_SLOT, PDM_BPC * 8, PDM_NCH),
        .gpio_cfg = { .clk = PIN_CLK, .din = PIN_DAT },
    };
#   undef DEFAULT
    ESP_ERROR_CHECK( i2s_new_channel(&chan_conf, NULL, &i2s_handle) );
    ESP_ERROR_CHECK( i2s_channel_init_pdm_rx_mode(i2s_handle, &pdm_conf) );
}
#else // TARGET_IDF_4
#   define I2S_ACQUIRE()    i2s_start(NUM_I2S)
#   define I2S_RELEASE()    i2s_stop(NUM_I2S)
#   define I2S_READ(...)    i2s_read(NUM_I2S, __VA_ARGS__)
#   define PDM_SHZ          CONFIG_BASE_PDM_SAMPLE_RATE
#   define PDM_BPC          ( I2S_BITS_PER_SAMPLE_16BIT / 8 )
#   define PDM_TYPE         int16_t
#   ifdef CONFIG_BASE_PDM_STEREO
#       define PDM_NCH      I2S_CHANNEL_STEREO
#       define PDM_FCH      I2S_CHANNEL_FMT_RIGHT_LEFT
#   else
#       define PDM_NCH      I2S_CHANNEL_MONO
#       define PDM_FCH      I2S_CHANNEL_FMT_ONLY_RIGHT
#   endif

static void i2s_initialize() {
    i2s_config_t i2s_conf = {
        .mode                   = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM,
        .sample_rate            = PDM_SHZ,
        .bits_per_sample        = PDM_BPC * 8,
        .channel_format         = PDM_FCH,
        .communication_format   = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count          = 8,
        .dma_buf_len            = 128,
    };
    i2s_pin_config_t pin_conf = {
        .mck_io_num     = I2S_PIN_NO_CHANGE,
        .bck_io_num     = I2S_PIN_NO_CHANGE,
        .ws_io_num      = PIN_CLK,
        .data_out_num   = I2S_PIN_NO_CHANGE,
        .data_in_num    = PIN_DAT,
    };
    ESP_ERROR_CHECK( i2s_driver_install(NUM_I2S, &i2s_conf, 0, NULL) );
    ESP_ERROR_CHECK( i2s_set_pin(NUM_I2S, &pin_conf) );
    ESP_ERROR_CHECK( i2s_stop(NUM_I2S) );
}
#endif // TARGET_IDF_5

static void aud_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    static char eqls[80 - 4 - 3 - 13];
    FILE *stream = arg;
    audio_evt_t *evt = *(audio_evt_t **)data;
    if (id == AUD_EVENT_START) {
        memset(eqls, '=', sizeof(eqls) - 1);
        eqls[sizeof(eqls) - 1] = '\0';
    } else if (id == AUD_EVENT_STOP) {
        fputc('\n', stream);
        fflush(stream);
    }
    if (id != AUD_EVENT_DATA || evt->id % 10 || !evt->mode->nch) return;
    size_t nch = evt->mode->nch, vmax = BIT(evt->mode->depth * 8 - 1);
    size_t tlen = (sizeof(eqls) - (nch - 1) * 6) / nch;
    PDM_TYPE *buf = evt->data, vol[nch], len[nch];
    LOOPN(i, evt->len / nch / evt->mode->depth) {
        LOOPN(j, nch) {
            vol[j] = MAX(vol[j], ABS(buf[i * nch + j]));
        }
    }
    fprintf(stream, "\r%s [", format_timestamp_us(0));
    LOOPN(j, nch) {
        vol[j] = vol[j] * 100 / vmax;           // 0 ~ 100
        len[j] = vol[j] * tlen / 100;           // 0 ~ tlen
        if (j) fputc('|', stream);
        if (j % 2 || nch == 1) {
            fprintf(stream, "%-3d%%%.*s%c%*s",
                vol[j], len[j], eqls, vol[j] ? '+' : ' ', tlen - len[j], "");
        } else {
            fprintf(stream, "%*s%c%.*s%3d%%",
                tlen - len[j], "", vol[j] ? '+' : ' ', len[j], eqls, vol[j]);
        }
    }
    fputc(']', stream);
    fflush(stream);
}

static void audio_capture(void *arg) {
    audio_mode_t mode = { PDM_SHZ, PDM_NCH, PDM_BPC };
    wav_header_t WAV = {
        "RIFF", -1,
        "WAVE",
        "fmt ", WAV_HEADER_FMT_LEN,
            0x01, mode.nch, mode.srate,
            mode.nch * mode.depth * mode.srate,
            mode.nch * mode.depth, mode.depth * 8,
        "data", -1
    };
    size_t dlen = (uint64_t)WAV.Bps * (uint32_t)arg / 1000;
    size_t rlen, plen = sizeof(void **), blen = WAV.Bps / 50; // 20ms buffer
    WAV.filelen = (WAV.datalen = dlen) + sizeof(WAV) - 8;
    audio_evt_t wav = { .len = sizeof(WAV), .data = &WAV };
    audio_evt_t evt = { .data = malloc(blen), .mode = &mode };
    void *dptr = &wav;
    if (!evt.data) goto exit;
    AVC_POST(AUD_EVENT_START, &dptr, plen, -1); msleep(10);
    dptr = &evt;
    I2S_ACQUIRE();
    for (evt.id = 0; audio_run && dlen; evt.id++) {
        if (I2S_READ(evt.data, blen, &rlen, TIMEOUT(25)) || !rlen) break;
        dlen -= (evt.len = MIN(rlen, dlen));
        AVC_POST(AUD_EVENT_DATA, &dptr, plen, 15);
    }
    I2S_RELEASE();
    memset(dptr = &wav, 0, sizeof(wav));
    AVC_POST(AUD_EVENT_STOP, &dptr, plen, -1); msleep(10);
    UREGEVTS(AVC, aud_shdl);
    TRYFREE(evt.data);
exit:
    vTaskDelete(NULL);
}
#   undef I2S_ACQUIRE
#   undef I2S_RELEASE
#   undef I2S_READ
#   undef PDM_TYPE
#   undef PDM_SHZ
#   undef PDM_NCH
#   undef PDM_BPC
#endif // CONFIG_BASE_USE_I2S

#ifdef CONFIG_BASE_USE_CAM
static void cam_initialize() {
    esp_err_t err = ESP_OK;
    static const char *pin_names[14] = {
        "CAM PWDN", "CAM RESET", "CAM XCLK",
        "CAM VSYNC", "CAM HREF", "CAM PCLK",
        "CAM D7", "CAM D6", "CAM D5", "CAM D4",
        "CAM D3", "CAM D2", "CAM D1", "CAM D0",
    };
#   ifdef CONFIG_BASE_CAM_CUSTOM
#       define P(name) CONFIG_BASE_GPIO_CAM_##NAME
    int pins[LEN(pin_names)] = {
        P(PWDN), P(RESET), P(XCLK), P(VSYNC), P(HREF), P(PCLK),
        P(D7), P(D6), P(D5), P(D4), P(D3), P(D2), P(D1), P(D0)
    };
#       undef P
#   else
    int pins[LEN(pin_names)];
    if (parse_all(CONFIG_BASE_CAM_PINS, pins, LEN(pins)) != LEN(pins)) {
        ESP_LOGE(TAG, "Could not parse CAM pins: %s", CONFIG_BASE_CAM_PINS);
        err = ESP_ERR_INVALID_ARG;
    }
#   endif
    LOOPN(i, LEN(pins)) {
        if (pins[i] == -1) {
            if (i >= 2) err = ESP_ERR_INVALID_ARG;
        } else if (gpio_usable(pins[i])) {
            gpio_default_usage[pins[i]] = pin_names[i];
        } else {
            ESP_LOGE(TAG, "Invalid %s pin: %d", pin_names[i], pins[i]);
            err = ESP_ERR_INVALID_ARG;
        }
    }
    if (err) return;
    camera_config_t conf = {
        .pin_pwdn = pins[0], .pin_reset = pins[1], .pin_xclk = pins[2],
        .pin_sccb_sda = -1, .pin_sccb_scl = -1, .sccb_i2c_port = NUM_I2C,
        .pin_vsync = pins[3], .pin_href = pins[4], .pin_pclk = pins[5],
        .pin_d7 = pins[6],  .pin_d6 = pins[7],
        .pin_d5 = pins[8],  .pin_d4 = pins[9],
        .pin_d3 = pins[10], .pin_d2 = pins[11],
        .pin_d1 = pins[12], .pin_d0 = pins[13],
        .xclk_freq_hz = 20000000, // 20MHz
        .ledc_timer = LEDC_TIMER_3,
        .ledc_channel = LEDC_CHANNEL_4,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QSXGA, // init as large buffer as possible
        .jpeg_quality = 10,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };
    if (( err = esp_camera_init(&conf) )) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
    } else {
        sensor_t *cam = esp_camera_sensor_get();
        if (cam->id.PID == OV3660_PID) {
            cam->set_brightness(cam, 1);
            cam->set_saturation(cam, -2);
        }
        cam->set_framesize(cam, FRAMESIZE_HD);
    }
}

static void vid_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    FILE *stream = arg;
    video_evt_t *evt = *(video_evt_t **)data;
    if (id == VID_EVENT_STOP) {
        fputc('\n', stream);
    } else if (id == VID_EVENT_DATA && (evt->id % evt->mode->fps) == 0) {
        fprintf(stream, "\r%08d %dx%dx%d %.4s %dBytes",
                evt->id, evt->mode->width, evt->mode->height,
                evt->mode->depth, evt->mode->fourcc, evt->len);
    }
    fflush(stream);
}

static void video_capture(void *arg) {
    sensor_t *cam = esp_camera_sensor_get();
    if (!cam) return;
    const resolution_info_t *res = resolution + cam->status.framesize;
    video_mode_t mode = { 15, res->width, res->height, 3, "MJPG" }; // FIXME fps
    uint32_t ms = (uint32_t)arg, retry = 5;
    uint32_t BPF = mode.width * mode.height * mode.depth * 0.6; // BytePerFrame
    avi_header_t AVI = {
        "RIFF", -1,
        "AVI ",
        "LIST", AVI_HEADER_HDLR_LEN, "hdlr",
            "avih", AVI_HEADER_AVIH_LEN,
                1000000 / mode.fps, mode.fps * BPF, 0, 0x910, -1, 0, 1,
                0x100000, mode.width, mode.height, { 0, 0, 0, 0 },
        "LIST", AVI_HEADER_STRL_LEN, "strl",
            "strh", AVI_HEADER_STRH_LEN,
                "vids", "MJPG", 0, 0, 0, 0, 1, mode.fps, 0, -1,
                BPF, -1, 0, 0, 0, mode.width, mode.height,
            "strf", AVI_HEADER_STRF_LEN,
                AVI_HEADER_STRF_LEN, mode.width, mode.height, 1,
                mode.depth * 8, "MJPG", BPF, 0, 0, 0, 0,
        "LIST", -1, "movi",
    };
    size_t nframe = ms * (ms == -1 ? 1 : mode.fps), plen = sizeof(void **);
    AVI.total_frames = AVI.length = nframe;
    camera_fb_t *fb = NULL;
    video_evt_t avi = { .len = sizeof(AVI), .data = &AVI };
    video_evt_t evt = { .data = NULL, .mode = &mode };
    void *dptr = &avi;
    AVC_POST(VID_EVENT_START, &dptr, plen, -1); msleep(10);
    dptr = &evt;
    vTaskResume(xTaskGetHandle("cam_task"));
    for (evt.id = 0; video_run && retry && evt.id < nframe; evt.id++) {
        if (evt.data && fb && fb->buf != evt.data) free(evt.data);
        TRYNULL(fb, esp_camera_fb_return);
        if (!( fb = esp_camera_fb_get() )) break;
        if (fb->format == PIXFORMAT_JPEG) {
            evt.data = fb->buf;
            evt.len = fb->len;
        } else if (!frame2jpg(fb, 80, (uint8_t **)&evt.data, &evt.len)) {
            ESP_LOGE(TAG, "JPEG compression failed");
            retry--;
        }
        AVC_POST(VID_EVENT_DATA, &dptr, plen, 33);
    }
    vTaskSuspend(xTaskGetHandle("cam_task"));
    memset(dptr = &avi, 0, sizeof(avi));
    AVC_POST(VID_EVENT_STOP, &dptr, plen, -1); msleep(10);
    UREGEVTS(AVC, vid_shdl);
    if (evt.data && fb && fb->buf != evt.data) free(evt.data);
    TRYNULL(fb, esp_camera_fb_return);
    vTaskDelete(NULL);
}
#endif // CONFIG_BASE_USE_CAM

#undef AVC_POST

esp_err_t avc_command(
    const char *ctrl, int targets, uint32_t tout_ms, FILE *stream
) {
    if (!targets) targets = AUDIO_TARGET | VIDEO_TARGET;
    bool atgt = targets & AUDIO_TARGET, vtgt = targets & VIDEO_TARGET;
    TaskHandle_t atask = xTaskGetHandle("audio");
    TaskHandle_t vtask = xTaskGetHandle("video");
    if (ctrl) {
        if (!strbool(ctrl)) {
            if (atgt) audio_run = false;
            if (vtgt) video_run = false;
            for (int ms = 10; ms && (atgt || vtgt); ms--) {
                if (atgt && !xTaskGetHandle("audio")) atgt = false;
                if (vtgt && !xTaskGetHandle("video")) vtgt = false;
                msleep(ms);
            }
        } else {
            void *arg = (void *)(tout_ms ?: (uint32_t)-1);
#ifdef CONFIG_BASE_USE_I2S
            if (atgt && !atask) {
                audio_run = true;
                xTaskCreate(audio_capture, "audio", 8192, arg, 20, &atask);
                if (!atask) return ESP_ERR_NO_MEM;
            }
#endif
#ifdef CONFIG_BASE_USE_CAM
            if (vtgt && !vtask) {
                video_run = true;
                xTaskCreate(video_capture, "video", 2048, arg, 20, &vtask);
                if (!vtask) return ESP_ERR_NO_MEM;
            }
#endif
        }
    } else if (stream) {
#ifdef CONFIG_BASE_USE_I2S
        if (atgt && !aud_shdl) REGEVTS(AVC, aud_visual, stream, &aud_shdl);
#endif
#ifdef CONFIG_BASE_USE_CAM
        if (vtgt && !vid_shdl) REGEVTS(AVC, vid_visual, stream, &vid_shdl);
#endif
    } else {
        printf("Audio Capture: %s\n", atask ? "enabled" : "disabled");
        printf("Video Capture: %s\n", vtask ? "enabled" : "disabled");
    }
    return ESP_OK;
}

/******************************************************************************
 * Task Watchdog
 */

static void twdt_initialize() {
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) \
 || defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1)
    // Idle tasks are created on each core automatically by RTOS scheduler
    // with the lowest possible priority (0). Our tasks have higher priority,
    // thus leaving almost no time for idle tasks to run. Disable WDT on them.
#   ifdef CONFIG_FREERTOS_UNICORE
    LOOPN(i, 1)
#   else
    LOOPN(i, 2)
#   endif
    {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(i);
        if (idle && !esp_task_wdt_status(idle) && !esp_task_wdt_delete(idle)) {
            ESP_LOGI(TAG, "Task IDLE%d @ CPU%d removed from WDT", i, i);
        }
    }
#endif // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPUx
}

void driver_initialize() {
    const char * tags[] = {
        "gpio", "button", "adc button", "led_indicator", "Knob"
    };
    ITERV(tag, tags) { esp_log_level_set(tag, ESP_LOG_WARN); }

    uart_initialize();
    twdt_initialize();
    pwm_initialize();
#ifdef CONFIG_BASE_USE_ADC
    adc_initialize();
#endif
#ifdef CONFIG_BASE_USE_DAC
    dac_initialize();
#endif
#ifdef CONFIG_BASE_USE_SPI
    spi_initialize();
#endif
#ifdef CONFIG_BASE_USE_I2C
    i2c_initialize();
#endif
#ifdef CONFIG_BASE_USE_GPIOEXP
    gexp_initialize();
#endif
    gpio_initialize();
#ifdef CONFIG_BASE_USE_LED
    led_initialize();
#endif
#ifdef CONFIG_BASE_USE_I2S
    i2s_initialize();
#endif
#ifdef CONFIG_BASE_USE_CAM
    cam_initialize();
#endif
}
