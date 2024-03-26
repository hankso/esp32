/* 
 * File: filesys.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-15 19:43:15
 */

/* Framework EspAsyncWebServer depends on Arduino FS libraries, so we have to
 * implement our file system based on Arduino FS instead of native ESP32 VFS.
 *
 * - SDMMCFS is implemented for SD Card FAT File System.
 *
 * - FLASHFS supports SPIFFS and FAT format. It is configurable by defining
 *   macro `CONFIG_FFS_FAT` or `CONFIG_FFS_SPIFFS`.
 *
 * This module relies on Arduino FS Abstract Layer but nothing.
 *
 * fs::File implements an `operator bool()` operator, thus File instances
 * can be validated in a boolean context like:
 *
 *      File file = MyFS.open("/filename");
 *      if (!!file || file==true || (bool)file || file ? true : false) {
 *          printf("FileImplPtr is valid\n");
 *          file.close();
 *      }
 *
 * fs::File's boolean operator is actually the validation of `FileImplPtr _p`,
 * which is returned and registered by fs::FSImpl::open. If file doesn't exist
 * or `mode` doesn't contain write flag, `open` should return a null pointer.
 * So we can use a different `exists` logic and employ it in `open`.
 *
 * Note that fs::FileImpl's `operator bool()` has no relation with FileImplPtr,
 * it is used to determine whether the file exists:
 *
 *      MyFSFileImpl file_impl(MyFS, "/path", "r");
 *      if (file_impl) {
 *          printf("file / folder does exist\n");
 *          file_impl.close();
 *      }
 *
 * Note: File system adds about 108 KB to the final firmware.
 */

#pragma once

#include "globals.h"

#include "dirent.h"                 // for DIR
#include "wear_levelling.h"         // for wl_handle_t
#include "diskio_impl.h"            // for FF_DRV_NOT_USED
#include "sys/stat.h"               // for struct stat
#include "driver/sdmmc_types.h"     // for sdmmc_card_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILESYS_FLASH,              // if defined CONFIG_USE_FFS
    FILESYS_SDCARD,             // if defined CONFIG_USE_SDFS
} filesys_type_t;

typedef struct {
    uint64_t used;
    uint64_t total;
    size_t blkcnt;
    size_t blksize;
    int pdrv;                   // available if FAT Flash or FAT SDCard
    union {
        wl_handle_t wlhdl;      // if defined CONFIG_FFS_FAT
        sdmmc_card_t *card;
    };
    filesys_type_t type;
} filesys_info_t;

void filesys_initialize();
bool filesys_acquire(filesys_type_t, uint32_t msec);  // take write lock
bool filesys_release(filesys_type_t);                 // give write lock
bool filesys_get_info(filesys_type_t, filesys_info_t *);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <FS.h>
#include <FSImpl.h>

namespace fs {

class CFSFileImpl;

class CFSImpl : public FSImpl {
protected:
    friend class CFSFileImpl;
public:
    FileImplPtr open(const char *path, const char *mode, const bool create) override;
    bool        exists(const char *path) override;
    bool        rename(const char *from, const char *to) override;
    bool        remove(const char *path) override;
    bool        mkdir(const char *path) override;
    bool        rmdir(const char *path) override;
};

class CFSFileImpl : public FileImpl {
protected:
    CFSImpl *           _fs;
    FILE *              _file;
    DIR *               _dir;
    bool                _badfile;
    bool                _baddir;
    char *              _path;
    bool                _isdir;
    char *              _npath; // path of next file
    bool                _nisdir;
    char *              _fpath; // path with mountpoint
    mutable bool        _written;
    mutable struct stat _stat;
private:
    void dir_next();

    bool getstat() const {
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
public:
    CFSFileImpl(CFSImpl *fs, const char *path, const char *mode);
    ~CFSFileImpl() override { close(); }
    size_t      write(const uint8_t *buf, size_t size) override;
    size_t      read(uint8_t *buf, size_t size) override;
    bool        seek(uint32_t pos, SeekMode mode) override;
    size_t      tell() const;
    void        flush() override;
    void        close() override;
    const char* name() const override;

    operator    bool()                  { return !_badfile || !_baddir; }
    const char* path() const override   { return (const char *)_path; }
    size_t      size() const override   { getstat(); return _stat.st_size; }
    time_t      getLastWrite() override { getstat(); return _stat.st_mtime; }
    size_t      position() const override { return tell(); }
    boolean     isDirectory(void) override { return _isdir; }
    void        rewindDirectory(void) override { if (!_baddir) rewinddir(_dir); }

    bool        setBufferSize(size_t size);
    boolean     seekDir(long pos) override;
    String      getNextFileName(void) override;
    String      getNextFileName(bool *isDir) override;
    FileImplPtr openNextFile(const char *mode) override;
};

class CFS : public FS {
protected:
    uint64_t _total, _used;
public:
    CFS() : FS(FSImplPtr(new CFSImpl())), _total(0), _used(0) {}

    // work through directory
    virtual void walk(const char *, void (*cb)(File, void *), void *) = 0;
    // print information of file entries by walk through directory
    void list(const char *dir, FILE *stream);
    // conver list result to JSON
    char * list(const char *dir);

    uint64_t usedBytes() { return _used; }
    uint64_t totalBytes() { return _total; }

    void getInfo(filesys_info_t *info) {
        memset(info, 0, sizeof(filesys_info_t));
        info->used = _used;
        info->total = _total;
        info->pdrv = FF_DRV_NOT_USED;
    }

    void printInfo(FILE *stream = stdout) {
        fprintf(stream, "File System used %llu/%llu KB (%llu%%)\n",
                _used / 1024, _total / 1024, 100 * _used / _total);
    }
};

#ifdef CONFIG_USE_FFS
class FLASHFS : public CFS {
private:
    const char *_label;
    wl_handle_t _wlhdl = WL_INVALID_HANDLE;
public:
    // can specify partition label name
    FLASHFS(const char *label=NULL) : _label(label) {}

    bool begin(bool fmt=false, const char *base=CONFIG_FFS_MP, uint8_t max=10);
    void end();

    void walk(const char *path, void (*cb)(File, void *), void *arg) override;
    void getInfo(filesys_info_t *info);
};
#endif

#ifdef CONFIG_USE_SDFS
class SDMMCFS : public CFS {
private:
    sdmmc_card_t *_card = NULL;
public:
    SDMMCFS() {}

    bool begin(bool fmt=false, const char *base=CONFIG_SDFS_MP, uint8_t max=10);
    void end();

    void walk(const char *path, void (*cb)(File, void *), void *arg) override;
    void getInfo(filesys_info_t *info);
    void printInfo(FILE *stream = stdout);
};
#endif

} // namespace fs

#ifdef CONFIG_USE_FFS
extern fs::FLASHFS FFS;
#endif
#ifdef CONFIG_USE_SDFS
extern fs::SDMMCFS SDFS;
#endif

#endif // __cplusplus
