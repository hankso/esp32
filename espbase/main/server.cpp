/* 
 * File: server.cpp
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:29:05
 */
#include "server.h"
#include "config.h"
#include "update.h"
#include "drivers.h"
#include "console.h"
#include "filesys.h"
#include "ledmode.h"
#include "timesync.h"

#define TYPE_HTML "text/html"
#define TYPE_TEXT "text/plain"
#define TYPE_JSON "application/json"
#define TYPE_UENC "application/x-www-form-urlencoded"
#define TYPE_MPRT "multipart/form-data"
#define CHUNK_SIZE 512

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
"<body>"
"</html>";

#if defined(CONFIG_BASE_USE_WIFI) && defined(CONFIG_BASE_USE_WEBSERVER)

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

static void log_msg(AsyncWebServerRequest *req, const char *msg = "") {
    ESP_LOGI(TAG, "%s %s %s", req->methodToString(), req->url().c_str(), msg);
}

static void log_param(AsyncWebServerRequest *req) {
    size_t len = req->params();
    if (!len) return;
    AsyncWebParameter *p;
    for (uint8_t i = 0; (i < len) && (p = req->getParam(i)); i++) {
        ESP_LOGI(TAG, "Param[%d] key:%s, post:%d, file:%d[%d] `%s`",
                i, p->name().c_str(), p->isPost(), p->isFile(), p->size(),
                p->isFile() ? "[skip file content]" : p->value().c_str());
    }
}

/******************************************************************************
 * HTTP & static files API
 */

String getWebpage() {
    static String basename = "index.html", none = "";
    String path = Config.sys.DIR_HTML + basename;
    return (FFS.exists(path) || FFS.exists(path + ".gz")) ? path : none;
}

void onSuccess(AsyncWebServerRequest *req) {
    log_msg(req);
    req->send(200);
}

void onCommand(AsyncWebServerRequest *req) {
    log_msg(req);
    log_param(req);
    if (req->hasParam("cmd", true)) {
        char *ret = NULL;
        const char *cmd = req->getParam("cmd", true)->value().c_str();
        if (!strlen(cmd)) {
            req->send(400, TYPE_TEXT, "Invalid command to execute");
        } else if (( ret = console_handle_command(cmd, true, false) )) {
            req->send(200, TYPE_TEXT, ret);
        } else {
            req->send(200);
        }
        TRYFREE(ret);
    } else if (req->hasParam("gcode", true)) {
        const char *gcode = req->getParam("gcode", true)->value().c_str();
        ESP_LOGW(TAG, "GCode parser: `%s`", gcode);
        req->send(501, TYPE_TEXT, "GCode parser is not implemented yet");
    } else {
        req->send(400, TYPE_TEXT, "Invalid parameter");
    }
}

void onConfig(AsyncWebServerRequest *req) {
    log_msg(req);
    if (req->hasParam("json", true)) {
        if (!config_loads(req->getParam("json", true)->value().c_str())) {
            req->send(500, TYPE_TEXT, "Load config from JSON failed");
        } else {
            req->send(200);
        }
    } else {
        char *json = config_dumps();
        if (!json) {
            req->send(500, TYPE_TEXT, "Dump configs into JSON failed");
        } else {
            req->send(200, TYPE_JSON, json);
        }
        TRYFREE(json);
    }
}

void onUpdate(AsyncWebServerRequest *req) {
    log_msg(req);
    if (!req->hasParam("raw") && getWebpage().length()) {
        req->redirect("/updation"); // vue.js SPA router
    } else {
        req->send(200, TYPE_HTML, UPDATE_HTML); // fallback to simple one
    }
}

void onUpdateDone(AsyncWebServerRequest *req) {
    log_msg(req);
    log_param(req);
    if (req->hasParam("reset", true)) {
        log_msg(req, "reset");
        ota_updation_reset();
        req->send(200, TYPE_TEXT, "OTA Updation reset done");
    } else if (req->hasParam("size", true)) {
        String size = req->getParam("size", true)->value();
        log_msg(req, ("size: " + size).c_str());
        // erase OTA target partition for preparing
        if (!ota_updation_begin(size.toInt())) {
            req->send(400, TYPE_TEXT, ota_updation_error());
        } else {
            req->send(200, TYPE_TEXT, "OTA Updation ready for upload");
        }
    } else {
        const char *error = ota_updation_error();
        if (error) return req->send(400, TYPE_TEXT, error);
        req->send(200, TYPE_TEXT, "OTA Updation success - reboot");
        msleep(500);
        esp_restart();
    }
}

void onUpdatePost(
    AsyncWebServerRequest *req, String filename,
    size_t index, uint8_t *data, size_t len, bool isFinal
) {
    if (!index) {
        log_msg(req, filename.c_str());
        if (!ota_updation_begin(0)) {
            AsyncWebServerResponse *res = req->beginResponse(
                400, TYPE_TEXT, ota_updation_error());
            res->addHeader("Connection", "close");
            return req->send(res);
        }
        printf("Update file: %s\n", filename.c_str());
    }
    if (!ota_updation_error()) {
        led_set_light(0, 1);
        ota_updation_write(data, len);
        led_set_light(0, 0);
    }
    if (isFinal) {
        if (!ota_updation_end())
            return req->send(400, TYPE_TEXT, ota_updation_error());
        printf("Update success: %s\n", format_size(index + len, false));
    }
}

void onEditor(AsyncWebServerRequest *req) {
    log_msg(req);
    if (req->hasParam("list")) { // listdir
        String path = req->getParam("path")->value();
        if (!path.startsWith("/")) path = "/" + path;
        File root = FFS.open(path);
        if (!root) {
            req->send(404, TYPE_TEXT, path + " dir does not exists");
        } else if (!root.isDirectory()) {
            req->send(400, TYPE_TEXT, "No file entries under " + path);
        } else {
            root.close();
            char *json = FFS.list(path.c_str());
            req->send(200, TYPE_JSON, json ? json : "failed");
            TRYFREE(json);
        }
    } else if (req->hasParam("path")) { // serve static files for editor
        String path = req->getParam("path")->value();
        if (!path.startsWith("/")) path = "/" + path;
        File file = FFS.open(path);
        if (!file) {
            req->send(404, TYPE_TEXT, path + " file does not exist");
        } else if (file.isDirectory()) {
            req->send(400, TYPE_TEXT, "Could not download dir " + path);
        } else {
            req->send(file, path, String(), req->hasParam("download"));
        }
    } else if (getWebpage().length()) { // redirect to editor page
        req->redirect("/editor");
    } else {
        req->send(404, TYPE_HTML, ERROR_HTML);
    }
}

void onCreate(AsyncWebServerRequest *req) {
    // handle file|dir create
    log_msg(req);
    if (!req->hasParam("path")) {
        return req->send(400, TYPE_TEXT, "No filename specified");
    }
    String
        path = req->getParam("path")->value(),
        type = req->hasParam("type") ? \
               req->getParam("type")->value() : "file";
    if (type == "file") {
        if (FFS.exists(path)) {
            return req->send(403, TYPE_TEXT, "File already exists");
        }
        File file = FFS.open(path, "w");
        if (!file) {
            return req->send(500, TYPE_TEXT, "Create failed");
        }
    } else if (type == "dir") {
        File dir = FFS.open(path);
        if (dir.isDirectory()) {
            dir.close();
            return req->send(403, TYPE_TEXT, "Dir already exists");
        } else if (!FFS.mkdir(path)) {
            return req->send(500, TYPE_TEXT, "Create failed");
        }
    }
    req->send(200);
}

void onDelete(AsyncWebServerRequest *req) {
    // handle file|dir delete
    log_msg(req);
    if (!req->hasParam("path"))
        return req->send(400, TYPE_TEXT, "No path specified");
    String path = req->getParam("path")->value();
    if (!FFS.exists(path))
        return req->send(403, TYPE_TEXT, "File/dir does not exist");
    if (
        path == Config.sys.DIR_DATA ||
        path == Config.sys.DIR_DOCS ||
        path == Config.sys.DIR_HTML
    )
        return req->send(400, TYPE_TEXT, "No access to delete");
    if (!FFS.remove(path)) {
        req->send(500, TYPE_TEXT, "Delete file/dir failed");
    } else if (req->hasParam("from")) {
        req->redirect(req->getParam("from")->value());
    } else {
        req->send(200);
    }
}

void onUpload(
    AsyncWebServerRequest *req, String filename,
    size_t index, uint8_t *data, size_t len, bool isFinal
) {
    static File file;
    if (!index) {
        log_msg(req, filename.c_str());
        if (file) return req->send(400, TYPE_TEXT, "Busy uploading");
        if (!filename.startsWith("/")) filename = "/" + filename;
        if (FFS.exists(filename) && !req->hasParam("overwrite")) {
            return req->send(403, TYPE_TEXT, "File already exists");
        }
        printf("Upload file: %s\n", filename.c_str());
        file = FFS.open(filename, "w");
    }
    if (file) {
        led_set_light(0, 1);
        file.write(data, len);
        led_set_light(0, 0);
        printf("\rProgress: %8s", format_size(index, false));
    }
    if (isFinal && file) {
        file.flush();
        file.close();
        printf("Upload success: %s\n", format_size(index + len, false));
    }
}

void onUploadStrict(
    AsyncWebServerRequest *req, String filename,
    size_t index, uint8_t *data, size_t len, bool isFinal
) {
    if (!filename.startsWith(Config.sys.DIR_DATA)) {
        log_msg(req, "No access to upload");
        return req->send(400, TYPE_TEXT, "No access to upload");
    }
    onUpload(req, filename, index, data, len, isFinal);
}

void onError(AsyncWebServerRequest *req) {
    log_msg(req, "onError");
    if (req->method() == HTTP_OPTIONS) return req->send(200);
    String index = getWebpage();
    if (index.length()) return req->send(FFS, index);
    return req->send(404, TYPE_HTML, ERROR_HTML);
}

/******************************************************************************
 * WebSocket message parser and callbacks
 */

typedef struct {
    AsyncWebSocketClient *client;
    char name[32];
    char *buf;
    size_t idx;
    size_t len;
} wsdata_ctx_t;

void handle_websocket_message(wsdata_ctx_t *ctx) {
    if (!ctx->buf || !ctx->len) return;
    char *ret;
    if (ctx->buf[0] == '{') {
        ret = console_handle_rpc(ctx->buf); // handle JSON-RPC
    } else {
        ret = console_handle_command(ctx->buf, true, false); // handle cmds
    }
    if (ret) {
        ctx->client->text(ret);
        TRYFREE(ret);
    }
}

/* AsyncWebSocket Frame Information struct contains
 * {
 *   uint8_t  message_opcode // WS_TEXT | WS_BINARY
 *   uint8_t  opcode         // WS_CONTINUATION if fragmented
 *
 *   uint32_t num            // frame number of a fragmented message
 *   uint8_t  final          // whether this is the last frame
 *   
 *   uint64_t len            // length of the current frame
 *   uint64_t index          // data offset in current frame
 * }
 *
 * Assuming that a Message from the ws client is fragmented as follows:
 *   | Frame0__ | Frame1___ | Frame2____ |
 *     ^   ^      ^           ^  ^
 *     A   B      C           D  E
 * A: num=0, final=false, opcode=message_opcode,  index=0, len=8,  size=4
 * B: num=0, final=false, opcode=message_opcode,  index=4, len=8,  size=4
 * C: num=1, final=false, opcode=WS_CONTINUATION, index=0, len=9,  size=9
 * D: num=2, final=true,  opcode=WS_CONTINUATION, index=0, len=10, size=3
 * E: num=2, final=true,  opcode=WS_CONTINUATION, index=3, len=10, size=7
 */
static void onWebSocketData(
    wsdata_ctx_t *ctx, AwsFrameInfo *info, uint8_t *data, size_t size
) {
    if (info->final && info->num == 0) {
        // Message is not fragmented so there's only one frame
        ESP_LOGD(TAG, "%s msg[%llu]", ctx->name, info->len);
        if (info->index == 0) {
            // We are starting this only frame
            ctx->len = ((info->opcode == WS_TEXT) ? 1 : 2) * info->len + 1;
            if (EMALLOC(ctx->buf, ctx->len)) goto clean;
            ctx->idx = 0;
        } else if (!ctx->buf) {
            ESP_LOGW(TAG, "%s error: lost first packets. Skip", ctx->name);
            goto clean;
        } else {
            // This frame is splitted into packets
        }
    } else if (info->index == 0) {
        // Message is fragmented. Starting a new frame
        ESP_LOGD(TAG, "%s msg frame%d[%llu]", ctx->name, info->num, info->len);
        size_t len = ((info->message_opcode == WS_TEXT) ? 1 : 2) * info->len;
        if (info->num == 0) {
            // Starting the first frame
            if (EMALLOC(ctx->buf, ctx->len = len + 1)) goto clean;
            ctx->idx = 0;
        } else if (!ctx->buf) {
            ESP_LOGW(TAG, "%s error: lost first frame. Skip", ctx->name);
            goto clean;
        } else {
            // Extend buffer for the coming frame
            char *tmp = (char *)realloc(ctx->buf, ctx->len + len);
            if (tmp == NULL) goto clean;
            ctx->buf = tmp;
            ctx->len += len;
        }
    } else if (!ctx->buf) {
        ESP_LOGW(TAG, "%s error: lost message head. Skip", ctx->name);
        goto clean;
    } else {
        // Message is fragmented. Current frame is splitted
        ESP_LOGD(TAG, "%s msg frame%d[%llu] packet[%llu-%llu]",
                 ctx->name, info->num, info->len,
                 info->index, info->index + size);
    }
    // Save/append packets to message buffer
    if (info->opcode == WS_TEXT) {
        ctx->idx += snprintf(
            ctx->buf + ctx->idx, ctx->len - ctx->idx, (char *)data);
    } else {
        for (size_t i = 0; i < size; i++) {
            ctx->idx += snprintf(
                ctx->buf + ctx->idx, ctx->len - ctx->idx, "%02X", data[i]);
        }
    }
    if (!info->final || info->index + size != info->len) return;
    // Current message end and all frames buffered
    handle_websocket_message(ctx);
clean:
    ctx->idx = ctx->len = 0;
    TRYFREE(ctx->buf);
    return;
}

static void onWebSocket(
    AsyncWebSocket *server, AsyncWebSocketClient *client,
    AwsEventType type, void *arg, uint8_t *data, size_t datalen
) {
    wsdata_ctx_t *ctx;
    server->cleanupClients();
    if (!( ctx = (wsdata_ctx_t *)client->_tempObject )) {
        if (ECALLOC(ctx, 1, sizeof(wsdata_ctx_t))) {
            ESP_LOGE(TAG, "Could not allocate context for websocket");
            return client->close();
        }
        ctx->client = client;
        snprintf(ctx->name, sizeof(ctx->name), "ws#%u %s:%d",
                 client->id(), client->remoteIP().toString().c_str(),
                 client->remotePort());
        client->_tempObject = ctx;
    }
    switch (type) {
    case WS_EVT_CONNECT:
        ESP_LOGI(TAG, "%s connected", ctx->name);
        client->ping();
        break;
    case WS_EVT_DISCONNECT:
        ESP_LOGI(TAG, "%s disconnected", ctx->name);
        TRYFREE(client->_tempObject);
        break;
    case WS_EVT_PONG:
        break;
    case WS_EVT_ERROR:
        ESP_LOGW(TAG, "%s error(%u)", ctx->name, *((uint16_t *)arg));
        break;
    case WS_EVT_DATA:
        onWebSocketData(ctx, (AwsFrameInfo *)arg, data, datalen);
        break;
    default:
        ESP_LOGD(TAG, "%s event %d datalen %u", ctx->name, type, datalen);
    }
}

/******************************************************************************
 * Web Server API
 */

class APIRewrite : public AsyncWebRewrite {
    public:
        // Rewrite "/api/xxx" to "/xxx"
        APIRewrite(): AsyncWebRewrite("/api/", "/") {}
        bool match(AsyncWebServerRequest *request) override {
            if (!request->url().startsWith(_from)) return false;
            _toUrl = request->url().substring(_from.length() - 1);
            return true;
        }
};

static AsyncWebServer webserver = AsyncWebServer(80);
static AsyncWebSocket websocket = AsyncWebSocket("/ws");
static bool started = false;

void server_loop_begin() {
    if (started) return;
    webserver.begin();
    started = true;
}

void server_loop_end() {
    if (started) {
        webserver.end();
        started = false;
    }
}

void server_initialize() {
    webserver.reset();
    webserver.addRewrite(new APIRewrite());

    // WebSocket Handler
    websocket.onEvent(onWebSocket);
    websocket.setAuthentication(Config.web.WS_NAME, Config.web.WS_PASS);
    webserver.addHandler(&websocket);
    // STA APIs
    webserver.on("/alive", HTTP_ANY, onSuccess);
    webserver.on("/exec", HTTP_POST, onCommand);
    // AP APIs
    webserver.on("/edit", HTTP_GET, onEditor).setFilter(ON_AP_FILTER);
    webserver.on("/edit", HTTP_PUT, onCreate).setFilter(ON_AP_FILTER);
    webserver.on("/edit", HTTP_DELETE, onDelete).setFilter(ON_AP_FILTER);
    webserver.on("/edit", HTTP_POST, onSuccess, onUpload)
        .setFilter(ON_AP_FILTER);
    webserver.on("/config", HTTP_ANY, onConfig).setFilter(ON_AP_FILTER);
    webserver.on("/update", HTTP_GET, onUpdate).setFilter(ON_AP_FILTER);
    webserver.on("/update", HTTP_POST, onUpdateDone, onUpdatePost)
        .setFilter(ON_AP_FILTER);
    webserver.on("/apmode", HTTP_ANY, onSuccess).setFilter(ON_AP_FILTER);
    // Static files
    webserver.serveStatic("/data/", FFS, Config.sys.DIR_DATA);
    webserver.serveStatic("/docs/", FFS, Config.sys.DIR_DOCS);
    webserver.serveStatic("/",      FFS, Config.sys.DIR_HTML)
        .setDefaultFile("index.html")
        .setCacheControl("max-age=3600")
        .setLastModified(__DATE__ " " __TIME__ " GMT")
        .setAuthentication(Config.web.HTTP_NAME, Config.web.HTTP_PASS);
    webserver.onFileUpload(onUploadStrict);
    webserver.onNotFound(onError);

    // Headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    server_loop_begin();
}

#elif defined(CONFIG_BASE_USE_WIFI) // && CONFIG_BASE_USE_WEBSERVER

#include "esp_rom_md5.h"
#include "esp_tls_crypto.h"
#include "esp_http_server.h"

#define ESP_ERR_HTTPD_AUTH_DIGEST   (ESP_ERR_HTTPD_BASE + 10)
#define ESP_ERR_HTTPD_SKIP_DATA     (ESP_ERR_HTTPD_BASE + 11)

#define FLAG_AP_ONLY    BIT0
#define FLAG_NEED_AUTH  BIT1
#define FLAG_DIR_DATA   BIT2
#define FLAG_DIR_DOCS   BIT3
#define FLAG_DIR_HTML   BIT4

#define send_str httpd_resp_sendstr // alias for just better syntax

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
    for (http_param_t *ptr = (http_param_t *)ctx, *next; ptr; ptr = next) {
        next = ptr->next;
        TRYFREE(ptr->key);
        TRYFREE(ptr->val);
        TRYFREE(ptr);
    }
}

// parse urlencoded key-val pairs: <key1>[=<val1>]&<key2>=<val2>
static void parse_params(char *buf, bool body, void ** ctx) {
    if (!buf || !ctx) return;
    http_param_t *ptr = *(http_param_t **)ctx, *param;
    while (ptr && ptr->next) { ptr = ptr->next; }
    for (char *tok = strtok(buf, "&"); tok; tok = strtok(NULL, "&")) {
        char *eql = strchr(tok, '=') ?: (tok + strlen(tok));
        if (ECALLOC(param, 1, sizeof(http_param_t))) break;
        param->key = strndup(tok, eql - tok);
        param->val = strlen(eql) > 1 ? strdup(eql + 1) : NULL;
        param->body = body;
        if (param->key) {
            ptr = ptr ? (ptr->next = param) : (*(http_param_t **)ctx = param);
        } else {
            TRYFREE(param->val);
            TRYFREE(param);
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
    http_auth_t *ctx = (http_auth_t *)arg;
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
    char buf[ctx->ulen + ctx->plen + 2];    // 1 for ':' and 1 for '\0'
    size_t len = (1 + (ctx->ulen + ctx->plen) / 3) * 4 + 1, notused;
    sprintf(buf, "%s:%s", ctx->user, ctx->pass);
    if (!ECALLOC(ctx->basic, 1, 6 + len)) { // 6 for "Basic "
        esp_crypto_base64_encode(
            (uint8_t *)stpcpy(ctx->basic, "Basic "), len,
            &notused, (uint8_t *)buf, strlen(buf));
    }
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

static const char * get_index(const char *dirname) {
    static char path[255];
    snprintf(path, sizeof(path) - 3, fjoin(2, dirname, "index.html"));
    if (fisfile(path)) return path;
    if (fisfile(strcat(path, ".gz"))) return path;
    return NULL;
}

static char * get_header(httpd_req_t *req, const char *key) {
    char *buf = NULL;
    size_t len = httpd_req_get_hdr_value_len(req, key) + 1;
    if (len <= 1 || ECALLOC(buf, 1, len)) return buf;
    if (httpd_req_get_hdr_value_str(req, key, buf, len)) TRYFREE(buf);
    return buf;
}

static bool has_param(httpd_req_t *req, const char *key, int body) {
    for (http_param_t *p = (http_param_t *)req->sess_ctx; p; p = p->next) {
        if (strcasecmp(p->key, key)) continue;
        if (body == FROM_ANY || p->body == !!body) return true;
    }
    return false;
}

static const char * get_param(httpd_req_t *req, const char *key, int body) {
    for (http_param_t *p = (http_param_t *)req->sess_ctx; p; p = p->next) {
        if (strcasecmp(p->key, key)) continue;
        if (body == FROM_ANY || p->body == !!body) return p->val;
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

static inline const char * guess_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext || !strlen(++ext))  return TYPE_TEXT;
    if (startswith(ext, "htm"))  return TYPE_HTML;
    if (startswith(ext, "json")) return TYPE_JSON;
    if (startswith(ext, "css"))  return "text/css";
    if (startswith(ext, "xml"))  return "text/xml";
    if (startswith(ext, "ttf"))  return "font/ttf";
    if (startswith(ext, "eot"))  return "font/rot";
    if (startswith(ext, "woff")) return "font/woff";
    if (startswith(ext, "png"))  return "image/png";
    if (startswith(ext, "gif"))  return "image/gif";
    if (startswith(ext, "jpg"))  return "image/jpeg";
    if (startswith(ext, "ico"))  return "image/x-icon";
    if (startswith(ext, "svg"))  return "image/svg+xml";
    if (startswith(ext, "pdf"))  return "application/pdf";
    if (startswith(ext, "zip"))  return "application/zip";
    if (startswith(ext, "gz"))   return "application/x-gzip";
    if (startswith(ext, "js"))   return "application/javascript";
    return TYPE_TEXT;
}

static esp_err_t send_file(httpd_req_t *req, const char *path, bool download) {
    FILE *fd;
    size_t len;
    struct stat st;
    const char *fullpath = fnorm(path), *dispos[2] = {"inline", "attachment"};
    char *buf, *basename = strrchr(fullpath, '/');
    char clen[10], cdis[strlen(basename ?: "") + 24];
    if (!basename || stat(fullpath, &st) || !( fd = fopen(fullpath, "r") ))
        return send_err(req, 500, "Failed to open file");
    sprintf(cdis, "%s; filename=\"%s\"", dispos[download], basename + 1);
    httpd_resp_set_hdr(req, "Content-Disposition", cdis);
    httpd_resp_set_hdr(req, "Content-Type", guess_type(basename));
    if (endswith(basename, ".gz"))
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if (st.st_size) {
        snprintf(clen, sizeof(clen), "%ld", st.st_size);
        httpd_resp_set_hdr(req, "Content-Length", clen);
    }
    esp_err_t err = EMALLOC(buf, CHUNK_SIZE);
    while (!err && ( len = fread(buf, 1, CHUNK_SIZE, fd) )) {
        err = httpd_resp_send_chunk(req, buf, len);
    }
    httpd_resp_sendstr_chunk(req, NULL);
    TRYFREE(buf);
    fclose(fd);
    if (err) send_err(req, 500, "Failed to send file");
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
    char *ctype = get_header(req, "Content-Type"), *buf;
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
                } else if (vals[1]) {
                    if (!vals[0]) vals[0] = vals[1];
                    crlf = strstr(head += slen, sep);
                    state = PARSE_DATA;
                    idx = 0;
                } else {
                    state = vals[0] ? PARSE_BDARY : PARSE_ERROR;
                }
            }
            if (state == PARSE_DATA) {
                bool fend = false;
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
    TRYFREE(ctype);
    return rc < 0 ? ESP_FAIL : ESP_OK; // HTTPD_SOCK_ERR_xxx < 0
#undef PARSE_BDARY
#undef PARSE_HEADER
#undef PARSE_DATA
#undef PARSE_FAILED
#undef PARSE_ERROR
}

static esp_err_t redirect(httpd_req_t *req, const char *location) {
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    return send_str(req, "redirect");
}

static void log_msg(httpd_req_t *req, const char *msg = "") {
    const char *mstr = http_method_str((httpd_method_t)req->method);
    ESP_LOGI(TAG, "%s %s %s", mstr, req->uri, msg);
}

static void log_param(httpd_req_t *req) {
    http_param_t *param = (http_param_t *)req->sess_ctx;
    for (int i = 0; param; param = param->next) {
        ESP_LOGI(TAG, "Param[%d] key:%s, query:%d `%s`",
                 i++, param->key, !param->body, param->val);
    }
}

#define CHECK_REQUEST(req)                                                  \
    {                                                                       \
        esp_err_t err = check_request(req);                                 \
        if (err) return err < 0 ? ESP_FAIL : ESP_OK;                        \
    }

static esp_err_t check_request(httpd_req_t *req) {
    log_msg(req);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t err = ESP_OK;
    if (!err && !req->sess_ctx) {
        // parse URL query and POST body (size < CHUNK_SIZE)
        req->free_ctx = free_params;
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
        char *ctype = get_header(req, "Content-Type");
        size_t clen = req->content_len < CHUNK_SIZE ? req->content_len : 0;
        if (clen && (!ctype || strstr(ctype, TYPE_UENC))) {
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
        TRYFREE(ctype);
    }
    if (false && !err && (int)req->user_ctx & FLAG_AP_ONLY) { // TODO DEBUG
        char *host = get_header(req, "Host");
        if (strcmp(host ?: "", Config.net.AP_HOST)) {
            send_err(req, 403, "AP interface only");
            err = ESP_ERR_NOT_SUPPORTED;
        }
        TRYFREE(host);
    }
    if (!err && (int)req->user_ctx & FLAG_NEED_AUTH) {
        char *auth = get_header(req, "Authorization"), *rstr = NULL;
        const char *mstr = http_method_str((httpd_method_t)req->method);
        http_auth_t *ctx = (http_auth_t *)httpd_get_global_user_ctx(req->handle);
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
    log_msg(req, __func__);
    if (req->method == HTTP_OPTIONS) return send_str(req, NULL);
    return send_err(req, 404, ERROR_HTML);
}

static esp_err_t on_success(httpd_req_t *req) {
    CHECK_REQUEST(req);
    size_t nfd = 8;
    int fds[nfd];
    if (httpd_get_client_list(req->handle, &nfd, fds) || !nfd) return ESP_OK;
#ifdef CONFIG_BASE_DEBUG
    ESP_LOGI(TAG, "Got %d clients", nfd);
    LOOPN(i, nfd) {
        ESP_LOGI(TAG, "- fd=%d %s", fds[i], getaddrname(fds[i], false));
    }
#endif
    return send_str(req, NULL);
}

static esp_err_t on_command(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (!req->content_len || req->content_len > CHUNK_SIZE) {
        return send_err(req, 400, "Invalid content length");
    }
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
        if (fisfile(path) && !has_param(req, "overwrite", FROM_ANY))
            return ESP_ERR_HTTPD_SKIP_DATA;
        if (!( fn = strdup(path) ) || !( fd = fopen(path, "w") )) {
            send_err(req, 500, "Could not open file to write");
            goto error;
        }
        printf("Upload file: %s\n", path);
        led_set_blink((led_blink_t)1);
    }
    if (fd && len) {
        if (len != fwrite(data, 1, len, fd)) {
            send_err(req, 500, "Could not write to file");
            goto error;
        }
        printf("\rProgress: %8s", format_size(idx, false));
    }
    if (end && fd) {
        printf("Upload success: %s\n", format_size(idx + len, false));
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
        if (req->method == HTTP_GET && get_index(Config.sys.DIR_HTML))
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
            return send_err(req, 404, NULL);
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
        const char *json = get_param(req, "json", FROM_ANY);
        if (json && !config_loads(json)) {
            send_err(req, 500, "Failed to load config from JSON");
        } else {
            send_str(req, NULL);
        }
    } else {
        char *json = config_dumps();
        if (!json) {
            send_err(req, 500, "Failed to dump configs into JSON");
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
        if (strcmp(name, "update")) return ESP_ERR_HTTPD_SKIP_DATA;
        if (!has_param(req, "size", -1) && !ota_updation_begin(0)) goto error;
        printf("Update file: %s\n", name);
        led_set_blink((led_blink_t)2);
    }
    if (len && !ota_updation_write(data, len)) goto error;
    if (end) {
        if (!ota_updation_end()) goto error;
        printf("Update success: %s\n", format_size(idx + len, false));
        send_str(req, "OTA Updation success: reboot now");
        setTimeout(10, restart, NULL);
    }
    return ESP_OK;
error:
    led_set_blink((led_blink_t)0);
    send_err(req, 500, ota_updation_error());
    return ESP_FAIL;
}

static esp_err_t on_update(httpd_req_t *req) {
    CHECK_REQUEST(req);
    if (req->method == HTTP_GET) {
        if (!has_param(req, "raw", FROM_ANY) && get_index(Config.sys.DIR_HTML))
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
    const char *path, *url = req->uri + (startswith(req->uri, "/api/") ? 4 : 0);
    char basename[strcspn(url, "?#") + 3 + 1];
    path = fjoin(2, dirname, strncpy(basename, url, sizeof(basename) - 4));
    if (fisdir(path)) {
        const char *index = get_index(path);
        if (index) return send_file(req, index, false);
        if (get_index(Config.sys.DIR_HTML)) return redirect(req, "/editor");
    }
    if (fisfile(path)) return send_file(req, path, false);
    path = fjoin(2, dirname, strcat(basename, ".gz"));
    if (fisfile(path)) return send_file(req, path, false);
    return send_err(req, 404, NULL);
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
static void handle_websocket_message(void *arg) {
    httpd_ws_frame_t *pkt = (httpd_ws_frame_t *)arg;
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
    httpd_ws_frame_t *pkt = NULL;
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done!");
        goto exit;
    }
    if (( err = ECALLOC(pkt, 1, sizeof(httpd_ws_frame_t)) ) ||
        ( err = httpd_ws_recv_frame(req, pkt, 0) ) ||
        ( pkt->type != HTTPD_WS_TYPE_TEXT ) ||
        ( pkt->len > (2 * CHUNK_SIZE) || !pkt->final ) ||
        ( err = ECALLOC(pkt->payload, 1, pkt->len + 1 + sizeof(int)) ) ||
        ( err = httpd_ws_recv_frame(req, pkt, pkt->len) )
    ) goto exit;
    *(int *)(pkt->payload + pkt->len + 1) = httpd_req_to_sockfd(req);
    return httpd_queue_work(req->handle, handle_websocket_message, pkt);
exit:
    if (pkt) {
        TRYFREE(pkt->payload);
        TRYFREE(pkt);
    }
    return err;
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
// Use `httpd_req_t.sess_ctx` to store parsed `http_param_t *`
// Use `httpd_req_t.user_ctx` to store `FLAG_XXXs`
void server_loop_begin() {
    if (server) return;

    const httpd_uri_t apis[] = {
        // WebSocket APIs
        WS_API("/ws", on_websocket, NULL),
        // STA APIs
        HTTP_API("/alive",  GET,    on_success, NULL),
        HTTP_API("/exec",   POST,   on_command, FLAG_NEED_AUTH),
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

    esp_err_t err = httpd_start(&server, &config);
    if (err) {
        ESP_LOGE(TAG, "Start server failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Start server on port %d", config.server_port);
        LOOPN(i, LEN(apis)) {
            if (( err = httpd_register_uri_handler(server, apis + i) )) {
                ESP_LOGE(TAG, "Register uri `%s` failed: %s",
                         apis[i].uri, esp_err_to_name(err));
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

#endif // CONFIG_BASE_USE_WIFI && CONFIG_BASE_USE_WEBSERVER
