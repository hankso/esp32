/*
 * File: avutils.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2025/3/25 8:53:45
 */

#pragma once

#include <stdint.h>
#include <string.h>

typedef char fcc[4];
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define AUDIO_TARGET        (1 << 0)
#define VIDEO_TARGET        (1 << 1)
#define IMAGE_TARGET        (1 << 2)
#define SHIFT3(a, b, c)     (((a) << 16) | ((b) << 8) | (c))
#define SHIFT4(a, b, c, d)  (((a) << 24) | SHIFT3((b), (c), (d)))
#define FOURCC(a, b, c, d)  SHIFT4((u32)(a), (u32)(b), (u32)(c), (u32)(d))

#ifndef PACKED
#   define PACKED           __attribute__((__packed__))
#endif

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

typedef struct PACKED {
    u16 fps;        // frame per second
    u16 width;      // frame width in pixel
    u16 height;     // frame height in pixel
    u16 depth;      // frame depth in Byte
    fcc fourcc;
} video_mode_t;

typedef struct PACKED {
    u32 srate;      // sample rate
    u16 nch;        // number of channels
    u16 depth;      // Bytes per channel per sample (i.e. BPC)
} audio_mode_t;

static inline int parse_fourcc(const char *str, u32 *var) {
    u32 len = strlen(str ?: ""), val;
    if (len < 3 || len > 4) return -1;
    for (u8_t idx = 0; idx < 4; idx++) {
        val |= (u32)(idx < len ? str[idx] : ' ') << (idx * 8);
    }
    if (var) *var = val;
    return 0;
}

typedef struct PACKED {
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
} wav_header_t;

typedef struct PACKED {
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
} avi_header_t;

typedef struct PACKED {
    fcc two_code;   // e.g. 00dc for compressed video frames
    u32 length;
} avi_frame_t;

#ifdef __cplusplus
}
#endif
