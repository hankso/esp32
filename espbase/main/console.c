/*
 * File: console.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 15:58:29
 * Desc: Replacement of `esp_console_new_repl_xxx` & `esp_console_start_repl`.
*/

#include "console.h"
#include "drivers.h"            // for UART_NUM_XXX
#include "config.h"

#ifdef CONFIG_BASE_USE_CONSOLE

#include <sys/fcntl.h>
#include "cJSON.h"
#include "esp_console.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

static char prompt[32] = "$ ", context[32];

static void *mutex = NULL;

void console_register_commands();                   // see commands.cpp

void console_register_prompt(const char *str, const char *ctx) {
    if (ctx) {
        if (!strlen(ctx)) context[0] = '\0';
        else snprintf(context, sizeof(context), "> %s ", ctx);
    }
    size_t slen = strlen(str ?: ""), smax = sizeof(prompt);
    if (!slen) {
        if (str) prompt[0] = '\0';
        return;
    }
#ifdef CONFIG_LOG_COLORS
    const char * color = LOG_COLOR(LOG_COLOR_PURPLE);
    slen += strlen(color) + strlen(LOG_RESET_COLOR) + 1;
    if (!linenoiseIsDumbMode() && slen <= smax) {
        snprintf(prompt, slen, "%s%s%s", color, str, LOG_RESET_COLOR);
        ESP_LOGI(TAG, "Using colorful prompt %s", prompt);
    } else
#endif
    {
        snprintf(prompt, smax, str);
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
    usb_serial_jtag_driver_config_t conf = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
#   ifndef IDF_TARGET_V4
    if (!usb_serial_jtag_is_driver_installed())
#   endif
        usb_serial_jtag_driver_install(&conf);
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
    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN),
        .hint_bold = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );
    console_register_prompt(Config.sys.PROMPT, "");
    console_register_commands();

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
 *      char *buf = (char *)calloc(1024, sizeof(char));
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

char * console_handle_rpc(const char *json) {
    char *ret = NULL, *cmd = NULL, *tmp = NULL;
    cJSON *obj = cJSON_Parse(json),
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
        size_t size = 0; FILE *buf = open_memstream(&cmd, &size);
        fprintf(buf, "%s", method->valuestring);
        for (cJSON *child = params->child; child; child = child->next) {
            fprintf(buf, " %s", child->valuestring);
        }
        fclose(buf);
    } else {                                    // command without arguments
        cmd = strdup(method->valuestring);
    }
    if (!cmd) {                                 // command not parsed from json
        ret = rpc_error(-32400, "System Error");
        goto exit;
    }
    ESP_LOGD(TAG, "Got RPC command: `%s`", cmd);
    tmp = console_handle_command(cmd, true, false);
    ESP_LOGD(TAG, "Got RPC result: %s", tmp);
    if (uid) ret = rpc_response(uid->valuestring, tmp ?: "");   // not notify
exit:
    TRYNULL(obj, cJSON_Delete);
    TRYFREE(tmp);
    TRYFREE(cmd);
    return ret;
}

#else // CONFIG_BASE_USE_CONSOLE

void console_initialize() {}

void console_register_prompt(const char *s) { NOTUSED(s); }

char * console_handle_command(const char *c, bool p, bool h) {
    return NULL; NOTUSED(c); NOTUSED(h);
}

void console_handle_one() {}

void console_handle_loop(void *a) { return; NOTUSED(a); }

void console_loop_begin(int x) { return; NOTUSED(x); }

char * console_handle_rpc(const char *j) { return NULL; NOTUSED(j); }

#endif // CONFIG_BASE_USE_CONSOLE
