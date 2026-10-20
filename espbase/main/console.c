/*
 * File: console.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 15:58:29
 * Desc: Replacement of `esp_console_new_repl_xxx` & `esp_console_start_repl`.
*/

#include "console.h"
#include "drivers.h"            // for UART_NUM_XXX
#include "filesys.h"            // for PATH_MAX_LEN
#include "config.h"

#ifdef CONFIG_BASE_USE_CONSOLE

#include <sys/fcntl.h>
#include "esp_console.h"
#include "esp_task_wdt.h"
#include "linenoise/linenoise.h"

#ifdef IDF_TARGET_V4
#   include "esp_vfs_dev.h"
#else
#   include "driver/uart_vfs.h"
#endif

#ifdef CONFIG_ESP_CONSOLE_USB_CDC
#   include "esp_vfs_cdcacm.h"
#endif
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#   include "driver/usb_serial_jtag.h"
#   ifdef IDF_TARGET_V4
#       include "esp_vfs_usb_serial_jtag.h"
#   else
#       include "driver/usb_serial_jtag_vfs.h"
#   endif
#endif

static const char *TAG = "Console";

static char prompt[PATH_MAX_LEN + 32] = "$ ", context[32];

static void *mutex = NULL;

void console_register_commands();                   // see commands.cpp

void console_register_prompt(const char *str, const char *ctx) {
    size_t slen = strlen(str ?: ""), clen = strlen(ctx ?: "");
    if (clen + 3 >= sizeof(context)) {
        ESP_LOGE(TAG, "Context %s is too long for %d", ctx, sizeof(context));
    } else if (clen) {
        snprintf(context, sizeof(context), "> %s ", ctx);
    } else if (ctx) {
        context[0] = '\0';
    }
    char *pwd = getenv("PWD") ?: "/";
    size_t tlen = strlen(pwd) + slen + 3;
#ifdef CONFIG_LOG_COLORS
    const char *c1 = LOG_COLOR(LOG_COLOR_PURPLE), *c2 = LOG_COLOR(LOG_COLOR_BLUE);
    tlen += strlen(c1) + strlen(c2) + strlen(LOG_RESET_COLOR) * 2;
#endif
    if (tlen >= sizeof(prompt)) {
        ESP_LOGE(TAG, "Prompt %s is too long for %d", prompt, sizeof(prompt));
    } else if (slen) {
#ifdef CONFIG_LOG_COLORS
        if (!linenoiseIsDumbMode()) {
            snprintf(prompt, sizeof(prompt), "%s%s%s:%s%s%s$ ",
                     c1, str, LOG_RESET_COLOR, c2, pwd, LOG_RESET_COLOR);
        } else
#endif
        {
            snprintf(prompt, sizeof(prompt), "%s:%s$ ", str, pwd);
        }
    } else if (str) {
        prompt[0] = '\0';
    }
}

void console_initialize() {
    /* esp_system/startup.c -> esp_vfs_console_register()
     *  - /dev/uart/{NUM_UART}
     *  - /dev/usbserjtag
     *  - /dev/cdcacm
     *  - /dev/secondary
     * and then point /dev/console to the device configured by CONFIG_ESP_CONSOLE_XXX
     */
#ifdef IDF_TARGET_V4
#   define uart_vfs_dev_port_set_rx_line_endings esp_vfs_dev_uart_port_set_rx_line_endings
#   define uart_vfs_dev_port_set_tx_line_endings esp_vfs_dev_uart_port_set_tx_line_endings
#   define uart_vfs_dev_use_driver               esp_vfs_dev_uart_use_driver
#   define usb_serial_jtag_vfs_set_rx_line_endings esp_vfs_dev_usb_serial_jtag_set_rx_line_endings
#   define usb_serial_jtag_vfs_set_tx_line_endings esp_vfs_dev_usb_serial_jtag_set_tx_line_endings
#   define usb_serial_jtag_vfs_use_driver          esp_vfs_usb_serial_jtag_use_driver
#endif
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
    uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
    uart_vfs_dev_use_driver(UART_NUM_0);
#elif defined(CONFIG_ESP_CONSOLE_UART_CUSTOM) && defined(CONFIG_BASE_USE_UART)
    uart_vfs_dev_port_set_rx_line_endings(NUM_UART, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(NUM_UART, ESP_LINE_ENDINGS_CRLF);
    uart_vfs_dev_use_driver(NUM_UART);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);              // non-blocking mode
    fcntl(fileno(stdin), F_SETFL, 0);
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
#   ifndef IDF_TARGET_V4
    if (!usb_serial_jtag_is_driver_installed())
#   endif
        usb_serial_jtag_driver_install(&usb_cfg);
    usb_serial_jtag_vfs_use_driver();
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_vfs_dev_cdcacm_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_cdcacm_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);              // non-blocking mode
    fcntl(fileno(stdin), F_SETFL, 0);
#endif

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    linenoiseSetMultiLine(1);
    linenoiseAllowEmpty(false);
    linenoiseSetMaxLineLen(256);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(50);
    if (linenoiseProbe()) {
        linenoiseSetDumbMode(1);
        ESP_LOGW(TAG, "Your terminal does not support escape sequences.");
        ESP_LOGW(TAG, "Line editing, history and console color are disabled.");
        ESP_LOGW(TAG, "Try using IDF-Monitor / PuTTY / SecureCRT.");
    }
    esp_console_config_t cfg = ESP_CONSOLE_CONFIG_DEFAULT();
#if CONFIG_LOG_COLORS
    cfg.hint_color = cfg.hint_bold = atoi(LOG_COLOR_CYAN);
#endif
    ESP_ERROR_CHECK( esp_console_init(&cfg) );
    console_register_prompt(Config.sys.PROMPT, "");
    console_register_commands();
    if (!EMPTY(Config.sys.HISTORY)) console_history_command("load", NULL);

    if (( mutex = MUTEX() )) {
        RELEASE(mutex);
    } else {
        ESP_LOGE(TAG, "Could not create semaphore! This is NOT thread-safe!");
    }
}

/* Redirect STDOUT to a buffer, thus any thing printed to STDOUT will be
 * collected as a string. But don't forget to free the buffer after result
 * is handled.
 *
 * Method 1. <stdio.h> setvbuf - store string in buffer (temporarily)
 *  pos: easy to set & unset, safe printing with buflen
 *  neg: `\r` does not work because STDOUT is always flushed
 *  e.g.:
 *      char *buf = calloc(1024, sizeof(char));
 *      setvbuf(stdout, buf, _IOFBF, 1024);
 *      printf("test string\n");
 *      setvbuf(stdout, NULL, _IONBF, 0);
 *      printf("Got string from STDOUT %lu: `%s`", strlen(buf), buf);
 *      free(buf);
 *
 * Method 2. <stdio.h> memstream - open memory as stream
 *  2.1 fmemopen(void *buf, size_t size, const char *mode)
 *  2.2 open_memstream(char * *ptr, size_t *sizeloc)
 *  pos: The open_memstream will dynamically allocate buffer and automatically
 *       grow. The fmemopen function support 'a'(append) mode.
 *  neg: Caller need to free the buffer after stream is closed. Stream opened
 *       by fmemopen function need buf config (i.e. setvbuf or fflush) and is
 *       limited by buffer size.
 *  e.g.:
 *      char *buf; size_t size;
 *      FILE *stdout_bak = stdout;
 *      stdout = open_memstream(&buf, &size);
 *      printf("hello\n");
 *      fclose(stdout); stdout = stdout_bak;
 *      printf("Got string from STDOUT %lu: `%s`", size, buf);
 *      free(buf);
 *
 * Method 3. <unistd.h> pipe & dup: this is not what we want
 *
 * Method 4. Pipe STDOUT to a file in memory or flash storage using VFS
 *  pos: We can do memory mapping.
 *  neg: Writing & reading from a file is slow and consume too much resources.
 *  e.g.:
 *      stdout = fopen("/spiffs/runtime.log", "w");         // this is slow
 *  e.g.:
 *      esp_vfs_t vfs_buffer = {
 *          .open = &my_malloc,
 *          .close = &my_free,
 *          .read = &my_strcpy,
 *          .write = &my_snprintf,
 *          .fstat = NULL,
 *          .flags = ESP_VFS_FLAG_DEFAULT,
 *      }
 *      esp_vfs_register("/dev/ram", &vfs_buffer, NULL);    // much faster
 *      stdin = fopen("/dev/ram/0", "r");
 *      stdout = fopen("/dev/ram/1", "w");
 *      stderr = fopen("/dev/ram/2", "w");
 *
 * Currently method 2 is in use. Try method 4 if necessary in the future.
 */

char * console_handle_command(const char *cmd, bool pipe, bool history) {
    if (!ACQUIRE(mutex, 100)) return strdup("Console task is busy");

    size_t size = 0;
    char *buf = NULL;
    FILE *bak = stdout;
    if (pipe) stdout = open_memstream(&buf, &size);

    int code, err = esp_console_run(cmd, &code) ?: code;
    if (err == ESP_OK || err == ESP_ERR_CONSOLE_ARGPARSE) {
        // do nothing
    } else if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Unrecognized command: `%s`", cmd);
    } else {
        ESP_LOGE(TAG, "Command error: %d (%s)", err, esp_err_to_name(err));
    }
    if (pipe) {
        fclose(stdout);
        stdout = bak;
    } else {
        putchar('\n');
    }
    if (buf != NULL) {
        while (size && strchr(" \r\n", buf[--size])) { buf[size] = '\0'; }
        if (!size) TRYFREE(buf);                    // no log output
        if (size && !pipe) putchar('\n');           // one more blank line
    }
    if (history) linenoiseHistoryAdd(cmd);
    RELEASE(mutex);
    return buf;
}

void console_handle_one() {
    char *raw = linenoise(context[0] ? context : prompt),
         *trim = strtrim(raw, " \t\r\n"), *cmd = NULL;
    size_t tlen = strlen(trim ?: "");
    if (!tlen || trim[0] == 0x5B) {                 // ctrl keycodes
        putchar('\n');
    } else if (!context[0] || startswith(trim, "ctx")) {
        console_handle_command(trim, false, true);
    } else if (!EMALLOC(cmd, strlen(context) + tlen)) {
        sprintf(cmd, "%s %s", context + 2, trim);
        console_handle_command(cmd, false, true);
    } else {
        ESP_LOGE(TAG, "%s %s", __func__, esp_err_to_name(ESP_ERR_NO_MEM));
    }
    TRYNULL(raw, linenoiseFree);
    TRYFREE(cmd);
}

void console_handle_loop(void *argv) {
    for (;;) {
        console_handle_one();
#if defined(CONFIG_TASK_WDT) && defined(IDF_TARGET_V4)
        esp_task_wdt_reset();
#endif
    }
}

void console_loop_begin(int xCoreID) {
#ifndef CONFIG_FREERTOS_UNICORE
    if (xCoreID == 0 || xCoreID == 1) {
        xTaskCreatePinnedToCore(
            console_handle_loop, "console", 8192, NULL, 1, NULL, xCoreID);
    } else
#endif
    {
        xTaskCreate(console_handle_loop, "console", 8192, NULL, 1, NULL);
    }
}

static char * rpc_error(double code, const char *errstr) {
    cJSON *rep = cJSON_CreateObject(), *error;
    cJSON_AddNullToObject(rep, "id");
    cJSON_AddItemToObject(rep, "error", error = cJSON_CreateObject());
    cJSON_AddStringToObject(rep, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", errstr);
    char *json = cJSON_PrintUnformatted(rep);
    cJSON_Delete(rep);
    return json;
}

static char * rpc_response(const char *id, const char *result) {
    cJSON *rep = cJSON_CreateObject();
    if (id) {
        cJSON_AddStringToObject(rep, "id", id);
    } else {
        cJSON_AddNullToObject(rep, "id");
    }
    cJSON_AddStringToObject(rep, "jsonrpc", "2.0");
    cJSON_AddStringToObject(rep, "result", result);
    char *json = cJSON_PrintUnformatted(rep);
    cJSON_Delete(rep);
    return json;
}

char * console_handle_jsonrpc(const char *json, int len) {
    char *ret = NULL, *cmd = NULL, *tmp = NULL;
    cJSON *obj = cJSON_ParseWithLength(json, len == -1 ? strlen(json) : len),
          *uid = cJSON_GetObjectItem(obj, "id"),
          *method = cJSON_GetObjectItem(obj, "method"),
          *params = cJSON_GetObjectItem(obj, "params");
    if (!obj) {
        ret = rpc_error(-32700, "Parse Error");
        goto exit;
    }
    if (!method || (params && !cJSON_IsArray(params))) {
        ret = rpc_error(-32600, "Invalid Request");
        goto exit;
    }
    if (cJSON_GetArraySize(params)) {           // command with arguments
        size_t size = 0; FILE *stream = open_memstream(&cmd, &size);
        fprintf(stream, "%s", method->valuestring);
        for (cJSON *child = params->child; child; child = child->next) {
            fprintf(stream, " %s", child->valuestring);
        }
        fclose(stream);
    } else {                                    // command without arguments
        cmd = strdup(method->valuestring);
    }
    if (!cmd) {                                 // command not parsed from json
        ret = rpc_error(-32400, "System Error");
        goto exit;
    }
    ESP_LOGD(TAG, "Got RPC command: `%s`", cmd);
    tmp = console_handle_command(cmd, true, false);
    ESP_LOGD(TAG, "Got RPC result: `%s`", tmp ?: "");
    if (uid) ret = rpc_response(uid->valuestring, tmp ?: "");   // not notify
exit:
    TRYNULL(obj, cJSON_Delete);
    TRYFREE(tmp);
    TRYFREE(cmd);
    return ret;
}

esp_err_t console_history_command(const char *cmd, const char *hist) {
    if (!hist) hist = Config.sys.HISTORY;
    filesys_type_t type = filesys_get_type(hist);
    if (!cmd) {
        printf("History file: %s (%d)\n", hist, filesys_isfile(type, hist));
    } else if (!strcasecmp(cmd, "load")) {
        if (!filesys_isfile(type, hist)) return ESP_ERR_INVALID_ARG;
        return linenoiseHistoryLoad(hist) != 0;
    } else if (!strcasecmp(cmd, "dump")) {
        if (EMPTY(hist)) return ESP_ERR_INVALID_ARG;
        return linenoiseHistorySave(hist) != 0;
    } else if (!strtob(cmd)) {
        return config_set("sys.history", "");
    } else if (filesys_isfile(type, hist)) {
        return config_set("sys.history", hist);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

#else // CONFIG_BASE_USE_CONSOLE

void console_initialize() {}

void console_register_prompt(const char *s, const char *c) {
    NOTUSED(s); NOTUSED(c);
}

char * console_handle_command(const char *c, bool p, bool h) {
    return NULL; NOTUSED(c); NOTUSED(h);
}

void console_handle_one() {}

void console_handle_loop(void *a) { return; NOTUSED(a); }

void console_loop_begin(int x) { return; NOTUSED(x); }

char * console_handle_jsonrpc(const char *j, int l) {
    return NULL; NOTUSED(j); NOTUSED(l);
}

esp_err_t console_history_command(const char *c, const char *h) {
    NOTUSED(c); NOTUSED(h); return ESP_ERR_NOT_SUPPORTED;
}

#endif // CONFIG_BASE_USE_CONSOLE
