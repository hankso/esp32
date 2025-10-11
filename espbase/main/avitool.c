/*
 * File: avitool.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2025/5/15 19:11:55
 */

#include "avitool.h"
#include "drivers.h"
#include "timesync.h"           // for format_timestamp

#include "cJSON.h"

ESP_EVENT_DEFINE_BASE(AVC_EVENT);

#ifndef CONFIG_BASE_USE_AVC
void avi_initialize() {}
esp_err_t avi_command(int t, const char *c, uint32_t o, void *p) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(t); NOTUSED(c); NOTUSED(o); NOTUSED(p);
}
#else // CONFIG_BASE_USE_AVC

static const char *TAG = "AVITool";

static UNUSED struct {
    bool run;
    TaskHandle_t task;
    esp_event_handler_instance_t shdl;
} audio, video;

// I2S PDM Microphone

#ifdef CONFIG_BASE_USE_I2S

#ifdef IDF_TARGET_V4
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
    i2s_config_t i2s_cfg = {
        .mode                   = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM,
        .sample_rate            = PDM_SHZ,
        .bits_per_sample        = PDM_BPC * 8,
        .channel_format         = PDM_FCH,
        .communication_format   = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count          = 8,
        .dma_buf_len            = 128,
    };
    i2s_pin_config_t pin_cfg = {
        .mck_io_num     = I2S_PIN_NO_CHANGE,
        .bck_io_num     = I2S_PIN_NO_CHANGE,
        .ws_io_num      = PIN_CLK,
        .data_out_num   = I2S_PIN_NO_CHANGE,
        .data_in_num    = PIN_DAT,
    };
    esp_err_t err = i2s_driver_install(NUM_I2S, &i2s_cfg, 0, NULL);
    if (!err) err = i2s_set_pin(NUM_I2S, &pin_cfg);
    if (!err) err = i2s_stop(NUM_I2S);
    if (!err) {
        gpio_usage(PIN_CLK, "I2S CLK");
        gpio_usage(PIN_DAT, "I2S DAT");
        I2S_ACQUIRE();
        I2S_RELEASE();
    } else {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
    }
}
#else // IDF_TARGET_V5
static i2s_chan_handle_t i2s_handle;
#   define I2S_ACQUIRE()    i2s_channel_enable(i2s_handle)
#   define I2S_RELEASE()    i2s_channel_disable(i2s_handle)
#   define I2S_READ(...)    i2s_channel_read(i2s_handle, __VA_ARGS__)
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
    i2s_chan_config_t chan_cfg = DEFAULT(CHANNEL, NUM_I2S, I2S_ROLE_MASTER);
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = DEFAULT(PDM_RX_CLK, PDM_SHZ),
        .slot_cfg = DEFAULT(PDM_RX_SLOT, PDM_BPC * 8, PDM_NCH),
        .gpio_cfg = { .clk = PIN_CLK, .din = PIN_DAT },
    };
#   undef DEFAULT
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &i2s_handle);
    if (!err) err = i2s_channel_init_pdm_rx_mode(i2s_handle, &pdm_cfg);
    if (!err) {
        gpio_usage(PIN_CLK, "I2S CLK");
        gpio_usage(PIN_DAT, "I2S DAT");
        I2S_ACQUIRE();
        I2S_RELEASE();
    } else {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
    }
}
#endif // IDF_TARGET_V4

static void aud_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    static char eqls[80 - 4 - 3 - 13];
    audio_data_t *pkt = *(audio_data_t **)data;
    FILE *stream = arg;
    if (id == AUD_EVENT_START) {
        memset(eqls, '=', sizeof(eqls) - 1);
        eqls[sizeof(eqls) - 1] = '\0';
    } else if (id == AUD_EVENT_STOP) {
        fputc('\n', stream);
        fflush(stream);
    }
    if (id != AUD_EVENT_DATA || pkt->id % 10 || !pkt->mode->nch) return;
    uint16_t nch = pkt->mode->nch, tlen = (sizeof(eqls) - (nch - 1) * 6) / nch;
    uint64_t vmax = BIT(pkt->mode->depth * 8 - 1);
    PDM_TYPE *buf = pkt->data, vol[nch], len[nch];
    LOOPN(i, pkt->len / nch / pkt->mode->depth) {
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

static void aud_capture(void *arg) {
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
    size_t rlen, blen = WAV.Bps / 50; // 20ms buffer
    void *data = malloc(2 * blen);
    if (!data) {
        audio.task = NULL;
        return;
    }
    WAV.filelen = (WAV.datalen = dlen) + sizeof(WAV) - 8;  // < U32_MAX
    audio_data_t wav = { .task = audio.task, .data = &WAV, .len = sizeof(WAV) };
    audio_data_t pkt = { .task = audio.task, .data = data, .mode = &mode };
    AVC_POST(AUD_EVENT_START, wav, -1);

    I2S_ACQUIRE();
    for (pkt.id = 0; audio.run && dlen; pkt.id++) {
        if (I2S_READ(pkt.data + blen, blen, &rlen, TIMEOUT(25)) || !rlen) break;
        dlen -= (pkt.len = MIN(rlen, dlen));
        if (!notify_released(500, 0)) continue;
        memcpy(pkt.data, pkt.data + blen, pkt.len);
        AVC_POST(AUD_EVENT_DATA, pkt, 10);
    }
    I2S_RELEASE();

    notify_released(500, 5);
    wav.len = 0; wav.data = NULL;
    AVC_POST(AUD_EVENT_STOP, wav, -1);
    notify_released(50, 10);
    UREGEVTS(AVC, audio.shdl);
    TRYFREE(pkt.data);
    audio.task = NULL;
    vTaskDelete(NULL);
}
#endif // CONFIG_BASE_USE_I2S
 
// SCCB / SMBus Camera

#ifdef CONFIG_BASE_USE_CAM
#   define CAM_ACQUIRE(c)   ( (c)->set_reg((c), 0x3008, 0x40, 0) ) // standby
#   define CAM_RELEASE(c)   ( (c)->set_reg((c), 0x3008, 0x40, 0x40) )
#   define CAM_HORRES(c)    ( resolution[(c)->status.framesize].width )
#   define CAM_VERRES(c)    ( resolution[(c)->status.framesize].height )
#   include "esp_camera.h"
#   ifdef CONFIG_BASE_USE_PSRAM
#       include "esp_psram.h"
#   endif

static camera_config_t cam_cfg = {
    .pin_sccb_sda = GPIO_NUM_NC,
    .pin_sccb_scl = GPIO_NUM_NC,
    .sccb_i2c_port = NUM_I2C,                   // see drivers.c
    .xclk_freq_hz = 20e6,                       // 20MHz
    .ledc_timer   = LEDC_TIMER_3,
    .ledc_channel = LEDC_CHANNEL_4,             // see drivers.c
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_SVGA,             // 800 x 600 / 5 ~= 90KB
    .jpeg_quality = 20,
    .fb_count     = 1,
    .fb_location  = CAMERA_FB_IN_DRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
};

static void cam_initialize() {
    esp_err_t err = ESP_OK;
    const char *names[] = {
        "CAM PWDN", "CAM RESET", "CAM VSYNC",
        "CAM HREF", "CAM XCLK", "CAM PCLK",
        "CAM D7", "CAM D6", "CAM D5", "CAM D4",
        "CAM D3", "CAM D2", "CAM D1", "CAM D0",
    };
    int pins[LEN(names)];
#   ifdef CONFIG_BASE_CAM_CUSTOM_PINS
    const char *str = CONFIG_BASE_CAM_CUSTOM_PINS;
#   else
    const char *str = CONFIG_BASE_CAM_PINS;
#   endif
    if (parse_pin(str, pins, LEN(pins), names) != LEN(pins)) {
        ESP_LOGE(TAG, "Invalid pins `%s` for CAMERA", str);
        return;
    }
    cam_cfg.pin_pwdn  = pins[0];    cam_cfg.pin_reset = pins[1];
    cam_cfg.pin_vsync = pins[2];    cam_cfg.pin_href  = pins[3];
    cam_cfg.pin_xclk  = pins[4];    cam_cfg.pin_pclk  = pins[5];
    cam_cfg.pin_d7    = pins[6];    cam_cfg.pin_d6    = pins[7];
    cam_cfg.pin_d5    = pins[8];    cam_cfg.pin_d4    = pins[9];
    cam_cfg.pin_d3    = pins[10];   cam_cfg.pin_d2    = pins[11];
    cam_cfg.pin_d1    = pins[12];   cam_cfg.pin_d0    = pins[13];
#   ifdef CONFIG_BASE_USE_PSRAM
    if (esp_psram_is_initialized()) {
        cam_cfg.frame_size   = FRAMESIZE_INVALID - 1;   // as large as possible
        cam_cfg.jpeg_quality = 12;                      // ov: higher quality
        cam_cfg.fb_count     = 2;                       // double buffered
        cam_cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    }
#   endif
    if (( err = esp_camera_init(&cam_cfg) )) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return;
    }
    sensor_t *cam = esp_camera_sensor_get();
    if (esp_camera_load_from_nvs("camera")) {
        camera_sensor_info_t *info = esp_camera_sensor_get_info(&cam->id);
        if (info->model == CAMERA_OV3660) {
            cam->set_brightness(cam, 1);
        } else if (info->model == CAMERA_OV5640) {
            cam->set_hmirror(cam, 1);
        }
        if (cam_cfg.fb_location == CAMERA_FB_IN_PSRAM) {
            cam->set_framesize(cam, info->max_size);
        } else {
            cam->set_framesize(cam, MIN(info->max_size, FRAMESIZE_HD));
        }
        esp_camera_save_to_nvs("camera");
    }
    CAM_ACQUIRE(cam);
    CAM_RELEASE(cam);
}

static void cam_flush() {
    if (cam_cfg.grab_mode != CAMERA_GRAB_WHEN_EMPTY) return;
    LOOPN(i, cam_cfg.fb_count) { esp_camera_fb_return(esp_camera_fb_get()); }
}

static camera_fb_t * cam_grab() {
    static bool warned = false;
    camera_fb_t *frame = NULL;
    if (video.task) {
        frame = esp_camera_fb_get();
        if (!warned) {
            ESP_LOGW(TAG, "Mix used async and sync APIs!");
            warned = true;
        }
    } else {
        sensor_t *cam = esp_camera_sensor_get();
        if (!cam) return NULL;
        CAM_ACQUIRE(cam);
        cam_flush();
        frame = esp_camera_fb_get();
        CAM_RELEASE(cam);
    }
    return frame;
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

static int cam_get(sensor_t *cam, uint8_t idx) {
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

static esp_err_t cam_set(sensor_t *cam, uint8_t idx, int val) {
    if (!cam || idx >= LEN(cam_attrs)) return ESP_ERR_INVALID_ARG;
    if (cam_get(cam, idx) == val) return ESP_OK;
    void **cptr = (void *)cam + cam_attrs[idx].coff;
    int (*cb)(sensor_t *, int) = *cptr;
    return cb(cam, val);
}

static float cam_fps(sensor_t *cam, int *val) {
    int hts = cam->get_reg(cam, 0x380C, 0xFFFF);
    int vts = cam->get_reg(cam, 0x380E, 0xFFFF);
    float clk = 1.25 * cam->xclk_freq_hz;
    if (!val) return clk / hts / vts;
    int tgt = *val ? CONS(clk / hts / *val, CAM_VERRES(cam), 0xFFFF) : vts;
    return tgt == vts ? 0 : cam->set_reg(cam, 0x380E, tgt, 0xFFFF);
}

static esp_err_t cam_loads(sensor_t *cam, const char *json) {
    cJSON *obj = cJSON_Parse(json);
    if (!obj) {
        ESP_LOGE(TAG, "Failed to load config from `%s`", json);
        return ESP_ERR_INVALID_ARG;
    }
    int err = ESP_OK, value, stdby = !video.task;
    if (stdby) CAM_ACQUIRE(cam);
    for (cJSON *ptr = obj->child; ptr && !err; ptr = ptr->next) {
        if (!ptr->string) continue;
        if (cJSON_IsNumber(ptr)) {
            value = ptr->valuedouble;
        } else {
            int32_t tmp;
            if (!parse_s32(cJSON_GetStringValue(ptr), &tmp)) continue;
            value = tmp;
        }
        if (!strcmp(ptr->string, "xclk")) {
            if (value > 240) value /= 1e6;
            if (!value) continue;
            err = cam->set_xclk(cam, cam_cfg.ledc_timer, value); // in MHz
        } else if (!strcmp(ptr->string, "framerate")) {
            err = cam_fps(cam, &value);
        } else LOOPN(i, LEN(cam_attrs)) {
            if (strcmp(ptr->string, cam_attrs[i].key)) continue;
            err = cam_set(cam, i, value);
            break;
        }
    }
    if (stdby) CAM_RELEASE(cam);
    cJSON_Delete(obj);
    return err ?: esp_camera_save_to_nvs("camera");
}

static char * cam_dumps(sensor_t *cam, FILE *stream) {
    if (stream) {
        size_t klen = strlen("framerate");
#   ifdef CONFIG_BASE_AUTO_ALIGN
        ITERP(attr, cam_attrs) { klen = MAX(klen, strlen(attr->key)); }
#   endif
        fprintf(stream, "%*s: %.3f\n", klen, "framerate", cam_fps(cam, NULL));
        fprintf(stream, "%*s: %d\n", klen, "width", CAM_HORRES(cam));
        fprintf(stream, "%*s: %d\n", klen, "height", CAM_VERRES(cam));
        fprintf(stream, "%*s: %d\n", klen, "xclk", cam->xclk_freq_hz);
        LOOPN(i, LEN(cam_attrs)) {
            fprintf(stream, "%*s: %d\n",
                    klen, cam_attrs[i].key, cam_get(cam, i));
        }
        fflush(stream);
        return NULL;
    }
    cJSON *obj = cJSON_CreateObject();
    LOOPN(i, LEN(cam_attrs)) {
        cJSON_AddNumberToObject(obj, cam_attrs[i].key, cam_get(cam, i));
    }
    cJSON *sizes = cJSON_AddArrayToObject(obj, "framesizes");
    LOOPN(i, FRAMESIZE_INVALID) {
        int wh[2] = { resolution[i].width, resolution[i].height };
        cJSON_AddItemToArray(sizes, cJSON_CreateIntArray(wh, LEN(wh)));
    }
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

static void vid_visual(void *arg, esp_event_base_t b, int32_t id, void *data) {
    video_data_t *pkt = *(video_data_t **)data;
    notify_increase(pkt->task);
    if (id == VID_EVENT_STOP) {
        fputc('\n', arg);
    } else if (id == VID_EVENT_DATA && (pkt->id % pkt->mode->fps) == 0) {
        static size_t last_id;
        static TickType_t last_ts;
        size_t di = pkt->id - last_id; last_id = pkt->id;
        TickType_t dt = xTaskGetTickCount() - last_ts; last_ts += dt;
        float real_fps = dt ? di * 1e3 / pdTICKS_TO_MS(dt) : 0;
        fprintf(arg, "\r%s %08d %dx%dx%d %dFPS %.4s %d Bytes %.*fFPS\n",
                format_timestamp_us(0),
                pkt->id, pkt->mode->width, pkt->mode->height,
                pkt->mode->depth, pkt->mode->fps,
                pkt->mode->fourcc, pkt->len,
                real_fps >= 10 ? 1 : 2, real_fps);
    }
    fflush(arg);
    notify_decrease(pkt->task);
}

static void vid_capture(void *arg) {
    sensor_t *cam = esp_camera_sensor_get();
    if (!cam) return;
    float fps = cam_fps(cam, NULL);
    video_mode_t mode = { fps, CAM_HORRES(cam), CAM_VERRES(cam), 3, "MJPG" };
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
    video_data_t avi = { .task = video.task, .data = &AVI, .len = sizeof(AVI) };
    video_data_t pkt = { .task = video.task, .data = NULL, .mode = &mode };
    AVC_POST(VID_EVENT_START, avi, -1);

    CAM_ACQUIRE(cam);
    cam_flush();
    for (pkt.id = 0; video.run && pkt.id < nframe; pkt.id++) {
        // FIXME: fb_count = 1: must return before get
        if (!( next = esp_camera_fb_get() )) break;
        if (!notify_released(500, 0)) {
            ESP_LOGD(TAG, "%08u: frame not released", pkt.id);
            esp_camera_fb_return(next);
            continue;
        }
        if (prev && prev->format != PIXFORMAT_JPEG) TRYFREE(pkt.data);
        if (next->format == PIXFORMAT_JPEG) {
            pkt.data = next->buf;
            pkt.len = next->len;
        } else if (!frame2jpg(next, 80, (uint8_t **)&pkt.data, &pkt.len)) {
            ESP_LOGE(TAG, "%08u: JPEG compression failed", pkt.id);
            esp_camera_fb_return(next);
            break;
        }
        AVC_POST(VID_EVENT_DATA, pkt, 10);
        TRYNULL(prev, esp_camera_fb_return);
        prev = next;
    }
    CAM_RELEASE(cam);

    notify_released(500, 5);
    avi.len = 0; avi.data = NULL;
    AVC_POST(VID_EVENT_STOP, avi, -1);
    notify_released(50, 10);
    UREGEVTS(AVC, video.shdl);
    if (prev && prev->format != PIXFORMAT_JPEG) TRYFREE(pkt.data);
    TRYNULL(prev, esp_camera_fb_return);
    video.task = NULL;
    vTaskDelete(NULL);
}
#endif // CONFIG_BASE_USE_CAM

esp_err_t avi_command(
    int targets, const char *ctrl, uint32_t tout_ms, void *ptr
) {
    if (!targets) targets = AUDIO_TARGET | VIDEO_TARGET;
    bool atgt = targets & AUDIO_TARGET, vtgt = targets & VIDEO_TARGET;
    if (ctrl) {
        if (targets & IMAGE_TARGET) {
#ifndef CONFIG_BASE_USE_CAM
            return ESP_ERR_NOT_SUPPORTED;
#else
            if (!ptr) return ESP_ERR_INVALID_ARG;
            sensor_t *cam = esp_camera_sensor_get();
            if (!cam) return ESP_ERR_INVALID_STATE;
            static camera_fb_t *frame;
            if (!strcasecmp(ctrl, "print")) {
                cam_dumps(cam, (FILE *)ptr);
            } else if (!strcasecmp(ctrl, "loads")) {
                return cam_loads(cam, (const char *)ptr);
            } else if (!strcasecmp(ctrl, "dumps")) {
                char *json = *(char **)ptr = cam_dumps(cam, NULL);
                return json ? ESP_OK : ESP_ERR_NO_MEM;
            } else if (!strcasecmp(ctrl, "fetch")) {
                video_data_t *pkt = (video_data_t *)ptr;
                if (!frame && !( frame = cam_grab() )) return ESP_FAIL;
                pkt->data = frame->buf;
                pkt->len = frame->len;
            } else if (!strcasecmp(ctrl, "yield")) {
                video_data_t *pkt = (video_data_t *)ptr;
                if (!frame || frame->buf != pkt->data || frame->len != pkt->len)
                    return ESP_ERR_INVALID_STATE;
                TRYNULL(frame, esp_camera_fb_return);
                pkt->data = NULL;
                pkt->len = 0;
            }
            return ESP_OK;
#endif
        } else if (!strcasecmp(ctrl, "print")) {
            UNUSED FILE *fp = (FILE *)ptr;
            if (!fp) return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_BASE_USE_I2S
            if (atgt && !audio.shdl) REGEVTS(AVC, aud_visual, fp, &audio.shdl);
#endif
#ifdef CONFIG_BASE_USE_CAM
            if (vtgt && !video.shdl) REGEVTS(AVC, vid_visual, fp, &video.shdl);
#endif
        } else if (strtob(ctrl)) {
            UNUSED void *arg = (void *)(tout_ms ?: UINT32_MAX);
#ifdef CONFIG_BASE_USE_I2S
            if (atgt && !audio.task) {
                audio.run = true;
                xTaskCreate(aud_capture, "audio", 8192, arg, 20, &audio.task);
                if (!audio.task) return ESP_ERR_NO_MEM;
            }
#endif
#ifdef CONFIG_BASE_USE_CAM
            if (vtgt && !video.task) {
                video.run = true;
                xTaskCreate(vid_capture, "video", 4096, arg, 20, &video.task);
                if (!video.task) return ESP_ERR_NO_MEM;
            }
#endif
        } else {
            if (atgt) audio.run = false;
            if (vtgt) video.run = false;
            LOOPND(ms, 100) {
                if (atgt && !audio.task) atgt = false;
                if (vtgt && !video.task) vtgt = false;
                if (!atgt && !vtgt) break;
                msleep(ms);
            }
        }
    }
    if (atgt) printf("Audio Capture: %s%s\n",
                     audio.task ? "on" : "off", audio.shdl ? " printing" : "");
    if (vtgt) printf("Video Capture: %s%s\n",
                     video.task ? "on" : "off", video.shdl ? " printing" : "");
    return ESP_OK;
}

// QRCode encoder

#ifdef WITH_QRENC
#   include "../qrcodegen.h"

static const char *qrtpls[] = {
    " \0  \u2580\0\u2584\0\u2588",  // 1 x 2 rect ` ▀▄█`
    " \0  \u281B\0\u28E4\0\u28FF",  // 1 x 2 dots `⠀⠛⣤⣿`
    "\u2588\u2588",                 // 2 x 2 rect `██`
    "\u28be\u2877",                 // 2 x 2 dots `⢾⡷`
    "\u25A0 ",                      // 2 x 2 rect `■ `
    "\u25CF ",                      // 2 x 2 dots `● `
};

esp_err_t avi_qrenc(qrenc_config_t *cfg) {
    if (!cfg || !cfg->data || !cfg->stream) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    uint8_t *rst = malloc(qrcodegen_BUFFER_LEN_MAX);
    uint8_t *tmp = malloc(qrcodegen_BUFFER_LEN_MAX);
    if (!rst || !tmp) {
        err = ESP_ERR_NO_MEM;
        goto exit;
    }
    enum qrcodegen_Ecc ecl = MIN(cfg->ecl, qrcodegen_Ecc_HIGH);
    enum qrcodegen_Mask mask = qrcodegen_Mask_AUTO;
    int min = qrcodegen_VERSION_MIN, max = qrcodegen_VERSION_MAX;
    if (cfg->max) max = CONS(cfg->max, min, max);
    if (cfg->flags & QRENC_BIN) {
        if (cfg->dlen > qrcodegen_BUFFER_LEN_MAX) return ESP_ERR_INVALID_SIZE;
        memcpy(tmp, cfg->data, cfg->dlen);
        err = !qrcodegen_encodeBinary(tmp, cfg->dlen, rst, ecl, min, max, mask, true);
    } else {
        err = !qrcodegen_encodeText(cfg->data, tmp, rst, ecl, min, max, mask, true);
    }
    if (err) goto exit;
    cfg->bdr = CMOD(cfg->bdr, 2);
    bool inv = cfg->flags & QRENC_INV;
    bool dot = cfg->flags & QRENC_DOTS;
    bool ansi = cfg->flags & QRENC_ANSI;
    uint8_t ixor = inv ? 0xFF : 0;
    int size = qrcodegen_getSize(rst), tl = size / 3, th = size * 2 / 3;
#   define MARKER(x, y)                                                     \
    ( x < 0 || y < 0 || x >= size || y >= size ||                           \
      (x < tl && y < tl) || (x < tl && y >= th) || (x >= th && y < tl) )
#   define ANSIP(x, y, s)                                                   \
    do {                                                                    \
        if (ansi) {                                                         \
            uint8_t r = (x + cfg->bdr) * 255 / (size + 2 * cfg->bdr),       \
                    g = (y + cfg->bdr) * 255 / (size + 2 * cfg->bdr);       \
            fprintf(cfg->stream, "\x1b[38;2;%u;%u;%um%s\x1b[0m",            \
                    r, (uint8_t)(cos((r + g) * M_2_PI / 1e3) * 255), g, s); \
        } else {                                                            \
            fprintf(cfg->stream, s);                                        \
        }                                                                   \
    } while (0)
    if (cfg->flags & QRENC_BMP) {                       // save as bmp
        int line = CMOD(size / 8, 4);
        int lpad = line - CDIV(size, 8);                // pad for 4 Bytes
        bmp_header_t BMP = {
            "BM", -1, 0, BMP_HEADER_FILE_LEN + BMP_HEADER_INFO_LEN,
            BMP_HEADER_INFO_LEN, size, size, 1,
            1, 0, line * size, 0, 0, 0, 0
        };
        int nplt = 1 << CONS(BMP.bpp, 1, 4);
        uint32_t PLT[nplt];
        LOOPN(i, nplt) { PLT[i] = 0xFFFFFFFF * i / (nplt - 1); };
        BMP.offset += sizeof(PLT);
        BMP.filelen = BMP.offset + BMP.datalen;
        fwrite(&BMP, sizeof(BMP), 1, cfg->stream);
        fwrite(&PLT, sizeof(PLT), 1, cfg->stream);
        uint8_t val = 0, final = size - 1;
        LOOPND(y, size) {                               // v-flip
            LOOPN(x, size) {
                uint8_t mod = x % 8;
                if (qrcodegen_getModule(rst, x, y)) val |= BIT(mod);
                if (mod == 7 || x == final) {
                    fputc(val ^ ixor, cfg->stream);
                    val = 0;
                }
            }
            LOOPN(i, lpad) { fputc(ixor, cfg->stream); }
        }
    } else if (cfg->flags & QRENC_SVG) {                // save as svg
        const char *colors[] = { "black", "white" };
        fprintf(cfg->stream,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %u %u\">"
                    "<rect fill=\"%s\" width=\"%u\" height=\"%u\"/>"
                    "<path fill=\"%s\" d=\"",
                size, size, colors[!inv], size, size, colors[inv]);
        LOOPN(y, size) {
            LOOPN(x, size) {
                if (!qrcodegen_getModule(rst, x, y)) continue;
                fprintf(cfg->stream, "M%u,%uh1v1h-1z", x, y);
            }
        }
        fprintf(cfg->stream, "\"/></svg>");
    } else if (cfg->flags & QRENC_FULL) {               // print full char
        uint8_t tpl = 2 + (cfg->flags & QRENC_MINI ? 2 : 0) + dot;
        for (int y = -cfg->bdr; y < size + cfg->bdr; y += 1) {
            for (int x = -cfg->bdr; x < size + cfg->bdr; x += 1) {
                const char *str = qrtpls[tpl];
                if (!qrcodegen_getModule(rst, x, y) ^ inv) {
                    str = "  ";
                } else if (MARKER(x, y)) {
                    str = qrtpls[2];
                }
                ANSIP(x, y, str);
            }
            fputc('\n', cfg->stream);
        }
    } else {                                            // print half char
        for (int y = -cfg->bdr; y < size + cfg->bdr; y += 2) {
            for (int x = -cfg->bdr; x < size + cfg->bdr; x += 1) {
                uint8_t idx = 0, tpl = !MARKER(x, y) && (dot ^ inv);
                if (qrcodegen_getModule(rst, x, y))     idx |= BIT0;
                if (qrcodegen_getModule(rst, x, y + 1)) idx |= BIT1;
                ANSIP(x, y, qrtpls[tpl] + 4 * (idx ^ (ixor & 3)));
            }
            fputc('\n', cfg->stream);
        }
    }
#   undef ANSIP
#   undef MARKER
exit:
    TRYFREE(rst);
    TRYFREE(tmp);
    return err;
}
#else
esp_err_t avi_qrenc(qrenc_config_t *c) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c);
}
#endif

void avi_initialize() {
#ifdef CONFIG_BASE_USE_I2S
    i2s_initialize();
#endif
#ifdef CONFIG_BASE_USE_CAM
    cam_initialize();
#endif
}

#endif // CONFIG_BASE_USE_AVC
