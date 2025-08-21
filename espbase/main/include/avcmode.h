/*
 * File: avcmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2025/3/25 8:53:45
 */

#pragma once

#include "globals.h"

#define AUDIO_TARGET        (1 << 0)
#define VIDEO_TARGET        (1 << 1)
#define IMAGE_TARGET        (1 << 2)
#define ACTION_READ         (1 << 4)
#define ACTION_WRITE        (1 << 5)
#define SHIFT3(a, b, c)     (((a) << 16) | ((b) << 8) | (c))
#define SHIFT4(a, b, c, d)  (((a) << 24) | SHIFT3((b), (c), (d)))
#define FOURCC(a, b, c, d)  SHIFT4((u32)(a), (u32)(b), (u32)(c), (u32)(d))
#define FRAMEDIV(id, n)     ( (id) % (n) == 0 )
#define FRAMESEC(id)        FRAMEDIV((id), mode.fps)

#define UNION_TYPE(type, name, suffix, p1, p2, p3)                          \
    union {                                                                 \
        struct {                                                            \
            type p1##_##name suffix;                                        \
            type p2##_##name suffix;                                        \
        };                                                                  \
        type p3##_##name[2] suffix;                                         \
    }

#ifdef __cplusplus
extern "C" {
#endif

void avc_initialize();

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
    u32 srate;      // sample rate
    u16 nch;        // number of channels
    u16 depth;      // Bytes per channel per sample (i.e. BPC)
} PACKED audio_mode_t;

typedef struct {
    u16 fps;        // frame per second
    u16 width;      // frame width in pixel
    u16 height;     // frame height in pixel
    u16 depth;      // frame depth in Byte
    fcc fourcc;
} PACKED video_mode_t;

typedef struct {
    size_t id;
    size_t len;
    void *data;
    void *task;
    audio_mode_t *mode;
} PACKED audio_evt_t;

typedef struct {
    size_t id;
    size_t len;
    void *data;
    void *task;
    video_mode_t *mode;
} PACKED video_evt_t;

// esp_event_post keeps a copy of event_data instead of passing it (a pointer)
// directly to the event handlers. To avoid copying whole xxx_evt_t structure,
// (audio_evt_t **) and (video_evt_t **) are passed to event handlers.
ESP_EVENT_DECLARE_BASE(AVC_EVENT);

#define AVC_POST(evt, data, tout)                                           \
    ({                                                                      \
        void *ptr = &(data);                                                \
        esp_event_post(AVC_EVENT, (evt), &ptr, sizeof(ptr), TIMEOUT(tout)); \
    })

enum {
    AUD_EVENT_START,    // evt.data is WAV header, evt.len = sizeof(wav_header_t)
    AUD_EVENT_DATA,     // evt.data is audio data, evt.len > 0, evt.id >= 0
    AUD_EVENT_STOP,     // evt.data is NULL,       evt.len = 0
    VID_EVENT_START,    // evt.data is AVI header, evt.len = sizeof(avi_header_t)
    VID_EVENT_DATA,     // evt.data is frame jpeg, evt.len > 0, evt.id >= 0
    VID_EVENT_STOP,     // evt.data is AVI tailer, evt.len = 8 + evt.id * 16
};

#define AUDIO_START(ms) avc_command("1", AUDIO_TARGET, (ms), NULL)
#define VIDEO_START(ms) avc_command("1", VIDEO_TARGET, (ms), NULL)
#define AUDIO_STOP()    avc_command("0", AUDIO_TARGET, 0, NULL)
#define VIDEO_STOP()    avc_command("0", VIDEO_TARGET, 0, NULL)
#define AUDIO_PRINT(s)  avc_command(NULL, AUDIO_TARGET, 0, (s))
#define VIDEO_PRINT(s)  avc_command(NULL, VIDEO_TARGET, 0, (s))
#define CAMERA_LOADS(v) avc_command((v), IMAGE_TARGET | ACTION_WRITE, 0, NULL)
#define CAMERA_DUMPS(v) avc_command((v), IMAGE_TARGET | ACTION_READ, 0, NULL)
#define CAMERA_PRINT(s) avc_command(NULL, IMAGE_TARGET | ACTION_READ, 0, (s))
esp_err_t avc_command(const char *ctrl, int tgt, uint32_t tout_ms, FILE *out);

typedef struct {
#define WAV_HEADER_FMT_LEN 16
    fcc RIFF; u32 filelen;
    fcc WAVE;
    fcc fmt; u32 fmtlen;
        u16 type;
        u16 nch;    // number of channels
        u32 shz;    // sample rate in Hz
        u32 Bps;    // Bytes per second (max: 48000*4*2 = 375KBps)
        u16 BpS;    // Bytes per Sample
        u16 bpC;    // bits per Channel (per sample)
    fcc data; u32 datalen;
} PACKED wav_header_t;

typedef struct {
#define AVI_HEADER_HDLR_LEN 192
#define AVI_HEADER_AVIH_LEN 56
#define AVI_HEADER_STRL_LEN 116
#define AVI_HEADER_STRH_LEN 56
#define AVI_HEADER_STRF_LEN 40
    fcc RIFF; u32 filelen;
    fcc AVI;
    fcc LST1; u32 lst1len; fcc hdlr;        // 4 + 56 + 8 + 116 + 8 = 192 Bytes
        fcc avih; u32 avihlen;              // 56 Bytes
            u32 us_per_frame;
            u32 max_Bps;
            u32 padding;
            u32 flags1;
            u32 total_frames;
            u32 initial_frames1;
            u32 streams;
            u32 buffer_size1;
            u32 width1;
            u32 height1;
            u32 reserved[4];
        fcc LST2; u32 lst2len; fcc strl;    // 4 + 56 + 8 + 40 + 8 = 116 Bytes
            fcc strh; u32 strhlen;          // 56 Bytes
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
                u32 true_size;
                u32 width2;
                u32 height2;
                u16 planes;
                u16 bpp;
                fcc compression;
                u32 size_image;
                u32 horppm;
                u32 verppm;
                u32 color_used;
                u32 color_import;
    fcc LST3; u32 lst3len; fcc movi;
} PACKED avi_header_t;

typedef struct {
    fcc two_code;   // e.g. 00dc for compressed video frames
    u32 length;
} PACKED avi_frame_t;

#ifdef __cplusplus
}
#endif
