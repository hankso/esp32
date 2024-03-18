/* 
 * File: usbmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#include "usbmode.h"
#include "config.h"

#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_USE_USB)

#include "tinyusb.h"
#include "../include_private/usb_descriptors.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#   include "driver/usb_serial_jtag.h"
#endif

#ifdef CONFIG_USB_CDC_DEVICE
#   include "tusb_cdc_acm.h"
#endif

#ifdef CONFIG_USB_CDC_HOST
#   include "usb/cdc_acm_host.h" // idf add-dependency usb_host_cdc_acm
#endif

#define TIMEOUT_IDLE 10
#define TIMEOUT_LOOP 50
#define TIMEOUT_WAIT 100

static const char *TAG = "USBMode";

static struct {
    esp_err_t err;
    bool running;
    uint32_t vid_pid;
    SemaphoreHandle_t usblib_sem, client_sem;
} ctx = { ESP_OK, false, 0, NULL, NULL };

static bool acquire(SemaphoreHandle_t s, uint32_t ms) {
    if (!s) return false;
    return xSemaphoreTake(s, ms ? pdMS_TO_TICKS(ms) : portMAX_DELAY) == pdTRUE;
}

static bool release(SemaphoreHandle_t s) {
    return s ? xSemaphoreGive(s) == pdTRUE : false;
}

// USBMode: Serial JTAG

static esp_err_t serial_jtag_init(usbmode_t prev) {
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
    usb_serial_jtag_driver_config_t conf = \
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&conf);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
#endif
}

static esp_err_t serial_jtag_exit(usbmode_t next) {
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
    return usb_serial_jtag_driver_uninstall();
#endif
}

// USBMode: CDC Device (slave)

#ifdef CONFIG_USB_CDC_DEVICE_SERIAL
static void cdc_device_cb(int itf, cdcacm_event_t *event) {
    static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    if (event->type == CDC_EVENT_RX) {
        size_t size = 0;
        esp_err_t err = tinyusb_cdcacm_read(itf, buf, sizeof(buf) - 1, &size);
        if (err) {
            ESP_LOGE(TAG, "Device CDC read error %s", esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "Device CDC got data[%u]", size);
            ESP_LOG_BUFFER_HEXDUMP(TAG, buf, size, ESP_LOG_DEBUG);
            tinyusb_cdcacm_write_queue(itf, buf, size); // echo
            tinyusb_cdcacm_write_flush(itf, 0);
        }
    } else if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        ESP_LOGI(TAG, "Device CDC line state DTR: %d, RTS: %d",
                event->line_state_changed_data.dtr,
                event->line_state_changed_data.rts);
    }
}
#endif

static esp_err_t cdc_device_init(usbmode_t prev) {
#ifndef CONFIG_USB_CDC_DEVICE
    return ESP_ERR_NOT_SUPPORTED;
#else
    // see esp-idf/components/tinyusb/additions/src/usb_descriptors.c
    int ver[3];
    esp_err_t err;
    if (parse_all(Config.info.VER, ver, 3) >= 2)
        descriptor_kconfig.bcdDevice = (uint8_t)(ver[0] << 8) | (uint8_t)ver[1];
    if (strlen(Config.info.UID))
        descriptor_str_kconfig[3] = Config.info.UID;
    tinyusb_config_t tusb_conf = {
        .external_phy = false,
        .descriptor = &descriptor_kconfig,
        .string_descriptor = descriptor_str_kconfig,
    };
    if (( err = tinyusb_driver_install(&tusb_conf) )) return err;
    LOOP(i, 1, LEN(descriptor_str_kconfig)) {
        ESP_LOGI(TAG, "desc[%d] = %s", i, descriptor_str_kconfig[i]);
    }
    tinyusb_config_cdcacm_t acm_conf = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = &cdc_device_cb,
        .callback_line_state_changed = &cdc_device_cb,
#   endif
    };
    if (( err = tusb_cdc_acm_init(&acm_conf) )) return err;
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (( err = esp_tusb_init_console(TINYUSB_CDC_ACM_0) )) return err;
    ESP_LOGI(TAG, "Running as CDC console device");
#   else
    ESP_LOGI(TAG, "Running as CDC serial device");
#   endif
    return err;
#endif
}

// USBMode: CDC Host

#ifdef CONFIG_USB_CDC_HOST

static uint32_t usb_dev_vid_pid(void *dev_hdl) {
    const usb_device_desc_t *desc;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &desc);
    if (err) return 0;
    return desc->idVendor << 16 | desc->idProduct;
}

static const char * vid_pid_str(uint32_t vid_pid) {
    static char buf[14]; // 0xXXXX:0xXXXX
    snprintf(buf, sizeof(buf), "0x%04X:0x%04X", vid_pid >> 16, vid_pid & 0xFF);
    return buf;
}

static bool cdc_acm_rx_cb(const uint8_t *data, size_t size, void *arg) {
    ESP_LOGI(TAG, "Host CDC got data[%u]", size);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, size, ESP_LOG_INFO);
    return true;
}

static void cdc_acm_cb(const cdc_acm_host_dev_event_data_t *event, void *arg) {
    uint32_t vp;
    switch (event->type) {
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(TAG, "Host CDC error %d", event->data.error);
            break;
        case CDC_ACM_HOST_SERIAL_STATE:
            ESP_LOGI(TAG, "Host CDC got serial state notification 0x%04X",
                    event->data.serial_state.val);
            break;
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            if (( vp = usb_dev_vid_pid(event->data.cdc_hdl) )) {
                ESP_LOGI(TAG, "Host CDC lost device %s", vid_pid_str(vp));
            } else {
                ESP_LOGI(TAG, "Host CDC lost device");
            }
            cdc_acm_host_close(event->data.cdc_hdl);
            break;
        default: ESP_LOGW(TAG, "Unhandled CDC event: %d\n", event->type);
    }
}

static void cdc_host_cb(usb_device_handle_t dev_hdl) {
    esp_err_t err;
    usb_device_info_t dev_info;
    const usb_device_desc_t *dev_desc;
    const usb_config_desc_t *cfg_desc;
    if (
        ( err = usb_host_device_info(dev_hdl, &dev_info) ) ||
        ( err = usb_host_get_device_descriptor(dev_hdl, &dev_desc) ) ||
        ( err = usb_host_get_active_config_descriptor(dev_hdl, &cfg_desc) )
    ) {
        ESP_LOGE(TAG, "Could not detect device: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "USB Client: Found new device: %d", dev_info.dev_addr);
    if (dev_info.str_desc_manufacturer) {
        printf("Manufacturer ");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        printf("Product      ");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        printf("S/N ID       ");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    printf("Speed mode   %s\n",
            dev_info.speed == USB_SPEED_LOW ? "low" : "full");
    printf("Config value %d\n", dev_info.bConfigurationValue);
    usb_print_device_descriptor(dev_desc);
    usb_print_config_descriptor(cfg_desc, NULL);
    ctx.vid_pid = usb_dev_vid_pid(dev_hdl);
    release(ctx.client_sem);
}

// USB Host library daemon task
static void usb_lib_task(void *arg) {
    uint32_t flags;
    const usb_host_config_t host_conf = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if (( ctx.err = usb_host_install(&host_conf) )) {
        ctx.running = false;
        goto exit;
    }
    release(ctx.usblib_sem); // host library is installed
    msleep(TIMEOUT_IDLE); // let client task spin up
    bool has_clients = ctx.running, has_devices = ctx.running;
    while (has_clients || has_devices) {
        if (!ctx.running) {
            usb_host_lib_info_t info;
            if (usb_host_lib_info(&info)) {
                has_clients = info.num_clients;
                has_devices = info.num_devices;
            }
        }
        usb_host_lib_handle_events(pdMS_TO_TICKS(TIMEOUT_LOOP), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
            has_clients = false;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB LIB: All devices freed");
            has_devices = false;
        }
    }
    ESP_LOGI(TAG, "USB LIB: no more clients and devices");
    usb_host_uninstall();
exit:
    release(ctx.usblib_sem); // host library is uninstalled
    vTaskDelete(NULL);
}

static void cdc_acm_task(void *arg) {
    if (!acquire(ctx.usblib_sem, TIMEOUT_WAIT) || !ctx.running) goto exit;
    const cdc_acm_host_driver_config_t driver_conf = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 10,
        .xCoreID = tskNO_AFFINITY,
        .new_dev_cb = cdc_host_cb,
    };
    const cdc_acm_host_device_config_t device_conf = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .user_arg = NULL,
        .event_cb = cdc_acm_cb,
        .data_cb = cdc_acm_rx_cb
    };
    if (( ctx.err = cdc_acm_host_install(&driver_conf) )) {
        ctx.running = false;
        goto exit;
    }
    while (1) {
        if (!ctx.running) {
            if (!( ctx.err = cdc_acm_host_uninstall() )) break;
            ctx.running = true;
        }
        if (!acquire(ctx.client_sem, TIMEOUT_LOOP)) continue;
        ESP_LOGI(TAG, "Host CDC opening device %s", vid_pid_str(ctx.vid_pid));
        cdc_acm_dev_hdl_t cdc_dev;
        ctx.err = cdc_acm_host_open(
            ctx.vid_pid >> 16, ctx.vid_pid & 0xFF, 0, &device_conf, &cdc_dev);
        if (ctx.err) {
            ESP_LOGE(TAG, "Host CDC open failed: %s", esp_err_to_name(ctx.err));
            continue;
        }
        cdc_acm_host_desc_print(cdc_dev);
        cdc_acm_line_coding_t line_coding;
        if (!cdc_acm_host_line_coding_get(cdc_dev, &line_coding)) {
            ESP_LOGI(TAG, "Host CDC device line: %u,%u%c%u",
                    line_coding.dwDTERate, line_coding.bDataBits,
                    "NOEMS"[line_coding.bParityType], line_coding.bCharFormat);
        }
        const char * TXSTR = "help\r\n";
        cdc_acm_host_data_tx_blocking(
            cdc_dev, (const uint8_t *)TXSTR, strlen(TXSTR), TIMEOUT_WAIT);
        msleep(TIMEOUT_WAIT);
        bool dtr = true, rts = false;
        cdc_acm_host_set_control_line_state(cdc_dev, dtr, rts);
    }
exit:
    release(ctx.client_sem); // client is deregistered
    vTaskDelete(NULL);
}
#endif

static esp_err_t cdc_host_init(usbmode_t prev) {
#ifndef CONFIG_USB_CDC_HOST
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (ctx.running) return ESP_OK;
    if (!ctx.usblib_sem) ctx.usblib_sem = xSemaphoreCreateBinary();
    if (!ctx.client_sem) ctx.client_sem = xSemaphoreCreateBinary();
    ctx.running = true;
    acquire(ctx.usblib_sem, 1); // make sure semaphores are taken
    acquire(ctx.client_sem, 1);
    xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 20, NULL);
    xTaskCreate(cdc_acm_task, "cdc_acm", 4096, NULL, 20, NULL);
    msleep(TIMEOUT_IDLE); // let tasks run
    return ctx.err;
#endif
}

static esp_err_t cdc_host_exit(usbmode_t next) {
    ctx.running = false;
    if (!acquire(ctx.client_sem, TIMEOUT_WAIT))
        return ctx.err ?: ESP_ERR_TIMEOUT;
    if (!acquire(ctx.usblib_sem, TIMEOUT_WAIT))
        ESP_LOGE(TAG, "usb_lib stop failed");
    return ctx.err;
}

static esp_err_t msc_device_init(usbmode_t prev) {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

static esp_err_t msc_host_init(usbmode_t prev) {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

static esp_err_t hid_device_init(usbmode_t prev) {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

static esp_err_t hid_host_init(usbmode_t prev) {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

static struct {
    usbmode_t mode;
    esp_err_t (*init)(usbmode_t prev);
    esp_err_t (*exit)(usbmode_t next);
} modes[] = {
    { SERIAL_JTAG, serial_jtag_init, serial_jtag_exit },
    { CDC_DEVICE, cdc_device_init, NULL },
    { CDC_HOST, cdc_host_init, cdc_host_exit },
    { MSC_DEVICE, msc_device_init, NULL },
    { MSC_HOST, msc_host_init, NULL },
    { HID_DEVICE, hid_device_init, NULL },
    { HID_HOST, hid_host_init, NULL },
};

#define ESP_ERR_DISABLED        0x501
#define ESP_ERR_NOT_INITED      0x502
#define ESP_ERR_PENDING_REBOOT  0x503

static int state = -ESP_ERR_NOT_INITED;

static const char * usbmode_str(usbmode_t mode) {
    if (mode == -1 && state >= 0) mode = state;
    switch (mode) {
        CASESTR(SERIAL_JTAG, 0);
        CASESTR(CDC_DEVICE, 0);
        CASESTR(HID_DEVICE, 0);
        CASESTR(CDC_HOST, 0);
        CASESTR(MSC_HOST, 0);
        default: if (mode != -1) return "Unknown";
    }
    esp_err_t err = -state;
    switch (err) {
        CASESTR(ESP_ERR_DISABLED, 8);
        CASESTR(ESP_ERR_NOT_INITED, 8);
        CASESTR(ESP_ERR_PENDING_REBOOT, 8);
        default: return esp_err_to_name(err);
    }
}

esp_err_t usbmode_switch(usbmode_t mode, bool restart) {
    bool exited = state == -ESP_ERR_NOT_INITED;
    esp_err_t err = ESP_OK;
    if (mode == state) return err;
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        LOOPN(j, LEN(modes)) {
            if (state != modes[j].mode || !modes[j].exit) continue;
            if (( err = modes[j].exit(mode) )) {
                ESP_LOGE(TAG, "USB mode %s exit failed: %s\n",
                        usbmode_str(state), esp_err_to_name(err));
                return err;
            }
            exited = true;
        }
        config_set("app.usb.mode", usbmode_str(modes[i].mode));
        if (!exited) {
            if (restart) esp_restart(); // reboot here
            state = -ESP_ERR_PENDING_REBOOT;
            ESP_LOGI(TAG, "USB mode set to %s (pending)", usbmode_str(mode));
            return err;
        }
        if (!( err = modes[i].init ? modes[i].init(state) : ESP_OK )) {
            state = modes[i].mode;
            ESP_LOGI(TAG, "USB mode set to %s", usbmode_str(mode));
        } else {
            ESP_LOGE(TAG, "USB mode set to %s failed: %s",
                    Config.app.USB_MODE, esp_err_to_name(err));
            state = -err;
        }
        return err;
    }
    ESP_LOGE(TAG, "Invalid USB mode %d: %s\n", mode, usbmode_str(mode));
    return ESP_ERR_NOT_FOUND;
}

void usbmode_initialize() {
    LOOPN(i, LEN(modes)) {
        if (strcmp(usbmode_str(modes[i].mode), Config.app.USB_MODE)) continue;
        usbmode_switch(modes[i].mode, false);
        return;
    }
    if (!strlen(Config.app.USB_MODE)) {
        ESP_LOGW(TAG, "USB is software blocked");
        state = -ESP_ERR_DISABLED;
    } else {
        ESP_LOGE(TAG, "Unknown USB mode. This should not happen!");
    }
}

void usbmode_status() {
    esp_err_t err;
    printf("USB mode is %s\n", usbmode_str(-1));
#ifdef CONFIG_USB_CDC_HOST
    if (state == CDC_HOST) {
        usb_host_lib_info_t info;
        if (( err = usb_host_lib_info(&info) )) {
            ESP_LOGE(TAG, "Could not get host info: %s", esp_err_to_name(err));
            return;
        }
        printf("%d devices, %d clients\n", info.num_devices, info.num_clients);
    }
#endif
}

#else // CONFIG_USE_USB

esp_err_t usbmode_switch(usbmode_t m, bool b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(b);
}

void usbmode_initialize() {}

void usbmode_status() {}

#endif // CONFIG_USE_USB
