/*
 * File: avcmode.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2025/5/15 19:11:55
 */

#include "avcmode.h"
#include "drivers.h"
#include "timesync.h"           // for format_timestamp

#include "cJSON.h"
#include "esp_camera.h"

ESP_EVENT_DEFINE_BASE(AVC_EVENT);

static UNUSED const char *TAG = "AVCMode";

static UNUSED bool audio_run, video_run;
static UNUSED esp_event_handler_instance_t aud_shdl, vid_shdl;

// I2S PDM Microphone

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
    I2S_RELEASE();
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
    I2S_RELEASE();
}
#endif // TARGET_IDF_5

static void aud_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    static char eqls[80 - 4 - 3 - 13];
    audio_evt_t *evt = *(audio_evt_t **)data;
    uint32_t eid = evt->id;
    FILE *stream = arg;
    if (id == AUD_EVENT_START) {
        memset(eqls, '=', sizeof(eqls) - 1);
        eqls[sizeof(eqls) - 1] = '\0';
    } else if (id == AUD_EVENT_STOP) {
        fputc('\n', stream);
        fflush(stream);
    }
    if (id != AUD_EVENT_DATA || eid % 10 || !evt->mode->nch) return;
    uint16_t nch = evt->mode->nch, tlen = (sizeof(eqls) - (nch - 1) * 6) / nch;
    uint64_t vmax = BIT(evt->mode->depth * 8 - 1);
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
    uint32_t dlen = WAV.Bps * MIN(UINT32_MAX / WAV.Bps, (uint32_t)arg / 1000);
    uint32_t rlen, blen = WAV.Bps / 50; // 20ms buffer
    void *data = malloc(2 * blen), *task = xTaskGetCurrentTaskHandle();
    if (!data) return vTaskDelay(NULL);
    WAV.filelen = (WAV.datalen = dlen) + sizeof(WAV) - 8;  // < U32_MAX
    audio_evt_t wav = { .task = task, .data = &WAV, .len = sizeof(WAV) };
    audio_evt_t evt = { .task = task, .data = data, .mode = &mode };
    AVC_POST(AUD_EVENT_START, wav, -1);

    I2S_ACQUIRE();
    for (evt.id = 0; audio_run && dlen; evt.id++) {
        if (I2S_READ(evt.data + blen, blen, &rlen, TIMEOUT(25)) || !rlen) break;
        dlen -= (evt.len = MIN(rlen, dlen));
        if (!notify_wait_for(0, 500, 0)) continue;
        memcpy(evt.data, evt.data + blen, evt.len);
        AVC_POST(AUD_EVENT_DATA, evt, 10);
    }
    I2S_RELEASE();

    notify_wait_for(0, 500, 5);
    wav.len = 0; wav.data = NULL;
    AVC_POST(AUD_EVENT_STOP, wav, -1);
    notify_wait_for(0, 50, 10);
    UREGEVTS(AVC, aud_shdl);
    TRYFREE(evt.data);
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
 
// SCCB / SMBus Camera

#ifdef CONFIG_BASE_USE_CAM
static sensor_t *cam;
#   define CAM_TMR          LEDC_TIMER_3
#   define CAM_CH           LEDC_CHANNEL_4
#   define CAM_ACQUIRE()    ( cam->set_reg(cam, 0x3008, 0x40, 0) ) // standby
#   define CAM_RELEASE()    ( cam->set_reg(cam, 0x3008, 0x40, 0x40) )
#   define CAM_WIDTH        ( resolution[cam->status.framesize].width )
#   define CAM_HEIGHT       ( resolution[cam->status.framesize].height )

static void cam_initialize() {
    esp_err_t err = ESP_OK;
    const char *pin_names[14] = {
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
        } else if (gpio_usage(pins[i], pin_names[i]) != pin_names[i]) {
            ESP_LOGE(TAG, "Invalid %s pin: %d", pin_names[i], pins[i]);
            err = ESP_ERR_INVALID_ARG;
        }
    }
    if (err) return;
    camera_config_t conf = {
        .pin_sccb_sda = -1,   .pin_sccb_scl = -1,   .sccb_i2c_port = NUM_I2C,
        .pin_pwdn = pins[0],  .pin_reset = pins[1], .pin_xclk = pins[2],
        .pin_vsync = pins[3], .pin_href = pins[4],  .pin_pclk = pins[5],
        .pin_d7 = pins[6],    .pin_d6 = pins[7],    .pin_d5 = pins[8],
        .pin_d4 = pins[9],    .pin_d3 = pins[10],   .pin_d2 = pins[11],
        .pin_d1 = pins[12],   .pin_d0 = pins[13],
        .xclk_freq_hz = 20e6,
        .ledc_timer   = CAM_TMR,
        .ledc_channel = CAM_CH,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QSXGA, // init as large buffer as possible
        .jpeg_quality = 10,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };
    if (( err = esp_camera_init(&conf) )) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return;
    }
    cam = esp_camera_sensor_get();
    if (esp_camera_load_from_nvs("camera")) {
        camera_sensor_info_t *info = esp_camera_sensor_get_info(&cam->id);
        if (info->model == CAMERA_OV3660) {
            cam->set_brightness(cam, 1);
            cam->set_saturation(cam, -2);
        }
        cam->set_framesize(cam, info->max_size);
        esp_camera_save_to_nvs("camera");
    }
    CAM_RELEASE();
}

static_assert(sizeof(sensor_t) <= UINT8_MAX, "offset overflow: use uint16_t");

static struct {
    const char *key;
    uint8_t coff, voff, vsize;
} cam_attrs[] = {
#   define ATTR(s, a, b)                                                    \
    { #a, offsetof(s, set_##a), offsetof(s, b), SIZEOF(s, b) }
#   define X(a)    ATTR(sensor_t, a, a)
#   define Y(a)    ATTR(sensor_t, a, status.a)
#   define Z(a, b) ATTR(sensor_t, a, status.b)
    X(pixformat), Y(framesize), Y(contrast), Y(brightness), Y(saturation),
    Y(sharpness), Y(denoise), Y(gainceiling), Y(quality), Y(colorbar),
    Z(whitebal, awb), Z(gain_ctrl, agc), Z(exposure_ctrl, aec),
    Y(hmirror), Y(vflip), Y(aec2), Y(awb_gain), Y(agc_gain), Y(aec_value),
    Y(special_effect), Y(wb_mode), Y(ae_level),
    Y(dcw), Y(bpc), Y(wpc), Y(raw_gma), Y(lenc),
#   undef ATTR
#   undef X
#   undef Y
#   undef Z
};

static int cam_get(uint8_t idx) {
    if (!cam || idx >= LEN(cam_attrs)) return 0;
    void *ptr = (void *)cam + cam_attrs[idx].voff;
    switch (cam_attrs[idx].vsize) {
    case 4: return *(uint32_t *)ptr;
    case 2: return *(uint16_t *)ptr;
    case 1:
        // hotfix for esp32-camera v2.0.15 bug
        // should have use:
        //      uint16_t gainceiling = smbus_read_word(0x3A18) & 0x3FF
        // instead of:
        //      uint8_t gainceiling
        if (!strcmp(cam_attrs[idx].key, "gainceiling")) return *(uint8_t *)ptr;
        return *(int8_t *)ptr;
    default: return 0;
    }
}

static esp_err_t cam_set(uint8_t idx, int val) {
    if (!cam || idx >= LEN(cam_attrs)) return ESP_ERR_INVALID_ARG;
    if (cam_get(idx) == val) return ESP_OK;
    void **cptr = (void *)cam + cam_attrs[idx].coff;
    int (*cb)(sensor_t *, int) = *cptr;
    return cb(cam, val);
}

static float cam_fps(int *val) {
    if (!cam) return val ? ESP_ERR_INVALID_STATE : 0;
    int hts = cam->get_reg(cam, 0x380C, 0xFFFF);
    int vts = cam->get_reg(cam, 0x380E, 0xFFFF);
    float clk = 1.25 * cam->xclk_freq_hz;
    if (!val) return clk / hts / vts;
    int tgt = *val ? CONS(clk / hts / *val, CAM_HEIGHT, 0xFFFF) : vts;
    return tgt == vts ? 0 : cam->set_reg(cam, 0x380E, tgt, 0xFFFF);
}

static esp_err_t cam_loads(const char *json) {
    if (!cam) return ESP_ERR_INVALID_STATE;
    cJSON *obj = cJSON_Parse(json);
    if (!obj) {
        ESP_LOGE(TAG, "Failed to load config from `%s`", json);
        return ESP_ERR_INVALID_ARG;
    }
    int err = ESP_OK, value, stdby = !xTaskGetHandle("video");
    if (stdby) CAM_ACQUIRE();
    for (cJSON *ptr = obj->child; ptr && !err; ptr = ptr->next) {
        if (!ptr->string) continue;
        if (cJSON_IsNumber(ptr)) {
            value = ptr->valuedouble;
        } else if (!parse_int(cJSON_GetStringValue(ptr), &value)) continue;
        if (!strcmp(ptr->string, "xclk")) {
            if (value > 240) value /= 1e6;
            if (!value) continue;
            err = cam->set_xclk(cam, CAM_TMR, value); // in MHz
        } else if (!strcmp(ptr->string, "framerate")) {
            err = cam_fps(&value);
        } else LOOPN(i, LEN(cam_attrs)) {
            if (strcmp(ptr->string, cam_attrs[i].key)) continue;
            err = cam_set(i, value);
            break;
        }
    }
    if (stdby) CAM_RELEASE();
    cJSON_Delete(obj);
    return err ?: esp_camera_save_to_nvs("camera");
}

static char * cam_dumps(FILE *stream) {
    if (!cam) return NULL;
    if (stream) {
        size_t klen = strlen("framerate");
#   ifdef CONFIG_BASE_AUTO_ALIGN
        ITERP(attr, cam_attrs) { klen = MAX(klen, strlen(attr->key)); }
#   endif
        LOOPN(i, LEN(cam_attrs)) {
            fprintf(stream, "%*s: %d\n", klen, cam_attrs[i].key, cam_get(i));
        }
        fprintf(stream, "%*s: %d\n", klen, "xclk", cam->xclk_freq_hz);
        fprintf(stream, "%*s: %d\n", klen, "width", CAM_WIDTH);
        fprintf(stream, "%*s: %d\n", klen, "height", CAM_HEIGHT);
        fprintf(stream, "%*s: %.3f\n", klen, "framerate", cam_fps(NULL));
        fflush(stream);
        return NULL;
    }
    cJSON *obj = cJSON_CreateObject();
    LOOPN(i, LEN(cam_attrs)) {
        cJSON_AddNumberToObject(obj, cam_attrs[i].key, cam_get(i));
    }
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

static void vid_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    static TickType_t ts;
    video_evt_t *evt = *(video_evt_t **)data;
    uint32_t eid = evt->id;
    notify_increase(evt->task);
    TickType_t dt = xTaskGetTickCount() - ts; ts += dt;
    if (id == VID_EVENT_STOP) {
        fputc('\n', arg);
    } else if (id == VID_EVENT_DATA && (eid % evt->mode->fps) == 0) {
        float fps = dt ? 1e3 / pdTICKS_TO_MS(dt) : 0;
        fprintf(arg, "\r%s %08d %dx%dx%d %dFPS %.4s %d Bytes %.*fFPS\n",
                format_timestamp_us(0),
                eid, evt->mode->width, evt->mode->height,
                evt->mode->depth, evt->mode->fps,
                evt->mode->fourcc, evt->len,
                fps >= 10 ? 1 : 2, fps);
    }
    fflush(arg);
    notify_decrease(evt->task);
}

static void video_capture(void *arg) {
    if (!cam) return;
    float fps = cam_fps(NULL);
    video_mode_t mode = { (int)fps, CAM_WIDTH, CAM_HEIGHT, 3, "MJPG" };
    uint32_t BPF = mode.width * mode.height * mode.depth / 10; // BytePerFrame
    uint32_t nframe = fps * MIN(UINT32_MAX / fps, (uint32_t)arg / 1000.0);
    camera_fb_t *next = NULL, *prev = NULL;
    avi_header_t AVI = {
        "RIFF", -1,
        "AVI ",
        "LIST", AVI_HEADER_HDLR_LEN, "hdlr",
            "avih", AVI_HEADER_AVIH_LEN,
                1000000 / mode.fps, mode.fps * BPF, 0, 0x910, nframe, 0, 1,
                0x100000, mode.width, mode.height, { 0, 0, 0, 0 },
        "LIST", AVI_HEADER_STRL_LEN, "strl",
            "strh", AVI_HEADER_STRH_LEN,
                "vids", "MJPG", 0, 0, 0, 0, 1, mode.fps, 0, nframe,
                BPF, -1, 0, 0, 0, mode.width, mode.height,
            "strf", AVI_HEADER_STRF_LEN,
                AVI_HEADER_STRF_LEN, mode.width, mode.height, 1,
                mode.depth * 8, "MJPG", BPF, 0, 0, 0, 0,
        "LIST", -1, "movi",
    };
    void *task = xTaskGetCurrentTaskHandle();
    video_evt_t avi = { .task = task, .data = &AVI, .len = sizeof(AVI) };
    video_evt_t evt = { .task = task, .data = NULL, .mode = &mode };
    AVC_POST(VID_EVENT_START, avi, -1);

    CAM_ACQUIRE();
    for (evt.id = 0; video_run && evt.id < nframe; evt.id++) {
        if (!( next = esp_camera_fb_get() )) break;
        if (!notify_wait_for(0, 500, 0)) {
            ESP_LOGD(TAG, "%08u: frame not released", evt.id);
            esp_camera_fb_return(next);
            continue;
        }
        if (prev && prev->format != PIXFORMAT_JPEG) TRYFREE(evt.data);
        if (next->format == PIXFORMAT_JPEG) {
            evt.data = next->buf;
            evt.len = next->len;
        } else if (!frame2jpg(next, 80, (uint8_t **)&evt.data, &evt.len)) {
            ESP_LOGE(TAG, "%08u: JPEG compression failed", evt.id);
            esp_camera_fb_return(next);
            break;
        }
        AVC_POST(VID_EVENT_DATA, evt, 10);
        TRYNULL(prev, esp_camera_fb_return);
        prev = next;
    }
    CAM_RELEASE();

    notify_wait_for(0, 500, 5);
    avi.len = 0; avi.data = NULL;
    AVC_POST(VID_EVENT_STOP, avi, -1);
    notify_wait_for(0, 50, 10);
    UREGEVTS(AVC, vid_shdl);
    if (prev && prev->format != PIXFORMAT_JPEG) TRYFREE(evt.data);
    TRYNULL(prev, esp_camera_fb_return);
    vTaskDelete(NULL);
}
#   undef CAM_TMR
#   undef CAM_CH
#   undef CAM_ACQUIRE
#   undef CAM_RELEASE
#   undef CAM_WIDTH
#   undef CAM_HEIGHT
#endif // CONFIG_BASE_USE_CAM

// Async API

esp_err_t avc_command(
    const char *ctrl, int targets, uint32_t tout_ms, FILE *stream
) {
    if (!targets) targets = AUDIO_TARGET | VIDEO_TARGET;
    bool atgt = targets & AUDIO_TARGET, vtgt = targets & VIDEO_TARGET;
    TaskHandle_t atask = xTaskGetHandle("audio");
    TaskHandle_t vtask = xTaskGetHandle("video");
    if (targets & IMAGE_TARGET) {
#ifdef CONFIG_BASE_USE_CAM
        if (targets & ACTION_WRITE)
            return ctrl ? cam_loads(ctrl) : ESP_ERR_INVALID_ARG;
        if (targets & ACTION_READ) {
            if (ctrl) return !( *(char **)ctrl = cam_dumps(NULL) );
            if (stream) {
                cam_dumps(stream);
                return ESP_OK;
            }
        }
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    } else if (ctrl) {
        if (!strbool(ctrl)) {
            if (atgt) audio_run = false;
            if (vtgt) video_run = false;
            for (int ms = 10; ms && (atgt || vtgt); ms--) {
                if (atgt && !xTaskGetHandle("audio")) atgt = false;
                if (vtgt && !xTaskGetHandle("video")) vtgt = false;
                msleep(ms);
            }
        } else {
            UNUSED void *arg = (void *)(tout_ms ?: UINT32_MAX);
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
                xTaskCreate(video_capture, "video", 4096, arg, 20, &vtask);
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
        if (atgt) printf("Audio Capture: %s\n", atask ? "on" : "off");
        if (vtgt) printf("Video Capture: %s\n", vtask ? "on" : "off");
    }
    return ESP_OK;
}

// Sync API

void avc_initialize() {
#ifdef CONFIG_BASE_USE_I2S
    i2s_initialize();
#endif
#ifdef CONFIG_BASE_USE_CAM
    cam_initialize();
#endif
}
