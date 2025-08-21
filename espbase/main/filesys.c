/* 
 * File: filesys.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-15 19:42:04
 */

#include "filesys.h"
#include "drivers.h"            // for PIN_XXX
#include "config.h"

#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "diskio_wl.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"

#ifdef CONFIG_BASE_USE_ELF
#   include "esp_elf.h"
#endif

static const char *TAG = "Filesys";

typedef struct {
    filesys_type_t type;
    const char *mp, *part;
    union {
        wl_handle_t wlhdl;
        sdmmc_card_t *card;
    };
} filesys_dev_t;

static void filesys_exit(filesys_dev_t *dev) {
#ifdef CONFIG_BASE_USE_FFS
    if (dev->type == FILESYS_FLASH) {
#   ifdef CONFIG_BASE_FFS_FAT
        if (!esp_vfs_fat_spiflash_unmount(dev->mp, dev->wlhdl))
#   else
        if (!esp_vfs_spiffs_unregister(dev->part))
#   endif
        {
            dev->type  = 0;
            dev->mp    = dev->part = NULL;
            dev->wlhdl = WL_INVALID_HANDLE;
        }
    }
#endif
#ifdef CONFIG_BASE_USE_SDFS
    if (dev->type == FILESYS_SDCARD) {
        if (!esp_vfs_fat_sdcard_unmount(dev->mp, dev->card)) {
            dev->type = 0;
            dev->mp   = NULL;
            dev->card = NULL;
        }
    }
#endif
}

static esp_err_t filesys_init(
    filesys_dev_t *dev, filesys_type_t type, const char *mp, const char *part
) {
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) {
        mp = mp ?: CONFIG_BASE_FFS_MP;
        part = part ?: CONFIG_BASE_FFS_PART;
    }
#endif
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD) {
        mp = mp ?: CONFIG_BASE_SDFS_MP;
        part = "";
    }
#endif
    if (part && !strlen(part)) part = NULL;
    if (!dev || !startswith(mp, "/")) return ESP_ERR_INVALID_ARG;
    if (dev->type && dev->type != type) return ESP_ERR_INVALID_STATE;
    if (dev->type && dev->mp == mp && dev->part == part) return ESP_OK;
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) {
        wl_handle_t wlhdl = WL_INVALID_HANDLE;
#   ifdef CONFIG_BASE_FFS_FAT
        esp_vfs_fat_mount_config_t conf = {
            .format_if_mount_failed = false,
            .max_files              = 10,
            .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE
        };
        esp_err_t err = esp_vfs_fat_spiflash_mount(mp, part, &conf, &wlhdl);
#   else
        esp_vfs_spiffs_conf_t conf = {
            .base_path              = mp,
            .partition_label        = part,
            .max_files              = 10,
            .format_if_mount_failed = false
        };
        esp_err_t err = esp_vfs_spiffs_register(&conf);
#   endif
        if (!err) {
            filesys_exit(dev);
            dev->mp    = mp;
            dev->part  = part;
            dev->wlhdl = wlhdl;
            dev->type  = type;
            ESP_LOGI(TAG, "FlashFS mounted %s to %s", dev->part, dev->mp);
        }
        return err;
    }
#endif // CONFIG_BASE_USE_FFS
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD) {
        esp_log_level_set("sdspi_transaction", ESP_LOG_WARN);
        sdmmc_card_t *card = NULL;
        esp_vfs_fat_mount_config_t mount = {
            .format_if_mount_failed = false,
            .max_files = 10,
            .allocation_unit_size = 16 * 1024,
        };
#   if defined(CONFIG_BASE_SDFS_SPI)                // SDSPI
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        sdspi_device_config_t spi = {
            .host_id  = host.slot = NUM_SPI,
            .gpio_cs  = PIN_CS0,
            .gpio_cd  = SDSPI_SLOT_NO_CD,
            .gpio_wp  = SDSPI_SLOT_NO_WP,
            .gpio_int = SDSPI_SLOT_NO_INT,
        };
        esp_err_t err = esp_vfs_fat_sdspi_mount(mp, &host, &spi, &mount, &card);
#   elif defined(CONFIG_IDF_TARGET_ESP32S3)         // SDMMC - ESP32S3
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t mmc = {
            .clk   = CONFIG_BASE_GPIO_MMC_CLK,
            .cmd   = CONFIG_BASE_GPIO_MMC_CMD,
            .d0    = CONFIG_BASE_GPIO_MMC_D0,
#       ifdef CONFIG_BASE_SDFS_MMC_4LINE
            .d1    = CONFIG_BASE_GPIO_MMC_D1,
            .d2    = CONFIG_BASE_GPIO_MMC_D2,
            .d3    = CONFIG_BASE_GPIO_MMC_D3,
#       endif
            .width = CONFIG_BASE_SDFS_MMC,
            .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
        };
        esp_err_t err = esp_vfs_fat_sdmmc_mount(mp, &host, &mmc, &mount, &card);
#   else                                            // SDMMC - ESP32
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t mmc = SDMMC_SLOT_CONFIG_DEFAULT();
        mmc.width = CONFIG_BASE_SDFS_MMC;
        mmc.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_err_t err = esp_vfs_fat_sdmmc_mount(mp, &host, &mmc, &mount, &card);
#   endif
        if (!err) {
            filesys_exit(dev);
            dev->mp = mp;
            dev->card = card;
            dev->type = type;
            ESP_LOGI(TAG, "SDCard mounted to %s", dev->mp);
        }
        return err;
    }
#endif // CONFIG_BASE_USE_SDFS
    return ESP_ERR_NOT_SUPPORTED;
}

static void * locks[FILESYS_COUNT];
static filesys_dev_t devs[FILESYS_COUNT];

void filesys_initialize() {
    LOOPN(i, FILESYS_COUNT) {
        if (!locks[i] && ( locks[i] = MUTEX() )) RELEASE(locks[i]);
        filesys_type_t type = i + FILESYS_FLASH;
        esp_err_t err = filesys_init(devs + i, type, NULL, NULL);
        if (err) {
            ESP_LOGE(TAG, "Failed to mount: %s", esp_err_to_name(err));
        } else {
            filesys_print_info(type);
        }
    }
}

bool filesys_acquire(filesys_type_t type, uint32_t msec) {
    return ACQUIRE(locks[type - FILESYS_FLASH], msec);
}

bool filesys_release(filesys_type_t type) {
    return RELEASE(locks[type - FILESYS_FLASH]);
}

bool filesys_get_info(filesys_type_t type, filesys_info_t *info) {
    filesys_info_t tmp;
    if (!info) info = &tmp;
    memset(info, 0, sizeof(filesys_info_t));
    info->pdrv = FF_DRV_NOT_USED;
    info->type = type;
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) {
#   ifdef CONFIG_BASE_FFS_FAT
        if (devs[0].wlhdl == WL_INVALID_HANDLE) return false;
        info->pdrv = ff_diskio_get_pdrv_wl(info->wlhdl = devs[0].wlhdl);
        FATFS *fs;
        DWORD free_clust;
        char drv[] = { '0' + info->pdrv, ':', '\0' };
        uint64_t ssize = wl_sector_size(info->wlhdl) ?: CONFIG_WL_SECTOR_SIZE;
        if (f_getfree(drv, &free_clust, &fs) == FR_OK) {
            info->used = ssize * (fs->n_fatent - 2 - free_clust) * fs->csize;
            info->total = ssize * (fs->n_fatent - 2) * fs->csize;
        }
        info->blksize = ssize;
        info->blkcnt = info->blksize ? info->total / info->blksize : 0;
#   else
        size_t used, total;
        if (!esp_spiffs_info(devs[0].part, &total, &used)) {
            info->used = used;
            info->total = total;
        }
#   endif
    }
#endif // CONFIG_BASE_USE_FFS
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD && devs[1].card) {
        info->pdrv = ff_diskio_get_pdrv_card(info->card = devs[1].card);
        info->blkcnt = info->card->csd.capacity;
        info->blksize = info->card->csd.sector_size;
        FATFS *fs;
        DWORD free_clust;
        char drv[] = { '0' + info->pdrv, ':', '\0' };
        if (f_getfree(drv, &free_clust, &fs) == FR_OK) {
#   if FF_MAX_SS != FF_MIN_SS
            uint64_t ssize = fs->ssize; // == card->csd.sector_size ?
#   else
            uint64_t ssize = FF_SS_SDCARD;
#   endif
            info->used = ssize * (fs->n_fatent - 2 - free_clust) * fs->csize;
            info->total = ssize * (fs->n_fatent - 2) * fs->csize;
        }
    }
#endif
    return info->total != 0;
}

void filesys_print_info(filesys_type_t type) {
    filesys_info_t info;
    if (!filesys_get_info(type, &info)) return;
    printf("File System used %llu/%llu KB (%llu%%)\n",
            info.used / 1024, info.total / 1024,
            100 * info.used / (info.total ?: 1));
#ifdef CONFIG_BASE_USE_SDFS
    if (info.type != FILESYS_SDCARD || !info.card) return;
    printf( // see sdmmc_card_print_info
        "Name: %s\n"
        "S/N:  %d\n"
        "VPID: 0x%04X:0x%04X\n"
        "Type: %s\n"
        "Size: %s\n"
        "Freq: %d %cHz%s\n"
        "CSD:  sector_size=%d, read_block_len=%d, capacity=0x%0*X\n"
        "SCR:  sd_spec=%d, bus_width=%d (valid if type = SDIO)\n",
        info.card->cid.name, info.card->cid.serial,
        info.card->cid.mfg_id, info.card->cid.oem_id,
        info.card->is_sdio ? "SDIO" : info.card->is_mmc ? "MMC" :
        info.card->ocr & SD_OCR_SDHC_CAP ? "SDHC/SDXC" : "SDSC",
        format_size(info.card->csd.capacity * info.card->csd.sector_size),
        info.card->max_freq_khz / (info.card->max_freq_khz < 1000 ? 1 : 1000),
        info.card->max_freq_khz < 1000 ? 'K' : 'M',
        info.card->is_ddr ? ", DDR" : "",
        info.card->csd.sector_size, info.card->csd.read_block_len,
        info.card->csd.capacity >> 16 ? 8 : 4, info.card->csd.capacity,
        info.card->scr.sd_spec, info.card->scr.bus_width
    );
#endif
}

char * filesys_norm_r(
    filesys_type_t type, filesys_path_t buf, const char *path
) {
    const char *prepend = NULL;
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) prepend = CONFIG_BASE_FFS_MP;
#endif
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD) prepend = CONFIG_BASE_SDFS_MP;
#endif
    if (!buf || !strlen(prepend ?: "") || !strlen(path ?: "")) {
        if (buf) *buf = '\0';
        return buf;
    } else if (path != buf) {
        if (!startswith(path, prepend)) {
            snprintf(buf, PATH_MAX_LEN, "%s/%s", prepend, path);
        } else {
            strncpy(buf, path, PATH_MAX_LEN - 1);
        }
    } else if (!startswith(path, prepend)) { // change mountpoint
        char *slash = strdup(strchr(path + 1, '/') ?: "");
        if (slash) {
            snprintf(buf, PATH_MAX_LEN, "%s/%s", prepend, slash);
            free(slash);
        }
    } else {
        return buf;
    }
    char *out, *inp;
    for (out = inp = buf; inp[0]; inp++) {
        if (strchr("\\/", inp[0])) {
            inp += (strspn(inp, "\\/") ?: 1) - 1;   // skip joining slashes
            if (inp[1] == '\0') break;              // trim the tail slash
            if (inp[1] == '.') {                    // handle './' and '../'
                if (strchr("\\/", inp[2])) {
                    inp++; continue;
                } else if (inp[2] == '.' && strchr("\\/", inp[3])) {
                    char *prev = memrchr(buf, '/', out - buf);
                    if (prev && prev != buf) out = prev;
                    inp += 2; continue;
                }
            }
        }
        *out++ = inp[0] == '\\' ? '/' : inp[0];     // escape backslash
    }
    *out = '\0';
    return buf;
}

const char * filesys_norm(filesys_type_t type, const char *path) {
    static filesys_path_t buf; return filesys_norm_r(type, buf, path);
}

static char * filesys_vjoin(
    filesys_type_t type, filesys_path_t buf, size_t argc, va_list argv
) {
    if (!buf || !argc) return buf;
    va_list ap;
    va_copy(ap, argv);
    filesys_path_t path;
    size_t len = 0, idx = 0;
    while (idx < argc && va_arg(ap, const char *) != buf) { idx++; }
    if (idx == argc) {
        va_end(ap);
        va_copy(ap, argv);
    } else {
        argc -= idx + 1;
        len = strlen(strncpy(path, buf, sizeof(path) - 1));
    }
    while (argc--) {
        const char *chunk = va_arg(ap, const char *) ?: "";
        if (strchr("\\/", chunk[0])) chunk++;
        len += snprintf(path + len, sizeof(path) - len, "/%s", chunk);
    }
    va_end(ap);
    return filesys_norm_r(type, buf, path);
}

char * filesys_join_r(filesys_type_t t, filesys_path_t b, size_t argc, ...) {
    va_list ap;
    va_start(ap, argc);
    char *ret = filesys_vjoin(t, b, argc, ap);
    va_end(ap);
    return ret;
}

const char * filesys_join(filesys_type_t type, size_t argc, ...) {
    static filesys_path_t buf;
    va_list ap;
    va_start(ap, argc);
    char *ret = filesys_vjoin(type, buf, argc, ap);
    va_end(ap);
    return ret;
}

bool filesys_touch(filesys_type_t type, const char *path) {
    FILE *fd = fopen(filesys_norm(type, path), "a");
    return fd && fclose(fd) == 0; // try utime if not working
}

static const char * statperm(mode_t mode) {
    static char buf[11]; // trwxrwxrwx
    mode_t match[] = {
        S_IFBLK, S_IFCHR, S_IFIFO, S_IFREG, S_IFDIR, S_IFLNK, S_IFSOCK
    };
    const char * tchr = "bcp-dls";
    buf[0] = ' ';
    LOOPN(i, strlen(tchr)) {
        if ((mode & S_IFMT) != match[i]) continue;
        buf[0] = tchr[i]; break;
    }
    for (int i = 0; i < 3; i++, mode >>= 3) {
        for (int j = 0, idx = (3 - i) * 3; j < 3; j++, idx--) {
            buf[idx] = mode & (1 << j) ? "xwr"[j] : '-';
        }
    }
    buf[10] = '\0';
    return buf;
}

void filesys_pstat(filesys_type_t type, const char *path) {
    path = filesys_norm(type, path);
    struct stat st;
    if (stat(path, &st)) return;
    const char *desc;
    switch (st.st_mode & S_IFMT) {
    case S_IFBLK:   desc = "block special"; break;
    case S_IFCHR:   desc = "character special"; break;
    case S_IFIFO:   desc = "FIFO special"; break;
    case S_IFREG:   desc = "regular file"; break;
    case S_IFDIR:   desc = "directory"; break;
    case S_IFLNK:   desc = "symbolic link"; break;
    case S_IFSOCK:  desc = "socket file"; break;
    default:        desc = "unknown";
    }
    char tbuf[3][36]; // YYYY-mm-dd HH:MM:SS.MILLISECS TZONE
    LOOPN(i, 3) {
        struct timespec *ts = &st.st_atim + i * sizeof(ts);
        struct tm *ptm = localtime(&ts->tv_sec);
        if (strftime(tbuf[i], 36, "%F %T.123456789 %z", ptm)) {
            sprintf(tbuf[i] + 20, "%09ld", ts->tv_nsec);
            tbuf[i][29] = ' '; // fix null-terminator
        } else {
            tbuf[i][0] = '\0';
        }
    }
    printf("  File: %s\n"
           "  Size: %ld\t\tBlocks: %ld\tIO Block: %ld\t%s\n"
           "Device: %xh/%dd\t\tInode: %d\tLinks: %d\n"
           "Access: (%04o/%s)  Uid: %d\tGid: %d\n"
           "Access: %s\nModify: %s\nChange: %s\n",
           path, st.st_size, st.st_blocks, st.st_blksize, desc,
           st.st_dev, st.st_dev, st.st_ino, st.st_nlink,
           st.st_mode & ~S_IFMT, statperm(st.st_mode), st.st_uid, st.st_gid,
           tbuf[0], tbuf[1], tbuf[2]);
}

#ifdef CONFIG_BASE_FFS_SPI
#   define SPIFFS_SENTINEL "_SENTINEL"
static int spiffs_childs(const char *path) {
    /* SPIFFS does not support directory. `stat` always fails and stat.st_mode
     * will always be 0. So we use `opendir` and iterate the folder to
     * determine whether the folder exists.
     * Note: folder path should trim out tailing slashes.
     */
    int num = 0;
    DIR *dir = opendir(fnorm(path));
    for (struct dirent *ent; ( ent = readdir(dir) ); num++) {}
    closedir(dir);
    return num;
}
#endif

bool filesys_exists(filesys_type_t type, const char *path) {
    struct stat st;
    bool ret = !stat(filesys_norm(type, path), &st);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ret ? true : spiffs_childs(path);
#endif
    return ret;
}

bool filesys_isdir(filesys_type_t type, const char *path) {
    struct stat st;
    bool ret = !stat(filesys_norm(type, path), &st);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ret ? false : spiffs_childs(path);
#endif
    return ret && S_ISDIR(st.st_mode);
}

bool filesys_isfile(filesys_type_t type, const char *path) {
    struct stat st;
    bool ret = !stat(filesys_norm(type, path), &st);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ret;
#endif
    return ret && S_ISREG(st.st_mode);
}

bool filesys_mkdir(filesys_type_t type, const char *path) {
    if (filesys_isdir(type, path)) return true;
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) {
        filesys_path_t buf;
        return ftouch(fjoinr(buf, 2, path, SPIFFS_SENTINEL));
    }
#endif
    return mkdir(filesys_norm(type, path), 0755) == 0;
}

bool filesys_rmdir(filesys_type_t type, const char *path) {
    if (!filesys_isdir(type, path)) return true;
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) {
        // only empty directories created by filesys_mkdir can be removed
        if (spiffs_childs(path) > 1) return false;
        filesys_path_t buf;
        fjoinr(buf, 2, path, SPIFFS_SENTINEL);
        return fisfile(buf) ? unlink(buf) == 0 : false;
    }
#endif
    return rmdir(filesys_norm(type, path)) == 0;
}

static int vsort(const void *a, const void *b) {
    return strverscmp(*(const char **)a, *(const char **)b);
}

void filesys_walk(
    filesys_type_t type, const char *path, walk_cb_t callback, void *arg
) {
    // `scandir` is not provided by xtensa-esp32-elf or component/newlib.
    // So we have to write some dirty codes to iterate folder with `readdir`
    // and sort the result by 1) dir-first and 2) strverscmp.
    // This implementation uses less memory than glibc `scandir`!
    filesys_path_t dirname;
    if (!strlen(filesys_norm_r(type, dirname, path))) return;
    struct stat st;
    struct dirent *ent;
    size_t num[2] = { 0, 0 }, cnt[2] = { 0, 0 }; // for dir and non-dir
    char **lst[2] = {NULL, NULL}, *slash; NOTUSED(slash);
    DIR *dir = opendir(dirname);
    while (( ent = readdir(dir) )) {
#ifdef CONFIG_BASE_FFS_SPI
        if (!strcmp(ent->d_name, SPIFFS_SENTINEL)) continue;
        if (type == FILESYS_FLASH && ( slash = strchr(ent->d_name, '/') )) {
            int dlen = slash - ent->d_name + 1, samedir = false;
            LOOPN(i, cnt[0]) {
                if (!strncmp(lst[0][i], ent->d_name, dlen)) samedir = true;
            }
            if (samedir) continue;
            ent->d_name[dlen] = '\0'; // keep tailing slash temporarily
            ent->d_type = DT_DIR;
        }
#endif
        int i = ent->d_type != DT_DIR;
        if (cnt[i] == num[i]) {
            num[i] = (num[i] ?: 5) * 2;
            if (EREALLOC(lst[i], num[i] * sizeof(char **))) goto exit;
        }
        if (!( lst[i][cnt[i]++] = strdup(ent->d_name) )) goto exit;
    }
    LOOPN(i, LEN(lst)) {
        qsort(lst[i], cnt[i], sizeof(char *), vsort);
        LOOPN(j, cnt[i]) {
            char *basename = lst[i][j];
            const char *fullpath = filesys_join(type, 2, dirname, basename);
#ifdef CONFIG_BASE_FFS_SPI
            size_t len = strlen(basename);
            if (basename[len - 1] == '/') { // it's SPIFFS directory
                basename[len - 1] = '\0';   // remove tailing slash
                memset(&st, 0, sizeof(st)); // generate fake stat data
                st.st_size = 4096;
                st.st_mode = S_IFDIR | 0755;
            } else
#endif
            if (stat(fullpath, &st)) {
                ESP_LOGE(TAG, "Could not get stat of `%s`", fullpath);
                continue;
            }
            callback(basename, &st, arg);
        }
    }
exit:
    LOOPN(i, LEN(lst)) {
        while (cnt[i] > 0) { free(lst[i][--cnt[i]]); }  // strdup
        if (num[i]) free(lst[i]);                       // allocated
    }
    closedir(dir);
}

static void print_files(const char *base, const struct stat *st, void *arg) {
    char *utf8 = NULL, buf[13]; // MTH DD HH:MM\0
    time_t ts = time(NULL);
    struct tm *ptm = localtime(&st->st_mtime);
    if (localtime(&ts)->tm_year == ptm->tm_year) {
        strftime(buf, sizeof(buf), "%b %d %H:%M", ptm);
    } else {
        strftime(buf, sizeof(buf), "%b %d  %Y", ptm);
    }
    LOOPN(i, strlen(base)) {
        if (base[i] < 0x7F) continue;
        if (( utf8 = gbk2str(base) )) base = utf8;
        break;
    }
    fprintf((FILE *)arg, "%s %8s %12s %s%s\n", // something like 'ls -alh'
            statperm(st->st_mode), format_size(st->st_size),
            buf, base, S_ISDIR(st->st_mode) ? "/" : "");
    TRYFREE(utf8);
}

void filesys_listdir(filesys_type_t type, const char *path, FILE *stream) {
    filesys_walk(type, path, print_files, stream);
}

static void jsonify_files(const char *base, const struct stat *st, void *arg) {
    cJSON *n = cJSON_CreateObject();
    cJSON_AddStringToObject(n, "name", base);
    cJSON_AddNumberToObject(n, "size", st->st_size);
    cJSON_AddNumberToObject(n, "date", st->st_mtime);
    cJSON_AddStringToObject(n, "type", S_ISDIR(st->st_mode) ? "dir" : "file");
    cJSON_AddItemToArray((cJSON *)arg, n);
}

char * filesys_listdir_json(filesys_type_t type, const char *path) {
    cJSON *lst = cJSON_CreateArray();
    filesys_walk(type, path, jsonify_files, lst);
    char *json = cJSON_PrintUnformatted(lst);
    cJSON_Delete(lst);
    return json;
}

uint8_t * filesys_load(filesys_type_t type, const char *path, size_t *lim) {
    struct stat st;
    uint8_t *buf = NULL;
    if (stat(path = filesys_norm(type, path), &st) || !st.st_size ||
        (lim && st.st_size > *lim) || EMALLOC(buf, st.st_size)) return buf;
    FILE *fd = fopen(path, "r");
    size_t len = fd ? fread(buf, 1, st.st_size, fd) : 0;
    if (len != st.st_size) {
        TRYFREE(buf);
    } else if (lim) {
        *lim = len;
    }
    fclose(fd);
    return buf;
}

#ifdef CONFIG_BASE_USE_ELF
static bool elf_init; // mute elf_loader loggings

static esp_err_t load_elf(
    filesys_type_t type, const char *path, size_t *len,
    esp_elf_t *elf, uint8_t **buf
) {
    if (!elf_init) {
        esp_log_level_set("ELF", ESP_LOG_WARN);
        elf_init = true;
    }
    esp_err_t err = esp_elf_init(elf);
    if (err) return err;
    uint8_t *data = filesys_load(type, path, len);
    if (!data) return ESP_ERR_INVALID_ARG;
    if (( err = esp_elf_relocate(elf, data) ) || !buf) {
        free(data);
    } else {
        *buf = data;
    }
    if (err) esp_elf_deinit(elf);
    return err;
}

esp_err_t filesys_execute(
    filesys_type_t type, const char *path, int argc, char **argv
) {
    size_t len = 10240;
    esp_elf_t elf;
    esp_err_t err = load_elf(type, path, &len, &elf, NULL);
    if (!err) err = esp_elf_request(&elf, 0, argc, argv);
    esp_elf_deinit(&elf);
    return err;
}

esp_err_t filesys_readelf(filesys_type_t type, const char *path, int level) {
    size_t len = 10240;
    uint8_t *buf = NULL;
    esp_elf_t elf;
    esp_err_t err = load_elf(type, path, &len, &elf, &buf);
    elf32_hdr_t *ehdr = (elf32_hdr_t *)buf;
    elf32_phdr_t *phdr = (elf32_phdr_t *)(buf + ehdr->phoff);
    elf32_shdr_t *shdr = (elf32_shdr_t *)(buf + ehdr->shoff);
    size_t plen = sizeof(elf32_phdr_t), slen = sizeof(elf32_shdr_t);
    if (err || len < sizeof(elf32_hdr_t) ||
        len < (ehdr->phoff + ehdr->phnum * plen) ||
        len < (ehdr->shoff + ehdr->shnum * slen))
    {
        err = err ?: ESP_ERR_INVALID_SIZE;
        goto exit;
    }
#   define V(vals, idx) ((idx < LEN(vals) ? vals[idx] : NULL) ?: "unknown")
#   define K(k)         printf("  %s:%*s ", (k), 34 - strlen(k), "")
#   define B(v)         printf("%d (bytes)\n", (v))
#   define H(v)         printf("0x%x\n", (v))
#   define D(v)         printf("%d\n", (v))
    if (level > 0) {
        uint8_t *p = ehdr->ident, xtensa = ehdr->machine == 0x5e;
        const char
        *bits[] = { 0, "32", "64" }, *endian[] = { 0, "little", "big" },
        *version[] = { 0, "current" }, *osabi[] = {
            "UNIX - System V", "UNIX - System V", "HP-UX", "NetBSD", "Linux",
            "Solaris", "IRIX", "FreeBSD", "TRU64", "ARM", "Stand-alone"
        }, *types[] = {
            0, "REL (relocatable)", "EXEC (executable)", "DYN (shared)", "CORE"
        };
        puts("ELF Header:");
        K("Magic");     hexdumpl(ehdr->ident, sizeof(ehdr->ident), -1);
        K("Class");     printf("%c%c%c%s\n", p[1], p[2], p[3], V(bits, p[4]));
        K("Data");      printf("%s-endian\n", V(endian, p[5]));
        K("Version");   printf("%d (%s)\n", p[6], V(version, p[6]));
        K("OS/ABI");    puts(V(osabi, p[7]));
        K("ABI Version");               D(p[8]);
        K("Type");      puts(V(types, ehdr->type));
        K("Machine");   xtensa ? puts("Tensilica Xtensa") : H(ehdr->machine);
        K("Version");                   H(ehdr->version);
        K("Entry point address");       H(ehdr->entry);
        K("Start of program headers");  B(ehdr->phoff);
        K("Start of section headers");  B(ehdr->shoff);
        K("Flags");                     H(ehdr->flags);
        K("Size of this headers");      B(ehdr->ehsize);
        K("Size of program headers");   B(ehdr->phentsize);
        K("Number of program headers"); D(ehdr->phnum);
        K("Size of section headers");   B(ehdr->shentsize);
        K("Number of section headers"); D(ehdr->shnum);
        K("Section header string table index"); D(ehdr->shstrndx);
    }
    if (level > 1 && ehdr->ident[4] == 1) { // 32bits
        const char *types[] = {
            "NULL", "LOAD", "DYNAMIC", "INTERP", "NOTE", "SHLIB", "PHDR"
        };
        puts("\nProgram Headers:");
        puts("  Type    Offset   VirtAddr PhysAddr FileSize MemSize  Flg Align");
        LOOPN(i, ehdr->phnum) {
            printf("  %-7s", V(types, phdr->type));
            for (Elf32_Off *p = &phdr->offset, j = 0; j < 5; j++)
                printf(" 0x%06x", p[j]);
            printf(" %c%c%c", phdr->flags & 4 ? 'R' : ' ',
                    phdr->flags & 2 ? 'W' : ' ', phdr->flags & 1 ? 'E' : ' ');
            printf(" 0x%04x\n", phdr->align);
            phdr++;
        }
    }
    if (level > 2 && ehdr->ident[4] == 1) {
        const char *name, *types[] = {
            "NULL", "PROGBITS", "SYMTAB", "STRTAB", "RELA", "HASH",
            "DYNAMIC", "NOTE", "NOBITS", "REL", "SHLIB", "DYNSYM"
        };
        puts("\nSection Headers:");
        puts("  Nr Name     Type     Addr     Offset Size   ES Flag Ln In Al");
        LOOPN(i, ehdr->shnum) {
            switch (shdr->name) {
            case 0:  name = ""; break;
            case 1:  name = "shstrtab"; break;
            case 11: name = "hash"; break;
            case 17: name = "dynsym"; break;
            case 25: name = "dynstr"; break;
            case 33: name = "rela.dyn"; break;
            case 43: name = "rela.plt"; break;
            case 53: name = "text"; break;
            case 59: name = "rodate"; break;
            case 67: name = "got"; break;
            default: name = "unknown"; break;
            }
            printf("  %2d %-8s %-8s %08x %06x %06x %02x %c%c%c%c %2d %2d %2d\n",
                   i, name, V(types, shdr->type), shdr->addr,
                   shdr->offset, shdr->size, shdr->entsize,
                   shdr->flags & 1 ? 'W' : ' ', shdr->flags & 2 ? 'A' : ' ',
                   shdr->flags & 4 ? 'X' : ' ', shdr->flags & 16 ? 'M' : ' ',
                   shdr->link, shdr->info, shdr->addralign);
            shdr++;
        }
    }
    if (level > 3) {
        // v1.0.0 BUG: esp_elf_print_sec(&elf) should skip ELF_SEC_DRLRO
        const char *secs[] = { "text", "bss", "data", "rodata" };
        puts("\nESP ELF Structure:");
        LOOPN(i, LEN(secs)) {
            printf("%6s: 0x%08x size 0x%08x\n", secs[i],
                   elf.sec[i].addr, elf.sec[i].size);
        }
        printf(" entry: %p load %p\n", elf.entry, buf);
    }
#   undef K
#   undef B
#   undef H
#   undef D
#   undef V
exit:
    esp_elf_deinit(&elf);
    TRYFREE(buf);
    return err;
}
#else
esp_err_t filesys_execute(filesys_type_t t, const char *p, int c, char **v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(t); NOTUSED(p); NOTUSED(c); NOTUSED(v);
}
esp_err_t filesys_readelf(filesys_type_t t, const char *p, int l) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(t); NOTUSED(p); NOTUSED(l);
}
#endif // CONFIG_BASE_USE_ELF
