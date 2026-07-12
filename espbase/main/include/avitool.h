/*
 * File: avitool.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2025/3/25 8:53:45
 */

#pragma once

#include "globals.h"

#if defined(CONFIG_BASE_USE_CAM) && !__has_include("esp_camera.h")
#   warning "Run `idf.py add-dependency espressif/esp32-camera`"
#   undef CONFIG_BASE_USE_CAM
#endif

#if defined(CONFIG_BASE_USE_CAM) || defined(CONFIG_BASE_USE_I2S)
#   define CONFIG_BASE_USE_AVC
#endif

#if __has_include("qrcode.h") && __has_include("../qrcodegen.h")
#   define WITH_QRENC
#else
#   warning "Run `idf.py add-dependency espressif/qrcode`"
#endif

#ifdef CONFIG_BASE_USE_CAM
#   if __has_include("quirc.h")
#       define WITH_QRDEC
#   else
#       warning "Run `idf.py add-dependency espressif/quirc`"
#   endif
#endif

#define SHIFT3(a, b, c)     (((a) << 16) | ((b) << 8) | (c))
#define SHIFT4(a, b, c, d)  (((a) << 24) | SHIFT3((b), (c), (d)))
#define FOURCC(a, b, c, d)  SHIFT4((u32)(d), (u32)(c), (u32)(b), (u32)(a))

#ifdef __cplusplus
extern "C" {
#endif

typedef char     fcc[4];
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

typedef struct {
    fcc RIFF; u32 filelen;
    fcc WAVE;
    fcc fmt; u32 fmtlen;
#define WAV_HEADER_FMT_LEN 16
        u16 type;
        u16 nch;                            // number of channel
        u32 shz;                            // sample rate      (e.g. 44100Hz)
        u32 Bps;                            // Bytes per second (BpS * shz)
        u16 BpS;                            // Bytes per Sample (nch * bpc / 8)
        u16 bpc;                            // bits per channel (e.g. 16 or 32)
    fcc data; u32 datalen;
} PACKED wav_header_t;

typedef struct {
    fcc RIFF; u32 filelen;
    fcc AVI;
    fcc LST1; u32 lst1len; fcc hdlr;        // 4 + 56 + 8 + 116 + 8 = 192 Bytes
#define AVI_HEADER_HDLR_LEN 192
        fcc avih; u32 avihlen;              // 56 Bytes
#define AVI_HEADER_AVIH_LEN 56
            u32 us_per_frame;
            u32 max_Bps;
            u32 padding;
            u32 flags1;
#define AVI_HEADER_AVIH_FLAG 0x110          // HAS_INDEX | IS_INTERLEAVED
            u32 total_frames;
            u32 initial_frames1;
            u32 num_streams;
            u32 buffer_size1;
            u32 width1;
            u32 height1;
            u32 reserved[4];
        fcc LST2; u32 lst2len; fcc strl;    // 4 + 56 + 8 + 40 + 8 = 116 Bytes
#define AVI_HEADER_STRL_LEN 116
            fcc strh; u32 strhlen;          // 56 Bytes
#define AVI_HEADER_STRH_LEN 56
                fcc fourcc;
                fcc handler;
                u32 flags2;
                u16 priority;
                u16 language;
                u32 initial_frames2;
                u32 scale;
                u32 fps;
                u32 start;
                u32 length;
                u32 buffer_size2;
                u32 quality;
                u32 sample_size;
                u16 left, top, right, bottom;
            fcc strf; u32 strflen;          // 40 Bytes
#define AVI_HEADER_STRF_LEN 40
                u32 true_size;
                u32 width2;
                u32 height2;
                u16 planes;
                u16 bpp;
                fcc compress;
                u32 datalen;
                u32 horppm;
                u32 verppm;
                u32 color_used;
                u32 color_import;
    fcc LST3; u32 lst3len; fcc movi;
} PACKED avi_header_t;

typedef struct {
    fcc ckid;       // e.g. mjpg for 00dc, rgb for 01db, wav for 02wb
    u32 dlen;
} PACKED avi_chunk_t;

typedef struct {
    fcc idx1;
    u32 idx1len;
#define AVI_HEADER_IDX1_LEN (16 * 1)
    struct {
        fcc ckid;   // same as avi_chunk_t -> ckid
        u32 flags;  // 0x1: LIST, 0x10: KEYFRAME, 0x100: NO_TIME
        u32 offset; // chunk offset from the start of the movi list
        u32 size;   // chunk size in bytes
    } streams[1];   // = avih -> num_streams (here we only have 1 video stream)
} PACKED avi_tailer_t;

typedef struct {
#define BMP_HEADER_FILE_LEN 14
    char BM[2];
    u32 filelen;
    u32 reserved;
    u32 offset;
    u32 infolen;    // 40 Bytes
#define BMP_HEADER_INFO_LEN 40
    u32 width;
    u32 height;
    u16 planes;     // must be 1
    u16 bpp;        // 1(B/W), 4(16-color), 8(256-color), 24(RGB)
    u32 compress;   // 0(none), 1(RLE8), 2(RLE4)
    u32 datalen;
    u32 horppm;
    u32 verppm;
    u32 color_used;
    u32 color_import;
} PACKED bmp_header_t;

typedef struct {
    u32 srate;      // sample rate
    u16 nch;        // number of channel
    u16 bpc;        // bits per channel per sample
} audio_mode_t;

typedef struct {
    u16 fps;        // frame per second
    u16 width;      // frame width in pixel
    u16 height;     // frame height in pixel
    u16 bpp;        // bits per pixel
    union {
        fcc fourcc;
        uint32_t fmt;
    };
} video_mode_t;

typedef struct {
    size_t id;
    size_t len;
    void *data;
    void *task;
    audio_mode_t mode;
} audio_data_t;

typedef struct {
    size_t id;
    size_t len;
    void *data;
    void *task;
    video_mode_t mode;
} video_data_t;

// esp_event_post keeps a copy of event_data instead of passing it (a pointer)
// directly to the event handlers. To avoid copying whole xxx_data_t structure,
// (audio_data_t **) and (video_data_t **) are passed to event handlers.
ESP_EVENT_DECLARE_BASE(AVC_EVENT);

#define AVC_POST(id, data, tout)                                            \
    ({                                                                      \
        void *ptr = &(data);                                                \
        esp_event_post(AVC_EVENT, (id), &ptr, sizeof(ptr), TIMEOUT(tout));  \
    })

enum {
    AUD_EVENT_START,    // pkt.data = WAV header, pkt.len = sizeof(wav_header_t)
    AUD_EVENT_DATA,     // pkt.data = audio data, pkt.len > 0, pkt.id >= 0
    AUD_EVENT_STOP,     // pkt.data = NULL,       pkt.len = 0
    VID_EVENT_START,    // pkt.data = AVI header, pkt.len = sizeof(avi_header_t)
    VID_EVENT_DATA,     // pkt.data = video data, pkt.len > 0, pkt.id >= 0
    VID_EVENT_STOP,     // pkt.data = NULL,       pkt.len = 0
};

enum {
    AUDIO_TARGET = 0x01,
    VIDEO_TARGET = 0x02,
    IMAGE_TARGET = 0x04,
};

#define AUDIO_START(ms) avi_command(AUDIO_TARGET, "on", (ms), NULL)
#define VIDEO_START(ms) avi_command(VIDEO_TARGET, "on", (ms), NULL)
#define AUDIO_STOP()    avi_command(AUDIO_TARGET, "off", 0,   NULL)
#define VIDEO_STOP()    avi_command(VIDEO_TARGET, "off", 0,   NULL)
#define AUDIO_PRINT(s)  avi_command(AUDIO_TARGET, "print", 0, (s))
#define VIDEO_PRINT(s)  avi_command(VIDEO_TARGET, "print", 0, (s))
#define CAMERA_PRINT(s) avi_command(IMAGE_TARGET, "print", 0, (s))
#define CAMERA_LOADS(v) avi_command(IMAGE_TARGET, "loads", 0, (void *)(v))
#define CAMERA_DUMPS(v) avi_command(IMAGE_TARGET, "dumps", 0, (void *)&(v))
#define CAMERA_FETCH(e) avi_command(IMAGE_TARGET, "fetch", 0, &(e))
#define CAMERA_YIELD(e) avi_command(IMAGE_TARGET, "yield", 0, &(e))
esp_err_t avi_command(int tgt, const char *ctrl, uint32_t tout_ms, void *ptr);

typedef struct {
    uint8_t flags;          // default 0 = TEXT | PRINT | HALF | RECT | GRAY
#define QRENC_BIN   BIT(0)  // encode as binary data
#define QRENC_BMP   BIT(1)  // save as bmp format
#define QRENC_SVG   BIT(2)  // save as svg format
#define QRENC_FULL  BIT(3)  // print with full char (2x size)
#define QRENC_MINI  BIT(4)  // print with mini rect | dots
#define QRENC_DOTS  BIT(5)  // print with unicode braille
#define QRENC_ANSI  BIT(6)  // print as colorful rainbow
#define QRENC_INV   BIT(7)  // black & white inverted
    FILE *stream;
    const void *data;
    size_t dlen;
    uint8_t max;            // version 1~40
    uint8_t ecl;            // 0: Low, 1: Medium, 2: Quartile, 3: High
    uint8_t bdr;
} qrenc_config_t;

esp_err_t avi_qrenc(qrenc_config_t *);
esp_err_t avi_qrdec(video_data_t *);

void avi_initialize();      // called in drivers.c

#ifdef __cplusplus
}
#endif
