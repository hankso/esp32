/* 
 * File: globals.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:51:08
 */

#pragma once

#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>

#include "espmisc.h"

// GCC tricks

#define UNUSED              __attribute__((unused))
#define PACKED              __attribute__((packed))
#define FALLTH              __attribute__((fallthrough))
#define NOTUSED(x)          (void)(x)
#define CONCAT_(a, b)       a ## b
#define CONCAT(a, b)        CONCAT_(a, b)
#define STR_(x)             #x
#define STR(x)              STR_(x)
#define EMPTY(s)            ( !(s) || !*(s) )
#define CASESTR(x, l)       case x: return #x + l
#define CASESTRV(v, x, l)   case x: (v) = #x + l; break
#define SIZEOF(s, a)        sizeof(((s *)0)->a)
#define LEN(arr)            ( sizeof(arr) / sizeof(*arr) )
#define LOOP(x, l, h)       for (typeof(h) x = (l), _h = (h); x < _h; x++)
#define LOOPD(x, h, l)      for (typeof(h) x = (h), _l = (l); x > _l; x--)
#define LOOPN(x, n)         LOOP(x, 0, (n))
#define LOOPND(x, n)        LOOPD(x, (n) - 1, -1)
#define LPCHR(c, n)         do { LOOPN(x, (n)) putchar(c); } while (0)
#define LPCHRN(c, n)        do { LPCHR((c), (n)); putchar('\n'); } while (0)
#define TRYNULL(p, f)       do { if (p) f(p); (p) = (typeof(p))0; } while (0)
#define TRYFREE(p)          TRYNULL((p), free)

#define ITERPN(v, a, n)                                                     \
        for (typeof(*(a)) *v = (a), *_##v = v + (n); v < _##v; v++)
#define ITERVN(v, a, n)                                                     \
        for (typeof(*(a)) *_##v = (a), v = *_##v; _##v < (a) + (n); v = *++_##v)
#define ITERP(v, a)         ITERPN(v, (a), LEN((a)))
#define ITERV(v, a)         ITERVN(v, (a), LEN((a)))

#define CENTER(m, c, n)                                                     \
        do {                                                                \
            const char *s = (m) ?: "";                                      \
            int p = n - 2 - strlen(s), h = p / 2;                           \
            LPCHR((c), h); printf(" %s ", s); LPCHRN((c), p - h);           \
        } while (0)

// May be defined somewhere

#ifndef BIT
#   define BIT(n)           ( 1UL << (n) )
#endif

#ifndef ABS
#   define ABS(x)           ({ typeof(x) X_ = (x); X_ < 0 ? -X_ : X_; })
#endif

#ifndef ABSDIFF
#   define ABSDIFF(a, b)    ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? A_ - B_ : B_ - A_; })
#endif

#ifndef MOD
#   define MOD(a, b)        ({ typeof(a) A_ = (a), B_ = (b), M_ = A_ % B_;  \
                               M_ < 0 ? M_ + B_ : M_; })
#endif

#ifndef CDIV
#   define CDIV(a, b)       ({ typeof(a) A_ = (a), B_ = (b);                \
                               B_ ? (A_ + B_ - 1) / B_ : 0; })
#   define CMOD(a, b)       ( CDIV(a, b) * b ) // TODO: (a + b - 1) & ~(b - 1)
#endif

#ifndef MAX
#   define MAX(a, b)        ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? A_ : B_; })
#   define MIN(a, b)        ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? B_ : A_; })
#endif

#ifndef CONS
#   define CONS(x, l, h)    MAX((l), MIN((x), (h)))
#endif

#ifndef bitread
#   define bitsread(v, o, m)    ( ((v) >> (o)) & (m) )
#   define bitnread(v, o, n)    bitsread((v), (o), BIT(n) - 1)
#   define bitread(v, o)        bitsread((v), (o), 1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t parse_all(const char *str, int *arr, size_t arrlen) {
    size_t len = strlen(str ?: ""), idx = 0;
    const char *ptr = str, *end = str + len;
    char *endptr;
    while (ptr < end && idx < arrlen) {
        int val = strtol(ptr, &endptr, 0);
        if (endptr != ptr) arr[idx++] = val;
        ptr = endptr + 1;
    }
    return idx;
}

static inline bool parse_f64(const char *str, double *ptr) {
    if (EMPTY(str)) return false;
    char *endptr;
    double val = strtod(str, &endptr);
    if (endptr == str) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_f32(const char *str, float *ptr) {
    if (EMPTY(str)) return false;
    char *endptr;
    float val = strtof(str, &endptr);
    if (endptr == str) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_s64(const char *str, int64_t *ptr) {
    if (EMPTY(str)) return false;
    char *endptr;
    int64_t val = strtoll(str, &endptr, 0);
    if (endptr == str) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_s32(const char *str, int32_t *ptr) {
    int64_t val = 0;
    if (!parse_s64(str, &val) || val < INT32_MIN || val > INT32_MAX) return 0;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_u32(const char *str, uint32_t *ptr) {
    int64_t val = 0;
    if (!parse_s64(str, &val) || val < 0 || val > UINT32_MAX) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_u16(const char *str, uint16_t *ptr) {
    int64_t val = 0;
    if (!parse_s64(str, &val) || val < 0 || val > UINT16_MAX) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline bool parse_u8(const char *str, uint8_t *ptr) {
    int64_t val = 0;
    if (!parse_s64(str, &val) || val < 0 || val > UINT8_MAX) return false;
    if (ptr) *ptr = val;
    return true;
}

static inline char *strrstr(const char *str, const char *sub) {
    const char *last = NULL, *found;
    while (sub && ( found = strstr(str, sub) )) { str = (last = found) + 1; }
    return (char *)last;
}

static inline bool strtob(const char *str) {
    if (EMPTY(str)) return false;
    return strspn(str, "1yY") == 1 || !strcasecmp(str, "on");
}

static inline size_t strncnt(const char *str, const char *wants, size_t slen) {
    if (EMPTY(wants)) return false;
    size_t cnt = 0, idx = 0, len = strnlen(str ?: "", slen);
    while (idx < len) { if (strchr(wants, str[idx++])) cnt++; }
    return cnt;
}

static inline char * strtrim(char *str, const char *chars) {
    size_t slen = strlen(str ?: "");
    if (!slen || !chars) return str;
    size_t head = strspn(str, chars);
    size_t tail = slen - 1;
    while (tail > head && strchr(chars, str[tail])) { tail--; }
    str[tail + 1] = '\0';
    return str + head;
}

static inline int stridx(const char *str, const char *tpl) {
    size_t slen = strlen(str ?: ""), tlen = strlen(tpl ?: "");
    if (!slen || !tlen) return -1;
    size_t cnt = strncnt(tpl, ",;/\\|", -1);
    uint16_t idx, ret = parse_u16(str, &idx);   // case 1: number as index
    if (ret && idx < MIN(cnt ?: tlen, tlen)) return idx;
    const char *tmp = strcasestr(tpl, str);         // case 2: "aaa|bbb|ccc"
    if (tmp && cnt) return strncnt(tpl, ",;/\\|", tmp - tpl);
    if (tmp && slen == 1) return tmp - tpl;         // case 3: "ABC"
    if (slen > 1 && !cnt && ( tmp = strchr(tpl, str[0]) ))
        return tmp - tpl;                           // case 4: match first char
    printf("Invalid `%s`: choose from `%s`\n", str, tpl);
    return -1;
}

static inline bool endswith(const char *str, const char *tail) {
    if (!str || !tail) return false;
    int offset = strlen(str) - strlen(tail);
    return !strcmp(str + MAX(offset, 0), tail);
}

static inline bool startswith(const char *str, const char *head) {
    if (!str || !head) return false;
    size_t headlen = strlen(head);
    return strlen(str) < headlen ? false : !strncmp(str, head, headlen);
}

static inline char * b64encode(const char *inp, char *dst, size_t slen) {
    size_t dlen = CDIV(slen, 3) * 4 + 1;
    if (!inp || !slen || (!dst && !( dst = (char *)malloc(dlen) ))) return dst;
    const char *end = inp + slen, *chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (char *out = dst; inp < end; inp += 3, out += 4) {
        uint32_t u24 = inp[0] << 16;
        if ((inp + 1) < end) u24 |= inp[1] << 8;
        if ((inp + 2) < end) u24 |= inp[2];
        out[0] = chars[(u24 >> 18) & 0x3F];
        out[1] = chars[(u24 >> 12) & 0x3F];
        out[2] = (inp + 1) >= end ? '=' : chars[(u24 >> 6) & 0x3F];
        out[3] = (inp + 2) >= end ? '=' : chars[u24 & 0x3F];
    }
    dst[dlen - 1] = '\0';
    return dst;
}

static inline uint8_t numdigits(int n) {
    uint8_t i = !n;
    while (n) { n /= 10; i++; }
    return i;
}

static inline char hexdigits(uint8_t v) {
    if (v < 0xA) return v + '0';
    if (v > 0xF) return hexdigits(v & 0xF);
    return v - 0xA + 'A';
}

static inline void hexdump(FILE *stream, const void *buf, size_t bytes, size_t line_width) {
    if (!stream || !buf || !bytes || line_width < 3) return;
    size_t maxbytes = line_width / 3;
    LOOPN(i, bytes) {
        if (i && i % maxbytes == 0) fputc('\n', stream);
        fprintf(stream, "%02X ", ((uint8_t *)buf)[i]);
    }
    if (bytes) fputc('\n', stream);
}

static inline void hexdumpc(FILE *stream, const void *buf, size_t bytes) {
    if (!stream || !buf || !bytes) return;
    char ascii[0x10 + 1];
    const uint8_t *ptr = (const uint8_t *)buf;
    LOOPN(i, CMOD(bytes, 0x10)) {
        int idx = i % 0x10;
        if (idx == 0) printf("%p:", ptr + i);
        if (i < bytes) {
            fprintf(stream, "%s%02x", i % 2 ? "" : " ", ptr[i]);
            ascii[idx] = isprint(ptr[i]) ? ptr[i] : '.';
        } else {
            fprintf(stream, "%*s", i % 2 ? 2 : 3, "");
            ascii[idx] = ' ';
        }
        if (idx == 0xF) {
            ascii[idx + 1] = 0;
            fprintf(stream, " |%s|\n", ascii);
        }
    }
}

static inline void hexdumpl(FILE *stream, const void *buf, size_t bytes, size_t line_width) {
    if (!stream || !buf || !bytes || line_width < 3) return;
    size_t maxbytes = line_width / 3, count = MIN(bytes, maxbytes);
    LOOPN(i, count) { fprintf(stream, "%02X ", ((uint8_t *)buf)[i]); }
    if (bytes && maxbytes && bytes > maxbytes)
        fprintf(stream, "... [%zu/%zu]", count, bytes);
    fputc('\n', stream);
}

static inline char * hexdumps(const void *src, char *dst, size_t slen, size_t dlen) {
    if (!dst || !dlen) return 0;
    size_t maxbytes, count, offset = 0;
    if ((slen * 2 + 1) <= dlen) {
        maxbytes = (dlen - 1) / 2;
    } else if (dlen < (9 + numdigits(slen) + numdigits(dlen / 2))) {
        dst[0] = '\0';
        return dst; // no space
    } else { // append tail to dst
        offset = dlen - 9 - numdigits(slen);
        maxbytes = (offset - numdigits(dlen / 2)) / 2;
    }
    count = MIN(slen, maxbytes);
    LOOPN(i, count) {
        dst[2 * i + 0] = hexdigits(((uint8_t *)src)[i] >> 4);
        dst[2 * i + 1] = hexdigits(((uint8_t *)src)[i] & 0xF);
    }
    if (offset) {
        offset -= numdigits(count);
        if (offset > (count * 2))
            memset(dst + count * 2, ' ', offset - count * 2);
        sprintf(dst + offset, " ... [%zu/%zu]", count, slen);
    } else {
        dst[count * 2] = '\0';
    }
    return dst;
}

static inline const char * unicode2str(uint32_t unicode) {
    // Convert between Unicode and UTF-8 encoded Bytes
    // 0x000000 - 0x00007F <=> 0b0xxxxxxx
    // 0x000080 - 0x0007FF <=> 0b110xxxxx 0b10xxxxxx
    // 0x000800 - 0x00FFFF <=> 0b1110xxxx 0b10xxxxxx 0b10xxxxxx
    // 0x010000 - 0x1FFFFF <=> 0b11110xxx 0b10xxxxxx 0b10xxxxxx 0b10xxxxxx
    static char buf[5];
    if (unicode < 0x80) {
        buf[0] = unicode;
        buf[1] = '\0';
    } else if (unicode < 0x800) {
        buf[0] = 0xC0 | ((unicode >> 6) & 0x1F);
        buf[1] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[2] = '\0';
    } else if (unicode < 0x10000) {
        buf[0] = 0xE0 | ((unicode >> 12) & 0xF);
        buf[1] = 0x80 | ((unicode >> 6) & 0x3F);
        buf[2] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[3] = '\0';
    } else if (unicode < 0x1FFFFF) {
        buf[0] = 0xF0 | ((unicode >> 18) & 0x7);
        buf[1] = 0x80 | ((unicode >> 12) & 0x3F);
        buf[2] = 0x80 | ((unicode >> 6) & 0x3F);
        buf[3] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[4] = '\0';
    } else {
        buf[0] = '\0';
    }
    return buf;
}

static inline size_t str2unicode(const uint8_t *str, uint32_t *uptr) {
    if (!str || str[0] > 0xF7) return 0;
    uint32_t nbytes, unicode;
    if (str[0] >= 0xF0) {
        nbytes = 4;
        unicode = str[0] & 0x7;
    } else if (str[0] >= 0xE0) {
        nbytes = 3;
        unicode = str[0] & 0xF;
    } else if (str[0] >= 0xC0) {
        nbytes = 2;
        unicode = str[0] & 0x1F;
    } else {
        unicode = str[0];
        nbytes = unicode ? 1 : 0;
    }
    if (strlen((const char *)str) < nbytes) return 0;
    for (uint32_t i = 1; i < nbytes; i++) {
        if ((str[i] >> 6) != 0x2) return 0; // invalid format
        unicode = (unicode << 6) | (str[i] & 0x3F);
    }
    if (uptr) *uptr = unicode;
    return nbytes;
}

static inline const char * unicode2gbk(FILE *fd, uint32_t unicode) {
    // Convert between Unicode and GBK encoded Bytes
    // 0x0800 - 0xFFFF <=> 0b1xxxxxxx 0bxxxxxxxx (0x8140 - 0xFEFF)
    // Must provide file descriptor to `gbktable.bin` which is a 47KB
    // mapping file between 16Bit chinese unicode and GBK index.
    static char buf[3];
    buf[0] = '\0';
    if (unicode < 0x800 || unicode > 0xFFFF || !fd || fseek(fd, 0, SEEK_SET))
        return buf;
    for (uint16_t val; fread(&val, 1, 2, fd) == 2;) {
        if (unicode != val) continue;
        uint16_t idx = ftell(fd) / 2, num = idx / (0x100 - 0x40) + 1;
        uint16_t gbk = 0x8100 + idx + num * 0x40;
        buf[0] = gbk >> 8;
        buf[1] = gbk & 0xFF;
        buf[2] = '\0';
        break;
    }
    return buf;
}

static inline size_t gbk2unicode(FILE *fd, const uint8_t *str, uint32_t *uptr) {
    if (!fd || !str || str[0] > 0xFE) return 0;
    if (str[0] < 0x81) {
        if (uptr) *uptr = str[0];
        return str[0] ? 1 : 0;
    }
    uint16_t unicode = 0, val = ((str[0] - 0x81) << 8) | str[1];
    if (!fseek(fd, (val - CDIV(val, 0x100) * 0x40) * 2, SEEK_SET) &&
        fread(&unicode, 1, 2, fd) == 2 && unicode && uptr) *uptr = unicode;
    return unicode ? 2 : 0;
}

#ifdef __cplusplus
}
#endif
