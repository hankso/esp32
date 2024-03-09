/* 
 * File: filesys.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-15 19:43:15
 */

/* Framework EspAsyncWebServer depends on Arduino FS libraries, so we have to
 * implement our file system based on Arduino FS instead of native ESP32 VFS.
 *
 * - SDMMCFS is implemented for SD Card FAT File System in SPI mode.
 *
 * - FLASHFS supports SPIFFS & FFATFS and is configurable by defining macro
 * `CONFIG_USE_FFATFS`. It default initializes SPIFFS on `storage` partition.
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
 *      MyFSFileImpl file_impl(MyFS, "/filename", "r");
 *      if (file_impl) {
 *          printf("file/dir does exist\n");
 *          file_impl.close();
 *      }
 *
 * Note: File system adds about 108 KB to the final firmware.
 */

#pragma once

#include "globals.h"

#include "dirent.h"
#include "sdmmc_cmd.h"
#include "wear_levelling.h"
#include "sys/stat.h"
#include "driver/sdmmc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void filesys_initialize();
void filesys_ffs_info(size_t *used, size_t *total);
void filesys_sdfs_info(size_t *used, size_t *total);

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
    bool                _getstat() const;
private:
    void        dir_next();
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
    size_t      size() const override   { _getstat(); return _stat.st_size; }
    time_t      getLastWrite() override { _getstat(); return _stat.st_mtime; }
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
    size_t _total, _used;
public:
    CFS() : FS(FSImplPtr(new CFSImpl())), _total(0), _used(0) {}

    // work through directory
    virtual void walk(const char *, void (*cb)(File, void *), void *) = 0;
    // print information of file entries
    void list(const char *path, FILE *stream);
    // conver list result to JSON
    char * list(const char *path);

    size_t totalBytes() { return _total; }
    size_t usedBytes() { return _used; }

    void printInfo() {
        fprintf(stdout, "File System used %d/%d KB (%d%%)\n",
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

    void printInfo() {
        if (!_card) {
            fprintf(stdout, "SD Card not detected\n");
            return;
        }
        CFS::printInfo();
        sdmmc_card_print_info(stdout, _card);
    }
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
