/* 
 * File: server.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:29:05
 */
#include "server.h"
#include "config.h"             // for Config
#include "update.h"             // for ota_updation_xxx
#include "filesys.h"            // for filesys_xxx
#include "avcmode.h"            // for AUDIO_XXX && VIDEO_XXX
#include "ledmode.h"            // for led_set_blink
#include "console.h"            // for console_handle_xxx
#include "timesync.h"           // for format_datetime

#include "esp_rom_md5.h"
#include "esp_http_server.h"

#ifdef CONFIG_BASE_USE_WEBSERVER

#define ESP_ERR_HTTPD_AUTH_DIGEST   (ESP_ERR_HTTPD_BASE + 10)
#define ESP_ERR_HTTPD_SKIP_DATA     (ESP_ERR_HTTPD_BASE + 11)

#define FLAG_AP_ONLY    BIT0
#define FLAG_NEED_AUTH  BIT1
#define FLAG_DIR_DATA   BIT2
#define FLAG_DIR_DOCS   BIT3
#define FLAG_DIR_HTML   BIT4

#define TYPE_HTML "text/html"
#define TYPE_TEXT "text/plain"
#define TYPE_JSON "application/json"
#define TYPE_UENC "application/x-www-form-urlencoded"
#define TYPE_MPRT "multipart/form-data"
#define CHUNK_SIZE 2048

#define send_str httpd_resp_sendstr // simple alias

static const char *TAG = "Server";
static const char *ERROR_HTML =
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Page not found</title>"
"</head>"
"<body>"
    "<h1>404: Page not found</h1><hr>"
    "<p>Sorry. The page you requested could not be found.</p>"
    "<p>Go back to <a href='/'>homepage</a></p>"
"</body>"
"</html>";
static const char *UPDATE_HTML =
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Basic OTA Updation</title>"
"</head>"
"<body>"
    "<h4>You should always try Advanced OTA Updation page first!</h4>"
    "<form action='/update' method='post' enctype='multipart/form-data'>"
        "<input type='file' name='update'>"
        "<input type='submit' value='Upload'>"
    "</form>"
"</body>"
"</html>";
static const char *MEDIA_HTML =
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Simple Media test</title>"
    "<style>img {cursor:pointer;min-width:320px;min-height:240px;}</style>"
"</head>"
"<body>"
    "<img id=\"vp\" src=\"media?mjpg&still\"><br>"
    "<audio id=\"ap\" preload=\"none\" src=\"media?wav\" controls "
        "controlslist=\"nodownload noremoteplayback noplaybackrate\"></audio>"
    "<script>"
        "vp.addEventListener('click', e => {"
            "let tmp = vp.src;"
            "if (tmp.endsWith('&still')) {"
                "vp.src = tmp.substr(0, tmp.length - 6);"
            "} else {"
                "vp.src = '';"
                "vp.src = tmp + '&still';"
            "}"
        "});"
        "ap.addEventListener('pause', e => {"
            "let tmp = ap.src;"
            "ap.src = '';"
            "ap.load();"
            "ap.src = tmp;"
        "});"
    "</script>"
"</body>"
"</html>";

static httpd_handle_t server;

typedef struct http_param {
    struct http_param *next;
    char *key, *val;
#define FROM_QUERY  0
#define FROM_BODY   1
#define FROM_ANY    -1
    bool body;
} http_param_t;

static void free_params(void *ctx) {
    for (http_param_t *ptr = ctx, *next; ptr; ptr = next) {
        TRYFREE(ptr->key); TRYFREE(ptr->val);
        next = ptr->next; free(ptr);
    }
}

// parse urlencoded key-val pairs: <key1>[=<val1>]&<key2>=<val2>
static void parse_params(char *buf, bool body, void **arg) {
    if (!buf || !arg) return;
    http_param_t **ctx = (http_param_t **)arg, *ptr, *param;
    for (char *tok = strtok(buf, "&"); tok; tok = strtok(NULL, "&")) {
        char *eql = strchr(tok, '=') ?: (tok + strlen(tok));
        char *key = strndup(tok, eql - tok);
        char *val = strlen(eql) > 1 ? strdup(eql + 1) : NULL;
        if (!key) { TRYFREE(val); break; } // ESP_ERR_NO_MEM
        for (ptr = *ctx;; ptr = ptr->next) {
            if (ptr && ptr->next && strncmp(ptr->key, tok, eql - tok)) continue;
            if (ptr && ptr->next) {
                TRYFREE(ptr->key); TRYFREE(ptr->val);
                ptr->key = key; ptr->val = val; ptr->body = body;
            } else if (ECALLOC(param, 1, sizeof(http_param_t))) {
                TRYFREE(key); TRYFREE(val);
            } else {
                param->key = key; param->val = val; param->body = body;
                ptr = ptr ? (ptr->next = param) : (*ctx = param);
            }
            break;
        }
    }
}

// parse and match key-val pairs: <key1>[="<val1>"]<sep><key2>="<val2>"
static size_t parse_kvs(char *inp, const char *sep, size_t argc,
                        const char **keys, const char **vals)
{
    if (!argc || !keys || !vals) return 0;
    memset(vals, 0, sizeof(const char *) * argc);
    for (char *tok = strtok(inp, sep); tok; tok = strtok(NULL, sep)) {
        char *eql = strchr(tok, '=') ?: (tok + strlen(tok));
        LOOPN(i, argc) {
            if (strncmp(tok, keys[i], eql - tok) || strlen(eql) < 2) continue;
            vals[i] = strtrim(eql + 1, "\""); break;
        }
    }
    size_t count = 0;
    LOOPN(i, argc) { if (vals[i]) count++; }
    return count;
}

static size_t url_decode(char *buf, size_t len) {
    char *ptr = buf, *end = buf + len, hex[] = "0x00";
    while (buf < end) {
        if (buf[0] == '%' && (end - buf) > 2) {
            hex[2] = buf[1];
            hex[3] = buf[2];
            *ptr++ = strtol(hex, NULL, 16);
            buf += 3;
            continue;
        }
        *ptr++ = buf[0] == '+' ? ' ' : buf[0];
        buf++;
    }
    return len - (end - ptr);
}

// Basic HTTP auth
//      Server request: Basic realm="{HOSTNAME}"
//      Client respond: Basic base64({USERNAME}:{PASSWORD})
// Digest HTTP auth
//      Server request: Digest realm="{HOSTNAME}",
//                      nonce="md5({HOSTNAME}:get_timestamp())",
//                      opaque="get_timestamp()",
//                      qop="auth",
//                      algorithm="MD5"
//      Client respond: Digest realm="{HOSTNAME}",
//                      nonce="{SERVER_NONCE}",
//                      opaque="{SERVER_OPAQUE}",
//                      qop="auth", nc={NCVALUE}, cnonce="{CLIENT_CONCE}",
//                      algorithm="MD5",
//                      uri="{URL}",
//                      username="{USERNAME}",
//                      response="md5(
//                          md5({USERNAME}:{HOSTNAME}:{PASSWORD}):
//                          {SERVER_NONCE}:{NCVALUE}:{CLIENT_CONCE}:auth:
//                          md5({METHOD}:{URL})
//                      )"

typedef struct {
    const char *host;
    const char *user;
    const char *pass;
    size_t hlen, ulen, plen;
    char *basic;
    char ha1[33];
} http_auth_t;

static const char * md5_catcol(char out[33], size_t argc, ...) {
    va_list ap;
    uint8_t md5[16];
    md5_context_t ctx;
    va_start(ap, argc);
    esp_rom_md5_init(&ctx);
    while (argc--) {
        const char *chunk = va_arg(ap, const char *);
        size_t len = strlen(chunk ?: "");
        if (!len) continue;
        esp_rom_md5_update(&ctx, chunk, len);
        if (argc) esp_rom_md5_update(&ctx, ":", 1);
    }
    va_end(ap);
    esp_rom_md5_final(md5, &ctx);
    LOOPN(i, sizeof(md5)) { sprintf(out + i * 2, "%02x", md5[i]); }
    return out;
}

static void auth_exit(void *arg) {
    http_auth_t *ctx = arg;
    if (ctx) {
        TRYFREE(ctx->basic);
        TRYFREE(ctx);
    }
}

static http_auth_t * auth_init() {
    http_auth_t *ctx = NULL;
    if (ECALLOC(ctx, 1, sizeof(http_auth_t))) return ctx;
    ctx->hlen = strlen(ctx->host = Config.info.NAME);
    ctx->ulen = strlen(ctx->user = Config.web.HTTP_NAME);
    ctx->plen = strlen(ctx->pass = Config.web.HTTP_PASS);
    md5_catcol(ctx->ha1, 3, ctx->user, ctx->host, ctx->pass);
    char buf[ctx->ulen + ctx->plen + 2];        // 2 for ':' and '\0'
    sprintf(buf, "%s:%s", ctx->user, ctx->pass);
    size_t len = (strlen(buf) + 2) / 3 * 4 + 7; // 7 for "Basic " and '\0'
    if (!ECALLOC(ctx->basic, 1, len))
        b64encode(buf, stpcpy(ctx->basic, "Basic "), strlen(buf));
    return ctx;
}

static char * auth_request(http_auth_t *ctx, bool basic) {
    char *buf = NULL;
    if (!ctx) return buf;
    if (!basic && !EMALLOC(buf, ctx->hlen + 62 + 10 + 32)) {
        char ts[11], nonce[33];
        snprintf(ts, sizeof(ts), "%.0f", get_timestamp_us(NULL));
        md5_catcol(nonce, 2, ctx->host, ts);
        sprintf(buf, "Digest realm=\"%s\",nonce=\"%s\",opaque=\"%s\","
                     "qop=\"auth\",algorithm=\"MD5\"", ctx->host, nonce, ts);
    } else if (basic && !EMALLOC(buf, ctx->hlen + 15)) {
        sprintf(buf, "Basic realm=\"%s\"", ctx->host);
    }
    return buf;
}

static bool auth_validate(http_auth_t *ctx, const char *method, char *resp) {
    if (!ctx) return false;
    if (startswith(resp, "Basic ")) return !strcmp(ctx->basic ?: "", resp);
    if (!startswith(resp, "Digest ")) return false;
    const char *vals[10], *keys[10] = {
        "realm", "username", "nonce", "qop", "algorithm", "response",
        "opaque", "uri", "nc", "cnonce",
    };
    if (parse_kvs(resp + 7, ", ", 10, keys, vals) != 10) return false;
    char nonce[33], ha2[33], digest[33];
    md5_catcol(nonce,   2, vals[0], vals[6]);
    md5_catcol(ha2,     2, method, vals[7]);
    md5_catcol(digest,  6, ctx->ha1, nonce, vals[8], vals[9], "auth", ha2);
    const char *tgt[6] = {ctx->host, ctx->user, nonce, "auth", "MD5", digest};
    LOOPN(i, LEN(tgt)) { if (strcmp(vals[i], tgt[i])) return false; }
    return (strtol(vals[6], NULL, 0) + 86400) > get_timestamp_us(NULL);
}

static const char * get_static(const char *path) {
    // 1. search default file "index.html" under folder
    // 2. try to append ".gz" if origin filename is not found
    static char buf[PATH_MAX_LEN + 3]; // 3 for ".gz"
    fnormr(buf, path);
    if (fisdir(buf)) fjoinr(buf, 2, buf, "index.html");
    return (fisfile(buf) || fisfile(strcat(buf, ".gz"))) ? buf : NULL;
}

static char * get_header(httpd_req_t *req, const char *key) {
    char *buf = NULL;
    size_t len = httpd_req_get_hdr_value_len(req, key) + 1;
    if (len <= 1 || ECALLOC(buf, 1, len)) return buf;
    if (httpd_req_get_hdr_value_str(req, key, buf, len)) TRYFREE(buf);
    return buf;
}

static bool has_header(httpd_req_t *req, const char *key, const char *val) {
    char *header = get_header(req, key);
    bool ret = val ? header && strcasestr(header, val) : header != NULL;
    TRYFREE(header);
    return ret;
}

static bool has_param(httpd_req_t *req, const char *key, int body) {
    for (http_param_t *param = req->sess_ctx; param; param = param->next) {
        if (strcasecmp(param->key, key)) continue;
        if (body == FROM_ANY || param->body == !!body) return true;
    }
    return false;
}

static const char * get_param(httpd_req_t *req, const char *key, int body) {
    for (http_param_t *param = req->sess_ctx; param; param = param->next) {
        if (strcasecmp(param->key, key)) continue;
        if (body == FROM_ANY || param->body == !!body) return param->val;
    }
    return NULL;
}

static esp_err_t send_err(httpd_req_t *req, int code, const char *msg) {
    httpd_err_code_t ecode;
    switch (code) {
    case 400: ecode = HTTPD_400_BAD_REQUEST;            break;
    case 401: ecode = HTTPD_401_UNAUTHORIZED;           break;
    case 403: ecode = HTTPD_403_FORBIDDEN;              break;
    case 404: ecode = HTTPD_404_NOT_FOUND;              break;
    case 405: ecode = HTTPD_405_METHOD_NOT_ALLOWED;     break;
    case 408: ecode = HTTPD_408_REQ_TIMEOUT;            break;
    case 501: ecode = HTTPD_501_METHOD_NOT_IMPLEMENTED; break;
    case 500: FALLTH;
    default:  ecode = HTTPD_500_INTERNAL_SERVER_ERROR;  break;
    }
    return httpd_resp_send_err(req, ecode, msg);
}

static const char * guess_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext || !strlen(++ext))  return TYPE_TEXT;
    if (!strcmp(ext, "gz")) {
        char *prev = memrchr(filename, '.', ext - filename - 1);
        if (!prev) return "application/x-gzip";
        ext = prev + 1;
    }
    if (startswith(ext, "htm"))  return TYPE_HTML;
    if (startswith(ext, "json")) return TYPE_JSON;
    if (startswith(ext, "css"))  return "text/css";
    if (startswith(ext, "xml"))  return "text/xml";
    if (startswith(ext, "ttf"))  return "font/ttf";
    if (startswith(ext, "eot"))  return "font/rot";
    if (startswith(ext, "woff")) return "font/woff";
    if (startswith(ext, "wav"))  return "audio/wav";
    if (startswith(ext, "png"))  return "image/png";
    if (startswith(ext, "gif"))  return "image/gif";
    if (startswith(ext, "jpg"))  return "image/jpeg";
    if (startswith(ext, "ico"))  return "image/x-icon";
    if (startswith(ext, "svg"))  return "image/svg+xml";
    if (startswith(ext, "pdf"))  return "application/pdf";
    if (startswith(ext, "zip"))  return "application/zip";
    if (startswith(ext, "js"))   return "application/javascript";
    return TYPE_TEXT;
}

static esp_err_t send_file(httpd_req_t *req, const char *path, bool dl) {
    struct stat st;
    const char *fullpath = fnorm(path);
    char *basename = strrchr(fullpath, '/');
    if (!basename || stat(fullpath, &st))
        return send_err(req, 500, "Failed to open file");

    const char *mtime = format_datetime(&st.st_mtim);
    if (has_header(req, "If-Modified-Since", mtime)) {
        httpd_resp_set_status(req, "304 Not Modified");
        return send_str(req, NULL);
    }

    char clen[10], cdis[strlen(basename) + 24], *buf = basename + 1;
    httpd_resp_set_type(req, guess_type(basename));
    httpd_resp_set_hdr(req, "Last-Modified", mtime);
    sprintf(cdis, "%s; filename=\"%s\"", dl ? "attachment" : "inline", buf);
    httpd_resp_set_hdr(req, "Content-Disposition", cdis);
    if (endswith(basename, ".gz"))
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if (st.st_size) {
        snprintf(clen, sizeof(clen), "%ld", st.st_size);
        httpd_resp_set_hdr(req, "Content-Length", clen);
    }

    size_t len;
    FILE *fd = fopen(fullpath, "r");
    esp_err_t err = fd ? EMALLOC(buf, CHUNK_SIZE) : ESP_ERR_INVALID_STATE;
    while (!err && ( len = fread(buf, 1, CHUNK_SIZE, fd) )) {
        err = httpd_resp_send_chunk(req, buf, len);
    }
    TRYFREE(buf);
    TRYNULL(fd, fclose);
    if (err) send_err(req, 500, "Failed to send file");
    else     httpd_resp_sendstr_chunk(req, NULL);
    return err;
}

// use ESP_FAIL to close socket, ESP_OK to continue, or ESP_ERR_HTTPD_SKIP_DATA
typedef esp_err_t (*file_cb_t)(httpd_req_t *req, const char *name,
                               size_t idx, char *buf, size_t len, bool end);

static esp_err_t parse_files(httpd_req_t *req, file_cb_t callback) {
#define PARSE_BDARY     0
#define PARSE_HEADER    1
#define PARSE_DATA      2
#define PARSE_FAILED    3
#define PARSE_ERROR     4
    if (!req->content_len) return send_err(req, 400, "Invalid content length");
    char *ctype = get_header(req, "Content-Type"), *buf = NULL;
    char *bdary = strstr(ctype ?: "", "boundary=");
    if (!ctype || !strstr(ctype, TYPE_MPRT) || strlen(bdary ?: "") < 9) {
        TRYFREE(ctype);
        return send_err(req, 400, "Invalid content type");
    }
    if (EMALLOC(buf, CHUNK_SIZE + 1)) {
        TRYFREE(ctype);
        return send_err(req, 500, NULL);
    }
    bdary += 7; bdary[0] = bdary[1] = '-'; strtok(bdary, ";");
    const char *vals[2], *keys[2] = { "name", "filename" }, *sep = "\r\n";
    size_t off = 0, idx = 0, blen = strlen(bdary), slen = strlen(sep);
    int rc = 0, remain = req->content_len, state = PARSE_BDARY;
    while (( rc = httpd_req_recv(req, buf + off, CHUNK_SIZE - off) ) >= 0) {
        if (!rc && !off) break;
        remain -= rc; rc += off; buf[rc] = (off = 0);
        char *head = buf, *tail = head + rc, *crlf = strstr(head, sep);
        while (head < tail && state < PARSE_FAILED) {
            if (state == PARSE_BDARY) {
                char *ptr = strstr(head, bdary);
                if (!crlf || !ptr) break;
                if (!strncmp(ptr + blen, "--", 2)) head = tail;
                if (strncmp(ptr + blen, sep, slen)) break;
                crlf = strstr(head = ptr + blen + slen, sep);
                vals[0] = vals[1] = NULL;
                state = PARSE_HEADER;
            }
            if (state == PARSE_HEADER) {
                if (!crlf) break;
                if (crlf != head) {
                    if (startswith(head, "Content-Disposition:")) {
                        crlf[0] = '\0';
                        parse_kvs(head + 20, "; ", LEN(keys), keys, vals);
                    }
                    crlf = strstr(head = crlf + slen, sep);
                } else if (vals[1]) {                   // filename
                    if (!vals[0]) vals[0] = vals[1];    // name
                    crlf = strstr(head += slen, sep);
                    state = PARSE_DATA;
                    idx = 0;
                } else {
                    state = vals[0] ? PARSE_BDARY : PARSE_ERROR;
                }
            }
            if (state == PARSE_DATA) {
                bool fend = !remain;
                size_t flen = tail - head, clen;
                while (crlf) {
                    if (( clen = tail - crlf - slen ) < blen) {
                        flen = crlf - head;
                    } else if (!strncmp(crlf + slen, bdary, blen)) {
                        flen = crlf - head;
                        fend = true;
                        break;
                    }
                    crlf = strstr(crlf + slen, sep);
                }
                state = fend ? PARSE_BDARY : PARSE_DATA;
                if (!flen && (!fend || !idx)) break;
                ESP_LOGD(TAG, "idx: %u, remain: %d, fend: %d, flen: %u",
                         idx, remain, fend, flen);
                if (!( rc = callback(req, vals[0], idx, head, flen, fend) )) {
                    crlf = strstr(head += flen, "\r\n");
                    idx += flen;
                } else if (rc == ESP_ERR_HTTPD_SKIP_DATA) {
                    crlf = strstr(head += flen, "\r\n");
                    state = PARSE_BDARY;
                } else {
                    state = PARSE_FAILED;
                }
            }
        }
        if (state >= PARSE_FAILED) break;
        if (head < tail && head != buf) memmove(buf, head, off = tail - head);
    }
    if (state == PARSE_ERROR) {
        rc = send_err(req, 400, "Invalid syntax");
    } else if (state == PARSE_FAILED) {
        rc = ESP_FAIL;
    } else if (!remain) {
        rc = send_str(req, NULL);
    } else if (rc == HTTPD_SOCK_ERR_TIMEOUT) {
        rc = send_err(req, 408, NULL);
    }
    TRYFREE(buf);
    TRYFREE(ctype);
    return rc < 0 ? ESP_FAIL : ESP_OK; // HTTPD_SOCK_ERR_xxx < 0
#undef PARSE_BDARY
#undef PARSE_HEADER
#undef PARSE_DATA
#undef PARSE_FAILED
#undef PARSE_ERROR
}

static esp_err_t redirect(httpd_req_t *req, const char *location) {
    // GET unchanged but others may or may not changed to GET
    //   301: Moved Permanently
    //   302: Found
    // GET unchanged and others changed to GET (body lost)
    //   303: See Other
    // Method and body not changed
    //   307: Temporary Redirect
    //   308: Permanent Redirect
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    return send_str(req, "redirect");
}

static void log_msg(httpd_req_t *req) {
    const char *mstr = http_method_str(req->method);
    if (req->content_len) {
        ESP_LOGI(TAG, "%s %s %d", mstr, req->uri, req->content_len);
    } else {
        ESP_LOGI(TAG, "%s %s", mstr, req->uri);
    }
}

static void log_param(httpd_req_t *req) {
    int cnt = 0;
    for (http_param_t *param = req->sess_ctx; param; param = param->next) {
        if (!param->key || !strlen(param->key)) continue;
        ESP_LOGI(TAG, "Param[%d] key:%s, query:%d `%s`",
                 cnt++, param->key, !param->body, param->val ?: "");
    }
}

#define CHECK_REQUEST(req)                                                  \
    do {                                                                    \
        esp_err_t err = check_request(req);                                 \
        if (err) return err < 0 ? ESP_FAIL : ESP_OK;                        \
    } while (0)

static esp_err_t check_request(httpd_req_t *req) {
    log_msg(req);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t err = ESP_OK;
    req->free_ctx = free_params;
    http_param_t *ptr = req->sess_ctx;
    if (ptr) {
        TRYNULL(ptr->next, free_params);
    } else if (!( err = ECALLOC(ptr, 1, sizeof(http_param_t)) )) {
        ptr->key = strdup(""); // SENTINEL
        req->sess_ctx = ptr;
    }
    if (!err) {
        size_t qlen = httpd_req_get_url_query_len(req) + 1;
        if (qlen > 1) {
            char buf[qlen];
            if (!( err = httpd_req_get_url_query_str(req, buf, qlen) )) {
                buf[url_decode(buf, qlen)] = '\0';
                parse_params(buf, false, &req->sess_ctx);
            } else {
                send_err(req, 400, "Invalid query");
            }
        }
    }
    if (!err) { // parse POST body which size < CHUNK_SIZE
        bool wanted = has_header(req, "Content-Type", TYPE_UENC);
        size_t clen = req->content_len < CHUNK_SIZE ? req->content_len : 0;
        if (clen && wanted) {
            char buf[clen + 1];
            if (( err = httpd_req_recv(req, buf, clen) ) > 0) {
                buf[url_decode(buf, err)] = '\0';
                parse_params(buf, true, &req->sess_ctx);
                err = ESP_OK;
            } else if (err == HTTPD_SOCK_ERR_TIMEOUT) {
                send_err(req, 408, NULL);
            } else {
                send_err(req, 400, "Invalid request body");
                err = err ?: ESP_FAIL;
            }
        }
    }
#ifndef CONFIG_BASE_DEBUG
    if (!err && (int)req->user_ctx & FLAG_AP_ONLY) {
        if (!has_header(req, "Host", Config.net.AP_HOST)) {
            send_err(req, 403, "AP interface only");
            err = ESP_ERR_NOT_SUPPORTED;
        }
    }
#endif
    if (!err && (int)req->user_ctx & FLAG_NEED_AUTH) {
        char *auth = get_header(req, "Authorization"), *rstr = NULL;
        const char *mstr = http_method_str(req->method);
        http_auth_t *ctx = httpd_get_global_user_ctx(req->handle);
        if (ctx && !auth_validate(ctx, mstr, auth)) {
            if (( rstr = auth_request(ctx, strbool(Config.web.AUTH_BASE)) )) {
                httpd_resp_set_hdr(req, "Connection", "keep-alive");
                httpd_resp_set_hdr(req, "WWW-Authenticate", rstr);
                send_err(req, 401, "Please login first");
                err = ESP_ERR_HTTPD_AUTH_DIGEST;
            } else {
                send_err(req, 500, "Could not generate auth request");
                err = ESP_FAIL;
            }
        }
        TRYFREE(auth);
        TRYFREE(rstr);
    }
    return err;
}

static esp_err_t on_error(httpd_req_t *req, httpd_err_code_t err) {
    if (req->method == HTTP_OPTIONS) return send_str(req, NULL);
    const char *index = get_static(Config.sys.DIR_HTML);
    if (index) return send_file(req, index, false);
    return send_err(req, 404, ERROR_HTML);
}

static esp_err_t on_success(httpd_req_t *req) {
    CHECK_REQUEST(req);
#ifdef CONFIG_BASE_DEBUG
    size_t nfd = 8;
    int fds[nfd];
    if (httpd_get_client_list(req->handle, &nfd, fds) || !nfd) return ESP_OK;
    ESP_LOGI(TAG, "Got %d clients", nfd);
    LOOPN(i, nfd) {
        ESP_LOGI(TAG, "- fd=%d %s", fds[i], getaddrname(fds[i], false));
    }
#endif
    return send_str(req, NULL);
}

static esp_err_t on_command(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (!req->content_len || req->content_len > CHUNK_SIZE)
        return send_err(req, 400, "Invalid content length");
    log_param(req);
    const char *cmd = get_param(req, "cmd", FROM_ANY) ?: "";
    if (strlen(cmd)) {
        char *ret = console_handle_command(cmd, true, false);
        if (ret) httpd_resp_set_type(req, "text/plain");
        send_str(req, ret);
        TRYFREE(ret);
        return ESP_OK;
    }
    const char *gcode = get_param(req, "gcode", FROM_ANY) ?: "";
    if (strlen(gcode)) {
        ESP_LOGW(TAG, "GCode parser: `%s`", gcode);
        return send_err(req, 501, "GCode parser is not implemented yet");
    }
    return send_err(req, 400, "Invalid parameter");
}

static esp_err_t on_upload_file(httpd_req_t *req, const char *name,
                                size_t idx, char *data, size_t len, bool end)
{
    static FILE *fd;
    static char *fn;
    if (!idx) {
        const char *path = fnorm(name);
        if (fisfile(path) && !has_param(req, "overwrite", FROM_ANY)) {
            ESP_LOGD(TAG, "Skip upload file %s", name);
            return ESP_ERR_HTTPD_SKIP_DATA;
        }
        if (!( fn = strdup(path) ) || !( fd = fopen(path, "w") )) {
            send_err(req, 500, "Could not open file to write");
            goto error;
        }
        fprintf(stderr, "Upload file: %s\n", path);
    }
    if (fd && len) {
        if (len != fwrite(data, 1, len, fd)) {
            send_err(req, 500, "Could not write to file");
            goto error;
        }
        fprintf(stderr, "\rProgress: %8s", format_size(idx));
        fflush(stderr);
    }
    if (end && fd) {
        fputc('\n', stderr);
        fprintf(stderr, "Upload success: %s\n", format_size(idx + len));
        TRYNULL(fd, fclose);
        TRYFREE(fn);
    }
    return ESP_OK;
error:
    TRYNULL(fd, fclose);
    if (fn) unlink(fn);
    TRYFREE(fn);
    return ESP_FAIL;
}

static esp_err_t on_editor(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (req->method == HTTP_POST) return parse_files(req, on_upload_file);
    const char *type = get_param(req, "type", FROM_ANY) ?: "";
    const char *path = fnorm(get_param(req, "path", FROM_ANY));
    if (!strlen(path)) {
        if (req->method == HTTP_GET && get_static(Config.sys.DIR_HTML))
            return redirect(req, "/editor");
        return send_err(req, 400, "No path specified");
    }
    if (req->method == HTTP_GET) {
        if (has_param(req, "list", FROM_ANY)) {
            if (!fisdir(path)) return send_err(req, 400, "No entries found");
            char *json = filesys_listdir_json(FILESYS_FLASH, path);
            if (!json)         return send_err(req, 500, "JSON dump failed");
            httpd_resp_set_type(req, TYPE_JSON);
            send_str(req, json);
            TRYFREE(json);
            return ESP_OK;
        } else if (!fisfile(path)) {
            return send_err(req, 400, "Path is directory");
        } else {
            return send_file(req, path, has_param(req, "download", FROM_ANY));
        }
    } else if (req->method == HTTP_PUT) {
        if (!strcmp(type, "dir")) {
            if (!fmkdir(path)) return send_err(req, 500, "Create dir failed");
        } else {
            if (!ftouch(path)) return send_err(req, 500, "Create file failed");
        }
    } else if (req->method == HTTP_DELETE) {
        if (!strcmp(type, "dir")) {
            if (!frmdir(path)) return send_err(req, 500, "Delete dir failed");
        } else if (fisfile(path)) {
            if (unlink(path))  return send_err(req, 500, "Delete file failed");
        }
        const char *url = get_param(req, "from", FROM_ANY) ?: "";
        if (strlen(url))       return redirect(req, url);
    } else                     return send_err(req, 405, NULL);
    return send_str(req, NULL); // 200 OK
}

static esp_err_t on_config(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (req->method == HTTP_POST) {
        const char *json = get_param(req, "json", FROM_BODY);
        if (json && !config_loads(json)) {
            send_err(req, 500, "Failed to load config from JSON");
        } else {
            send_str(req, NULL);
        }
    } else {
        char *json = config_dumps();
        if (!json) {
            send_err(req, 500, "Failed to dump config into JSON");
        } else {
            httpd_resp_set_type(req, TYPE_JSON);
            send_str(req, json);
        }
        TRYFREE(json);
    }
    return ESP_OK;
}

static inline void restart(void *arg) { esp_restart(); NOTUSED(arg); }

static esp_err_t on_update_file(httpd_req_t *req, const char *name,
                                size_t idx, char *data, size_t len, bool end)
{
    if (!idx) {
        fprintf(stderr, "Update file: %s\n", name);
        if (strcmp(name, "update")) return ESP_ERR_HTTPD_SKIP_DATA;
        if (!has_param(req, "size", -1) && !ota_updation_begin(0)) goto error;
        led_set_blink(1);
    }
    if (len && !ota_updation_write(data, len)) goto error;
    if (end) {
        if (!ota_updation_end()) goto error;
        fprintf(stderr, "Update success: %s\n", format_size(idx + len));
        send_str(req, "OTA Updation success: reboot now");
        setTimeout(500, restart, NULL);
    }
    return ESP_OK;
error:
    led_set_blink(0);
    send_err(req, 500, ota_updation_error());
    return ESP_FAIL;
}

static esp_err_t on_update(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (req->method == HTTP_GET) {
        if (!has_param(req, "raw", FROM_ANY) && get_static(Config.sys.DIR_HTML))
            return redirect(req, "/updation"); // vue.js router
        return send_str(req, UPDATE_HTML);
    }
    log_param(req);
    if (has_param(req, "reset", FROM_ANY)) {
        ota_updation_reset();
        if (!req->content_len)
            return send_str(req, "OTA Updation reset done");
    }
    if (has_param(req, "size", FROM_ANY)) {
        int size;
        if (!parse_int(get_param(req, "size", FROM_ANY), &size))
            return send_err(req, 400, NULL);
        if (!ota_updation_begin(size))
            return send_err(req, 500, ota_updation_error());
        if (!req->content_len)
            return send_str(req, "OTA Updation is ready");
    }
    return parse_files(req, on_update_file);
}

static esp_err_t on_static(httpd_req_t *req) {
    CHECK_REQUEST(req);
    int flag = (int)req->user_ctx;
    const char * dirname = "/";
    if (flag & FLAG_DIR_DATA && strlen(Config.sys.DIR_DATA)) {
        dirname = Config.sys.DIR_DATA;
    } else if (flag & FLAG_DIR_DOCS && strlen(Config.sys.DIR_DOCS)) {
        dirname = Config.sys.DIR_DOCS;
    } else if (flag & FLAG_DIR_HTML && strlen(Config.sys.DIR_HTML)) {
        dirname = Config.sys.DIR_HTML;
    }
    char basename[strcspn(req->uri, "?#") + 1];
    snprintf(basename, sizeof(basename), req->uri); // truncate and pad null
    if (startswith(basename, dirname)) dirname = NULL;
    const char *path = get_static(fjoin(2, dirname, basename));
    return path ? send_file(req, path, 0) : on_error(req, HTTPD_404_NOT_FOUND);
}

typedef struct {
    int fd;
    bool stop, once;
    char addr[ADDRSTRLEN];
    esp_event_handler_instance_t inst;
} http_media_t;

static UNUSED int socket_send_all(int fd, void *buf, size_t len) {
    int ret = 0;
    while (len > 0 && fd > 0) {
        if (( ret = httpd_socket_send(server, fd, buf, len, 0) ) < 0) break;
        buf += ret;
        len -= ret;
    }
    if (len) ESP_LOGD(TAG, "%d remain = %d, ret = %d", fd, len, ret);
    return MIN(ret, 0);
}

#ifdef CONFIG_BASE_USE_I2S
static http_media_t audio_ctx;

static void handle_audio_streaming(void *arg) {
    audio_evt_t *evt = arg;
    notify_increase(evt->task);
    if (!evt->len) goto stop;
    if (!socket_send_all(autio_ctx.fd, evt->data, evt->len)) goto exit;
stop:
    UREGEVTS(AVC, audio_ctx.inst);
    if (audio_ctx.stop) AUDIO_STOP();
    ESP_LOGI(TAG, "Audio stream to %s stopped", audio_ctx.addr);
exit:
    notify_decrease(evt->task);
}
#endif

#ifdef CONFIG_BASE_USE_CAM
static http_media_t video_ctx;

static void handle_video_streaming(void *arg) {
    if (!video_ctx.fd) return;  // once
    video_evt_t *evt = arg;
    notify_increase(evt->task);
    if (!evt->mode) goto exit;  // VID_EVENT_START
    if (!evt->len) goto stop;   // VID_EVENT_STOP
    static char bdary[51 + 14 + 1] =
        "--FRAME\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: ";
    int blen = 51 + snprintf(bdary + 51, 15, "%d\r\n\r\n", evt->len);
    if (!socket_send_all(video_ctx.fd, bdary, blen) &&
        !socket_send_all(video_ctx.fd, evt->data, evt->len) &&
        !video_ctx.once) goto exit;
    if (video_ctx.once) {
        httpd_sess_trigger_close(server, video_ctx.fd);
        video_ctx.fd = 0;
    }
stop:
    UREGEVTS(AVC, video_ctx.inst);
    if (video_ctx.stop) VIDEO_STOP();
    ESP_LOGI(TAG, "Video stream to %s stopped", video_ctx.addr);
exit:
    notify_decrease(evt->task);
}
#endif

#if defined(CONFIG_BASE_USE_I2S) || defined(CONFIG_BASE_USE_CAM)
static void on_media_data(void *func, esp_event_base_t b, int32_t i, void *p) {
    // this function is called in sys_evt task
    // recv data from capture task
    // send data to httpd task
    httpd_queue_work(server, func, *(void **)p);
    return; NOTUSED(b); NOTUSED(i);
}
#endif

static esp_err_t on_media(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (has_param(req, "video", FROM_ANY)) {
#ifndef CONFIG_BASE_USE_CAM
        send_err(req, 403, "Video stream not available");
#else
        const char *video = get_param(req, "video", FROM_ANY);
        if (req->method == HTTP_POST) {
            if (video && CAMERA_LOADS(video)) {
                return send_err(req, 500, "Failed to load config from JSON");
            } else {
                return send_str(req, NULL);
            }
        }
        if (strcmp(video ?: "", "mjpg")) {
            char *json = NULL;
            if (CAMERA_DUMPS((const char *)&json)) {
                send_err(req, 500, "Failed to dump config from JSON");
            } else {
                httpd_resp_set_type(req, TYPE_JSON);
                send_str(req, json);
            }
            TRYFREE(json);
            return ESP_OK;
        }
        if (video_ctx.inst) return send_err(req, 403, "Video stream is busy");
        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace;boundary=--FRAME\r\n"
            "Cache-Control: no-store\r\n"
            "X-Framerate: 60\r\n\r\n";
        int fd = video_ctx.fd = httpd_req_to_sockfd(req);
        httpd_socket_send(req->handle, fd, resp, strlen(resp), 0);
        REGEVTS(AVC, on_media_data, handle_video_streaming, &video_ctx.inst);
        if (!video_ctx.inst) return ESP_FAIL;
        video_ctx.stop = xTaskGetHandle("video") == NULL;
        video_ctx.once = has_param(req, "still", FROM_ANY);
        VIDEO_START(-1);
        snprintf(video_ctx.addr, ADDRSTRLEN, getaddrname(fd, false));
        ESP_LOGI(TAG, "Video stream to %s started", video_ctx.addr);
#endif
    } else if (has_param(req, "audio", FROM_ANY)) {
#ifndef CONFIG_BASE_USE_I2S
        send_err(req, 403, "Audio stream not available");
#else
        const char *audio = get_param(req, "audio", FROM_ANY);
        if (strcmp(audio ?: "", "wav")) return send_str(req, NULL);
        if (audio_ctx.inst) return send_err(req, 403, "Audio stream is busy");
        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: audio/wav\r\n"
            "Cache-Control: no-store\r\n\r\n";
        int fd = audio_ctx.fd = httpd_req_to_sockfd(req);
        httpd_socket_send(req->handle, fd, resp, strlen(resp), 0);
        REGEVTS(AVC, on_media_data, handle_audio_streaming, &audio_ctx.inst);
        if (!audio_ctx.inst) return ESP_FAIL;
        audio_ctx.stop = xTaskGetHandle("audio") == NULL;
        AUDIO_START(-1);
        snprintf(audio_ctx.addr, ADDRSTRLEN, getaddrname(fd, false));
        ESP_LOGI(TAG, "Audio stream to %s started", audio_ctx.addr);
#endif
    } else {
        return send_str(req, MEDIA_HTML);
    }
    return ESP_OK;
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
static void handle_websocket_message(void *arg) {
    httpd_ws_frame_t *pkt = arg;
    if (!pkt) return;
    if (pkt->payload) {
        int fd = *(int *)(pkt->payload + pkt->len + 1);
        char *ret = pkt->payload[0] == '{' ?
                    console_handle_rpc((char *)pkt->payload) :
                    console_handle_command((char *)pkt->payload, true, false);
        if (ret) {
            httpd_ws_frame_t rep = {
                .final = true, .fragmented = false,
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)ret, .len = strlen(ret)
            };
            httpd_ws_send_frame_async(server, fd, &rep);
            free(ret);
        }
        free(pkt->payload);
    }
    free(pkt);
}

static esp_err_t on_websocket(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    if (req->method == HTTP_GET) return err; // handshake done
    httpd_ws_frame_t *pkt = NULL;
    if (( err = ECALLOC(pkt, 1, sizeof(httpd_ws_frame_t)) ) ||
        ( err = httpd_ws_recv_frame(req, pkt, 0) ) ||
        ( pkt->type != HTTPD_WS_TYPE_TEXT ) ||
        ( pkt->len > (2 * CHUNK_SIZE) || !pkt->final ) ||
        ( err = ECALLOC(pkt->payload, 1, pkt->len + 1 + sizeof(int)) ) ||
        ( err = httpd_ws_recv_frame(req, pkt, pkt->len) )
    ) {
        if (pkt) TRYFREE(pkt->payload);
        TRYFREE(pkt);
        return err;
    }
    *(int *)(pkt->payload + pkt->len + 1) = httpd_req_to_sockfd(req);
    return httpd_queue_work(req->handle, handle_websocket_message, pkt);
}
#endif

bool rewrite_api(const char *tpl, const char *uri, size_t len) {
    // Rewrite "/api/xxx" to "/xxx"
    if (startswith(uri, "/api/")) { uri += 4; len -= 4; }
    return httpd_uri_match_wildcard(tpl, uri, len);
}

void server_initialize() {
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    server_loop_begin();
}

#ifndef CONFIG_HTTPD_WS_SUPPORT
#   define HTTP_API(u, m, h, c)                                             \
        {                                                                   \
            .uri      = (u),                                                \
            .method   = HTTP_ ## m,                                         \
            .handler  = (h),                                                \
            .user_ctx = (void *)(c)                                         \
        }
#   define WS_API(u, h, c)
#else
#   define HTTP_API(u, m, h, c)                                             \
        {                                                                   \
            .uri      = (u),                                                \
            .method   = HTTP_ ## m,                                         \
            .handler  = (h),                                                \
            .user_ctx = (void *)(c),                                        \
            .is_websocket = false,                                          \
            .handle_ws_control_frames = false,                              \
            .supported_subprotocol = NULL                                   \
        }
#   define WS_API(u, h, c)                                                  \
        {                                                                   \
            .uri      = (u),                                                \
            .method   = HTTP_GET,                                           \
            .handler  = (h),                                                \
            .user_ctx = (void *)(c),                                        \
            .is_websocket = true,                                           \
            .handle_ws_control_frames = false,                              \
            .supported_subprotocol = NULL                                   \
        }
#endif // CONFIG_HTTPD_WS_SUPPORT

// Use `httpd_config_t.global_user_ctx` to store authorization info
// Use `httpd_req_t.sess_ctx` to store `http_param_t *` which is auto released
// Use `httpd_req_t.user_ctx` to store `FLAG_XXXs` which is never released
void server_loop_begin() {
    if (server) return;

    httpd_uri_t apis[] = {
        // WebSocket APIs
        WS_API("/ws", on_websocket, NULL),
        // STA APIs
        HTTP_API("/alive",  GET,    on_success, NULL),
        HTTP_API("/exec",   POST,   on_command, FLAG_NEED_AUTH),
        HTTP_API("/media",  GET,    on_media,   FLAG_NEED_AUTH),
        HTTP_API("/media",  POST,   on_media,   FLAG_NEED_AUTH),
        // AP APIs
        HTTP_API("/edit",   GET,    on_editor,  FLAG_AP_ONLY),
        HTTP_API("/edit",   PUT,    on_editor,  FLAG_AP_ONLY | FLAG_NEED_AUTH),
        HTTP_API("/edit",   DELETE, on_editor,  FLAG_AP_ONLY | FLAG_NEED_AUTH),
        HTTP_API("/edit",   POST,   on_editor,  FLAG_AP_ONLY | FLAG_NEED_AUTH),
        HTTP_API("/config", GET,    on_config,  FLAG_AP_ONLY),
        HTTP_API("/config", POST,   on_config,  FLAG_AP_ONLY | FLAG_NEED_AUTH),
        HTTP_API("/update", GET,    on_update,  FLAG_AP_ONLY),
        HTTP_API("/update", POST,   on_update,  FLAG_AP_ONLY | FLAG_NEED_AUTH),
        HTTP_API("/apmode", GET,    on_success, FLAG_AP_ONLY),
        // Static files
        HTTP_API("/data/*", GET,    on_static,  FLAG_DIR_DATA | FLAG_AP_ONLY),
        HTTP_API("/docs/*", GET,    on_static,  FLAG_DIR_DOCS),
        HTTP_API("/*",      GET,    on_static,  FLAG_DIR_HTML),
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = LEN(apis);
    config.uri_match_fn = rewrite_api;
    config.global_user_ctx = auth_init();
    config.global_user_ctx_free_fn = auth_exit;

    // Call stack: httpd_start => httpd_thread => httpd_server => select
    // If select rfds:
    //      1. httpd_accept_conn
    //      2. httpd_process_ctrl_msg <= httpd_queue_work
    //      3. httpd_process_session  => uri->handler(req)
    // So avoid using infinite loop in uri handlers.
    // Use httpd_queue_work instead.
    esp_err_t err = httpd_start(&server, &config);
    if (err) {
        ESP_LOGE(TAG, "Start server failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Start server on port %d", config.server_port);
        ITERP(api, apis) {
            if (( err = httpd_register_uri_handler(server, api) )) {
                ESP_LOGE(TAG, "Register uri `%s` failed: %s",
                         api->uri, esp_err_to_name(err));
                break;
            }
        }
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, on_error);
    }
}

void server_loop_end() { TRYNULL(server, httpd_stop); }

#else

void server_initialize() {}
void server_loop_begin() {}
void server_loop_end() {}

#endif // CONFIG_BASE_USE_WEBSERVER
