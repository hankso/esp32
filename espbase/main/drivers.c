/* 
 * File: drivers.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:54:28
 */

#include "drivers.h"
#include "hidtool.h"            // for hid_report_xxx
#include "ledmode.h"            // for led_initialize
#include "avcmode.h"            // for avc_initialize
#include "sensors.h"            // for tscn_command
#include "screen.h"             // for scn_initialize && scn_command
#include "config.h"

#include "esp_intr_alloc.h"
#include "soc/soc_caps.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_BASE_USE_BTN
#   include "iot_button.h"
#endif

#ifdef CONFIG_BASE_USE_KNOB
#   include "iot_knob.h"
#endif

static UNUSED const char *TAG = "Driver";

/*
 * UART with custom pin
 */

static void uart_initialize() {
    // esp_vfs_dev_uart_register is called on startup code to use /dev/uart0
    fflush(stdout); fsync(fileno(stdout));

#ifdef CONFIG_BASE_USE_UART
    // UART driver configuration
    uart_config_t cfg = {
#   ifdef CONFIG_ESP_CONSOLE_UART_BAUDRATE
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
    uart_param_config(NUM_UART, &cfg);
    uart_set_pin(NUM_UART, PIN_TXD, PIN_RXD, PIN_RTS, PIN_CTS);
    ESP_ERROR_CHECK( uart_driver_install(NUM_UART, 256, 0, 0, NULL, 0) );
#endif
}

/*
 * ADC analog in
 */

#ifdef CONFIG_BASE_USE_ADC
#   include "soc/adc_periph.h"

#   ifdef IDF_TARGET_V4
#       include "driver/adc.h"
#       include "esp_adc_cal.h"
#   else
#       include "esp_adc/adc_oneshot.h"
#   endif

static struct {
    adc_channel_t chans[3];
    gpio_num_t pins[3];
    const adc_unit_t unit;
    const adc_atten_t atten;
#   ifdef IDF_TARGET_V4
    const adc_bits_width_t width;
    esp_adc_cal_characteristics_t cali;
#   else
    const adc_bitwidth_t width;
    adc_oneshot_unit_handle_t hdl;
    adc_cali_handle_t calis[3];
#   endif
    uint8_t nsample;
} adc = {
    .pins = {
#   ifdef PIN_ADC0
        PIN_ADC0,
#   else
        GPIO_NUM_NC,
#   endif
#   ifdef PIN_ADC1
        PIN_ADC1,
#   else
        GPIO_NUM_NC,
#   endif
#   ifdef PIN_ADC2
        PIN_ADC2,
#   else
        GPIO_NUM_NC,
#   endif
    },
    .unit = ADC_UNIT_1, // only use ADC1
    .atten = ADC_ATTEN_DB_12,
#   if defined(CONFIG_BASE_ADC_HALL_SENSOR) && defined(IDF_TARGET_V4)
    .width = ADC_WIDTH_BIT_12,
#   elif defined(IDF_TARGET_V4)
    .width = ADC_WIDTH_BIT_DEFAULT,
#   else
    .width = ADC_BITWIDTH_DEFAULT,
#   endif
};

static adc_channel_t gpio2adc(gpio_num_t pin) {
    LOOPN(j, pin == GPIO_NUM_NC ? 0 : SOC_ADC_MAX_CHANNEL_NUM) {
        if (adc_channel_io_map[0][j] == pin) return j;
    }
    return -1;
}

static void adc_initialize() {
    char buf[16], T[] = "ADC";

#ifdef IDF_TARGET_V4
    esp_err_t err = adc1_config_width(adc.width);
#   ifdef CONFIG_IDF_TARGET_ESP32
    ESP_LOGD(TAG, "%s: eFuse VRef %ssupported", T,
             esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) ? "" : "not ");
#   endif
#   if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONIFG_IDF_TARGET_ESP32S3)
    ESP_LOGD(TAG, "%s: eFuse Two Point %ssupported", T,
             esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) ? "" : "not ");
#   endif
    esp_adc_cal_value_t vtype = esp_adc_cal_characterize(
        adc.unit, adc.atten, adc.width, 1100, &adc.cali);
    ESP_LOGI(TAG, "%s: characterized using %s", T,
             vtype == ESP_ADC_CAL_VAL_EFUSE_VREF ? "eFuse VRef" :
             vtype == ESP_ADC_CAL_VAL_EFUSE_TP ? "eFuse TP" : "default VRef");
#else
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = adc.unit };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &adc.hdl);
#endif

    if (err) {
        ESP_LOGE(TAG, "%s initialize failed: %s", T, esp_err_to_name(err));
        LOOPN(i, LEN(adc.chans)) { adc.chans[i] = -1; }
        return;
    }
    if (parse_u8(Config.sys.ADC_MULT, &adc.nsample)) adc.nsample = 5;

    LOOPN(i, LEN(adc.chans)) {
        if (( adc.chans[i] = gpio2adc(adc.pins[i]) ) == -1) {
            if (adc.pins[i] != GPIO_NUM_NC)
                ESP_LOGE(TAG, "%s: invalid pin %d", T, adc.pins[i]);
            continue;
        }
#ifdef IDF_TARGET_V4
        err = adc1_config_channel_atten(adc.chans[i], adc.atten);
#else
        adc_oneshot_chan_cfg_t cfg = {
            .atten = adc.atten,
            .bitwidth = adc.width,
        };
        if (( err = adc_oneshot_config_channel(adc.hdl, adc.chans[i], &cfg) ))
            goto done;
#   if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t curve = {
            .unit_id = adc.unit,
            .chan = adc.chans[i],
            .atten = adc.atten,
            .bitwidth = adc.width,
        };
        err = adc_cali_create_scheme_curve_fitting(&curve, &adc.calis[i]);
#   endif
#   if ADC_CALI_SCHEME_LINE_FITTING_SUPPROTED
        adc_cali_line_fitting_config_t line = {
            .unit_id = adc.unit,
            .atten = adc.atten,
            .bitwidth = adc.width,
        };
        if (!adc.calis[i])
            err = adc_cali_create_scheme_line_fitting(&line, &adc.calis[i]);
#   endif
#endif
done:
        if (err) {
           adc.chans[i] = -1;
           err = ESP_OK;
        } else {
            size_t len = snprintf(buf, sizeof(buf), "%s%d", T, adc.chans[i]);
            UNUSED int size = sizeof(buf) - len;
#if defined(CONFIG_BASE_ADC_HALL_SENSOR)
            if (i < 2) snprintf(buf + len, size, "(HALL SEN %c)", "PN"[i]);
#elif defined(CONFIG_BASE_ADC_JOYSTICK)
            if (i < 2) snprintf(buf + len, size, "(Joystick %c)", "XY"[i]);
#endif
            gpio_usage(adc.pins[i], strdup(buf));
        }
    }
}

int adc_hall() {
#   if defined(CONFIG_BASE_ADC_HALL_SENSOR) && defined(IDF_TARGET_V4)
    int raw = -1;
    adc_power_acquire();
    LOOPN(i, adc.nsample) {
        usleep(10);
        raw += hall_sensor_read();
    }
    adc_power_release();
    ITERV(chan, adc.chans) {
        if (chan != -1) adc1_config_channel_atten(chan, adc.atten);
    }
    return raw == -1 ? raw : (raw / adc.nsample);
#   else
    return -1;
#   endif
}

int adc_read(uint8_t idx) {
    if (idx >= LEN(adc.chans) || adc.chans[idx] == -1) return -1;
    int raw, cum = 0, cnt = 0;
    LOOPN(i, adc.nsample) {
        usleep(10);
#   ifdef IDF_TARGET_V4
        raw = adc1_get_raw(adc.chans[idx]);
#   else
        adc_oneshot_read(adc.hdl, adc.chans[idx], &raw);
#   endif
        if (raw == -1) return -1;
        cum += raw;
        cnt++;
    }
#   ifdef IDF_TARGET_V4
    return cnt ? esp_adc_cal_raw_to_voltage(cum / cnt, &adc.cali) : -1;
#   else
    if (cnt) adc_cali_raw_to_voltage(adc.calis[idx], cum / cnt, &raw);
    return cnt ? raw : -1;
#   endif
}

int adc_joystick(int *dx, int *dy) {
#   ifdef CONFIG_BASE_ADC_JOYSTICK
    static int px, py;
    int x = adc_read(0);
    int y = adc_read(1);
    if (x == -1 || y == -1) return -1;
    if (dx) *dx = x - px ?: x;
    if (dy) *dy = y - py ?: y;
    px = x; py = y;
    return x << 16 | y;
#   else
    return -1; NOTUSED(dx); NOTUSED(dy);
#   endif
}

#else // CONFIG_BASE_USE_ADC
int adc_hall() { return 0; }
int adc_read(uint8_t i) { return -1; NOTUSED(i); }
int adc_joystick(int *x, int *y) { return -1; NOTUSED(x); NOTUSED(y); }
#endif // CONFIG_BASE_USE_ADC

/*
 * DAC analog out
 */

#if defined(CONFIG_BASE_USE_DAC) && SOC_DAC_SUPPORTED
#   include "soc/dac_periph.h"

#   ifdef IDF_TARGET_V4
#       define SOC_DAC_CHAN_NUM SOC_DAC_PERIPH_NUM
#       include "driver/dac_common.h"
#   else
#       include "driver/dac_cosine.h"
#       include "driver/dac_oneshot.h"
#   endif

typedef enum {
    DAC_NONE,
    DAC_COSINE,
    DAC_ONESHOT,
} dac_type_t;

static struct {
    dac_channel_t chans[2];
    gpio_num_t pins[2];
#   ifdef IDF_TARGET_V4
    dac_type_t types[2];
#   else
    dac_cosine_handle_t cos[2];
    dac_oneshot_handle_t one[2];
#   endif
} dac = {
    .pins = {
#   ifdef CONFIG_BASE_GPIO_DAC0
        GPIO_NUMBER(CONFIG_BASE_GPIO_DAC0),
#   else
        GPIO_NUM_NC,
#   endif
#   ifdef CONFIG_BASE_GPIO_DAC1
        GPIO_NUMBER(CONFIG_BASE_GPIO_DAC1),
#   else
        GPIO_NUM_NC,
#   endif
    }
};

static dac_channel_t gpio2dac(gpio_num_t pin) {
    LOOPN(i, pin == GPIO_NUM_NC ? 0 : SOC_DAC_CHAN_NUM) {
        if (dac_periph_signal.dac_channel_io_num[i] == pin) return i;
    }
    return -1;
}

static void dac_initialize() {
    char buf[16], T[] = "DAC";
    LOOPN(i, LEN(dac.chans)) {
        if (( dac.chans[i] = gpio2dac(dac.pins[i]) ) == -1) {
            if (dac.pins[i] != GPIO_NUM_NC)
                ESP_LOGE(TAG, "%s: invalid pin %d", T, dac.pins[i]);
            continue;
        }
        snprintf(buf, sizeof(buf), "%s%d", T, dac.chans[i]);
        gpio_usage(dac.pins[i], strdup(buf));
    }
}

esp_err_t dac_write(uint8_t idx, uint8_t val) {
    if (idx >= LEN(dac.chans) || dac.chans[idx] == -1)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
#   ifdef IDF_TARGET_V4
    if (dac.types[idx] == DAC_COSINE) err = dac_cw_generator_disable();
    if (dac.types[idx] != DAC_ONESHOT) {
        if (!err) err = dac_output_enable(dac.chans[idx]);
        dac.types[idx] = err ? DAC_NONE : DAC_ONESHOT;
    }
    return err ?: dac_output_voltage(dac.chans[idx], val);
#   else
    if (dac.cos[idx]) {
        if (!err) err = dac_cosine_stop(dac.cos[idx]);
        if (!err) err = dac_cosine_del_channel(dac.cos[idx]);
        if (!err) dac.cos[idx] = NULL;
    }
    if (!dac.one[idx]) {
        dac_oneshot_config_t cfg = { .chan_id = dac.chans[idx] };
        if (!err) err = dac_oneshot_new_channel(&cfg, &dac.one[idx]);
    }
    return err ?: dac_oneshot_output_voltage(dac.one[idx], val);
#   endif
}

esp_err_t dac_cwave(uint8_t idx, uint32_t fspo) {
    if (idx >= LEN(dac.chans) || dac.chans[idx] == -1)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
#   ifdef IDF_TARGET_V4
    if (dac.types[idx] == DAC_ONESHOT) err = dac_output_disable(dac.chans[idx]);
    if (dac.types[idx] != DAC_COSINE) {
        if (!err) err = dac_cw_generator_enable();
        dac.types[idx] = err ? DAC_NONE : DAC_COSINE;
    }
    dac_cw_config_t cfg = {
        .en_ch  = dac.chans[idx],
        .freq   = CONS(fspo >> 16, 130, 55000),
        .scale  = (fspo >> 12) & 0x3,
        .phase  = (fspo >> 8) & 0xF,
        .offset = (fspo & 0xFF) - 128,
    };
    return err ?: dac_cw_generator_config(&cfg);
#   else
    if (dac.one[idx] && !( err = dac_oneshot_del_channel(dac.one[idx]) ))
        dac.one[idx] = NULL;
    static uint32_t last;
    if (last == fspo) return err;
    last = fspo;
    if (dac.cos[idx]) {
        if (!err) err = dac_cosine_stop(dac.cos[idx]);
        if (!err) err = dac_cosine_del_channel(dac.cos[idx]);
        if (!err) dac.cos[idx] = NULL;
    }
    dac_cosine_config_t cfg = {
        .chan_id = dac.chans[idx],
        .freq_hz = MAX(130, fspo >> 16),
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten   = (fspo >> 12) & 0x3,
        .phase   = (fspo >> 8) & 0xF,
        .offset  = (fspo & 0xFF) - 128,
    };
    if (!err) err = dac_cosine_new_channel(&cfg, &dac.cos[idx]);
    if (!err) err = dac_cosine_start(dac.cos[idx]);
    return err;
#   endif
}
#else
esp_err_t dac_write(uint8_t i, uint8_t v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(i); NOTUSED(v);
}
esp_err_t dac_cwave(uint8_t i, uint32_t v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(i); NOTUSED(v);
}
#endif

/*
 * Touch sensor (capacity pad or matrix or slider)
 */

#ifdef CONFIG_BASE_USE_TPAD
#   include "soc/touch_sensor_periph.h"
#   include "driver/touch_sensor.h"
#   include "touch_element/touch_button.h"

static struct {
    touch_pad_t chans[1];
    gpio_num_t pins[1];
    touch_button_handle_t hdls[1];
} tpad = {
    .pins = {
#   ifdef CONFIG_BASE_GPIO_TPAD
        GPIO_NUMBER(CONFIG_BASE_GPIO_TPAD),
#   else
        GPIO_NUM_NC,
#   endif
    }
};

static touch_pad_t gpio2tpad(gpio_num_t pin) {
    LOOPN(i, pin == GPIO_NUM_NC ? 0 : SOC_TOUCH_SENSOR_NUM) {
        if (touch_sensor_channel_io_map[i] == pin) return i;
    }
    return -1;
}

static void cb_tpad(
    touch_button_handle_t hdl, touch_button_message_t *msg, void *arg
) {
    static const char *T = "button";
    gpio_num_t pin = *(gpio_num_t *)arg;
    switch (msg->event) {
    case TOUCH_BUTTON_EVT_ON_PRESS:
        ESP_LOGD(T, "%d press", pin); break;
    case TOUCH_BUTTON_EVT_ON_RELEASE:
        ESP_LOGD(T, "%d release", pin); break;
    case TOUCH_BUTTON_EVT_ON_LONGPRESS:
        ESP_LOGD(T, "%d long press", pin); break;
    default: break;
    }
}

static void tpad_initialize() {
    char buf[16], T[] = "TPAD";
    touch_elem_dispatch_t method = TOUCH_ELEM_DISP_CALLBACK;
    touch_elem_global_config_t elem_cfg = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
    touch_button_global_config_t gbtn_cfg = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
    uint32_t evts = TOUCH_ELEM_EVENT_ON_PRESS |
                    TOUCH_ELEM_EVENT_ON_RELEASE |
                    TOUCH_ELEM_EVENT_ON_LONGPRESS;
    esp_err_t err = touch_element_install(&elem_cfg);
    if (!err) err = touch_button_install(&gbtn_cfg);
    if (err) {
        ESP_LOGE(TAG, "%s initialize failed: %s", T, esp_err_to_name(err));
        LOOPN(i, LEN(tpad.chans)) { tpad.chans[i] = -1; }
        return;
    }
    LOOPN(i, LEN(tpad.chans)) {
        if (( tpad.chans[i] = gpio2tpad(tpad.pins[i]) ) == -1) {
            if (tpad.pins[i] != GPIO_NUM_NC)
                ESP_LOGE(TAG, "%s: invalid pin %d", T, tpad.pins[i]);
            continue;
        }
        touch_button_config_t btn_cfg = {
            .channel_num = tpad.chans[i],
            .channel_sens = 0.1,
        };
        touch_button_handle_t hdl;
        if (!err) err = touch_button_create(&btn_cfg, &hdl);
        if (!err) err = touch_button_subscribe_event(hdl, evts, &tpad.pins[i]);
        if (!err) err = touch_button_set_dispatch_method(hdl, method);
        if (!err) err = touch_button_set_callback(hdl, cb_tpad);
        if (!err) err = touch_button_set_longpress(hdl, 1500);
        if (err) {
            touch_button_delete(hdl);
            tpad.chans[i] = -1;
            err = ESP_OK;
        } else {
            snprintf(buf, sizeof(buf), "%s%d", T, tpad.chans[i]);
            gpio_usage(tpad.pins[i], strdup(buf));
            tpad.hdls[i] = hdl;
        }
    }
    touch_element_start();
}

int tpad_read(uint8_t idx) {
    if (idx >= LEN(tpad.chans) || tpad.chans[idx] == -1) return -1;
#   ifdef CONFIG_IDF_TARGET_ESP32
    uint16_t val;
    return touch_pad_read_filtered(tpad.chans[idx], &val) ? -1 : val;
#   else
    uint32_t val;
    return touch_pad_filter_read_smooth(tpad.chans[idx], &val) ? -1 : val;
#   endif
}
#else
int tpad_read(uint8_t i) { return -1; NOTUSED(i); }
#endif

/*
 * PWM by hardware LEDC
 */

// LEDC_TIMER_0 and LEDC_CHANNEL_0 is for LED

#define SPEED_MODE  LEDC_LOW_SPEED_MODE

#define BUZZER_TMR  LEDC_TIMER_1
#define BUZZER_RES  LEDC_TIMER_13_BIT
#define BUZZER_CH   LEDC_CHANNEL_1

#define SERVO_TMR   LEDC_TIMER_2
#define SERVO_RES   LEDC_TIMER_13_BIT
#define SERVO_CHH   LEDC_CHANNEL_2
#define SERVO_CHV   LEDC_CHANNEL_3

// LEDC_TIMER_3 and LEDC_CHANNEL_4 is for Camera XCLK

static void pwm_initialize() {
#ifdef CONFIG_BASE_USE_SERVO
    ledc_timer_config_t servo_cfg = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = SERVO_TMR,
        .duty_resolution    = SERVO_RES,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&servo_cfg) );
    ledc_channel_config_t hor_cfg = {
        .gpio_num           = PIN_SVOH,
        .speed_mode         = servo_cfg.speed_mode,
        .channel            = SERVO_CHH,
        .timer_sel          = servo_cfg.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ledc_channel_config_t ver_cfg = {
        .gpio_num           = PIN_SVOV,
        .speed_mode         = servo_cfg.speed_mode,
        .channel            = SERVO_CHV,
        .timer_sel          = servo_cfg.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&hor_cfg) );
    ESP_ERROR_CHECK( ledc_channel_config(&ver_cfg) );
#endif
#ifdef CONFIG_BASE_USE_BUZZER
    ledc_timer_config_t buzzer_cfg = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = BUZZER_TMR,
        .duty_resolution    = BUZZER_RES,
        .freq_hz            = 5000, // 0-5kHz is commonlly used
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK( ledc_timer_config(&buzzer_cfg) );
    ledc_channel_config_t chan_cfg = {
        .gpio_num           = GPIO_NUMBER(CONFIG_BASE_GPIO_BUZZER),
        .speed_mode         = buzzer_cfg.speed_mode,
        .channel            = BUZZER_CH,
        .timer_sel          = buzzer_cfg.timer_num,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK( ledc_channel_config(&chan_cfg) );
    gpio_usage(chan_cfg.gpio_num, "Buzzer");
#endif
}

static UNUSED esp_err_t pwm_set_duty(int channel, int duty) {
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

/*
 * SPI Master interface
 */

#ifdef CONFIG_BASE_USE_SPI
static void spi_initialize() {
    spi_bus_config_t cfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = PIN_MISO,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
#   if defined(CONFIG_BASE_SCN_SPI) && defined(WITH_LVGL)
        .max_transfer_sz = CONFIG_BASE_SCN_HRES * CONFIG_BASE_SCN_VRES,
#   endif
    };
    esp_err_t err = spi_bus_initialize(NUM_SPI, &cfg, SPI_DMA_CH_AUTO);
    if (err && err != ESP_ERR_INVALID_STATE)
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
}
#endif

/*
 * I2C Master interface
 */

#ifndef CONFIG_BASE_USE_I2C
esp_err_t i2c_wtrd(
    uint8_t b, uint8_t a, void *w, size_t W, void *r, size_t R
) {
    return ESP_ERR_INVALID_STATE;
    NOTUSED(b); NOTUSED(a); NOTUSED(w); NOTUSED(W); NOTUSED(r); NOTUSED(R);
}
esp_err_t i2c_probe(uint8_t b, uint8_t a) {
    return ESP_ERR_INVALID_STATE; NOTUSED(b); NOTUSED(a);
}
#elif IDF_TARGET_V4
static void i2c_initialize() {
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_BASE_I2C_SPEED,
    };
#   ifdef CONFIG_BASE_USE_I2C0
    cfg.sda_io_num = PIN_SDA0;
    cfg.scl_io_num = PIN_SCL0;
    ESP_ERROR_CHECK( i2c_param_config(I2C_NUM_0, &cfg) );
    ESP_ERROR_CHECK( i2c_driver_install(I2C_NUM_0, cfg.mode, 0, 0, 0) );
#   endif
#   ifdef CONFIG_BASE_USE_I2C1
    cfg.sda_io_num = PIN_SDA1;
    cfg.scl_io_num = PIN_SCL1;
    ESP_ERROR_CHECK( i2c_param_config(I2C_NUM_1, &cfg) );
    ESP_ERROR_CHECK( i2c_driver_install(I2C_NUM_1, cfg.mode, 0, 0, 0) );
#   endif
}

esp_err_t i2c_probe(uint8_t bus, uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, TIMEOUT(20));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_wtrd(             // optmized on i2c_master_write_read_device
    uint8_t bus, uint8_t addr, void *wb, size_t wl, void *rb, size_t rl
) {
    if (!addr || addr >= 0x80) return ESP_ERR_INVALID_ARG;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(cmd);
    if (wl) {
        uint8_t wt = (addr << 1) | I2C_MASTER_WRITE;
        if (!err)       err = i2c_master_write_byte(cmd, wt, true);
        if (!err && wb) err = i2c_master_write(cmd, wb, wl, true);
    }
    if (wl && rl && !err) err = i2c_master_start(cmd);
    if (rl) {
        uint8_t rd = (addr << 1) | I2C_MASTER_WRITE;
        if (!err)       err = i2c_master_write_byte(cmd, rd, true);
        if (!err && rb) err = i2c_master_read(cmd, rb, rl, I2C_MASTER_LAST_NACK);
    }
    if (!err) err = i2c_master_stop(cmd);
    if (!err) err = i2c_master_cmd_begin(bus, cmd, TIMEOUT(20));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_probe(uint8_t b, uint8_t a) { return i2c_wtrd(b, a, 0, 1, 0, 0); }
#else // IDF_TARGET_V4
static struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev[0x80];
} i2c[I2C_NUM_MAX];

static void i2c_initialize() {
    i2c_master_bus_config_t cfg = {
        .clk_source                 = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt          = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
#   ifdef CONFIG_BASE_USE_I2C0
    cfg.i2c_port    = I2C_NUM_0;
    cfg.sda_io_num  = PIN_SDA0;
    cfg.scl_io_num  = PIN_SCL0;
    ESP_ERROR_CHECK( i2c_new_master_bus(&cfg, &i2c[cfg.i2c_port].bus) );
#   endif
#   ifdef CONFIG_BASE_USE_I2C1
    cfg.i2c_port    = I2C_NUM_1;
    cfg.sda_io_num  = PIN_SDA1;
    cfg.scl_io_num  = PIN_SCL1;
    ESP_ERROR_CHECK( i2c_new_master_bus(&cfg, &i2c[cfg.i2c_port].bus) );
#   endif
}

esp_err_t i2c_wtrd(
    uint8_t bus, uint8_t addr, void *wb, size_t wl, void *rb, size_t rl
) {
    if (!addr || addr >= 0x80) return ESP_ERR_INVALID_ARG;
    if (bus >= LEN(i2c) || !i2c[bus].bus) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    if (!i2c[bus].dev[addr]) {
        i2c_device_config_t cfg = {
            .device_address  = addr,
            .scl_speed_hz    = CONFIG_BASE_I2C_SPEED,
        };
        err = i2c_master_bus_add_device(i2c[bus].bus, &cfg, i2c[bus].dev + addr);
        if (err) return err;
    }
    if (wl && rl) {
        err = i2c_master_transmit_receive(i2c[bus].dev[addr], wb, wl, rb, rl, 100);
    } else if (wl) {
        err = i2c_master_transmit(i2c[bus].dev[addr], wb, wl, 100);
    } else if (rl) {
        err = i2c_master_receive(i2c[bus].dev[addr], rb, rl, 100);
    }
    return err;
}

esp_err_t i2c_probe(uint8_t bus, uint8_t addr) {
    if (bus >= LEN(i2c) || !i2c[bus].bus) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(i2c[bus].bus, addr, 100);
}
#endif // CONFIG_BASE_USE_I2C

esp_err_t smbus_wregs(
    uint8_t bus, uint8_t addr, uint16_t reg, uint8_t *val, size_t len
) {
    // SMBus Write protocol:
    //      S | (ADDR + W) | ACK | REG | ACK | {DATA | ACK} * n | P
    bool word = SMBUS_IS_WORD(reg);
    uint8_t *buf = malloc(2 + len);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = SMBUS_HI_WORD(reg);
    buf[1] = SMBUS_LO_WORD(reg);
    memcpy(buf + 2, val, len);
    esp_err_t err = i2c_wtrd(bus, addr, buf + !word, len + 1 + word, NULL, 0);
    free(buf);
    return err;
}

esp_err_t smbus_rregs(
    uint8_t bus, uint8_t addr, uint16_t reg, uint8_t *val, size_t len
) {
    // SMBus Read protocol:
    //      S | (ADDR + W) | ACK | REG | ACK |
    //      S | (ADDR + R) | ACK | {DATA | ACK} * (n - 1) | DATA | NACK | P
    bool word = SMBUS_IS_WORD(reg);
    uint8_t buf[2] = { SMBUS_HI_WORD(reg), SMBUS_LO_WORD(reg) };
    return i2c_wtrd(bus, addr, buf + !word, 1 + word, val, len);
}

esp_err_t smbus_write_byte(uint8_t b, uint8_t a, uint16_t reg, uint8_t val) {
    return smbus_wregs(b, a, reg, &val, 1);
}

esp_err_t smbus_read_byte(uint8_t b, uint8_t a, uint16_t reg, uint8_t *val) {
    return smbus_rregs(b, a, reg, val, 1);
}

esp_err_t smbus_write_word(uint8_t b, uint8_t a, uint16_t reg, uint16_t val) {
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    return smbus_wregs(b, a, reg, buf, 2);
}

esp_err_t smbus_read_word(uint8_t b, uint8_t a, uint16_t reg, uint16_t *val) {
    uint8_t buf[2] = { 0, 0 };
    esp_err_t err = smbus_rregs(b, a, reg, buf, 2);
    if (!err) *val = buf[0] << 8 | buf[1];
    return err;
}

esp_err_t smbus_regtable(uint8_t b, uint8_t a, smbus_regval_t *p, size_t len) {
    esp_err_t err = p ? ESP_OK : ESP_ERR_INVALID_ARG;
    TickType_t tout;
    uint8_t vo, tmp;
    uint16_t vh, reg, opt;
    for (; !err && len; len--, p++) {
        vo = vh = p->val;
        opt = p->reg >> 16;
        reg = p->reg & 0xFFFF;
        switch (opt) {
        case 0xFF: msleep(vh); break;
        case 0: err = smbus_write_byte(b, a, reg, vo); break;
        case 1: err = smbus_write_word(b, a, reg, vh); break;
        case 2: err = smbus_read_byte(b, a, reg, &vo); p->val = vo; break;
        case 3: err = smbus_read_word(b, a, reg, &vh); p->val = vh; break;
        case 4: err = smbus_clearbits(b, a, reg, vo); break;
        case 5: err = smbus_setbits(b, a, reg, vo); break;
        case 6: err = smbus_toggle(b, a, reg, vo); break;
        case 7: FALLTH; case 8: FALLTH; case 9:
            tout = xTaskGetTickCount() + TIMEOUT(p->val >> 16);
            while (!( err = smbus_read_byte(b, a, reg, &tmp) )) {
                if (opt == 7 && (tmp & vo) == 0) break;
                if (opt == 8 && (tmp & vo) != 0) break;
                if (opt == 9 && (tmp & vo) == vo) break;
                if (xTaskGetTickCount() >= tout) {
                    err = ESP_ERR_TIMEOUT;
                    break;
                }
            }
            break;
        default: ESP_LOGD(TAG, "Unknown opt value: %u", opt);
        }
    }
    return err;
}

esp_err_t smbus_clearbits(uint8_t b, uint8_t a, uint16_t reg, uint8_t mask) {
    uint8_t val = 0;
    esp_err_t err = smbus_read_byte(b, a, reg, &val);
    return err ?: smbus_write_byte(b, a, reg, val & ~mask);
}

esp_err_t smbus_setbits(uint8_t b, uint8_t a, uint16_t reg, uint8_t mask) {
    uint8_t val = 0;
    esp_err_t err = smbus_read_byte(b, a, reg, &val);
    return err ?: smbus_write_byte(b, a, reg, val | mask);
}

esp_err_t smbus_toggle(uint8_t b, uint8_t a, uint16_t reg, uint8_t bit) {
    if (bit > 7) return ESP_ERR_INVALID_ARG;
    uint8_t val = 0, mask = BIT(bit);
    esp_err_t err = smbus_read_byte(b, a, reg, &val);
    if (val & mask) {
        val &= ~mask;
    } else {
        val |= mask;
    }
    return err ?: smbus_write_byte(b, a, reg, val);
}

esp_err_t smbus_dump(uint8_t b, uint8_t a, uint16_t reg, size_t num) {
    esp_err_t err = ESP_ERR_INVALID_ARG;
    uint8_t *buf = NULL, length = 16, wb = SMBUS_IS_WORD(reg) ? 4 : 2;
    if (!num || ( err = EMALLOC(buf, num) )) return err;
    if (( err = smbus_rregs(b, a, reg, buf, num) )) goto exit;
    reg = SMBUS_HI_WORD(reg) << 8 | SMBUS_LO_WORD(reg);
    printf("I2C %d-%02X register table 0x%0*X - 0x%0*X\n%*s",
           b, a, wb, reg, wb, reg + num, wb, "");
    LOOPN(i, length) { printf(" %02X", i); }
    if (reg % length) // pad start spaces
        printf("\n%0*X%*s", wb, reg - (reg % length), 3 * (reg % length), "");
    LOOPN(i, num) {
        if ((i + reg) % length == 0) printf("\n%0*X", wb, i + reg); // newline
        printf(" %02X", buf[i]);
    }
    putchar('\n');
exit:
    TRYFREE(buf);
    return err;
}

uint8_t smbus_detect(uint8_t bus, smbus_device_t *devices, size_t len) {
    esp_err_t err = i2c_probe(bus, 0);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) return 0;
    uint8_t count = 0;
    LOOP(addr, 1, 0x7F) {
        if (i2c_probe(bus, addr)) continue;
        LOOPN(i, len) {
            if (devices[i].addr != addr) continue;
            if (!( err = devices[i].init(bus, addr) )) {
                count++;
                continue;
            }
            ESP_LOGW(TAG, "init I2C%d-%02X failed: %s",
                     bus, addr, esp_err_to_name(err));
        }
    }
    return count;
}

uint8_t i2c_detect(uint8_t bus) {
    esp_err_t err = i2c_probe(bus, 0);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) return 0;
    uint8_t count = 0;
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
        switch (i2c_probe(bus, addr)) {
        case ESP_OK:            printf(" %02X", addr); count++; break;
        case ESP_ERR_TIMEOUT:   printf(" UU"); break;
        default:                printf(" --");
        }
        fflush(stdout);
    }
    putchar('\n');
    return count;
}

/*
 * GPIO Expander
 */

#ifdef CONFIG_BASE_GEXP_I2C
static struct {
    uint8_t bus, addr, data;
} i2c_gexp[3];

static esp_err_t i2c_gexp_init(uint8_t bus, uint8_t addr) {
    LOOPN(i, LEN(i2c_gexp)) {
        if (i2c_gexp[i].addr) continue;
        i2c_gexp[i].bus = bus;
        i2c_gexp[i].addr = addr;
        i2c_gexp[i].data = data;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_SIZE;
}

static esp_err_t i2c_gexp_set_level(gexp_num_t pin, bool level) {
    if (!PIN_IS_I2CEXP(pin)) return ESP_ERR_INVALID_ARG;
    uint8_t num = pin - PIN_I2C_BASE, idx = num >> 3, mask = BIT(num & 0x7);
    if (!i2c_gexp[idx].addr) return ESP_ERR_INVALID_STATE;
    uint8_t addr = i2c_gexp[idx].addr, *datp = &i2c_gexp[idx].data;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return i2c_wtrd(i2c_gexp[idx].bus, addr, datp, 1, NULL, 0);
}

static esp_err_t i2c_gexp_get_level(gexp_num_t pin, bool *level, bool sync) {
    if (!PIN_IS_I2CEXP(pin)) return ESP_ERR_INVALID_ARG;
    uint8_t num = pin - PIN_I2C_BASE, idx = num >> 3, mask = BIT(num & 0x7);
    if (!i2c_gexp[idx].addr) return ESP_ERR_INVALID_STATE;
    uint8_t addr = i2c_gexp[idx].addr, *datp = &i2c_gexp[idx].data;
    esp_err_t err = ESP_OK;
    if (sync) err = i2c_wtrd(i2c_gexp[idx].bus, addr, NULL, 0, datp, 1);
    if (!err && level) *level = *datp & mask;
    return err;
}
#endif

#ifdef CONFIG_BASE_GEXP_SPI
static struct {
    spi_device_handle_t dev;
    spi_transaction_t trans;
    uint8_t *data;
} spi_gexp;

static esp_err_t spi_gexp_set_level(gexp_num_t pin, bool level) {
    if (!spi_gexp.dev) return ESP_ERR_INVALID_STATE;
    if (!PIN_IS_SPIEXP(pin)) return ESP_ERR_INVALID_ARG;
    int num = pin - PIN_SPI_BASE;
    uint8_t idx = num >> 3, mask = BIT(num & 0x7), *datp = spi_gexp.data + idx;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return spi_device_polling_transmit(spi_gexp.dev, &spi_gexp.trans);
}

static esp_err_t spi_gexp_get_level(gexp_num_t pin, bool *level, bool sync) {
    if (!spi_gexp.dev) return ESP_ERR_INVALID_STATE;
    if (!PIN_IS_SPIEXP(pin)) return ESP_ERR_INVALID_ARG;
    int num = pin - PIN_SPI_BASE;
    uint8_t idx = num >> 3, mask = BIT(num & 0x7), *datp = spi_gexp.data + idx;
    esp_err_t err = ESP_OK;
    if (sync) err = spi_device_polling_transmit(spi_gexp.dev, &spi_gexp.trans);
    if (!err) *level = *datp & mask;
    return err;
}
#endif

#ifdef CONFIG_BASE_GPIO_INT
static void gpio_isr(void *arg) {
    gpio_num_t pin = (gpio_num_t)arg;
#   ifdef CONFIG_BASE_USE_TSCN
    tscn_command(gpio_get_level(pin) ? "off" : "on");
#   endif
#   ifdef CONFIG_BASE_GEXP_I2C
    LOOPN(i, LEN(i2c_gexp)) {
        if (i2c_gexp_get_level(PIN_I2C_BASE + i * 8, NULL, true)) continue;
#       ifdef CONFIG_BASE_DEBUG
        ets_printf("I2C GPIOExp: %s\n", format_binary(i2c_gexp[i].data, 8));
#       endif
    }
#   endif
#   ifdef CONFIG_BASE_GEXP_SPI
    if (!spi_gexp_get_level(PIN_SPI_BASE, NULL, true))
    LOOPN(i, PIN_SPI_COUNT / 8) {
#       ifdef CONFIG_BASE_DEBUG
        ets_printf("SPI GPIOExp: %s\n", format_binary(spi_gexp.data[i], 8));
#       endif
    }
#   endif
}
#endif

static void gexp_initialize() {
    esp_err_t err;
#ifdef CONFIG_BASE_GPIO_INT
#   if defined(CONFIG_BASE_BOARD_S3NL191)
    gpio_num_t pin = GPIO_NUM_16;
#   else
    gpio_num_t pin = GPIO_NUMBER(CONFIG_BASE_GPIO_INT);
#   endif
    gpio_config_t int_cfg = {
        .pin_bit_mask = BIT64(pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    if (!strcasecmp(Config.sys.INT_EDGE, "HIGH")) {
        int_cfg.intr_type = GPIO_INTR_HIGH_LEVEL;
        int_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "LOW")) {
        int_cfg.intr_type = GPIO_INTR_LOW_LEVEL;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "POS")) {
        int_cfg.intr_type = GPIO_INTR_POSEDGE;
        int_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (!strcasecmp(Config.sys.INT_EDGE, "NEG")) {
        int_cfg.intr_type = GPIO_INTR_NEGEDGE;
    }
    ESP_ERROR_CHECK( gpio_config(&int_cfg) );
    ESP_ERROR_CHECK( gpio_install_isr_service(0) ); // or ESP_INTR_FLAG_IRAM
    ESP_ERROR_CHECK( gpio_isr_handler_add(pin, gpio_isr, (void *)pin) );
    gpio_usage(pin, "Interrupt");
#endif
#ifdef CONFIG_BASE_GEXP_I2C
    if (PIN_I2C_COUNT <= 0 || PIN_I2C_COUNT > (LEN(i2c_gexp) * 8)) {
        ESP_LOGE(TAG, "Invalid I2C GPIOExp pin count: %d", PIN_I2C_COUNT);
        return;
    }
    smbus_device_t devices[] = {
        { 0x20, i2c_gexp_init },
        { 0x21, i2c_gexp_init },
        { 0x22, i2c_gexp_init },
    };
    uint8_t found = 0;
    LOOPN(bus, I2C_NUM_MAX) {
        found += smbus_detect(bus, devices, LEN(devices));
    }
    if (!found) {
        err = ESP_ERR_NOT_FOUND;
        ESP_LOGE(TAG, "I2C GPIOExp init error: %s", esp_err_to_name(err));
    }
#endif
#ifdef CONFIG_BASE_GEXP_SPI
    // If the transmitted data is 32bits or less, it is preferred to use
    // tx_data in spi_transaction_t. Each expander chip uses 8bits.
    if (PIN_SPI_COUNT <= 0 || PIN_SPI_COUNT % 8) {
        ESP_LOGE(TAG, "Invalid SPI GPIOExp pin count: %d", PIN_SPI_COUNT);
        return;
    }
    if (PIN_SPI_COUNT <= 32) {
        spi_gexp.data = spi_gexp.trans.tx_data;
        spi_gexp.trans.flags = SPI_TRANS_USE_TXDATA;
        spi_gexp.trans.length = PIN_SPI_COUNT;
    } else if (ECALLOC(spi_gexp.data, 1, PIN_SPI_COUNT / 8)) {
        ESP_LOGE(TAG, "Could not allocate memory for SPI GPIOExp");
        return;
    } else {
        spi_gexp.trans.length = PIN_SPI_COUNT;
        spi_gexp.trans.tx_buffer = spi_gexp.data;
    }
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0b10,                       // CPOL = 1, CPHA = 0
        .duty_cycle_pos = 128,              // 128/256 = 50% (Tlow = Thigh)
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = SPI_MASTER_FREQ_8M,
        .input_delay_ns = 0,
        .spics_io_num = GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_GEXP),
        .flags = 0,
        .queue_size = 1,                    // only one transaction allowed
        .pre_cb = NULL,
        .post_cb = NULL
    };
    if (( err = spi_bus_add_device(NUM_SPI, &dev_cfg, &spi_gexp.dev) )) {
        ESP_LOGE(TAG, "SPI GPIOExp init error: %s", esp_err_to_name(err));
    } else {
        gpio_usage(dev_cfg.spics_io_num, "SPI CS GPIOExp");
    }
#endif
    NOTUSED(err);
}

/*
 * GPIO Interrupt (inc. button & knob & tpad)
 */

static const char * usage_table[GPIO_PIN_COUNT] = {
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
    [PIN_TXD]   = "UART TX" STR(CONFIG_BASE_UART_NUM),
    [PIN_RXD]   = "UART RX" STR(CONFIG_BASE_UART_NUM),
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
};

const char * gpio_usage(gpio_num_t pin, const char *usage) {
    if (pin < 0 || pin >= GPIO_PIN_COUNT) return NULL;
    const char *current = usage_table[pin];
    if (!strlen(usage ?: "")) return current;
    if (current && !startswith(current, "Strapping")) {
        ESP_LOGW(TAG, "GPIO %d already used as %s", pin, current);
    } else {
        current = usage_table[pin] = usage;
        ESP_LOGD(TAG, "GPIO %d is used for %s", pin, usage);
    }
    return current;
}

esp_err_t gpio_reconfig(gpio_num_t pin, const char *str) {
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!strlen(str ?: "") || !GPIO_IS_VALID_GPIO(pin)) return err;
    char *dup = strupr(strdup(str)), *save = NULL;
    for (str = strtok_r(dup, ",", &save); str; str = strtok_r(0, "|", &save)) {
        if (startswith(str, "MODE:")) {
            gpio_mode_t mode = GPIO_MODE_DISABLE;
            if (strchr(str + 5, 'I')) mode |= GPIO_MODE_INPUT;
            if (strchr(str + 5, 'O')) mode |= GPIO_MODE_OUTPUT;
            err = gpio_set_direction(pin, mode);
        } else if (startswith(str, "PULL:")) {
            if (strchr(str + 5, 'U')) {
                err = gpio_pullup_en(pin);
            } else {
                err = gpio_pullup_dis(pin);
            }
            if (strchr(str + 5, 'D')) {
                err = gpio_pulldown_en(pin);
            } else {
                err = gpio_pulldown_dis(pin);
            }
        } else if (startswith(str, "DRV:")) {
            uint8_t val;
            if (parse_u8(str + 4, &val) && val < GPIO_DRIVE_CAP_MAX)
                err = gpio_set_drive_capability(pin, (gpio_drive_cap_t)val);
        }
        if (err) break;
    }
    TRYFREE(dup);
    return err;
}

#ifdef CONFIG_BASE_USE_BTN
static UNUSED button_handle_t btn[2];

static UNUSED void cb_button(void *arg, void *data) {
    static const char *T = "button";
    int pin = (int)data, val = (iot_button_get_event(arg) << 8) | pin;
    scn_command(SCN_BTN, &val);
    switch (val >> 8) {
    case BUTTON_PRESS_DOWN:
        ESP_LOGD(T, "%d press", pin);
#   if defined(CONFIG_BASE_BTN_INPUT) && !defined(CONFIG_BASE_BOARD_S3NL191)
        if (pin == PIN_BTN) hid_report_sdial(HID_TARGET_ALL, SDIAL_D);
#   endif
        break;
    case BUTTON_PRESS_UP:
        ESP_LOGD(T, "%d release[%" PRIu32 "]", pin, iot_button_get_ticks_time(arg));
#   if defined(CONFIG_BASE_BTN_INPUT) && !defined(CONFIG_BASE_BOARD_S3NL191)
        if (pin == PIN_BTN) hid_report_sdial(HID_TARGET_ALL, SDIAL_U);
#   endif
        break;
    case BUTTON_SINGLE_CLICK:
        ESP_LOGD(T, "%d single click", pin); break;
    case BUTTON_DOUBLE_CLICK:
        ESP_LOGD(T, "%d double click", pin); break;
    case BUTTON_MULTIPLE_CLICK:
        ESP_LOGD(T, "%d click %d times", pin, iot_button_get_repeat(arg)); break;
    case BUTTON_LONG_PRESS_HOLD:
        ESP_LOGD(T, "%d long press %d", pin, iot_button_get_long_press_hold_cnt(arg));
        break;
    }
}

static UNUSED button_handle_t button_init(
    button_config_t *cfg, gpio_num_t pin, button_cb_t cb
) {
    button_handle_t hdl = iot_button_create(cfg);
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

#   ifdef CONFIG_BASE_USE_KNOB
static knob_handle_t knob;
static const char *KTAG = "knob";

static void cb_knob(void *arg, void *data) {
    switch (iot_knob_get_event(arg)) {
    case KNOB_LEFT:
        ESP_LOGD(KTAG, "left rotate %d", iot_knob_get_count_value(arg));
        hid_report_sdial(HID_TARGET_ALL, SDIAL_L);
        break;
    case KNOB_RIGHT:
        ESP_LOGD(KTAG, "right rotate %d", iot_knob_get_count_value(arg));
        hid_report_sdial(HID_TARGET_ALL, SDIAL_R);
        break;
    default: break;
    }
}
#   endif
#endif // CONFIG_BASE_USE_BTN

static void gpio_initialize() {
#ifdef CONFIG_BASE_USE_BTN
#   ifdef CONFIG_BASE_BTN_INPUT
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = PIN_BTN,
            .active_level = strtob(Config.sys.BTN_HIGH),
        }
    };
    btn[0] = button_init(&btn_cfg, PIN_BTN, cb_button);
#   endif
#   ifdef CONFIG_BASE_BTN_GPIO0
    const char *usage = gpio_usage(GPIO_NUM_0, NULL);
    if (!usage || startswith(usage, "Strapping")) {
        button_config_t io0_cfg = {
            .type = BUTTON_TYPE_GPIO,
            .gpio_button_config = { .gpio_num = GPIO_NUM_0 },
        };
        btn[1] = button_init(&io0_cfg, GPIO_NUM_0, cb_button);
        if (btn[1] && !usage) {
            gpio_usage(GPIO_NUM_0, "Button");
        } else if (btn[1]) {
            static char desc[32];
            snprintf(desc, sizeof(desc), "%s (Button)", usage);
            gpio_usage(GPIO_NUM_0, desc);
        }
    }
#   endif
#   ifdef CONFIG_BASE_ADC_JOYSTICK
    button_config_t adc_cfg = {
        .type = BUTTON_TYPE_ADC,
#       ifndef IDF_TARGET_V4
        .adc_button_config = { .adc_handle = adc.hdl },
#       endif
    };
    LOOPN(i, LEN(adc.chans)) {
        if (adc.chans[i] == -1) continue;
        adc_cfg.adc_button_config.adc_channel = adc.chans[i];
        adc_cfg.adc_button_config.button_index = 0;
        adc_cfg.adc_button_config.min = 0;
        adc_cfg.adc_button_config.max = 1400; // 0.0-1.4V
        jstk[2 * i + 0] = button_init(&adc_cfg, adc.pins[i], cb_joystick);
        adc_cfg.adc_button_config.button_index = 1;
        adc_cfg.adc_button_config.min = 1900;
        adc_cfg.adc_button_config.max = 3300; // 1.9-3.3V
        jstk[2 * i + 1] = button_init(&adc_cfg, adc.pins[i], cb_joystick);
    }
#   endif
#   ifdef CONFIG_BASE_USE_KNOB
    knob_event_t events[] = { KNOB_LEFT, KNOB_RIGHT };
    knob_config_t knob_cfg = {
        .default_direction = 0,     // 0:positive; 1:negative
        .gpio_encoder_a = PIN_ENCA,
        .gpio_encoder_b = PIN_ENCB
    };
    if (!( knob = iot_knob_create(&knob_cfg) )) {
        ESP_LOGE(KTAG, "bind to GPIO%d & %d failed", PIN_ENCA, PIN_ENCB);
    } else ITERV(event, events) {
        iot_knob_register_cb(knob, event, cb_knob, NULL);
    }
#   endif
#endif
}

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)  // backport from v5.5
#   include "hal/gpio_ll.h"

typedef struct {
    uint32_t fun_sel, sig_out;
    gpio_drive_cap_t drv;
    bool pu, pd, ie, oe, oe_ctrl_by_periph, oe_inv, od, slp_sel;
} gpio_io_config_t;

esp_err_t gpio_get_io_config(gpio_num_t pin, gpio_io_config_t *cfg) {
    if (!GPIO_IS_VALID_GPIO(pin)) return ESP_ERR_INVALID_ARG;
#   ifdef IDF_TARGET_V4
    memset(cfg, 0, sizeof(gpio_io_config_t));
    return gpio_get_drive_capability(pin, &cfg->drv);
#   else
    uint32_t drv;
    gpio_ll_get_io_config(GPIO_LL_GET_HW(GPIO_PORT_0), pin,
        &cfg->pu, &cfg->pd, &cfg->ie, &cfg->oe,
        &cfg->oe_ctrl_by_periph, &cfg->oe_inv, &cfg->od,
        &drv, &cfg->fun_sel, &cfg->sig_out, &cfg->slp_sel);
    cfg->drv = drv;
    return ESP_OK;
#   endif
}
#endif

esp_err_t gexp_set_level(int pin, bool level) {
    if (GPIO_IS_VALID_GPIO(pin)) {
#ifdef IDF_TARGET_V4
        esp_err_t err = gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
#else
        gpio_io_config_t cfg;
        esp_err_t err = gpio_get_io_config(pin, &cfg);
        if (!err) err = cfg.oe ? ESP_OK : ESP_ERR_INVALID_STATE;
#endif
        return err ?: gpio_set_level(pin, level);
    }
#ifdef CONFIG_BASE_GEXP_I2C
    if (PIN_IS_I2CEXP(pin))      return i2c_gexp_set_level(pin, level);
#endif
#ifdef CONFIG_BASE_GEXP_SPI
    if (PIN_IS_SPIEXP(pin))      return spi_gexp_set_level(pin, level);
#endif
    return ESP_ERR_INVALID_ARG;
}

esp_err_t gexp_get_level(int pin, bool *level, bool sync) {
    if (GPIO_IS_VALID_GPIO(pin)) {
        *level = gpio_get_level(pin);
        return ESP_OK;
    }
#ifdef CONFIG_BASE_GEXP_I2C
    if (PIN_IS_I2CEXP(pin)) return i2c_gexp_get_level(pin, level, sync);
#endif
#ifdef CONFIG_BASE_GEXP_SPI
    if (PIN_IS_SPIEXP(pin)) return spi_gexp_get_level(pin, level, sync);
#endif
    return ESP_ERR_INVALID_ARG;
}

void gexp_table(bool i2c, bool spi) {
    gpio_io_config_t cfg;
    const char *value, *usage;
#ifdef IDF_TARGET_V4
    printf("Native GPIO %d-%d\nPIN Value Usage\n", 0, GPIO_PIN_COUNT - 1);
#else
    printf("Native GPIO %d-%d\n"
           "> I=Input, O|o|C|d=Output|OutInv|SigCtrl|OpenDrain\n"
           "> U|D|B=PullUp|Dn|Both, R=RTC, M=MUX, S=Sleep, 0-3=Strength\n"
           "\nPIN Value [ Flags ] Usage\n", 0, GPIO_PIN_COUNT - 1);
#endif
    LOOPN(pin, GPIO_PIN_COUNT) {
        if (gpio_get_io_config(pin, &cfg)) continue;
        value = gpio_get_level(pin) ? "HIGH" : "LOW";
        usage = gpio_usage(pin, NULL) ?: "";
#ifdef IDF_TARGET_V4
        printf("%-3d %5s %s\n", pin, value, usage);
#else
        int rtc = rtc_io_number_get(pin);
#   if SOC_RTCIO_PIN_COUNT > 0 && !SOC_GPIO_SUPPORT_RTC_INDEPENDENT
        if (rtc != -1) {
            cfg.pu = rtcio_hal_is_pullup_enabled(rtc);
            cfg.pd = rtcio_hal_is_pulldown_enabled(rtc);
            cfg.drv = rtcio_hal_get_drive_capability(rtc);
        }
#   endif
        bool sig = cfg.sig_out != SIG_GPIO_OUT_IDX && cfg.oe_ctrl_by_periph;
        bool mux = cfg.fun_sel != PIN_FUNC_GPIO;
        bool inv = !mux && cfg.oe_inv;
        printf(
            "%-3d %5s [%c%c%c%c%c%c%d] %s\n",
            pin, value, cfg.ie ? 'I' : ' ',
            sig ? 'C' : !cfg.oe ? ' ' : cfg.od ? 'd' : inv ? 'o' : 'O',
            cfg.pu && cfg.pd ? 'B' : cfg.pu ? 'U' : cfg.pd ? 'D' : ' ',
            rtc ? 'R' : ' ', mux ? 'M' : ' ', cfg.slp_sel ? 'S' : ' ',
            cfg.drv, usage);
#endif
    }
#ifdef CONFIG_BASE_GEXP_I2C
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
#ifdef CONFIG_BASE_GEXP_SPI
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

/*
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
        "gpio", "pp", "phy_init",
#ifdef CONFIG_BASE_USE_LED
        "led_indicator",
#endif
#ifdef CONFIG_BASE_USE_BTN
        "adc button", "knob",
#endif
#ifdef CONFIG_BASE_USE_CAM
        "cam_hal", "camera", "s3 ll_cam",
#endif
    };
    ITERV(tag, tags) { esp_log_level_set(tag, ESP_LOG_WARN); }

#ifdef CONFIG_BASE_BOARD_S3ECAM
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_8, false);
    gpio_usage(GPIO_NUM_8, "CAM !EN");
#endif

    uart_initialize();
    twdt_initialize();
    pwm_initialize();
#ifdef CONFIG_BASE_USE_ADC
    adc_initialize();
#endif
#ifdef CONFIG_BASE_USE_DAC
    dac_initialize();
#endif
#ifdef CONFIG_BASE_USE_TPAD
    tpad_initialize();
#endif
#ifdef CONFIG_BASE_USE_SPI
    spi_initialize();
#endif
#ifdef CONFIG_BASE_USE_I2C
    i2c_initialize();
#endif
    gpio_initialize();
#ifdef CONFIG_BASE_USE_LED
    led_initialize();
#endif
    avc_initialize();       // MIC and CAM
    scn_initialize();       // LCD display
    gexp_initialize();      // GPIO Expander
}
