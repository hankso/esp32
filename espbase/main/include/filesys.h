/* 
 * File: filesys.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-15 19:43:15
 */

#pragma once

#include "globals.h"

#include "dirent.h"                 // for DIR
#include "sys/stat.h"               // for struct stat
#include "diskio_impl.h"            // for FF_DRV_NOT_USED
#include "wear_levelling.h"         // for wl_handle_t
#include "driver/sdmmc_types.h"     // for sdmmc_card_t

#if defined(CONFIG_BASE_USE_ELF) && !__has_include("esp_elf.h")
#   warning "Run `idf.py add-dependency espressif/elf_loader`"
#   undef CONFIG_BASE_USE_ELF
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PATH_MAX_LEN 255

typedef char filesys_path_t[PATH_MAX_LEN];

typedef enum {
    FILESYS_FLASH = 1,  // if defined CONFIG_BASE_USE_FFS
    FILESYS_SDCARD = 2, // if defined CONFIG_BASE_USE_SDFS
    FILESYS_COUNT = 2
} filesys_type_t;

#define FILESYS_TYPE(sdcard) ( (sdcard) ? FILESYS_SDCARD : FILESYS_FLASH )

typedef struct {
    filesys_type_t type;
    uint64_t used;
    uint64_t total;
    size_t blkcnt;
    size_t blksize;
    int pdrv; // available if FAT Flash or FAT SDCard
} filesys_info_t;

void filesys_initialize();
bool filesys_acquire(filesys_type_t, uint32_t msec);  // take write lock
bool filesys_release(filesys_type_t);                 // give write lock
bool filesys_get_info(filesys_type_t, filesys_info_t *);
void filesys_print_info(filesys_type_t);

// Mountpoint will be prepended to the path according to filesys_type_t
char * filesys_norm_r(filesys_type_t, filesys_path_t, const char *);
char * filesys_join_r(filesys_type_t, filesys_path_t, size_t argc, ...);
const char * filesys_norm(filesys_type_t, const char *); // NOT reentrant
const char * filesys_join(filesys_type_t, size_t argc, ...); // NOT reentrant

// Return true if success/already-done, false if command failed
bool filesys_touch(filesys_type_t, const char *);
bool filesys_mkdir(filesys_type_t, const char *);
bool filesys_rmdir(filesys_type_t, const char *);
bool filesys_isdir(filesys_type_t, const char *);
bool filesys_isfile(filesys_type_t, const char *);
bool filesys_exists(filesys_type_t, const char *);

typedef void (*walk_cb_t)(const char *basename, const struct stat *, void *);
void filesys_walk(filesys_type_t, const char *, walk_cb_t, void *arg);
void filesys_pstat(filesys_type_t, const char *);
void filesys_listdir(filesys_type_t, const char *, FILE *stream);
char * filesys_listdir_json(filesys_type_t, const char *); // need free
uint8_t * filesys_load(filesys_type_t, const char *, size_t *); // need free
esp_err_t filesys_readelf(filesys_type_t, const char *, int verbose); // 0-4
esp_err_t filesys_execute(filesys_type_t, const char *, int argc, char **argv);

// Aliases
#define fnormr(...)     filesys_norm_r(FILESYS_FLASH, __VA_ARGS__)
#define fjoinr(...)     filesys_join_r(FILESYS_FLASH, __VA_ARGS__)
#define fnorm(...)      filesys_norm(FILESYS_FLASH, __VA_ARGS__)
#define fjoin(...)      filesys_join(FILESYS_FLASH, __VA_ARGS__)
#define fload(...)      filesys_load(FILESYS_FLASH, __VA_ARGS__)
#define ftouch(...)     filesys_touch(FILESYS_FLASH, __VA_ARGS__)
#define fmkdir(...)     filesys_mkdir(FILESYS_FLASH, __VA_ARGS__)
#define frmdir(...)     filesys_rmdir(FILESYS_FLASH, __VA_ARGS__)
#define fisdir(...)     filesys_isdir(FILESYS_FLASH, __VA_ARGS__)
#define fisfile(...)    filesys_isfile(FILESYS_FLASH, __VA_ARGS__)
#define fexists(...)    filesys_exists(FILESYS_FLASH, __VA_ARGS__)

#define snormr(...)     filesys_norm_r(FILESYS_SDCARD, __VA_ARGS__)
#define sjoinr(...)     filesys_join_r(FILESYS_SDCARD, __VA_ARGS__)
#define snorm(...)      filesys_norm(FILESYS_SDCARD, __VA_ARGS__)
#define sjoin(...)      filesys_join(FILESYS_SDCARD, __VA_ARGS__)
#define sload(...)      filesys_load(FILESYS_SDCARD, __VA_ARGS__)
#define stouch(...)     filesys_touch(FILESYS_SDCARD, __VA_ARGS__)
#define smkdir(...)     filesys_mkdir(FILESYS_SDCARD, __VA_ARGS__)
#define srmdir(...)     filesys_rmdir(FILESYS_SDCARD, __VA_ARGS__)
#define sisdir(...)     filesys_isdir(FILESYS_SDCARD, __VA_ARGS__)
#define sisfile(...)    filesys_isfile(FILESYS_SDCARD, __VA_ARGS__)
#define sexists(...)    filesys_exists(FILESYS_SDCARD, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
