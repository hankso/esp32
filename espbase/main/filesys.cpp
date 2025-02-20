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
#include "diskio_wl.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"

#include <Arduino.h>

#define PATH_MAX_LEN    255

static const char * TAG = "Filesys";

static SemaphoreHandle_t lock[2]; // for FFS & SDFS

void filesys_initialize() {
    LOOPN(i, LEN(lock)) {
        lock[i] = xSemaphoreCreateBinary();
        if (lock[i]) xSemaphoreGive(lock[i]);
    }
#ifdef CONFIG_BASE_USE_FFS
    if (FFS.begin()) FFS.printInfo();
#endif
#ifdef CONFIG_BASE_USE_SDFS
    if (SDFS.begin()) SDFS.printInfo();
#endif
}

bool filesys_acquire(filesys_type_t type, uint32_t msec) {
    uint8_t idx = type == FILESYS_SDCARD;
    return lock[idx] ? xSemaphoreTake(lock[idx], TIMEOUT(msec)) : false;
}

bool filesys_release(filesys_type_t type) {
    uint8_t idx = type == FILESYS_SDCARD;
    return lock[idx] ? xSemaphoreGive(lock[idx]) : false;
}

bool filesys_get_info(filesys_type_t type, filesys_info_t *info) {
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD) {
        SDFS.getInfo(info);
        return info->card != NULL;
    }
#endif
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) {
        FFS.getInfo(info);
#   ifdef CONFIG_BASE_FFS_FAT
        return info->total != 0 && info->wlhdl != WL_INVALID_HANDLE;
#   else
        return info->total != 0;
#   endif
    }
#endif
    memset(info, 0, sizeof(filesys_info_t));
    info->pdrv = FF_DRV_NOT_USED;
    return false;
}

const char * filesys_norm(filesys_type_t type, const char *path) {
    static char buf[PATH_MAX_LEN], *out, *inp;
    const char *prepend = NULL;
#ifdef CONFIG_BASE_USE_FFS
    if (type == FILESYS_FLASH) prepend = CONFIG_BASE_FFS_MP;
#endif
#ifdef CONFIG_BASE_USE_SDFS
    if (type == FILESYS_SDCARD) prepend = CONFIG_BASE_SDFS_MP;
#endif
    if (!prepend || !strlen(path ?: "")) {
        buf[0] = '\0';
        return buf;
    } else if (path != buf) {
        if (!startswith(path, prepend)) {
            snprintf(buf, sizeof(buf), "%s/%s", prepend, path);
        } else {
            strncpy(buf, path, sizeof(buf) - 1);
        }
    } else if (!startswith(path, prepend)) { // change mountpoint
        char *slash = strdup(strchr(path + 1, '/') ?: "");
        if (slash) {
            snprintf(buf, sizeof(buf), "%s/%s", prepend, slash);
            free(slash);
        }
    } else {
        return buf;
    }
    for (out = inp = buf; inp[0]; inp++) {
        if (strchr("\\/", inp[0])) {
            inp += (strspn(inp, "\\/") ?: 1) - 1;   // skip joining slashes
            if (inp[1] == '\0') break;              // trim the tail slash
            if (inp[1] == '.') {                    // handle './' and '../'
                if (strchr("\\/", inp[2])) {
                    inp++; continue;
                } else if (inp[2] == '.' && strchr("\\/", inp[3])) {
                    out = (char *)memrchr(buf, '/', out - buf) ?: out;
                    inp += 2; continue;
                }
            }
        }
        *out++ = inp[0] == '\\' ? '/' : inp[0];     // escape backslash
    }
    *out = '\0';
    return buf;
}

const char * filesys_join(filesys_type_t type, size_t argc, ...) {
    char buf[PATH_MAX_LEN];
    va_list ap;
    va_start(ap, argc);
    size_t len = 0;
    while (argc--) {
        const char *chunk = va_arg(ap, const char *) ?: "";
        if (strchr("\\/", chunk[0])) chunk++;
        len += snprintf(buf + len, sizeof(buf) - len, "/%s", chunk);
    }
    va_end(ap);
    return filesys_norm(type, buf);
}

bool filesys_touch(filesys_type_t type, const char *path) {
    FILE *fd = fopen(filesys_norm(type, path), "a");
    return fd && fclose(fd) == 0;
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
           "  Size: %d\t\tBlocks: %d\tIO Block: %d\t%s\n"
           "Device: %xh/%dd\t\tInode: %d\tLinks: %d\n"
           "Access: (%04o/%s)  Uid: %d\tGid: %d\n"
           "Access: %s\nModify: %s\nChange: %s\n",
           path, (int)st.st_size, (int)st.st_blocks, (int)st.st_blksize, desc,
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
    struct stat rst;
    bool ret = !stat(filesys_norm(type, path), &rst);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH && !ret) return spiffs_childs(path);
#endif
    return ret;
}

bool filesys_isdir(filesys_type_t type, const char *path) {
    struct stat rst;
    bool ret = !stat(filesys_norm(type, path), &rst);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ret ? false : spiffs_childs(path);
#endif
    return ret && S_ISDIR(rst.st_mode);
}

bool filesys_isfile(filesys_type_t type, const char *path) {
    struct stat rst;
    bool ret = !stat(filesys_norm(type, path), &rst);
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ret;
#endif
    return ret && S_ISREG(rst.st_mode);
}

bool filesys_mkdir(filesys_type_t type, const char *path) {
    if (filesys_isdir(type, path)) return true;
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) return ftouch(fjoin(2, path, SPIFFS_SENTINEL));
#endif
    return mkdir(filesys_norm(type, path), 0755) == 0;
}

bool filesys_rmdir(filesys_type_t type, const char *path) {
    if (!filesys_isdir(type, path)) return true;
#ifdef CONFIG_BASE_FFS_SPI
    if (type == FILESYS_FLASH) {
        // only empty directories created by filesys_mkdir can be removed
        if (spiffs_childs(path) > 1) return false;
        path = fjoin(2, path, SPIFFS_SENTINEL);
        return fisfile(path) ? unlink(path) == 0 : false;
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
    struct stat st;
    struct dirent *ent;
    char dirname[PATH_MAX_LEN], *slash;
    char **lst[2] = {NULL, NULL}, **ptr;
    size_t num[2] = { 0, 0 }, cnt[2] = { 0, 0 }; // for dir and non-dir
    DIR *dir = opendir(strcpy(dirname, filesys_norm(type, path)));
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
            if (( ptr = (char **)realloc(lst[i], num[i] * sizeof(ptr)) )) {
                lst[i] = ptr; // enlarge buffer to store duplicated filenames
            } else goto exit;
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
        if (num[i]) free(lst[i]);                       // realloc
    }
    closedir(dir);
}

static void print_files(const char *base, const struct stat *st, void *arg) {
    char buf[13]; // MTH DD HH:MM\0
    time_t ts = time(NULL);
    int this_year = localtime(&ts)->tm_year;
    struct tm *ptm = localtime(&st->st_mtime);
    if (this_year == ptm->tm_year) {
        strftime(buf, sizeof(buf), "%b %d %H:%M", ptm);
    } else {
        strftime(buf, sizeof(buf), "%b %d  %Y", ptm);
    }
    fprintf((FILE *)arg, "%s %8s %12s %s%s\n", // something like 'ls -alh'
            statperm(st->st_mode), format_size(st->st_size, false),
            buf, base, S_ISDIR(st->st_mode) ? "/" : "");
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

// File system APIs

namespace fs {

static bool _valid_path(const char *path) { return path && path[0] == '/'; }
static bool _write_mode(const char *mode) { return mode && strchr(mode, 'w'); }

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
#ifdef CONFIG_BASE_AUTO_ALIGN
    walk(path, &_loginfo_count, &tmp);
#endif
    walk(path, &_loginfo_print, &tmp);
}

char * CFS::list(const char *path) {
    cJSON *lst = cJSON_CreateArray();
    walk(path, &_jsonify_file, lst);
    char *json = cJSON_PrintUnformatted(lst);
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
    if (EMALLOC(_fpath, plen + len + 1)) return;
    strcpy(_fpath, _fs->mountpoint()); strcat(_fpath, path);

    if (!( _path = strdup(path) )) {
        TRYFREE(_fpath);
        return;
    }
    if (getstat()) {
        if (S_ISREG(_stat.st_mode)) {
            _file = fopen(_fpath, mode);
        } else if (S_ISDIR(_stat.st_mode)) {
            _dir = opendir(_fpath);
        } else {
            ESP_LOGE(TAG, "Path %s unknown type 0x%08X",
                    _fpath, _stat.st_mode & _IFMT);
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
    _baddir = !_isdir;
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
    struct dirent *ent = readdir(_dir);
#ifdef CONFIG_BASE_FFS_SPI // skip placeholder
    if (ent && !strcmp(ent->d_name, SPIFFS_SENTINEL)) ent = readdir(_dir);
#endif
    if (ent == NULL) {
        _nisdir = false;
        return;
    }
    if (( _nisdir = ent->d_type == DT_DIR ) || ent->d_type == DT_REG) {
        String fname = String(ent->d_name), name = String(_path);
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

#ifdef CONFIG_BASE_USE_SDFS

bool SDMMCFS::begin(bool format, const char *mp, uint8_t max) {
    if (_card) return true;

    esp_log_level_set("sdspi_transaction", ESP_LOG_WARN);

    esp_vfs_fat_mount_config_t mount_conf = {
        .format_if_mount_failed = format,
        .max_files = max,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host_conf = SDSPI_HOST_DEFAULT();

    sdspi_device_config_t slot_conf = {
        .host_id   = NUM_SPI,
        .gpio_cs   = PIN_CS0,
        .gpio_cd   = SDSPI_SLOT_NO_CD,
        .gpio_wp   = SDSPI_SLOT_NO_WP,
        .gpio_int  = SDSPI_SLOT_NO_INT,
    };

    // This convenience function calls init function `sdspi_host_init`, which
    // will initialize SPI bus and won't handle ESP_ERR_INVALID_STATE error.
    // So keep in mind to manually handle this error if you re-init a SPI bus.
    esp_err_t err = esp_vfs_fat_sdspi_mount(
        mp, &host_conf, &slot_conf, &mount_conf, &_card);
    if (err && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to mount SD Card: %s", esp_err_to_name(err));
        return false;
    }
    _impl->mountpoint(mp);
    ESP_LOGI(TAG, "SD Card mounted to %s", mp);
    FATFS *fs;
    DWORD free_clust;
    char drv[] = { (char)(48 + ff_diskio_get_pdrv_card(_card)), ':', '\0' };
    if (f_getfree(drv, &free_clust, &fs) == FR_OK) {
#   if FF_MAX_SS != FF_MIN_SS
        uint64_t ssize = fs->ssize; // == _card->csd.sector_size ?
#   else
        uint64_t ssize = FF_SS_SDCARD;
#   endif
        _used = ssize * (fs->n_fatent - 2 - free_clust) * fs->csize;
    }
    _total = (uint64_t)_card->csd.capacity * _card->csd.sector_size;
    return true;
}

void SDMMCFS::end() {
    if (!esp_vfs_fat_sdmmc_unmount()) {
        _impl->mountpoint(NULL);
        _card = NULL;
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

void SDMMCFS::getInfo(filesys_info_t *info) {
    CFS::getInfo(info);
    info->type = FILESYS_SDCARD;
    if (( info->card = _card )) {
        info->pdrv = ff_diskio_get_pdrv_card(_card);
        info->blkcnt = _card->csd.capacity;
        info->blksize = _card->csd.sector_size;
    }
}

void SDMMCFS::printInfo(FILE *stream) {
    CFS::printInfo(stream);
    if (!_card) return;
    // see sdmmc_card_print_info
    const char * type = (
        _card->is_sdio ? "SDIO"
            : (_card->is_mmc ? "MMC"
                : (_card->ocr & SD_OCR_SDHC_CAP ? "SDHC/SDXC"
                    : "SDSC"
    )));
    fprintf(
        stream,
        "Name: %s\n"
        "S/N:  %d\n"
        "VPID: 0x%04X:0x%04X\n"
        "Type: %s\n"
        "Size: %s\n"
        "Freq: %d %cHz%s\n"
        "CSD:  sector_size=%d, read_block_len=%d, capacity=0x%0*X\n",
        _card->cid.name, _card->cid.serial,
        _card->cid.mfg_id, _card->cid.oem_id,
        type, format_size(_total, false),
        _card->max_freq_khz / (_card->max_freq_khz < 1000 ? 1 : 1000),
        _card->max_freq_khz < 1000 ? 'K' : 'M', _card->is_ddr ? ", DDR" : "",
        _card->csd.sector_size, _card->csd.read_block_len,
        _card->csd.capacity >> 16 ? 8 : 4, _card->csd.capacity
    );
    if (_card->is_sdio) {
        fprintf(stream, "SCR:  sd_spec=%d, bus_width=%d\n",
                _card->scr.sd_spec, _card->scr.bus_width);
    }
}
#endif // CONFIG_BASE_USE_SDFS

// FAT File System / SPI Flash File System

#ifdef CONFIG_BASE_USE_FFS

bool FLASHFS::begin(bool format, const char *mp, uint8_t max) {
#ifdef CONFIG_BASE_FFS_FAT
    if (_wlhdl != WL_INVALID_HANDLE) return true;
    esp_vfs_fat_mount_config_t conf = {
        .format_if_mount_failed = format,
        .max_files = max,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount(mp, _label, &conf, &_wlhdl);
    if (!err) {
        FATFS *fs;
        DWORD free_clust;
        char drv[] = { (char)(48 + ff_diskio_get_pdrv_wl(_wlhdl)), ':', '\0' };
        if (f_getfree(drv, &free_clust, &fs) == FR_OK) {
            uint64_t ssize = wl_sector_size(_wlhdl) ?: CONFIG_WL_SECTOR_SIZE;
            _used = ssize * (fs->n_fatent - 2 - free_clust) * fs->csize;
            _total = ssize * (fs->n_fatent - 2) * fs->csize;
        }
    }
#else
    if (esp_spiffs_mounted(_label)) return true;
    size_t used, total;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mp,
        .partition_label = _label,
        .max_files = max,
        .format_if_mount_failed = format
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (!err && !esp_spiffs_info(_label, &total, &used)) {
        _used = used;
        _total = total;
    }
#endif // CONFIG_BASE_FFS_FAT
    if (err) {
        ESP_LOGE(TAG, "Failed to mount FlashFS: %s", esp_err_to_name(err));
        return false;
    } else {
        _impl->mountpoint(mp);
        ESP_LOGI(TAG, "FlashFS mounted to %s", mp);
        return true;
    }
}

void FLASHFS::end() {
    esp_err_t err =
#ifdef CONFIG_BASE_FFS_FAT
        esp_vfs_fat_spiflash_unmount(_impl->mountpoint(), _wlhdl);
#else
        esp_spiffs_mounted(_label) ? \
            esp_vfs_spiffs_unregister(_label) : ESP_ERR_INVALID_STATE;
#endif // CONFIG_BASE_FFS_FAT
    if (!err) {
        _impl->mountpoint(NULL);
        _wlhdl = WL_INVALID_HANDLE;
    }
}

void FLASHFS::walk(const char *dir, void (*cb)(File, void *), void *arg) {
    String base = dir, path;
    if (!base.startsWith("/")) base = "/" + base;
    if (!base.endsWith("/")) base = base + "/";
#ifdef CONFIG_BASE_FFS_FAT
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
#endif // CONFIG_BASE_FFS_FAT
        (*cb)(file, arg);
        file.close();
    }
    root.close();
}

void FLASHFS::getInfo(filesys_info_t *info) {
    CFS::getInfo(info);
    info->type = FILESYS_FLASH;
    if (( info->wlhdl = _wlhdl ) != WL_INVALID_HANDLE) {
        info->pdrv = ff_diskio_get_pdrv_wl(_wlhdl);
        info->blksize = wl_sector_size(_wlhdl) ?: CONFIG_WL_SECTOR_SIZE;
        info->blkcnt = info->blksize ? _total / info->blksize : 0;
    }
}
#endif // CONFIG_BASE_USE_FFS

} // namespace fs

#ifdef CONFIG_BASE_USE_FFS
fs::FLASHFS FFS;
#endif
#ifdef CONFIG_BASE_USE_SDFS
fs::SDMMCFS SDFS;
#endif
