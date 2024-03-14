/* 
 * File: filesys.cpp
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-15 19:42:04
 */

#include "filesys.h"
#include "drivers.h"

#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"

#include <Arduino.h>

using namespace fs;

static bool _valid_path(const char *path) { return path && path[0] == '/'; }
static bool _write_mode(const char *mode) { return mode && strchr(mode, 'w'); }

// File System APIs

FileImplPtr CFSImpl::open(const char *path, const char *mode, const bool create) {
    if (!_mountpoint || !_valid_path(path) ||
        (!_write_mode(mode) && !exists(path))) return FileImplPtr();
    return std::make_shared<CFSFileImpl>(this, path, mode);
}

bool CFSImpl::exists(const char *path) {
    if (!_mountpoint || !_valid_path(path)) return false;
    CFSFileImpl file(this, path, "r");
    if (file) {
        file.close();
        return true;
    } else {
        return false;
    }
}

bool CFSImpl::rename(const char *from, const char *to) {
    if (!exists(from) || !_valid_path(to)) return false;
    String mp = _mountpoint, f = mp + from, t = mp + to;
    return ::rename(f.c_str(), t.c_str()) == 0;
}

bool CFSImpl::remove(const char *path) {
    if (!exists(path)) return false;
    String mp = _mountpoint, fullpath = mp + path;
    return unlink(fullpath.c_str()) == 0;
}

bool CFSImpl::mkdir(const char *path) {
    if (!_mountpoint || !_valid_path(path)) return false;
    CFSFileImpl file(this, path, "r");
    if (file) return file.isDirectory();
    String mp = _mountpoint, fullpath = mp + path;
    return ::mkdir(fullpath.c_str(), ACCESSPERMS) == 0;
}

bool CFSImpl::rmdir(const char *path) { return remove(path); }

static void _jsonify_file(File file, void *arg) {
    cJSON *n = cJSON_CreateObject();
    cJSON_AddStringToObject(n, "name", file.name());
    cJSON_AddNumberToObject(n, "size", file.size());
    cJSON_AddNumberToObject(n, "date", file.getLastWrite());
    cJSON_AddStringToObject(n, "type", file.isDirectory() ? "folder" : "file");
    cJSON_AddItemToArray((cJSON *)arg, n);
}

typedef struct {
    FILE *stream;
    size_t align;
    bool header;
} loginfo_ctx_t;

static void _loginfo_count(File file, void *arg) {
    loginfo_ctx_t *ptr = (loginfo_ctx_t *)arg;
    if (!ptr->header) ptr->align = MAX(ptr->align, strlen(file.name()));
}

static void _loginfo_print(File file, void *arg) {
    loginfo_ctx_t *ptr = (loginfo_ctx_t *)arg;
    if (!ptr->header) {
        fprintf(ptr->stream, "Type     Size %-*s (Last Modified)\n",
                ptr->align, "Filename");
        ptr->header = true;
    }
    fprintf(ptr->stream, "%-4s %8s %-*s (%lu)\n",
        file.isDirectory() ? "DIR" : "FILE",
        format_size(file.size(), false),
        ptr->align, file.name(), file.getLastWrite());
}

void CFS::list(const char *path, FILE *stream) {
    loginfo_ctx_t tmp = { stream, strlen("Filename"), false };
#ifdef CONFIG_AUTO_ALIGN
    walk(path, &_loginfo_count, &tmp);
#endif
    walk(path, &_loginfo_print, &tmp);
}

char * CFS::list(const char *path) {
    cJSON *lst = cJSON_CreateArray();
    walk(path, &_jsonify_file, lst);
    char *json = cJSON_Print(lst);
    cJSON_Delete(lst);
    return json;
}

// File APIs

CFSFileImpl::CFSFileImpl(CFSImpl *fs, const char *path, const char *mode)
    : _fs(fs)
    , _file(NULL),      _dir(NULL)
    , _badfile(true),   _baddir(true)
    , _path(NULL),      _isdir(false)
    , _npath(NULL),     _nisdir(false)
    , _fpath(NULL),     _written(true)
{
    if (!path || !strlen(path)) return;
    size_t plen = strlen(_fs->mountpoint()), len = strlen(path);
    if (!( _fpath = (char *)malloc(plen + len + 1) )) return;
    strcpy(_fpath, _fs->mountpoint()); strcat(_fpath, path);

    if (!( _path = strdup(path) )) {
        TRYFREE(_fpath);
        return;
    }
    if (_getstat()) {
        if (S_ISREG(_stat.st_mode)) {
            _file = fopen(_fpath, mode);
        } else if (S_ISDIR(_stat.st_mode)) {
            _dir = opendir(_fpath);
        } else {
            printf("Unknown %s type 0x%08X", _fpath, _stat.st_mode & _IFMT);
        }
    } else {
        if (_write_mode(mode)) {
            _file = fopen(_fpath, mode);
        } else if (_path[len - 1] == '/') {
            _dir = opendir(_fpath);
        } else {
            // We're using different `FS.exists` logic so _dir can be NULL
            // do nothing
        }
    }
    _isdir = _dir != NULL;
    _badfile = _isdir || !_file;
    _baddir = !_isdir || !_dir;
}

bool CFSFileImpl::_getstat() const {
    if (!_fpath) return false;
    if (!_written) return true;
    if (!stat(_fpath, &_stat)) {
        _written = false;
        return true;
    } else {
        memset(&_stat, 0, sizeof(_stat));
        return false;
    }
}

size_t CFSFileImpl::write(const uint8_t *buf, size_t size) {
    if (_badfile || !buf || !size) return 0;
    _written = true;
    return fwrite(buf, 1, size, _file);
}

size_t CFSFileImpl::read(uint8_t *buf, size_t size) {
    if (_badfile || !buf || !size) return 0;
    return fread(buf, 1, size, _file);
}

bool CFSFileImpl::seek(uint32_t pos, SeekMode mode) {
    return _badfile ? false : fseek(_file, pos, mode) == 0;
}

size_t CFSFileImpl::tell() const { return _badfile ? 0 : ftell(_file); }

void CFSFileImpl::flush() {
    if (_badfile) return;
    fflush(_file);
    fsync(fileno(_file));
}

void CFSFileImpl::close() {
    TRYFREE(_path);
    TRYFREE(_npath);
    TRYFREE(_fpath);
    if (_dir)  { closedir(_dir); _dir = NULL; }
    if (_file) { fclose(_file);  _file = NULL; }
    _badfile = _baddir = true;
}

const char * CFSFileImpl::name() const {
    size_t len = strlen(_path);
    if (len < 2) return _path;
    char *ptr = (char *)_path + len - 2;
    while (ptr > _path && *ptr != '/' && *ptr != '\\') ptr--;
    return ptr + 1;
}

boolean CFSFileImpl::seekDir(long pos) {
    if (!_baddir)
        seekdir(_dir, pos);
    return !_baddir;
}

bool CFSFileImpl::setBufferSize(size_t size) {
    return _badfile ? 0 : !setvbuf(_file, NULL, _IOFBF, size);
}

void CFSFileImpl::dir_next() {
    // nisdir   npath    condition
    // false  + NULL  => baddir or end
    // false  + \x00  => skip this entry
    // false  + char  => next is file
    // true   + NULL  => ENOMEM
    // true   + char  => next is dir
    if (_baddir) return;
    TRYFREE(_npath);
    struct dirent *file = readdir(_dir);
    if (file == NULL) {
        _nisdir = false;
        return;
    }
    if (( _nisdir = file->d_type == DT_DIR ) || file->d_type == DT_REG) {
        String fname = String(file->d_name), name = String(_path);
        if (!fname.startsWith("/") && !name.endsWith("/")) name += "/";
        _npath = strdup((name + fname).c_str());
    } else {
        _npath = strdup("");
    }
}

String CFSFileImpl::getNextFileName() {
    dir_next();
    return _valid_path(_npath) ? _npath : "";
}

String CFSFileImpl::getNextFileName(bool *isDir) {
    dir_next();
    if (isDir)
        *isDir = _nisdir;
    return _valid_path(_npath) ? _npath : "";
}

FileImplPtr CFSFileImpl::openNextFile(const char *mode) {
    dir_next();
    if (_npath && !_npath[0])
        return openNextFile(mode);
    if (_valid_path(_npath))
        return std::make_shared<CFSFileImpl>(_fs, _npath, mode);
    return FileImplPtr();
}

// SDMMC - SPI interface (using native spi driver instead of Arduino SPI.h)

#ifdef CONFIG_USE_SDFS

bool SDMMCFS::begin(bool format, const char *base, uint8_t max) {
    esp_vfs_fat_sdmmc_mount_config_t mount_conf = {
        .format_if_mount_failed = format,
        .max_files = max,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host_conf = SDMMC_HOST_DEFAULT();

    // SDMMC_SLOT_CONFIG_DEFAULT()
    sdspi_slot_config_t slot_conf = {
        .gpio_cs   = PIN_CS0,
#ifdef CONFIG_GPIO_HSPI_SDCD
        .gpio_cd   = GPIO_NUMBER(CONFIG_GPIO_HSPI_SDCD),
#else
        .gpio_cd   = SDMMC_SLOT_NO_CD,
#endif
#ifdef CONFIG_GPIO_HSPI_SDWP
        .gpio_wp   = GPIO_NUMBER(CONFIG_GPIO_HSPI_SDWP),
#else
        .gpio_wp   = SDMMC_SLOT_NO_WP,
#endif
#ifdef CONFIG_GPIO_HSPI_SDINT
        .gpio_int  = GPIO_NUMBER(CONFIG_GPIO_HSPI_SDINT),
#else
        .gpio_int  = SDMMC_SLOT_NO_INT,
#endif
        .gpio_miso = PIN_MISO,
        .gpio_mosi = PIN_MOSI,
        .gpio_sck  = PIN_SCLK,
        .dma_channel = 1,
    };

    // This convenience function calls init function `sdspi_host_init`, which
    // will initialize SPI bus and won't handle ESP_ERR_INVALID_STATE error.
    // So keep in mind to manually handle this error if you re-init a SPI bus.
    esp_err_t err = esp_vfs_fat_sdmmc_mount(
        base, &host_conf, &slot_conf, &mount_conf, &_card);
    if (err) {
        printf("Failed to mount SD Card: %s\n", esp_err_to_name(err));
        return false;
    }
    _impl->mountpoint(base);
    printf("Mounted SD Card to %s\n", base);
    _total = (size_t)_card->csd.capacity * _card->csd.sector_size;
#ifdef CONFIG_FFS_FAT
    FATFS *info;
    DWORD free_clust;
    char drv = { (char)(48 + ff_diskio_get_pdrv_card(_card)), ':', '\0' };
    if (f_getfree(drv, &free_clust, &fsinfo) == FR_OK) {
#   if FF_MAX_SS != FF_MIN_SS
        _used = (fs->n_fatent - 2 - free_clust) * fsinfo->csize * fsinfo->ssize;
#   else
        _used = (fs->n_fatent - 2 - free_clust) * fsinfo->csize * FF_SS_SDCARD;
#   endif
    }
#endif
    return true;
}

void SDMMCFS::end() {
    esp_err_t err = esp_vfs_fat_sdmmc_unmount();
    if (!err) {
        _card = NULL;
        _impl->mountpoint(NULL);
    }
}

void SDMMCFS::walk(const char *path, void (*cb)(File, void *), void *arg) {
    String base = path;
    if (!base.startsWith("/")) base = "/" + base;
    if (!base.endsWith("/")) base += "/";
    File root = open(base), file;
    while (file = root.openNextFile()) {
        (*cb)(file, arg);
        file.close();
    }
    root.close();
}

SDMMCFS SDFS;

#endif // CONFIG_USE_SDFS

// FAT File System / SPI Flash File System

#ifdef CONFIG_USE_FFS

bool FLASHFS::begin(bool format, const char *base, uint8_t max) {
#ifdef CONFIG_FFS_FAT
    if (_wlhdl != WL_INVALID_HANDLE) return true;
    esp_vfs_fat_mount_config_t conf = {
        .format_if_mount_failed = format,
        .max_files = max,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount(base, _label, &conf, &_wlhdl);
    if (!err) {
        FATFS *fsinfo;
        DWORD free_clust;
        char drv[] = { (char)(48 + ff_diskio_get_pdrv_wl(_wlhdl)), ':', '\0' };
        if (f_getfree(drv, &free_clust, &fsinfo) == FR_OK) {
            size_t ssize = fsinfo->csize * CONFIG_WL_SECTOR_SIZE;
            _total = (fs->n_fatent - 2) * ssize;
            _used = (fs->n_fatent - 2 - free_clust) * ssize;
        }
    }
#else
    if (esp_spiffs_mounted(_label)) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base,
        .partition_label = _label,
        .max_files = max,
        .format_if_mount_failed = format
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (!err) esp_spiffs_info(_label, &_total, &_used);
#endif // CONFIG_FFS_FAT
    if (err) {
        printf("Failed to mount FlashFS: %s\n", esp_err_to_name(err));
        return false;
    } else {
        _impl->mountpoint(base);
        printf("Mounted FlashFS to %s\n", base);
        return true;
    }
}

void FLASHFS::end() {
    esp_err_t err =
#ifdef CONFIG_FFS_FAT
        esp_vfs_fat_spiflash_unmount(_impl->mountpoint(), _wlhdl);
#else
        esp_spiffs_mounted(_label) ? \
            esp_vfs_spiffs_unregister(_label) : ESP_ERR_INVALID_STATE;
#endif // CONFIG_FFS_FAT
    if (!err) {
        _impl->mountpoint(NULL);
        _wlhdl = WL_INVALID_HANDLE;
    }
}

void FLASHFS::walk(const char *dir, void (*cb)(File, void *), void *arg) {
    String base = dir, path;
    if (!base.startsWith("/")) base = "/" + base;
    if (!base.endsWith("/")) base = base + "/";
#ifdef CONFIG_FFS_FAT
    File root = open(base), file;
    while (file = root.openNextFile()) {
#else
    String lastDir = dir;
    File root = open("/"), file;
    while (file = root.openNextFile()) {
        // SPIFFS uses flatten file structure, so skip files under other dirs
        path = file.path();
        if (!path.startsWith(base)) continue;
        // resolve directory path from filename
        int idx = path.indexOf('/', base.length());
        if (idx != -1) {
            path = path.substring(0, idx + 1);
            if (lastDir == path) continue;
            file = open(lastDir = path);
        }
#endif // CONFIG_FFS_FAT
        (*cb)(file, arg);
        file.close();
    }
    root.close();
}

FLASHFS FFS;
#endif // CONFIG_USE_FFS

void filesys_initialize() {
#ifdef CONFIG_USE_FFS
    if (FFS.begin()) FFS.printInfo();
#endif
#ifdef CONFIG_USE_SDFS
    if (SDFS.begin()) SDFS.printInfo();
#endif
}

void filesys_ffs_info(size_t *used, size_t *total) {
#ifdef CONFIG_USE_FFS
    *used = FFS.usedBytes();
    *total = FFS.totalBytes();
#else
    *used = *total = 0;
#endif
}

void filesys_sdfs_info(size_t *used, size_t *total) {
#ifdef CONFIG_USE_SDFS
    *used = SDFS.usedBytes();
    *total = SDFS.usedBytes();
#else
    *used = *total = 0;
#endif
}
