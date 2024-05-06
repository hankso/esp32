/* 
 * File: drivers.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:54:28
 */

#include "drivers.h"
#include "ledmode.h"
#include "hidtool.h"

#include "esp_attr.h"
#include "esp_task_wdt.h"
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
    const gpio_num_t pins[2];
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
    .atten = ADC_ATTEN_DB_11,
#ifdef CONFIG_BASE_ADC_HALL
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
    const adc_oneshot_unit_init_cfg_t init_conf = { .unit_id = adc.unit };
    if (!( err = adc_oneshot_new_unit(&init_conf, &adc.oneshot) )) {
        const adc_oneshot_chan_cfg_t chan_conf = {
            .bitwidth = adc.width,
            .atten = adc.atten,
        };
        ITER(chan, adc.chans) {
            if (chan == ADC_CHANNEL_MAX) continue;
            err = adc_oneshot_config_channel(adc.oneshot, chan, &chan_conf);
            if (err) break;
        }
    }
    if (!err) err = adc_calibration_init(adc.unit, adc.atten, &adc.cali);
#else
    ITER(chan, adc.chans) {
        if (chan == ADC_CHANNEL_MAX) continue;
        if (( err = adc1_config_channel_atten(chan, adc.atten) )) break;
    }
    if (!err) err = adc1_config_width(adc.width);
#   ifdef TARGET_ESP32
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF)) {
        ESP_LOGI(TAG, "ADC: eFuse VRef not supported");
    } else {
        ESP_LOGD(TAG, "ADC: eFuse VRef supported");
    }
#   endif
#   if defined(TARGET_ESP32) || defined(TARGET_ESP32S3)
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
#if defined(CONFIG_BASE_ADC_HALL) && defined(TARGET_IDF_4)
    int raw = 0;
    LOOPN(i, CONFIG_BASE_ADC_MULTISAMPLING) {
        usleep(10);
        raw += hall_sensor_read();
    }
    ITER(chan, adc.chans) {
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
 * PWM by LEDC
 */

#define SPEED_MODE  LEDC_LOW_SPEED_MODE

#define TMR_SERVO   LEDC_TIMER_1
#define RES_SERVO   LEDC_TIMER_10_BIT
#define CH_SERVOH   LEDC_CHANNEL_1
#define CH_SERVOV   LEDC_CHANNEL_2

#define TMR_BUZZER  LEDC_TIMER_2
#define RES_BUZZER  LEDC_TIMER_10_BIT
#define CH_BUZZER   LEDC_CHANNEL_3

static void pwm_initialize() {
#ifdef CONFIG_BASE_USE_SERVO
    const ledc_timer_config_t servo_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = TMR_SERVO,
        .duty_resolution    = RES_SERVO,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&servo_conf) );
    const ledc_channel_config_t channel0_conf = {
        .gpio_num           = PIN_SVOH,
        .speed_mode         = SPEED_MODE,
        .channel            = CH_SERVOH,
        .timer_sel          = TMR_SERVO,
        .hpoint             = 0,
        .duty               = 0
    };
    const ledc_channel_config_t channel1_conf = {
        .gpio_num           = PIN_SVOV,
        .speed_mode         = SPEED_MODE,
        .channel            = CH_SERVOV,
        .timer_sel          = TMR_SERVO,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&channel0_conf) );
    ESP_ERROR_CHECK( ledc_channel_config(&channel1_conf) );
#endif
#ifdef CONFIG_BASE_USE_BUZZER
    const ledc_timer_config_t buzzer_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = TMR_BUZZER,
        .duty_resolution    = RES_BUZZER,
        .freq_hz            = 5000, // 0-5kHz is commonlly used
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&buzzer_conf) );
    const ledc_channel_config_t channel2_conf = {
        .gpio_num           = PIN_BUZZ,
        .speed_mode         = SPEED_MODE,
        .channel            = CH_BUZZER,
        .timer_sel          = TMR_BUZZER,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&channel2_conf) );
#endif
}

static esp_err_t pwm_set_duty(int channel, int duty) {
    esp_err_t err = ledc_set_duty(SPEED_MODE, channel, duty);
    if (!err) err = ledc_update_duty(SPEED_MODE, channel);
    return err;
}

#ifdef CONFIG_BASE_USE_SERVO
// mapping 0-180 deg to 0.5-2.5 ms
static const float servo_offset = 0.5 / 20 * ((1 << RES_SERVO) - 1);
static const float servo_scale  = 2.0 / 20 * ((1 << RES_SERVO) - 1) / 180;

esp_err_t pwm_set_degree(int hdeg, int vdeg) {
    esp_err_t err = ESP_OK;
    if (!err && hdeg >= 0) {
        hdeg = MIN(180, 166 * hdeg / 180 + 14);
        err = pwm_set_duty(CH_SERVOH, hdeg * servo_scale + servo_offset);
    }
    if (!err && vdeg >= 0) {
        vdeg = MIN(160, vdeg);
        err = pwm_set_duty(CH_SERVOV, vdeg * servo_scale + servo_offset);
    }
    return err;
}

esp_err_t pwm_get_degree(int *hdeg, int *vdeg) {
    int hduty = ledc_get_duty(SPEED_MODE, CH_SERVOH),
        vduty = ledc_get_duty(SPEED_MODE, CH_SERVOV);
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
static const float buzzer_scale = ((1 << RES_BUZZER) - 1) / 200;

esp_err_t pwm_set_tone(int freq, int pcnt) {
    esp_err_t err = freq > 20000 ? ESP_ERR_INVALID_ARG : ESP_OK;
    if (freq == 0) pcnt = 0;
    if (!err && freq > 0)  err = ledc_set_freq(SPEED_MODE, TMR_BUZZER, freq);
    if (!err && pcnt >= 0) err = pwm_set_duty(CH_BUZZER, pcnt * buzzer_scale);
    return err;
}

esp_err_t pwm_get_tone(int *freq, int *pcnt) {
    *freq = ledc_get_freq(SPEED_MODE, TMR_BUZZER);
    *pcnt = (int)(ledc_get_duty(SPEED_MODE, CH_SERVOH) / buzzer_scale);
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

/******************************************************************************
 * SPI Master interface
 */

#ifdef CONFIG_BASE_USE_SPI
static void spi_initialize() {
    const spi_bus_config_t buf_conf = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .max_transfer_sz = 81920,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = 0
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
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num         = PIN_SDA,
        .sda_pullup_en      = GPIO_PULLUP_ENABLE,
        .scl_io_num         = PIN_SCL,
        .scl_pullup_en      = GPIO_PULLUP_ENABLE,
        .master.clk_speed   = CONFIG_BASE_I2C_SPEED,
    };
    ESP_ERROR_CHECK( i2c_param_config(NUM_I2C, &i2c_conf) );
    ESP_ERROR_CHECK( i2c_driver_install(NUM_I2C, I2C_MODE_MASTER, 0, 0, 0) );
    
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
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(b); NOTUSED(a);
    NOTUSED(r); NOTUSED(v); NOTUSED(l);
}
esp_err_t smbus_rregs(int b, uint8_t a, uint8_t r, uint8_t *v, size_t l) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(b); NOTUSED(a);
    NOTUSED(r); NOTUSED(v); NOTUSED(l);
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
        printf("I2C %d-%02X register table\n", bus, addr);
        printf("ADDR:");
        LOOPN(i, LENGTH) { printf(" %02X", i); }
        printf("\n%04X:%*s", reg - (reg % LENGTH), 3 * (reg % LENGTH), "");
        LOOPN(i, num) {
            if (i % LENGTH == 0) printf("\n%04X:", i + reg);
            printf(" %02X", buf[i]);
        }
        putchar('\n');
    }
    TRYFREE(buf);
    return err;
}

static bool i2c_master_inited(int bus) {
    esp_err_t err = smbus_probe(bus, 0);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE)
        return false;
    return true;
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
    const spi_device_interface_config_t dev_conf = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0b10,                       // CPOL = 1, CPHA = 0
        .duty_cycle_pos = 128,              // 128/256 = 50% (Tlow = Thigh)
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 5 * 1000 * 1000,  // 5MHz
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
}
#endif

/******************************************************************************
 * GPIO Interrupt (inc. button & knob)
 */

static const char * const gpio_default_usage[GPIO_PIN_COUNT] = {
#if defined(TARGET_ESP32)
    [0]         = "Strapping PU",
    [2]         = "Strapping PD",
    [5]         = "Strapping PU",
    [6]         = "Flash SPICLK",
    [7]         = "Flash SPIQ",
    [8]         = "Flash SPID",
    [9]         = "Flash SPIHD",
    [10]        = "Flash SPIWP",
    [11]        = "Flash SPICS0",
    [16]        = "Flash D2WD",
    [17]        = "Flash D2WD",
    [20]        = "ESP32-PICO-V3",
#elif defined(TARGET_ESP32S3)
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
    [PIN_TXD]   = "UART" STR(CONFIG_BASE_UART_NUM) " TXD",
    [PIN_RXD]   = "UART" STR(CONFIG_BASE_UART_NUM) " RXD",
#endif
#ifdef CONFIG_BASE_USE_I2C0
    [PIN_SDA0]  = "I2C0 SDA",
    [PIN_SCL0]  = "I2C0 SCL",
#endif
#ifdef CONFIG_BASE_USE_I2C1
    [PIN_SDA1]  = "I2C1 SDA",
    [PIN_SCL1]  = "I2C1 SCL",
#endif
#ifdef CONFIG_BASE_USE_SPI
    [PIN_MISO]  = "SPI MISO",
    [PIN_MOSI]  = "SPI MOSI",
    [PIN_SCLK]  = "SPI SCLK",
#endif
#ifdef PIN_CS0
    [PIN_CS0]   = "SPI CS0 (SDCard)",
#endif
#ifdef PIN_CS1
    [PIN_CS1]   = "SPI CS1 (Screen)",
#endif
#ifdef PIN_CS2
    [PIN_CS2]   = "SPI CS2 (GPIOExp)",
#endif
#ifdef CONFIG_BASE_SCREEN_SPI
    [PIN_SDC]   = "SPI Screen D/C",
#endif
#ifdef CONFIG_BASE_GPIO_SCN_RST
    [PIN_SRST]  = "SPI Screen RESET",
#endif
#ifdef CONFIG_BASE_USE_INT
    [PIN_INT]   = "Interrupt",
#endif
#ifdef PIN_ADC1
    [PIN_ADC1]  = "ADC1",
#endif
#ifdef PIN_ADC2
    [PIN_ADC2]  = "ADC2",
#endif
#ifdef CONFIG_BASE_USE_DAC
    [PIN_DAC]   = "DAC",
#endif
#ifdef CONFIG_BASE_USE_TPAD
    [PIN_TPAD]  = "Touch",
#endif
#ifdef CONFIG_BASE_USE_BTN
    [PIN_BTN]   = "Button",
#endif
#ifdef CONFIG_BASE_USE_KNOB
    [PIN_ENCA]  = "Knob encoder A",
    [PIN_ENCB]  = "Knob encoder B",
#endif
#ifdef CONFIG_BASE_USE_SERVO
    [PIN_SVOH]  = "Servo Yaw",
    [PIN_SVOV]  = "Servo Pitch",
#endif
#ifdef CONFIG_BASE_USE_BUZZER
    [PIN_BUZZ]  = "Buzzer",
#endif
};

#ifdef CONFIG_BASE_USE_BTN
static button_handle_t btn[2];
#   ifdef CONFIG_BASE_ADC_JOYSTICK
static button_handle_t joystick[4];
#   endif

static void cb_button(void *arg, void *data) {
    static const char *BTN = "button";
    int pin = (int)data;
    button_event_t event = iot_button_get_event(arg);
    switch (event) {
    case BUTTON_PRESS_UP:
        ESP_LOGI(BTN, "%d release[%d]", pin, iot_button_get_ticks_time(arg));
        if (pin == PIN_BTN) hid_report_dial(HID_TARGET_ALL, DIAL_UP);
        break;
    case BUTTON_PRESS_DOWN:
        ESP_LOGI(BTN, "%d press", pin);
        if (pin == PIN_BTN) hid_report_dial(HID_TARGET_ALL, DIAL_DN);
        break;
    case BUTTON_SINGLE_CLICK:
        ESP_LOGI(BTN, "%d single click", pin);
        break;
    case BUTTON_DOUBLE_CLICK:
        ESP_LOGI(BTN, "%d double click", pin);
        break;
    case BUTTON_MULTIPLE_CLICK:
        ESP_LOGI(BTN, "%d click %d times", pin, iot_button_get_repeat(arg));
        break;
    case BUTTON_LONG_PRESS_HOLD:
        ESP_LOGI(BTN, "%d long press %d",
                 pin, iot_button_get_long_press_hold_cnt(arg));
        break;
    default: return;
    }
#   ifdef CONFIG_BASE_ADC_JOYSTICK
    if (event == BUTTON_PRESS_DOWN || event == BUTTON_LONG_PRESS_HOLD) {
        LOOPN(i, LEN(adc.pins)) {
            if (adc.pins[i] != pin) continue;
            int xy = adc_joystick(NULL, NULL);
            if (xy == -1) break;
            int x = (xy >> 16) > 2000 ? 1 : ((xy >> 16) < 1300 ? -1 : 0);
            int y = (xy & 0xFFFF) > 2000 ? 1 : ((xy & 0xFFFF) < 1300 ? -1 : 0);
            hid_report_mouse_move(HID_TARGET_ALL, x * 5, y * 5);
        }
    }
#   endif
}

static button_handle_t button_init(button_config_t *conf, gpio_num_t pin) {
    button_handle_t hdl = iot_button_create(conf);
    if (!hdl) {
        ESP_LOGE(TAG, "BTN bind to GPIO%d failed", pin);
    } else LOOPN(event, BUTTON_EVENT_MAX) {
        if (event != BUTTON_MULTIPLE_CLICK) {
            iot_button_register_cb(hdl, event, cb_button, (void *)pin);
        /*
        } else {
            button_event_config_t evt = {
                .event = event,
                .event_data.multiple_clicks.clicks = 3,
            };
            iot_button_register_event_cb(hdl, evt, cb_button, (void *)pin);
        */
        }
    }
    return hdl;
}
#endif

#ifdef CONFIG_BASE_USE_KNOB
static knob_handle_t knob;

static void cb_knob(void *arg, void *data) {
    switch (iot_knob_get_event(arg)) {
    case KNOB_LEFT:
        ESP_LOGD(TAG, "Knob: left rotate %d", iot_knob_get_count_value(arg));
        hid_report_dial(HID_TARGET_ALL, DIAL_L);
        break;
    case KNOB_RIGHT:
        ESP_LOGD(TAG, "Knob: right rotate %d", iot_knob_get_count_value(arg));
        hid_report_dial(HID_TARGET_ALL, DIAL_R);
        break;
    default: break;
    }
}
#endif

static void IRAM_ATTR UNUSED gpio_isr_endstop(void *arg) {
#ifdef CONFIG_BASE_USE_INT
    ets_printf("PIN_INT %s\n", gpio_get_level(PIN_INT) ? "RISE" : "FALL");
#endif
#ifdef CONFIG_BASE_GPIOEXP_I2C
    LOOPN(i, LEN(i2c_pin_data)) {
        if (i2c_gexp_get_level(PIN_I2C_BASE + i * 8, NULL, true)) continue;
        ets_printf("I2C GPIOExp: %s\n", format_binary(i2c_pin_data[i], 1));
    }
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
    if (spi_gexp_get_level(PIN_SPI_BASE, NULL, true)) return;
    LOOPN(i, PIN_SPI_COUNT / 8) {
        ets_printf("SPI GPIOExp: %s\n", format_binary(spi_pin_data[i], 1));
    }
#endif
}

static void gpio_initialize() {
#ifdef CONFIG_BASE_USE_INT
    const gpio_config_t int_conf = {
        .pin_bit_mask = BIT64(PIN_INT),
        .mode         = GPIO_MODE_INPUT,
#   if defined(CONFIG_BASE_INT_POSEDGE) || defined(CONFIG_BASE_INT_HIGH)
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
#   else
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
#   endif
#   if defined(CONFIG_BASE_INT_NEGEDGE)
        .intr_type    = GPIO_INTR_NEGEDGE,
#   elif defined(CONFIG_BASE_INT_POSEDGE)
        .intr_type    = GPIO_INTR_POSEDGE,
#   elif defined(CONFIG_BASE_INT_ANYEDGE)
        .intr_type    = GPIO_INTR_ANYEDGE,
#   elif defined(CONFIG_BASE_INT_LOW)
        .intr_type    = GPIO_INTR_LOW_LEVEL,
#   elif defined(CONFIG_BASE_INT_HIGH)
        .intr_type    = GPIO_INTR_HIGH_LEVEL,
#   endif
    };
    ESP_ERROR_CHECK( gpio_config(&int_conf) );
    ESP_ERROR_CHECK( gpio_install_isr_service(0) );
    ESP_ERROR_CHECK( gpio_isr_handler_add(PIN_INT, gpio_isr_endstop, NULL) );
#endif
#ifdef CONFIG_BASE_USE_KNOB
    const knob_event_t events[] = { KNOB_LEFT, KNOB_RIGHT };
    const knob_config_t knob_conf = {
        .default_direction = 0,     // 0:positive; 1:negative
        .gpio_encoder_a = PIN_ENCA,
        .gpio_encoder_b = PIN_ENCB
    };
    if (!( knob = iot_knob_create(&knob_conf) )) {
        ESP_LOGE(TAG, "Knob: bind to GPIO%d & %d failed", PIN_ENCA, PIN_ENCB);
    } else LOOPN(i, LEN(events)) {
        iot_knob_register_cb(knob, events[i], cb_knob, NULL);
    }
#endif
#ifdef CONFIG_BASE_USE_BTN
    button_config_t btn_conf = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = PIN_BTN,
#   ifdef CONFIG_BASE_BTN_ACTIVE_HIGH
            .active_level = 1,
#   else
            .active_level = 0,
#   endif
        }
    };
    btn[0] = button_init(&btn_conf, PIN_BTN);
    if (startswith(gpio_default_usage[GPIO_NUM_0] ?: "", "Strapping")) {
        btn_conf.gpio_button_config.gpio_num = GPIO_NUM_0;
        btn_conf.gpio_button_config.active_level = 0;
        btn[1] = button_init(&btn_conf, GPIO_NUM_0);
    }
#   ifdef CONFIG_BASE_ADC_JOYSTICK
    btn_conf.type = BUTTON_TYPE_ADC;
    btn_conf.long_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS + 20;
    LOOPN(i, LEN(adc.chans)) {
        if (adc.chans[i] == ADC_CHANNEL_MAX) continue;
        btn_conf.adc_button_config.adc_channel = adc.chans[i];
        btn_conf.adc_button_config.button_index = 0;
        btn_conf.adc_button_config.min = 0;
        btn_conf.adc_button_config.max = 1300; // 0-1.3V
        joystick[2 * i + 0] = button_init(&btn_conf, adc.pins[i]);
        btn_conf.adc_button_config.button_index = 1;
        btn_conf.adc_button_config.min = 2000;
        btn_conf.adc_button_config.max = 3300; // 2.0-3.3V
        joystick[2 * i + 1] = button_init(&btn_conf, adc.pins[i]);
        // FIXME: BUTTON_LONG_PRESS_HOLD stall if joystick ADC value > 2.0V
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
 * UART with custom pin
 */

static void uart_initialize() {
    // esp_vfs_dev_uart_register is called on startup code to use /dev/uart0
    fflush(stdout); fsync(fileno(stdout));

#ifdef CONFIG_BASE_USE_UART
    // UART driver configuration
    const uart_config_t uart_conf = {
        .baud_rate = 115200,
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
 * Task Watchdog
 */

static void twdt_initialize() {
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) \
 || defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1)
    // Idle tasks are created on each core automatically by RTOS scheduler
    // with the lowest possible priority (0). Our tasks have higher priority,
    // thus leaving almost no time for idle tasks to run. Disable WDT on them.
    #ifdef CONFIG_FREERTOS_UNICORE
    LOOPN(i, 1)
    #else
    LOOPN(i, 2)
    #endif // CONFIG_FREERTOS_UNICORE
    {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(i);
        if (idle && !esp_task_wdt_status(idle) && !esp_task_wdt_delete(idle)) {
            ESP_LOGI(TAG, "Task IDLE%d @ CPU%d removed from WDT", i, i);
        }
    }
#endif // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPUx
}

esp_err_t twdt_feed() {
#ifdef CONFIG_TASK_WDT
    return esp_task_wdt_reset();
#endif // CONFIG_TASK_WDT
    return ESP_OK;
}

void driver_initialize() {
    const char * tags[] = {
        "gpio", "Knob", "button",
        "adc button", "led_indicator"
    };
    LOOPN(i, LEN(tags)) { esp_log_level_set(tags[i], ESP_LOG_WARN); }

    pwm_initialize();
#ifdef CONFIG_BASE_USE_ADC
    adc_initialize();
#endif
#ifdef CONFIG_BASE_USE_DAC
    dac_initialize();
#endif
#ifdef CONFIG_BASE_USE_LED
    led_initialize();
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
    uart_initialize();
    twdt_initialize();
}
