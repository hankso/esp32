#!/usr/bin/env python3
# coding=utf-8
#
# File: esphelper.py
# Author: Hankso
# Webpage: https://github.com/hankso
# Time: Sun 02 Jun 2019 00:46:30 CST

# built-in
import os
import re
import csv
import ssl
import sys
import glob
import json
import time
import uuid
import zlib
import base64
import select
import shutil
import socket
import struct
import fnmatch
import hashlib
import argparse
import tempfile
import threading
import subprocess
from io import BytesIO, StringIO
from os import path as op, environ as ENV
from datetime import datetime
from contextlib import suppress
from urllib.parse import urljoin
from socketserver import ThreadingMixIn
from wsgiref.simple_server import WSGIServer

# requirements.txt: bottle, detools, requests, zeroconf
# requirements.txt: numpy, pyturbojpeg, turbojpeg, opencv-python
try:
    import bottle
except Exception:
    bottle = None
try:
    import detools
except Exception:
    detools = None
try:
    import requests
except Exception:
    requests = None
try:
    import zeroconf
except Exception:
    zeroconf = None
try:
    import numpy as np
except Exception:
    np = None
try:
    import turbojpeg as tj
except Exception:
    tj = None
try:
    import cv2
except Exception:
    cv2 = None

PORT = 9999
ROOT_DIR = op.dirname(op.abspath(__file__))
IDF_PATH = ENV['IDF_PATH'] if op.isdir(ENV.get('IDF_PATH', '')) else ''


def fromroot(*a): return op.join(ROOT_DIR, *a)


def findone(tpl, string, default='', pos=0, endpos=sys.maxsize):
    if not isinstance(tpl, re.Pattern):
        tpl = re.compile(tpl)
    m = tpl.search(string, pos, endpos)
    if not m:
        return default
    gs = m.groups()
    return m.group() if not gs else (gs[0] if len(gs) == 1 else gs)


def project():
    cmake = fromroot('CMakeLists.txt')
    if not op.isfile(cmake):
        import warnings
        warnings.warn('Project CMakeLists.txt not found!')
        return 'unknown'
    with open(cmake, encoding='utf8') as f:
        return findone(r'project\((\w+)[ \)]', f.read(), 'unknown')


def suffix(path, append, sext=None):
    base, ext = op.splitext(path)
    ext = (ext if sext is None else str(sext)).strip('.')
    if ext:
        ext = '.' + ext
    return f'{base}-{append}{ext}'.replace('--', '-')


def execute(*cmd, timeout=10, check=False, quiet=False, **kwargs):
    '''Run command with subprocess and get result'''
    kwargs.setdefault('bufsize', 1)
    kwargs.setdefault('encoding', 'utf8')
    kwargs.setdefault('stdout', subprocess.PIPE)
    kwargs.setdefault('stderr', subprocess.STDOUT)
    kwargs.setdefault('shell', True)
    kwargs.setdefault('text', True)
    cmd = ' '.join(map(str, cmd))
    if not quiet:
        print(f'Executing command `{cmd}`')
    with subprocess.Popen(cmd, **kwargs) as proc:
        try:
            output = proc.communicate(timeout=timeout)[0]
        except Exception:
            proc.kill()
            return ''
        if check and (rc := proc.poll()):
            raise subprocess.CalledProcessError(rc, proc.args)
        return output.strip()


def load_csv(iterable):
    '''Skip comments, empty lines and tabs/spaces'''
    if isinstance(iterable, str):
        with open(iterable, encoding='utf8') as f:
            iterable = f.readlines()
    yield from csv.reader(filter(
        lambda r: not r.startswith('#') and r.strip(), iterable
    ), skipinitialspace=True)


def load_chip(args):
    with open(fromroot(args.bdir, 'sdkconfig'), encoding='utf8') as f:
        for line in f:
            chip = findone(r'CONFIG_IDF_TARGET="(\w+)"', line)
            if chip:
                return chip


def load_gitignore(start=ROOT_DIR):
    '''Search from start dir and read .gitignore file'''
    path, parent = start, op.dirname(start)
    while path != parent:
        ignore = op.join(path, '.gitignore')
        if op.isfile(ignore):
            break
        path, parent = parent, op.dirname(parent)
    else:
        return []
    with open(ignore, encoding='utf8') as f:
        lines = f.read().splitlines()
    return sorted(set(filter(lambda v: v.strip() and v[0] != '#', lines)))


def walk_exclude(root, include_exts=[], excludes=None):
    '''os.walk with optional includes and excludes'''
    matches = list(map(
        fnmatch._compile_pattern,  # lru enabled
        set(map(str, excludes)) if excludes else load_gitignore()
    ))

    def should_exclude(path):
        for match in matches:
            if match(path):
                return True
        return False

    for root, dirs, files in os.walk(root, topdown=True):
        if matches:
            for d in filter(should_exclude, dirs):
                dirs.remove(d)
            for f in filter(should_exclude, files):
                files.remove(f)
        if include_exts:
            for f in files[:]:
                if f.endswith(include_exts):
                    continue
                files.remove(f)
        yield root, dirs, files


def process_nvs(args):
    partcsv = fromroot('partitions.csv')
    try:
        assert not args.offset or not args.size
        args.offset, args.size = '0xB000', '0x4000'
        for row in load_csv(partcsv):
            if row[1] != 'data' or row[2] != 'nvs':
                continue
            args.offset, args.size = row[3:5]
            break
    except AssertionError:
        pass
    except Exception as e:
        print('Parse', partcsv, 'failed:', e)
    argv_backup, stdout_backup = sys.argv[:], sys.stdout
    if args.quiet:
        sys.stdout = None
    try:
        for c in ('nvs_flash/nvs_partition_generator', 'esptool_py/esptool'):
            sys.path.append(op.join(IDF_PATH, 'components', c))
        try:
            import esp_idf_nvs_partition_gen.nvs_partition_gen as generate
        except ImportError:
            import nvs_partition_gen as generate
        import esptool

        if not args.pack:
            dist = tempfile.mktemp() + '.bin'
        elif op.isabs(args.pack):
            dist = args.pack
        else:
            dist = fromroot(args.bdir, args.pack)
        sys.argv[1:] = ['generate', args.output, dist, args.size]
        generate.main()
        if args.flash:
            sys.argv[1:] = ['-p', args.flash, 'write_flash', args.offset, dist]
            esptool.main()
    except (SystemExit, Exception) as e:
        return print('Generate NVS binary failed:', e, file=sys.stderr)
    finally:
        if op.exists(args.output):
            os.remove(args.output)
        sys.argv = argv_backup
        sys.stdout = stdout_backup


def gencfg(args):
    data = {}
    name = project()
    for path in filter(op.isfile, [
        args.tpl, fromroot(op.splitext(args.tpl)[0] + '.local')
    ]):
        for row in load_csv(path):
            data[row[0]] = row  # merge multiple csv files
    data = '\n'.join(','.join(line) for line in data.values()).format(**{
        'NAME': name,
        'UID': uuid.uuid4().hex[:args.len].upper(),
        'URL': f'http://{socket.gethostbyname(socket.gethostname())}:{PORT}',
        'OTA': name + '.bin',
        'HBT': 'board',
    })
    if not data:
        return print('Empty content for NVS flash')
    if args.output is sys.stdout and IDF_PATH:
        args.output = tempfile.mktemp()
    if not args.quiet:
        filename = getattr(args.output, 'name', str(args.output))
        print(f'Writing NVS information to `{filename}`')
    if hasattr(args.output, 'write'):
        args.output.write(data)
        args.output.flush()
    elif isinstance(args.output, str):
        with open(args.output, 'w') as f:
            f.write(data)
        if args.pack and IDF_PATH:
            process_nvs(args)


def gbk2idx(g):
    '''GBK encoding (0x8100 - 0xFE50) => index (0 - 24016)'''
    g -= 0x8100
    return g - (g + 0xFF) // 0x100 * 0x40


def idx2gbk(i): return 0x8100 + i + (i // (0x100 - 0x40) + 1) * 0x40


def gbk2str(g):
    '''GBK encoding (0x8100 - 0xFEFF) => string'''
    return bytes([g >> 8, g & 0xFF]).decode('gbk')


def gengbk(args):
    output = fromroot('data', 'etc', 'gbktable.bin')
    with open(output, 'wb') as f:
        for idx in range(gbk2idx(0xFE50)):
            try:
                u16 = ord(gbk2str(idx2gbk(idx)))  # 0x00A4 - 0xFFE5
            except Exception:
                u16 = 0
            f.write(struct.pack('<H', u16))


def font_output(args):
    if args.output:
        output = args.output
    elif args.bin:
        output = fromroot('data', 'etc', 'chinese')
    else:
        output = fromroot('main', 'scnfont.c')
    dirname, basename = op.split(output)
    if args.bin and not basename.startswith('lv_font'):
        basename = 'lv_font_' + basename
    if args.bin and not basename.endswith('bin'):
        basename = f'{basename}_{args.size}.bin'
    return op.join(dirname, basename)


def font_icon(args):
    lvgldir = fromroot('managed_components', 'lvgl__lvgl')
    if not op.exists(lvgldir):
        return ''
    fontdir = op.join(lvgldir, 'scripts', 'built_in_font')
    fontnum = op.join(fontdir, 'unscii-8.ttf')
    fontsym = op.join(fontdir, 'FontAwesome5-Solid+Brands+Regular.woff'),
    lvgl_tricks = ','.join(map(str, [
        61441, 61448, 61451, 61452, 61453, 61457,
        61459, 61461, 61465, 61468, 61473, 61478,
        61479, 61480, 61502, 61507, 61512, 61515,
        61516, 61517, 61521, 61522, 61523, 61524,
        61543, 61544, 61550, 61552, 61553, 61556,
        61559, 61560, 61561, 61563, 61587, 61589,
        61636, 61637, 61639, 61641, 61664, 61671,
        61674, 61683, 61724, 61732, 61787, 61931,
        62016, 62017, 62018, 62019, 62020, 62087,
        62099, 62189, 62212, 62810, 63426, 63650,
    ]))
    unicode_tricks = (
        ' -r 0x2580-0x25FF'         # block elements + geometric shapes
        ' -r 0x2600-0x26FF'         # miscellaneous symbols
        ' -r 0x2800-0x28FF'         # braille patterns
    )
    if args.size % 8:
        print('Skip UNSCII-8 because size is not multiple of 8')
        cmd = f'-r 0x20-0x7F --font "{fontnum}"'
    else:
        cmd = f'--font "{fontnum}" -r 0x20-0x7F'
    return cmd + unicode_tricks + f' --font "{fontsym}" -r "{lvgl_tricks}"'


def font_postproc(fn):
    with open(fn, 'r', encoding='utf8') as f:
        content = f.read() \
            .replace('lvgl/lvgl.h', 'lvgl.h') \
            .replace('const lv_font_t', 'lv_font_t')
    if '.fallback' not in content:
        content = content.replace(
            '.line_height', '.fallback = LV_FONT_DEFAULT,\n    .line_height')
    with open(fn, 'w', encoding='utf8') as f:
        f.write(content)


def genfont(args):
    '''
    1. npm install -g lv_font_conv
    2. lv_font_conv --help
    '''
    symbols = set()
    tpl = re.compile(r'[\u4E00-\u9FA5]')  # chinese characters
    for root, dirs, files in walk_exclude(fromroot('main')):
        for fn in filter(lambda fn: fn.endswith(('.c', '.cpp')), files):
            with open(op.join(root, fn), encoding='utf8') as f:
                symbols.update(tpl.findall(f.read()))
    if not symbols:
        return print('No chinese characters found. Skip')
    if not op.exists(args.font):
        return print(f'Font file `{args.font}` not found')
    args.output = font_output(args)
    try:
        print(execute((
            f'lv_font_conv --bpp {args.bpp} --size {args.size} --no-kerning '
            f'--format {"bin" if args.bin else "lvgl"} -o "{args.output}" '
            f'--font "{args.font}" --symbols "{"".join(sorted(symbols))}" '
            f'-r 0x3000-0x301F {font_icon(args)}'
        ), quiet=args.quiet))
        if not args.bin and op.isfile(args.output):
            font_postproc(args.output)
    except Exception as e:
        return print('Error', e)


# see ESP-IDF docs -> API Guides -> Build system -> common requirements
ESP_IDF_COMMON = '''
    app_trace bootloader bt cxx driver esp_app_format esp_bootloader_format
    esp_common esp_event esp_gdbstub esp_hw_support esp_ipc esp_netif esp_pm
    esp_ringbuf esp_rom esp_system esp_timer esp_wifi espcoredump esptool_py
    freertos hal heap log lwip main mbedtls newlib partition_table soc efuse
    tcpip_adapter vfs xtensa
'''.split()

ESP_IDF_SKIP = 'arduino arduino-esp32 linux'.split()


def glob_headers(dirname):
    '''Search .h files and return relative posix path'''
    try:
        dirname = op.normpath(dirname)
        return [
            op.relpath(path, dirname).replace('\\', '/')
            for path in glob.glob(op.join(dirname, '**/*.h'), recursive=True)
        ]
    except Exception:
        return []


def find_includes(components, headers):
    '''
    1. find component directories under `components`
    2. find INCLUDE_DIRS from component CMakeLists.txt
    3. find *.h files under INCLUDE_DIRS use `glob_headers`
    4. mark a component as dependency if `headers` are found
    '''
    tpl = re.compile(r'(?<!_)INCLUDE_DIRS ([\w\-/" ]+)', re.I)
    comps = {}
    for cmakefile in glob.glob(op.join(components, '*/CMakeLists.txt')):
        compdir = op.dirname(cmakefile)
        compname = op.basename(compdir)
        if compname in ESP_IDF_COMMON or compname in ESP_IDF_SKIP:
            continue
        found = set()
        with open(cmakefile, encoding='utf8') as f:
            for line in tpl.findall(f.read()):
                for folder in line.split():
                    folder = folder.strip('"')
                    if not folder or folder[0] == '$':
                        continue
                    path = op.join(compdir, folder)
                    found.update(glob_headers(path))
        if len(found):
            extra = None
        else:
            extra = glob_headers(compdir)
        for header in list(headers):
            if extra is not None:
                for path in extra:
                    if path.endswith(header):
                        break
                else:
                    continue
            elif header not in found:
                continue
            headers.remove(header)
            comps.setdefault(compname, []).append(header)
    return comps


def update_dependencies(comps):
    '''Overwrite main/CMakeLists.txt REQUIRES field'''
    compcmk = fromroot('main', 'CMakeLists.txt')
    if not op.isfile(compcmk):
        return
    try:
        with open(op.join(IDF_PATH, 'tools/cmake/version.cmake')) as f:
            verinfo = re.findall(r'IDF_VERSION_(\w+)\s*(\d+)', f.read())
        require = 'REQUIRES' + '_V' + dict(verinfo)['MAJOR']
    except Exception:
        require = 'REQUIRES'
    tpl = re.compile(r'( *)' + require + r'([a-z0-9\s\-_]+)')
    with open(compcmk, encoding='utf8') as f:
        old_content = f.read()
    prefix, old_comps = findone(tpl, old_content)
    indent = prefix.count(' ') // 4 * 4
    new_comps = sorted(comps)  # sorted(set(comps).union(old_comps.split()))
    if indent:
        sub = ' ' * indent + (
            '\n' + ' ' * (indent + 4)
        ).join([require] + new_comps) + '\n'
    else:
        sub = prefix + ' '.join([require] + new_comps)
    new_content = tpl.sub(sub, old_content)
    if new_content != old_content:
        with open(compcmk, 'w', encoding='utf8') as f:
            f.write(new_content)
    return True


def skip_libc(headers):
    headers.difference_update(glob_headers(op.join(
        ENV['IDF_TOOLS_PATH'], 'tools/xtensa-esp-elf',
        '*/xtensa-esp-elf/xtensa-esp-elf/include/'
    )))
    for header in list(headers):
        if header.startswith('driver/') or header.startswith('soc/'):
            headers.remove(header)


def gendeps(args):
    headers = set()
    tpl = re.compile(r'# *include *"([\w\.\-/]+)"')
    for root, dirs, files in walk_exclude(fromroot('main')):
        for fn in filter(lambda fn: fn.endswith(('.c', '.cpp', '.h')), files):
            with open(op.join(root, fn), encoding='utf8') as f:
                headers.update(tpl.findall(f.read()))
    try:
        headers.difference_update(os.listdir(fromroot('main', 'include')))
    except Exception as e:
        print('Skip local headers failed:', e)
    if not op.isdir(IDF_PATH):
        if not args.quiet:
            print('\n'.join(sorted(headers)))
        return
    try:
        skip_libc(headers)
    except Exception as e:
        print('Skip libc headers failed:', e)
    find_includes(fromroot('managed_components'), headers)  # skip
    comps = sorted({
        **find_includes(op.join(IDF_PATH, 'components'), headers),
        **find_includes(op.join(IDF_PATH, '..', 'components'), headers)
    }, key=lambda v: len(v))
    if not args.quiet:
        print(' '.join(comps))
    try:
        if update_dependencies(comps) and not args.quiet:
            print('main/CMakeLists.txt up to date')
    except Exception as e:
        print('Update main/CMakeLists.txt failed:', e)


def prebuild(args):
    print(f'-- Running prebuild scripts ({__file__}) ...')
    with suppress(Exception):
        assert '4.4' in IDF_PATH
        lst = glob.glob(fromroot('managed_components', '*', '.component_hash'))
        for path in lst:
            os.unlink(path)
    try:
        srcdir = fromroot('web')
        dstdir = fromroot('data', 'www')
        rebuild = f'pnpm run -C "{srcdir}" build'
        last_build = op.getmtime(dstdir) + 10
        for root, dirs, files in walk_exclude(srcdir):
            for fn in files:
                if op.getmtime(op.join(root, fn)) < last_build:
                    continue
                execute(rebuild, check=True, quiet=args.quiet)
                raise StopIteration
    except StopIteration:
        pass
    except Exception as e:
        print('-- Rebuild web page failed', e)
    args.quiet = True
    gengbk(args)
    gendeps(args)


def postbuild(args):
    try:
        sys.path.append(op.join(IDF_PATH, 'components/esptool_py/esptool'))
        import esptool
    except Exception:
        return print('-- `esptool` not found. Run esp-idf/export first')
    chip = load_chip(args)
    if not chip:
        return print('-- Chip target not set. Run idf.py set-target first')
    if not detools:
        return print('-- `detools` not found. Run pip install detools>=0.49')
    if not args.new:
        args.new = fromroot(args.bdir, project() + '.bin')
    if not op.isfile(args.new):
        return print(f'-- {op.basename(args.new)} not found. Run idf.py build')
    if not args.old:
        dirname, basename = op.split(args.new)
        args.old = op.join(dirname, 'delta', op.splitext(basename)[0] + '.bak')
    if not op.isfile(args.old):
        os.makedirs(op.dirname(args.old), exist_ok=True)
        shutil.copy(args.new, args.old)
        return print('-- Firmware not updated. Skip')
    with open(args.new, 'rb') as fnew, open(args.old, 'rb') as fold:
        hashnew = hashlib.sha256(fnew.read()).hexdigest()[:8]
        hashold = hashlib.sha256(fold.read()).hexdigest()[:8]
        if hashnew == hashold:
            return print('-- Firmware not updated. Skip')
    print(f'-- Running postbuild scripts ({__file__}) ...')
    pathold, basenew = op.dirname(args.old), op.basename(args.new)
    argv_backup, stdout_backup = sys.argv[:], sys.stdout
    buf, sys.stdout = BytesIO(), StringIO()
    try:
        sys.argv[1:] = ['--chip', chip, 'image_info', args.new]
        esptool.main()
        sys.stdout.seek(0)
        hashimg = findone(r'Hash: (\w+) \(valid\)', sys.stdout.read())
        if len(hashimg) != 64:
            raise RuntimeError('Invalid sha256 from esptool: {len(hashimg)}')
        with open(args.new, 'rb') as fnew, open(args.old, 'rb') as fold:
            detools.create_patch(fold, fnew, buf, compression='heatshrink')
            buf.seek(0)
            patch = buf.read()
        pthfile = op.splitext(args.new)[0] + '.patch'
        with open(pthfile, 'wb') as f:
            f.write(struct.pack(
                '<4s 32s I 24s',
                hashlib.sha256(b'esp_delta_ota').digest()[3::-1],
                bytes.fromhex(hashimg), len(patch), b''
            ) + patch)
        if not args.patch and hashold not in args.old:
            shutil.copy(args.new, args.old)
            shutil.copy(args.old, suffix(args.old, hashold, 'bin'))
        if not args.patch and hashnew not in args.new:
            shutil.copy(pthfile, op.join(pathold, suffix(basenew, f'{hashold}-{hashnew}', 'bin')))
            shutil.copy(args.new, op.join(pathold, suffix(basenew, hashnew, 'bin')))
    except (SystemExit, Exception) as e:
        return print('-- Calculate image info failed:', e, file=sys.stderr)
    finally:
        sys.argv = argv_backup
        sys.stdout = stdout_backup


def sdkconfig_modify(text, targets, mapping):
    for act, name in targets:
        if name not in mapping:
            bak, name = name, 'defaults.' + name
            if name not in mapping:
                print('skip', bak)
                continue
        path = mapping[name]
        head = '# >>> ' + path + '\n'
        tail = '# <<< ' + path + '\n'
        tpl = re.compile(head + '.*' + tail, re.DOTALL)
        exist = findone(tpl, text)
        if act != 1 and exist:
            text = tpl.sub('', text)
            print(path, 'deleted')
        if act != -1 and not exist:
            with open(path, encoding='utf8') as f:
                text += head + f.read() + tail
            print(path, 'appended')
    return text


def sdkconfig_merge(lsrc, lines):
    for line in lines:
        key = line.split('=')[0]
        for i, tmp in enumerate(lsrc):
            if tmp.startswith(key):
                lsrc[i] = line
                break
        else:
            lsrc.append(line)
    return lsrc


def sdkconfig(args):
    tpl = [re.compile(i) for i in (r'[\w.]+$', r'^#|\s*$', r'(.*)=n')]
    targets = [
        (-1 if i[0] == '-' else int(i[0] == '+'), findone(tpl[0], i, '~'))
        for i in (args.targets + args.unknown_args) if i
    ]
    mapping = {
        op.basename(path)[10:]: path.replace('\\', '/') for path in
        glob.glob(fromroot('sdkconfig*')) +
        glob.glob(fromroot(args.bdir, 'sdkconfig*'))
    }
    mapping.setdefault('local', fromroot('sdkconfig.local'))
    try:
        with open(mapping['local'], 'a+', encoding='utf8') as f:
            f.seek(0)
            local = f.read()
    except Exception:
        local = ''
    if targets:
        modified = sdkconfig_modify(local, targets, mapping)
        if modified != local:
            with open(mapping['local'], 'w', encoding='utf8') as f:
                f.write(modified)
        return
    print(f'Found {len(mapping)} files:\n-',
          '\n- '.join(map(relpath, mapping.values())))
    if '' not in mapping:
        return print('`sdkconfig` not found! Run idf.py menuconfig first')
    if 'defaults' not in mapping:
        return print('`sdkconfig.defaults` not found! Skip')
    with open(mapping['defaults'], encoding='utf8') as fsrc:
        lsrc = sdkconfig_merge(fsrc.read().splitlines(), local.splitlines())
    with open(mapping[''], encoding='utf8') as fdst:
        ldst = fdst.read().splitlines()
    for line in lsrc:
        if tpl[1].match(line) or tpl[2].sub(
            lambda m: f'# {m.groups()[0]} is not set', line
        ) in ldst:
            continue
        print(line, 'is set but has no effect')


def size_components(mapfile, quiet=False, files=False, **k):
    idfsize = op.join(IDF_PATH, 'tools', 'idf_size.py')
    if not op.isfile(idfsize):
        return print('idf_size.py not found. Run esp-idf/export first')
    if not op.isfile(mapfile):
        return print(op.basename(mapfile) + ' not found. Run idf.py build')
    tpl = [re.compile(i) for i in (r'lib(.*)\.a', r'lib_a-(.*)', r'(.*)\.obj')]
    try:
        comps = json.loads(execute(
            sys.executable, f'"{idfsize}"', f'"{mapfile}"', '--format json',
            '--files' if files else '--archives', quiet=quiet
        ))
        for comp in list(comps):
            if not any(comps[comp].values()):
                comps.pop(comp)
                continue
            chunks = comp.replace('espressif__', 'esp-').split(':')
            for i, c in enumerate(chunks):
                for r in tpl:
                    c = r.sub(lambda m: m.groups()[0], c)
                chunks[i] = c
            comps[':'.join(chunks)] = comps.pop(comp)
    except Exception as e:
        return print('Parse sizes failed:', e)
    return comps


def sort_components(comps, sort='flash_total', **k):
    FIELDS = {
        '.dram0.bss': 'DRAM bss',
        '.dram0.data': 'DRAM data',
        '.iram0.text': 'IRAM text',
        #  '.iram0.vectors': 'IRAM vectors',
        'ram_st_total': 'RAM total',
        #  '.rtc.text': 'RTC text',
        #  '.rtc.data': 'RTC data',
        #  '.rtc_noinit': 'RTC noinit',
        '.flash.text': 'FLASH text',
        '.flash.rodata': 'FLASH rodata',
        #  '.flash.rodata_noload': 'FLASH rodata_noload',
        #  '.flash.appdesc': 'FLASH app desc',
        'flash_total': 'FLASH total',
    }
    try:
        sort = sort.lower()
        keys = [i.lower() for i in FIELDS.keys()]
        vals = [i.lower() for i in FIELDS.values()]
        if sort.isdigit():
            field = list(FIELDS)[int(sort)]
        elif sort in keys:
            field = list(FIELDS)[keys.index(sort)]
        elif sort in vals:
            field = list(FIELDS)[vals.index(sort)]
        else:
            raise
        names = sorted(comps, key=lambda c: comps[c].get(field, 0), reverse=1)
    except Exception:
        names = sorted(comps)
    order, last = [], ''
    for field, printable in list(FIELDS.items()):
        vsum = sum(comps[comp].get(field, 0) for comp in comps)
        if not vsum:
            continue
        vlen = len(str(vsum))
        chunks = printable.split()
        if len(chunks) > 1:
            chunks[1] = '& ' + chunks[1]
        if chunks[0] == last:
            order.append([field, chunks[1], vsum, max(vlen, len(chunks[1]))])
        else:
            last = chunks[0]
            order.append([field, printable, vsum, max(vlen, len(printable))])
    return order, {i: comps[i] for i in names}


def blame(args):
    try:
        mapfile = fromroot(args.bdir, project() + '.map')
        comps = size_components(mapfile, quiet=args.quiet, files=args.files)
        order, comps = sort_components(comps, sort=args.sort)
        assert len(comps)
    except Exception:
        return
    nlen = max(len('Archive'), max(map(len, comps)))
    print(f'{"Archive":{nlen}} ' + ' '.join(
          f'{printable:{vlen}}' for f, printable, v, vlen in order))
    print(f'{"Total":{nlen}} ' + ' '.join(
          f'{vsum:{vlen}}' for f, p, vsum, vlen in order))
    for name, sizes in comps.items():
        print(f'{name:{nlen}} ' + ' '.join(
              f'{sizes.get(field, 0):{vlen}}' for field, p, v, vlen in order))


def bonjour_browser(services, devname=None, oneshot=False, timeout=3, **k):
    devices = []
    stopflag = threading.Event()

    class Listener(zeroconf.ServiceListener):
        def log_info(self, info):
            print(f'{len(devices)}: TTL {info.host_ttl} '
                  f'WEIGHT {info.weight} PRIORITY {info.priority}\n'
                  f'  PTR : {info.name}\n'
                  f'  SRV : {info.server}:{info.port}')
            txt = [
                f'[{i + 1}] {k}={v} ({len(v)})'
                for i, (k, v) in enumerate(info.decoded_properties.items())
                if len(k) and v is not None
            ]
            if txt:
                print('  TXT :', ('\n' + ' ' * 8).join(txt))
            for ipv6 in info.parsed_addresses(zeroconf.IPVersion.V6Only):
                print('  AAAA:', ipv6)
            for ipv4 in info.parsed_addresses(zeroconf.IPVersion.V4Only):
                print('  A   :', ipv4)
            print('')

        def add_service(self, zc, t, name):
            info = zc.get_service_info(t, name)
            if not k.get('quiet'):
                self.log_info(info)
            devices.append(info)
            if oneshot and (devname is None or name.startswith(devname)):
                stopflag.set()

    with zeroconf.Zeroconf() as ctx:
        for service in sorted(services, key=lambda s: s[::-1]):
            with zeroconf.ServiceBrowser(ctx, service, Listener()):
                if stopflag.wait(timeout / len(services)):
                    break
    for info in sorted(devices, key=lambda i: i.name):
        if devname and info.name.startswith(devname):
            return info.parsed_addresses()[0]


def search(args):
    if not zeroconf:
        return
    chunks = re.findall(
        r'_?([a-zA-Z0-9\-]+)', args.service.replace('local', '')
    ) + ['tcp']
    services = {'.'.join(['_' + i for i in chunks][:2]) + '.local.'}
    if args.all:
        services.update(zeroconf.ZeroconfServiceTypes.find(timeout=1))
    return bonjour_browser(services, project(), **vars(args))


def relpath(p, maxlen=True, ref='.'):
    with suppress(ValueError):
        p = op.normpath(op.relpath(p, ref))
    try:
        if maxlen is True:
            maxlen = os.get_terminal_size().columns
        elif isinstance(maxlen, int) and maxlen < 0:
            maxlen += os.get_terminal_size().columns
    except OSError:
        maxlen = None
    if not isinstance(maxlen, int) or len(p) < maxlen:
        return p
    chunks = p.split(op.sep)
    dotnum = sum(c == '..' for c in chunks)
    dotlen = dotnum * 3 - (dotnum != 0) + len(chunks[dotnum])
    maxlen -= dotlen + 3
    return p if maxlen < 0 else (p[:dotlen] + '...' + p[-maxlen:])


def print_method(prefix=''):
    if prefix:
        print(prefix + ': ', end='')
    print(bottle.request.method, bottle.request.url)


def print_request(prefix='Request', attr='headers', environ=False):
    dct = getattr(bottle.request, attr) or {}
    prefix += (' ' if prefix else '') + attr
    klen = max(len(prefix), max(map(len, dct.keys())) if dct else 0)
    print_method('\n' + prefix.ljust(klen))
    for k, v in dct.items():
        print(f'{k:{klen}}: {v}')
    if attr == 'environ' or not environ:
        return
    keys = sorted([k for k in bottle.request.environ if k not in ENV])
    klen = max(map(len, keys)) if keys else 0
    if keys:
        print('\n' + 'Environments'.ljust(klen))
    for k in keys:
        print(f'{k:{klen}}: {bottle.request.environ[k]}')


def on_edit(root):
    if 'list' in bottle.request.params:
        path = op.join(root, bottle.request.params.path.strip('/'))
        if not op.exists(path):
            bottle.abort(404)
        elif op.isfile(path):
            bottle.abort(403)
        d = []
        for fn in os.listdir(path):
            if fn.startswith('.'):
                continue
            fpath = op.join(path, fn)
            d.append({
                'name': fn,
                'type': 'dir' if op.isdir(fpath) else 'file',
                'size': op.getsize(fpath),
                'date': int(op.getmtime(fpath)) * 1000
            })
        bottle.response.content_type = 'application/json'
        return json.dumps(d)
    elif 'path' in bottle.request.params:
        return bottle.static_file(
            bottle.request.params.path.strip('/'), root,
            download='download' in bottle.request.params)
    return bottle.redirect('/editor')  # vue.js router


def on_edit_extra():
    print_request(attr='forms')
    if bottle.request.method == 'POST':
        for idx, file in enumerate(bottle.request.files.values()):
            print('file', idx, vars(file), dict(file.headers))
    time.sleep(0.5)


def load_config():
    configs = []
    try:
        args = make_parser().parse_args(['--quiet', 'gencfg'])
        args.output = StringIO()
        args.func(args)
        args.output.seek(0)
        record = False
        for line in csv.reader(args.output):
            if 'namespace' in line:
                record = 'config' in line
                continue
            if not record or len(line) != 4:
                continue
            configs.append([line[0], line[3]])
    except Exception as e:
        print('Load configuration failed:', e)
    return dict(configs)


def on_config():
    if not hasattr(on_config, 'data'):
        on_config.data = load_config()
    if bottle.request.method == 'GET':
        return on_config.data
    print_request(attr='forms')
    on_config.data.update(json.loads(bottle.request.forms.get('json', '{}')))


def on_update():
    if 'raw' in bottle.request.params:
        return 'PLACEHOLDER: raw updation HTML'  # see server.c -> UPDATE_HTML
    return bottle.redirect('/updation')  # vue.js router


def audio_random(fs, fmt, sec=0.25, volume=255, **k):
    tones = [261.6, 293.6, 329.6, 349.2, 392, 440, 493.8]  # Hz
    time.sleep(len(tones) * sec)
    x = np.concatenate([
        tone * np.linspace(idx * sec, (idx + 1) * sec, int(fs * sec))
        for idx, tone in enumerate(tones)
    ])
    return (volume * np.sin(2 * np.pi * x)).astype(fmt).tobytes()


def audio_stream(**kwargs):
    bottle.response.set_header('Cache-Control', 'no-store')
    bottle.response.set_header('Content-Type', 'audio/wav')
    remote = bottle.request.environ.get('REMOTE_HOST')
    fs = int(kwargs.get('fs', kwargs.get('rate', 44100)))
    nch = int(kwargs.get('nch', kwargs.get('channels', 1)))
    fmt = kwargs.get('fmt', kwargs.get('format', 'int16'))
    BpC = np.dtype(fmt).itemsize
    BpS = BpC * nch
    Bps = BpS * fs
    bpC = BpC * 8
    dts = kwargs.get('dt', (2 ** 32 - 36 - 1) / Bps)
    print(f'audio streaming to {remote} started')
    yield struct.pack(
        '= 4s I 4s 4s I H H I I H H 4s I',
        b'RIFF', 36 + int(dts * Bps), b'WAVE', b'fmt ', 0x10, 0x01,
        nch, fs, Bps, BpS, bpC, b'data', int(dts * Bps)
    )
    while True:
        try:
            yield audio_random(fs, fmt)
        except (StopIteration, KeyboardInterrupt, GeneratorExit):
            break
        except Exception as e:
            raise bottle.HTTPResponse(str(e), 500)
    print(f'audio streaming to {remote} stopped')


def video_random(fs, width, height, qual=1, depth=1, **k):
    time.sleep(1 / fs)
    img = np.random.randint(0, 255, (height, width, depth), dtype=np.uint8)
    if cv2 is not None:
        return cv2.imencode(
            '.jpg', img, (cv2.IMWRITE_JPEG_QUALITY, qual)
        )[1].tobytes()
    if tj is None:
        return b''
    idx = 0 if img.ndim == 2 else ((img.shape[-1] - 1) if img.ndim == 3 else 0)
    if hasattr(tj, 'TurboJPEG'):
        PF = [tj.TJPF_GRAY, tj.TJPF_GRAY, tj.TJPF_BGR, tj.TJPF_BGRA][idx]
        SA = tj.TJSAMP_420 if idx else tj.TJSAMP_GRAY
        return tj.TurboJPEG().encode(img, qual, PF, SA)
    else:
        PF = [tj.PF.GRAY, tj.PF.GRAY, tj.PF.BGR, tj.PF.BGRA][idx]
        SA = tj.SAMP.Y420 if idx else tj.SAMP.GRAY
        return tj.compress(img, qual, SA, pixelformat=PF)


def video_stream(bdary='FRAME', **kwargs):
    kwargs.setdefault('fs', 30)
    kwargs.setdefault('width', 1280)
    kwargs.setdefault('height', 720)
    bottle.response.set_header('Cache-Control', 'no-store')
    bottle.response.set_header(
        'Content-Type', 'multipart/x-mixed-replace; boundary=' + bdary
    )
    remote = bottle.request.environ.get('REMOTE_HOST')
    print(f'video streaming to {remote} started')
    while True:
        try:
            frame = video_random(**kwargs)
            yield '\r\n'.join([
                f'--{bdary}',
                'Content-Type: image/jpeg',
                f'Content-Length: {len(frame)}',
                '\r\n'
            ]).encode() + frame
            if 'still' in bottle.request.params:
                break
        except (StopIteration, KeyboardInterrupt, GeneratorExit):
            break
        except Exception as e:
            raise bottle.HTTPResponse(str(e), 500)
    print(f'video streaming to {remote} stopped')


def on_media(**k):
    if not hasattr(on_media, 'data'):
        on_media.data = {'pixformat': 4, 'framesize': 13}
    if bottle.request.method == 'POST':
        print_request(attr='forms')
        data = json.loads(bottle.request.forms.get('video', '{}'))
        for key, val in data.items():
            if not isinstance(val, (int, float)):
                data[key] = int(findone(r'^\d+', str(val), 0))
        return on_media.data.update(data)
    if 'video' in bottle.request.params:
        if not tj and not cv2:
            return bottle.HTTPResponse('Video streaming not available', 403)
        if bottle.request.params.video == 'mjpg':
            return video_stream(**k)
        return on_media.data
    if 'audio' in bottle.request.params:
        if not np:
            return bottle.HTTPResponse('Audio streaming not available', 403)
        if bottle.request.params.audio == 'wav':
            return audio_stream(**k)
        return on_media.data
    return bottle.redirect('/stream')  # vue.js router


def rpc_hid(*args):
    # see commands.cpp -> app_hid_args
    parser = getattr(rpc_hid, 'parser', None)
    if parser is None:
        parser = rpc_hid.parser = argparse.ArgumentParser()
        for i in ('-k', '-s', '-m', '-p', '-c', '-d', '--to'):
            parser.add_argument(i, type=str)
        for i in ('-t', '--ts'):
            parser.add_argument(i, type=int)
    try:
        args = vars(parser.parse_known_args(args)[0])
    except SystemExit:
        return parser.format_help()
    if args['ts'] is not None:
        dt = 1e3 * time.time() - args.pop('ts')
        args = {'dt': f'{dt}ms', **args}  # fix order
    print(datetime.now().strftime('%T.%f')[:-3],
          *[f'{k}={v}' for k, v in args.items() if v is not None])


def rpc_sleep(sec, *a):
    with suppress(Exception):
        time.sleep(float(sec))
    return time.strftime('%F %T')


def on_websocket():
    #  print_request(environ=True)
    ws = bottle.request.environ.get('wsgi.websocket')
    if ws is None:
        return print('WebSocket not handled by server')
    try:
        while not ws.closed:
            msg = ws.recv()
            if not msg:
                continue
            try:
                rpc = json.loads(msg)
                func = globals().get('rpc_' + rpc.pop('method'), lambda *a: a)
                rpc['result'] = func(*rpc.pop('params', ()))
                if 'id' in rpc:
                    ws.send(json.dumps(rpc))
            except Exception:
                print('WebSocket got', msg)
        print('WebSocket remote closed')
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print('WebSocket recv error:', e)


def on_board():
    if 'data' not in bottle.request.query:
        print_request(attr='json')
        return {
            'hbtime': 10, 'intval': 10,
            'iurl': urljoin(bottle.request.url, 'board?data')
        }
    print_request(attr='forms')
    for name, fobj in bottle.request.files.items():
        if name != 'image':
            print(name + ':', fobj.file.read())
            continue
        with suppress(Exception):
            fobj.save(fromroot('snapshot.jpg'), overwrite=True)


def static_factory(_root):
    '''
    1. auto append ".gz" if resolving file
    2. auto detect "index.html" if resolving directory
    3. fallback to file manager if no "index.html" under dir
    '''
    def on_static(filename, root=op.normpath(_root)):
        target = op.normpath(op.join(root, filename.strip('/\\')))
        try:
            assert target.startswith(root)
            filename = op.relpath(target, root)
        except Exception:
            return bottle.HTTPError(404)
        if op.isdir(target):
            return on_static(op.join(filename, 'index.html'), root)
        if op.isfile(target):
            return bottle.static_file(filename, root)
        if op.isfile(target + '.gz'):
            return bottle.static_file(
                filename + '.gz', root,
                mimetype=bottle.mimetypes.guess_type(filename)[0],
                headers={'Content-Encoding': 'gzip'})
        return bottle.HTTPError(404)
    return on_static


def error_factory(root):
    def on_error(err, on_static=static_factory(root)):
        print_request('Error')
        index = on_static('index.html')
        if isinstance(index, bottle.HTTPError):
            return bottle.Bottle.default_error_handler(None, err)
        return index
    return on_error


def test_apis(apis):
    configs = load_config()
    auth = (configs.get('web.http.name'), configs.get('web.http.pass'))
    ulen, mlen = [max(map(len, i)) if i else 0 for i in zip(*apis)]
    for url, method in apis:
        print(method.rjust(mlen), url.ljust(ulen), end=' ')
        try:
            kwargs = {'timeout': 1, 'allow_redirects': False}
            resp = requests.request(method, url, **kwargs)
            if resp.status_code in [301, 302, 303, 307, 308, 401]:
                kwargs['allow_redirects'] = True
                kwargs['auth'] = auth
                print(resp.status_code, end=' - ')
                resp = requests.request(method, url, **kwargs)
            print(resp.status_code, resp.reason)
        except Exception:
            print('408 - TIMEOUT')


def redirect_esp32(esphost):
    def wrapper(urlbase=esphost):
        # FIXME: Authorization may be stripped by HTTP client after redirection
        return bottle.redirect(urljoin(urlbase, bottle.request.url), 307)
    return wrapper if esphost else lambda: ''


class WebSocketError(Exception):
    pass


class WebSocket(object):
    def __init__(self, rfile, write, compressed):
        self._rfile, self._write = rfile, write
        self._code, self._closed, self._msg = 0, False, b''
        if compressed:
            self._enc = zlib.compressobj(7, zlib.DEFLATED, -zlib.MAX_WBITS)
            self._dec = zlib.decompressobj(-zlib.MAX_WBITS)
        else:
            self._enc = self._dec = None

    def _read_unpack(self, fmt):
        size = fmt if isinstance(fmt, int) else struct.calcsize(fmt)
        data = self._rfile.read(size)
        if len(data) != size:
            raise WebSocketError('Unexpected EOF while reading')
        return data if size is fmt else struct.unpack(fmt, data)

    def _recv_frame(self):
        buf = self._read_unpack('>BB')
        fin = buf[0] & 0x80
        rsv123 = buf[0] & 0x70
        opcode = buf[0] & 0x0F
        ismask = buf[1] & 0x80
        length = buf[1] & 0x7F
        if opcode > 0x07:
            if not fin:
                raise WebSocketError('Fragmented control frame')
            if length > 125:
                raise WebSocketError('Error length control frame')
        if length == 126:
            length = self._read_unpack('>H')[0]
        elif length == 127:
            length = self._read_unpack('>Q')[0]
        if ismask:
            mask = self._read_unpack('>BBBB')
        if self._enc and (rsv123 & 0x40):  # RSV1
            rsv123 &= ~0x40
            compressed = True
        else:
            compressed = False
        if rsv123:
            raise WebSocketError(f'Invalid RSV value: {rsv123}')
        payload = self._read_unpack(length)
        if ismask:
            payload = bytes(mask[i % 4] ^ c for i, c in enumerate(payload))
        if compressed:
            payload = (
                self._dec.decompress(payload) +
                self._dec.decompress(b'\x00\x00\xff\xff') +
                self._dec.flush()
            )
        return fin, opcode, payload

    def _handle_frame(self, fin, opcode, payload):
        if opcode == 0x00:                              # OPCODE_CONTINUATION
            if not self._code:
                raise WebSocketError('Unexpected frame with OPCODE_CONTINUE')
        elif opcode in [0x01, 0x02]:                    # OPCODE_TEXT|BINARY
            if self._code:
                raise WebSocketError('The opcode in non-fin frame is non-zero')
            self._code = opcode
        elif opcode == 0x08:                            # OPCODE_CLOSE
            if not payload:
                return self.close()
            if len(payload) < 2:
                raise WebSocketError(f'Invalid close frame: {payload}')
            code, msg = struct.unpack(f'>H{len(payload) - 2}s', payload)
            if (code < 1000 or 1004 <= code <= 1006 or 1012 <= code <= 1016 or
               code == 1100 or 2000 <= code <= 2999):
                raise WebSocketError(f'Invalid close code {code}')
            return self.close(code, msg)
        elif opcode == 0x09:                            # OPCODE_PING
            return self._send_frame(0x0A, payload)      # OPCODE_PONG
        elif opcode == 0x0A:                            # OPCODE_PONG
            return
        else:
            raise WebSocketError(f'Unexpected opcode {opcode}')
        self._msg += payload
        if not fin:
            return
        opcode, msg, self._code, self._msg = self._code, self._msg, 0, b''
        return msg if opcode == 0x02 else msg.decode('utf8')

    def _send_frame(self, opcode, payload, compress=False, mask=b'', fin=True):
        rsv123 = 0
        if self._enc and compress:
            payload = self._enc.compress(payload)
            payload += self._enc.flush(zlib.Z_SYNC_FLUSH)
            if payload.endswith(b'\x00\x00\xff\xff'):
                payload = payload[:-4]
            rsv123 = 0x40  # RSV1
        if len(mask) == 4:
            payload = bytes(mask[i % 4] ^ c for i, c in enumerate(payload))
        elif mask:
            raise WebSocketError(f'Invalid mask {mask}')
        length = len(payload)
        if length <= 125:
            extra = b''
        elif length <= 0xFFFF:
            extra = struct.pack('>H', length)
            length = 126
        elif length <= 0xFFFFFFFFFFFFFFFF:
            extra = struct.pack('>Q', length)
            length = 127
        else:
            raise WebSocketError(f'Frame too large: {length} Bytes')
        try:
            self._write(bytes([
                (0x80 if fin else 0) | (rsv123 & 0x70) | (opcode & 0x0F),
                (0x80 if mask else 0) | (length & 0x7F)
            ]) + extra + mask + payload)
        except socket.error as e:
            raise WebSocketError(e)

    closed = property(lambda self: self._closed)

    def close(self, code=1000, msg=b''):
        if self._closed:
            return
        if not isinstance(msg, (bytes, bytearray)):
            msg = str(msg).encode('utf8')
        payload = struct.pack(f'>H{len(msg)}s', code, msg)
        self._send_frame(0x08, payload)  # OPCODE_CLOSE
        self._closed = True

    def recv(self, timeout=1):
        if self._closed:
            raise WebSocketError('WebSocket already closed')
        try:
            while True:
                if not select.select([self._rfile], [], [], timeout)[0]:
                    return
                msg = self._handle_frame(*self._recv_frame())
                if msg is not None:
                    return msg
        except WebSocketError as e:
            self.close(1002, e)
        except UnicodeError as e:
            self.close(1007, e)
        except socket.error as e:
            self.close(msg=e)

    def send(self, msg, binary=False, compress=False):
        if self._closed:
            raise WebSocketError('WebSocket already closed')
        if not isinstance(msg, (bytes, bytearray)):
            msg = str(msg).encode('utf8')
        self._send_frame(0x02 if binary else 0x01, msg, compress=compress)


class Server(ThreadingMixIn, WSGIServer):
    '''
    Call stack of WSGI applications:
        1. WSGIServer.serve_forever()
        2. WSGIServer.get_request() => ssl.SSLContext.wrap_socket()
        3. WSGIServer.process_request() => threading.Thread()
        4. WSGIServer.finish_request()
        5. WSGIRequestHandler.handle()
        6. ServerHandler.run() => app(env, start_response)
        7. ServerHandler.finish_response()

    Patch WSGIServer.get_request to handle both HTTP and HTTPS (SSL Socket)
    Patch WSGIServer.process_request by ThreadingMixIn for Multithread
    Patch WSGIServer.set_app and Bottle.wsgi to add support for WebSocket
    '''

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.base_environ['WS_VERSIONS'] = [str(v) for v in (13, 8, 7)]
        self.base_environ['WS_PROTOCOL'] = set()
        if bottle.Bottle.wsgi != self.wsgi:
            bottle.Bottle.wsgi = self.wsgi

    def get_request(self):
        client, addr = self.socket.accept()
        self.base_environ['REQUEST_START'] = time.time()
        self.base_environ['REMOTE_PORT'] = addr[1]
        self.base_environ['REMOTE_HOST'] = f'{addr[0]}:{addr[1]}'
        if client.recv(1, socket.MSG_PEEK) == b'\x16':  # TLS client hello
            if hasattr(app := self.get_app(), 'ssl_ctx'):
                client = app.ssl_ctx.wrap_socket(client, server_side=True)
            self.base_environ['HTTPS'] = 'yes'
        else:
            self.base_environ['HTTPS'] = 'no'
        return client, addr

    def set_app(self, app):
        self.application = app
        app.server = self

    @staticmethod
    def wsgi(app, env, start_response, origin=bottle and bottle.Bottle.wsgi):
        env['wsgi.multithread'] = True
        env['wsgi.multiprocess'] = False
        def get(key, default=''): return env.get(key, default)
        if (get('REQUEST_METHOD') != 'GET' or get('wsgi.websocket')) or (
            get('HTTP_UPGRADE').lower() != 'websocket' and
            get('HTTP_CONNECTION').lower() != 'upgrade'
        ) or not isinstance(getattr(app, 'server'), Server):
            return origin(app, env, start_response)
        version = get('HTTP_SEC_WEBSOCKET_VERSION')
        if not version or version not in get('WS_VERSIONS'):
            header = {'Sec-Websocket-Version': ', '.join(get('WS_VERSIONS'))}
            resp = bottle.HTTPError(400 if version else 426, **header)
            start_response(resp._wsgi_status_line(), resp.headerlist)
            return [b'Invalid Sec-Websocket-Version']
        try:
            keyb = get('HTTP_SEC_WEBSOCKET_KEY').encode('latin-1')
            if len(base64.b64decode(keyb)) != 16:
                raise
        except Exception:
            start_response('400 Bad Request', [])
            return [b'Invalid Sec-Websocket-Key']
        prot = get('HTTP_SEC_WEBSOCKET_PROTOCOL').split(',')
        prot = get('WS_PROTOCOL').union(filter(None, map(str.strip, prot)))
        exts = get('HTTP_SEC_WEBSOCKET_EXTENSIONS').split(',')
        exts = set(filter(None, (i.split(';')[0].strip() for i in exts)))
        compress = 'premessage-deflate' in exts
        headers = {
            'Upgrade': 'websocket',
            'Connection': 'Upgrade',
            'Sec-Websocket-Accept': base64.b64encode(hashlib.sha1(
                keyb + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
            ).digest()).decode('latin-1'),
        }
        if prot:
            headers['Sec-Websocket-Protocol'] = ', '.join(prot)
        if compress:
            headers['Sec-Websocket-Extensions'] = 'premessage-deflate'
        resp = bottle.HTTPError(101, **headers)
        try:
            write = start_response(resp._wsgi_status_line(), resp.headerlist)
        except AssertionError:  # fix wsgiref.handlers.is_hop_by_hop
            try:
                write = start_response.__self__.write
            except Exception:
                start_response('500 Internal Server Error', [])
                return [b'Could not patch ServerHandler.start_response']
        env.update({
            'wsgi.websocket': WebSocket(get('wsgi.input'), write, compress),
            'wsgi.websocket_version': version,
        })
        write(b'')
        origin(app, env, lambda *a, **k: None)  # return value ignored
        return []


def webserver(args):
    '''
    bottle BaseRequest arguments outline:

            [url-encoded or multipart/form-data]
                             |
                             V
                    bottle.request.POST
                    |                 |
                    V                 V
        bottle.request.files  bottle.request.forms
                                      |
                                      +---> bottle.request.params
                                      |
        [URL Query String] -> bottle.request.query
    '''
    try:
        url = urljoin('http://', args.esphost + '/api/alive')
        assert args.esphost and requests.get(url, timeout=1).ok
        args.esphost = url[:-10]
    except Exception:
        try:
            args.esphost = 'http://' + search(argparse.Namespace(
                service='id', all=False, oneshot=True, timeout=3, quiet=True))
        except Exception:
            if args.test:
                return print('Could not find alive ESP device')

    args.root = op.abspath(args.root)
    if not op.exists(args.root):
        return print(f'Could not serve at `{args.root}`: no such directory')

    def check(username, password):
        print(f'checking username `{username}` password `{password}`')
        return True

    api = bottle.Bottle()
    api.route('/ws', 'ANY', on_websocket)
    api.route('/alive', 'GET', redirect_esp32(args.esphost))
    api.route('/exec', 'POST', redirect_esp32(args.esphost))
    api.route('/media', ['GET', 'POST'], on_media)
    api.route('/edit', 'GET', lambda: on_edit(args.root))
    api.route('/edit', ['POST', 'PUT', 'DELETE'], on_edit_extra)
    api.route('/config', ['GET', 'POST'], on_config)
    api.route('/update', 'GET', on_update)
    api.route('/update', 'POST', on_edit_extra)
    api.route('/apmode', 'GET', lambda: 'AP interface only')

    if args.test:
        return test_apis([
            (urljoin(args.esphost, '/api' + route.rule), route.method)
            for route in api.routes
        ])

    if not args.quiet:
        print('WebServer running at', relpath(args.root))
        if not args.static and args.esphost:
            print('Redirect requests to alive ESP32 at', args.esphost)
        elif not args.static:
            print('Simulate ESP32 APIs: exec/edit/config/update etc.')

    app = bottle.Bottle()
    if op.exists(args.certfile):
        app.ssl_ctx = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        app.ssl_ctx.load_cert_chain(args.certfile)
    if not args.static:
        app.mount('/api/', api)
    app.route('/', 'GET', lambda: bottle.redirect('index.html'))
    app.route('/auth', ['GET', 'POST'], bottle.auth_basic(check)(lambda: ''))
    app.route('/board', 'POST', on_board)
    app.route('/<filename:path>', 'ANY', static_factory(args.root))
    app.error(404)(error_factory(args.root))
    app.add_hook('before_request', lambda: bottle.response.set_header(
        'Access-Control-Allow-Origin', '*'))
    bottle.run(app, host=args.host, port=args.port, quiet=args.quiet,
               debug=True, server='wsgiref', server_class=Server)


def make_parser():
    parser = argparse.ArgumentParser(epilog='see <command> -h for more')
    parser.set_defaults(func=lambda args: parser.print_help())
    parser.add_argument(
        '-q', '--quiet', action='store_true', help='be slient on CLI')
    parser.add_argument(
        '-b', '-B', dest='bdir', default='build', help='set build directory')
    subparsers = parser.add_subparsers(
        prog='', title='Supported Commands are', metavar='')

    certpem = fromroot('data', 'etc', 'server.pem')
    distdir = [
        p for p in [
            fromroot('web', 'dist'),
            fromroot('data', 'www'),
            fromroot('data'),
            fromroot()
        ] if op.isdir(p) and os.listdir(p)
    ][0]
    sparser = subparsers.add_parser(
        'serve', help='Python implemented fake server for HTML debugging')
    sparser.add_argument(
        '-H', '--host', default='0.0.0.0',
        help='host to listen on [default 0.0.0.0]')
    sparser.add_argument(
        '-P', '--port', type=int, default=PORT,
        help=f'port to listen on [default {PORT}]')
    sparser.add_argument(
        '--certfile', default=certpem,
        help=f'cert file for HTTPS server [default {relpath(certpem)}]')
    sparser.add_argument(
        '--esphost', type=str, help='IP address of the alive ESP device')
    sparser.add_argument(
        '--static', action='count', help='only serve HTMLs without ESP32 API')
    sparser.add_argument(
        '--test', action='store_true', help='run API compatible test')
    sparser.add_argument(
        'root', nargs='?', default=distdir,
        help=f'path to serve static files [default {relpath(distdir)}]')
    sparser.set_defaults(func=webserver)

    sparser = subparsers.add_parser(
        'blame', help='Call idf_size.py to analyze memory and flash usage')
    sparser.add_argument(
        '-s', '--sort', default='', help='sort result by field name or index')
    sparser.add_argument(
        '--files', action='store_true', help='list files instead of archives')
    sparser.set_defaults(func=blame)

    sparser = subparsers.add_parser(
        'search', help='Query mDNS (UDP multicast) to find alive ESP devices')
    sparser.add_argument(
        '--all', action='store_true', help='print all founded mDNS records')
    sparser.add_argument(
        '--oneshot', action='store_true', help='stop search after found one')
    sparser.add_argument(
        '--service', default='id',
        help='service name of records to query [default id]')
    sparser.add_argument(
        '--timeout', type=float, default=3,
        help='search duration in seconds [default 3]')
    sparser.set_defaults(func=search)

    nvsfile = fromroot('nvs_flash.csv')
    sparser = subparsers.add_parser(
        'gencfg', help='Generate configuration from NVS partition template')
    sparser.add_argument(
        '-l', '--len', type=int, default=8,
        help='length of generated UID [default 8]')
    sparser.add_argument(
        '--tpl', default=nvsfile,
        help=f'render nvs text from template [default {relpath(nvsfile)}]')
    sparser.add_argument(
        '--pack', nargs='?', const='nvs.bin',
        help='encode nvs text into binary [default $BDIR/nvs.bin]')
    sparser.add_argument(
        '--size', type=str, help='nvs partition size [auto]')
    sparser.add_argument(
        '--offset', type=str, help='nvs partition offset [auto]')
    sparser.add_argument(
        '--flash', metavar='COM', help='flash nvs binary to specified port')
    sparser.add_argument(
        '--output', metavar='DEST', default=sys.stdout,
        help='write nvs text to file [default STDOUT]')
    sparser.set_defaults(func=gencfg)

    sparser = subparsers.add_parser(
        'genfont', help='Scan source files and tree-shake chinese LVGL font')
    sparser.add_argument('font', help='input font file (TTF/WOFF)')
    sparser.add_argument('--bin', action='store_true', help='output as binary')
    sparser.add_argument(
        '--bpp', type=int, default=1, help='bits per pixel [default 1]')
    sparser.add_argument(
        '--size', type=int, default=12,
        help='font size in pixels [default 12]')
    sparser.add_argument(
        '--output', metavar='PATH', help='dest font file [auto]')
    sparser.set_defaults(func=genfont)

    sparser = subparsers.add_parser(
        'gendeps', help='Scan source files to resolve components dependency')
    sparser.set_defaults(func=gendeps)

    sparser = subparsers.add_parser(
        'prebuild', help='Generate GBK and deps and build HTML pages')
    sparser.set_defaults(func=prebuild)

    sparser = subparsers.add_parser(
        'postbuild', help='Generate delta firmware for OTA updation')
    sparser.add_argument(
        'new', nargs='?',
        help='firmware to upgrade to [default $BDIR/$PROJ.bin]')
    sparser.add_argument(
        'old', nargs='?',
        help='firmware to upgrade from [default $BDIR/delta/$PROJ.bak]')
    sparser.add_argument(
        '--patch', action='store_true',
        help='only generate patch and skip firmware backup etc.')
    sparser.set_defaults(func=postbuild)

    sparser = subparsers.add_parser(
        'sdkconfig', help='Generate or modify sdkconfig related files')
    sparser.add_argument(
        'targets', nargs='*', metavar='[+|-]TARGET',
        help='append/delete/toggle config to sdkconfig.local')
    sparser.set_defaults(func=sdkconfig)

    return parser


def main(args=sys.argv[1:]):
    args, argv = make_parser().parse_known_args(args)
    args.unknown_args = argv
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
