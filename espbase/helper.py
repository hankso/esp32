#!/usr/bin/env python3
# coding=utf-8
#
# File: manager.py
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
import socket
import struct
import fnmatch
import argparse
import tempfile
import threading
import subprocess
from io import StringIO
from os import path as op, environ as env
from urllib.parse import urljoin
from wsgiref.simple_server import WSGIServer

# requirements.txt: bottle, requests, zeroconf
# requirements.txt: numpy, pyturbojpeg, opencv-python
try:
    import bottle
except Exception:
    bottle = None
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
    from turbojpeg import TurboJPEG
    from turbojpeg import TJPF_GRAY, TJPF_BGR, TJPF_BGRA
    from turbojpeg import TJSAMP_GRAY, TJSAMP_420
    tj = TurboJPEG()
except Exception:
    tj = None
try:
    import cv2
except Exception:
    cv2 = None

PORT = 9999
ROOT_DIR = op.dirname(op.abspath(__file__))
IDF_PATH = env['IDF_PATH'] if op.isdir(env.get('IDF_PATH', '')) else ''


def fromroot(*a): return op.join(ROOT_DIR, *a)


def execute(cmd, **kwargs):
    kwargs.setdefault('bufsize', 1)
    kwargs.setdefault('encoding', 'utf8')
    kwargs.setdefault('stdout', subprocess.PIPE)
    kwargs.setdefault('stderr', subprocess.STDOUT)
    kwargs.setdefault('shell', True)
    kwargs.setdefault('text', True)
    check = kwargs.pop('check', False)
    timeout = kwargs.pop('timeout', 10)
    if not kwargs.pop('quiet', False):
        print('Executing command `%s`' % cmd)
    with subprocess.Popen(cmd, **kwargs) as proc:
        try:
            output = proc.communicate(None, timeout)[0]
        except Exception:
            proc.kill()
            return ''
        if check and proc.returncode:
            raise subprocess.CalledProcessError(proc.returncode, cmd)
        return output.strip()


def load_gitignore(start=ROOT_DIR):
    path, parent = start, op.dirname(start)
    while path != parent:
        ignore = op.join(path, '.gitignore')
        if op.isfile(ignore):
            break
        path, parent = parent, op.dirname(parent)
    else:
        return []
    with open(ignore, encoding='utf8') as f:
        content = f.read().splitlines()
        return sorted(set(filter(lambda v: v and v[0] != '#', content)))


def walk_exclude(root, include_exts=[], excludes=None):
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


def random_id(len=8):
    #  from random import choice
    #  from string import hexdigits
    #  return ''.join([choice(hexdigits) for i in range(len)])
    from uuid import uuid4
    return uuid4().hex[:len].upper()


def project_name():
    try:
        with open(fromroot('CMakeLists.txt'), encoding='utf8') as f:
            content = f.read()
        return re.findall(r'project\((\w+)[ \)]', content)[0]
    except Exception:
        return 'testing'


def firmware_url(filename='app.bin', port=PORT):
    host = socket.gethostbyname(socket.gethostname())
    return urljoin('https://%s:%d' % (host, port), filename)


def process_nvs(args):
    partcsv = fromroot('partitions.csv')
    try:
        assert not args.offset or not args.size
        args.offset, args.size = '0xB000', '0x4000'
        with open(partcsv, encoding='utf8') as f:
            for line in f.readlines():
                if line.startswith('#') or not line.strip():
                    continue
                if 'data' not in line or 'nvs' not in line:
                    continue
                chunks = [a.strip(',') for a in line.split()]
                args.offset, args.size = chunks[3:5]
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
        from nvs_partition_gen import main as nvs_gen
        from esptool import _main as nvs_flash
        if op.isdir(op.dirname(args.pack)):
            dist = args.pack
        else:
            dist = tempfile.mktemp() + '.bin'
        sys.argv[1:] = ['generate', args.out, dist, args.size]
        nvs_gen()
        if args.flash:
            sys.argv[1:] = ['-p', args.flash, 'write_flash', args.offset, dist]
            nvs_flash()
    except (SystemExit, Exception) as e:
        return print('Generate NVS binary failed:', e, file=sys.stderr)
    finally:
        if op.exists(args.out):
            os.remove(args.out)
        sys.argv = argv_backup
        sys.stdout = stdout_backup


def gencfg(args):
    if not op.isfile(args.tpl):
        return
    with open(args.tpl, encoding='utf8') as f:
        prefix = project_name()
        data = f.read().format(**{
            'NAME': prefix, 'UID': random_id(args.len),
            'URL': firmware_url(prefix + '.bin'),
        }).replace('\n\n', '\n').replace(' ', '')
    if args.out == sys.stdout and IDF_PATH:
        args.out = tempfile.mktemp()
    if hasattr(args.out, 'name'):
        filename = args.out.name
    else:
        filename = str(args.out)
    if not args.quiet:
        print('Writing NVS information to `%s`' % filename)
    if hasattr(args.out, 'write'):
        args.out.write(data)
        args.out.flush()
    else:
        with open(args.out, 'w', encoding='utf8') as f:
            f.write(data)
    if args.pack and IDF_PATH and filename[0] != '<':
        args.out = filename
        process_nvs(args)


def font_output(args):
    if args.out:
        out = args.out
    elif args.bin:
        out = fromroot('files', 'data', 'chinese')
    else:
        out = fromroot('main', 'scnfont.c')
    dirname, basename = op.split(out)
    if args.bin and not basename.startswith('lv_font'):
        basename = 'lv_font_' + basename
    if args.bin and not basename.endswith('bin'):
        basename += '_%d.bin' % args.size
    return op.join(dirname, basename)


def font_icon(args):
    lvgldir = fromroot('managed_components', 'lvgl__lvgl')
    if not op.exists(lvgldir):
        return ''
    fontdir = op.join(lvgldir, 'scripts', 'built_in_font')
    if args.size % 8:
        print('Skip UNSCII-8 because size is not multiple of 8')
        cmd = '-r 0x20-0x7F --font "%s"' % op.join(fontdir, 'unscii-8.ttf')
    else:
        cmd = '--font "%s" -r 0x20-0x7F' % op.join(fontdir, 'unscii-8.ttf')
    return cmd + (
        ' -r 0x2580-0x25FF'             # block elements + geometric shapes
        ' -r 0x2600-0x26FF'             # miscellaneous symbols
        ' -r 0x2800-0x28FF'             # braille patterns
        ' --font "%s" -r "%s"'
    ) % (
        op.join(fontdir, 'FontAwesome5-Solid+Brands+Regular.woff'),
        ','.join(map(str, [                             # LVGL symbols
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
    )


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
        return print('Font file `%s` not found' % args.font)
    args.out = font_output(args)
    cmd = (
        'lv_font_conv --bpp %d --size %d --no-kerning --format %s -o "%s"'
        ' --font "%s" --symbols "%s" -r 0x3000-0x301F %s'
    ) % (
        args.bpp, args.size, 'bin' if args.bin else 'lvgl', args.out,
        args.font, ''.join(sorted(symbols)), font_icon(args)
    )
    try:
        execute(cmd, quiet=args.quiet)
        if not args.bin:
            font_postproc(args.out)
    except Exception as e:
        return print('Error', e)


# see ESP-IDF docs -> API Guides -> Build system -> common requirements
ESP_IDF_COMMON = '''
    app_trace bootloader console cxx driver efuse esp32 esp_common esp_eth
    esp_event esp_gdbstub esp_hw_support esp_ipc esp_netif esp_phy esp_pm
    esp_ringbuf esp_rom esp_system esp_timer esp_wifi espcoredump esptool_py
    freertos hal heap ieee802154 log lwip main mbedtls newlib nvs_flash
    openthread partition_table pthread soc spi_flash tcpip_adapter vfs
    wpa_supplicant xtensa
'''.split()


def glob_headers(dirname):
    '''find .h files and return relative posix path'''
    try:
        dirname = glob.glob(dirname)[0]
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
    tpl = re.compile(r'INCLUDE_DIRS ([\w\-/" ]+)', re.I)
    comps = {}
    for cmakefile in glob.glob(op.join(components, '*/CMakeLists.txt')):
        compdir = op.dirname(cmakefile)
        compname = op.basename(compdir)
        if compname in ESP_IDF_COMMON or 'arduino' in compname:
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
    '''overwrite main/CMakeLists.txt REQUIRES field'''
    compcmk = fromroot('main', 'CMakeLists.txt')
    if not op.isfile(compcmk):
        return
    tpl = re.compile(r'REQUIRES ([\w\- ]+)')
    try:
        with open(compcmk, encoding='utf8') as f:
            old_content = f.read()
        new_content = tpl.sub('REQUIRES ' + ' '.join(sorted(
            set(comps).union(tpl.findall(old_content)[0].split())
        )), old_content)
        if new_content == old_content:
            return
        with open(compcmk, 'w', encoding='utf8') as f:
            f.write(new_content)
    except Exception as e:
        print('Update main/CMakeLists.txt failed:', e)


def gendeps(args):
    headers = set()
    tpl = re.compile(r'# *include *["<]([\w\.\-/]+)[>"]')
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
        headers.difference_update(glob_headers(op.join(
            env['IDF_TOOLS_PATH'], 'tools/xtensa-esp-elf',
            '*/xtensa-esp-elf/xtensa-esp-elf/include/'
        )))
        for header in list(headers):
            if header.startswith('driver/') or header.startswith('soc/'):
                headers.remove(header)
    except Exception as e:
        print('Skip libc headers failed:', e)
    find_includes(fromroot('managed_components'), headers)  # skip
    comps = find_includes(op.join(IDF_PATH, 'components'), headers)
    comps.update(find_includes(op.join(IDF_PATH, '..', 'components'), headers))
    comps = sorted(comps, key=lambda v: len(v))
    if not args.quiet:
        print(' '.join(comps))
    update_dependencies(comps)


def prebuild(args):
    print('-- Running prebuild scripts (%s) ...' % __file__)
    try:
        os.unlink(fromroot(
            'managed_components', 'espressif__elf_loader', '.component_hash'))
    except Exception:
        pass
    try:
        srcdir = fromroot('webpage')
        dstdir = fromroot('files', 'www')
        rebuild = 'pnpm run -C "%s" build' % srcdir
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
        print('Rebuild webpage failed', e)
    args.quiet = True
    gendeps(args)


def sdkconfig(args):
    try:
        with open(fromroot('sdkconfig')) as f:
            configs = f.read().splitlines()
        with open(fromroot('sdkconfig.defaults')) as f:
            defaults = f.read().splitlines()
    except Exception:
        pass
    tpl = re.compile(r'(.*)=n')
    for line in defaults:
        if not line.strip() or line.startswith('#') or tpl.sub(
            lambda m: '# ' + m.groups()[0] + ' is not set', line
        ) in configs:
            continue
        print(line, 'is set but has no effect')


def size_components(quiet=False, files=False, **k):
    idfsize = op.join(IDF_PATH, 'tools', 'idf_size.py')
    mapfile = fromroot('build', project_name() + '.map')
    if not op.isfile(idfsize):
        return print('idf_size.py not found. Did you run esp-idf/export?')
    if not op.isfile(mapfile):
        return print(op.basename(mapfile) + ' not found. Did you build?')
    tpl = [re.compile(i) for i in (r'lib(.*)\.a', r'lib_a-(.*)', r'(.*)\.obj')]
    try:
        comps = json.loads(execute([
            sys.executable, idfsize, mapfile, '--json',
            '--files' if files else '--archives'
        ], quiet=quiet))
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
        comps = size_components(quiet=args.quiet, files=args.files)
        order, comps = sort_components(comps, sort=args.sort)
    except Exception:
        return
    nlen = max(map(len, comps))
    print('%-*.*s ' % (nlen, nlen, 'Archive') + ' '.join(
        '%*s' % (vlen, printable) for f, printable, v, vlen in order))
    print('%-*.*s ' % (nlen, nlen, 'Total') + ' '.join(
        '%*d' % (vlen, vsum) for f, p, vsum, vlen in order))
    for comp in comps:
        print('%-*s ' % (nlen, comp) + ' '.join(
            '%*d' % (vlen, comps[comp].get(field, 0))
            for field, p, v, vlen in order
        ))


def bonjour_browser(services, devname=None, oneshot=False, timeout=3, **k):
    devices = []
    stopflag = threading.Event()

    class Listener(zeroconf.ServiceListener):
        def log_info(self, info):
            print('%d:' % len(devices), 'TTL', info.host_ttl,
                  'WEIGHT', info.weight, 'PRIORITY', info.priority)
            print('  PTR :', info.name)
            print('  SRV : %s:%d' % (info.server.rstrip('.'), info.port))
            txt = [
                '[%d] %s=%s (%d)' % (i + 1, k, v, len(v))
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
    if zeroconf:
        chunks = re.findall(
            r'_?([a-zA-Z0-9\-]+)', args.service.replace('local', '')
        ) + ['tcp']
        services = {'.'.join(['_' + i for i in chunks][:2]) + '.local.'}
        if args.all:
            services.update(zeroconf.ZeroconfServiceTypes.find(timeout=1))
        return bonjour_browser(services, project_name(), **vars(args))
    print('TODO: send mDNS query packet and parse response')
    addr = ('224.0.0.251', 5353)
    opt = socket.inet_aton(addr[0]) + struct.pack('l', socket.INADDR_ANY)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    udp.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, opt)
    udp.bind(('0.0.0.0', addr[1]))
    udp.settimeout(1)
    timeout = time.time() + args.timeout
    while time.time() < timeout:
        try:
            msg, addr = udp.recvfrom(1024)
            print('Received from %s:%d' % addr, msg)
        except socket.timeout:
            pass
        except KeyboardInterrupt:
            break


def relpath(p, ref='.', strip=False):
    try:
        p = op.relpath(p, ref)
    except Exception:
        pass
    if not strip:
        return p
    if (len(p.split(op.sep)) > 3 and len(p) > 30) or len(p) > 80:
        m = re.findall(r'\w+%s(.+)%s\w+' % tuple([op.sep] * 2), p)
        p = p.replace((m or [''])[0], '...')
    return p


def on_edit(root):
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))
    if 'list' in bottle.request.query:
        path = op.join(root, bottle.request.query.get('path').strip('/'))
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
    elif 'path' in bottle.request.query:
        return bottle.static_file(
            bottle.request.query.get('path').strip('/'), root,
            download='download' in bottle.request.query)
    return bottle.redirect('/editor')  # vue.js router


def on_editc():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def on_editd():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def on_editu():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('header', dict(bottle.request.headers))
    print('params', dict(bottle.request.params))
    for idx, file in enumerate(bottle.request.files.values()):
        print('file', idx, vars(file), dict(file.headers))
    time.sleep(0.5)


def load_config():
    configs = []
    try:
        args = make_parser().parse_args(['--quiet', 'gencfg'])
        args.out = StringIO()
        args.func(args)
        args.out.seek(0)
        record = False
        for line in csv.reader(args.out):
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
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))
    if not hasattr(on_config, 'data'):
        on_config.data = load_config()
    if bottle.request.method == 'GET':
        return on_config.data
    data = bottle.request.params.get('json', {})
    if isinstance(data, str):
        data = json.loads(data)
    on_config.data.update(data)


def on_update():
    if 'raw' in bottle.request.query:
        return 'Raw updation HTML'
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
    if not np:
        raise bottle.HTTPResponse('Audio streaming not available', 500)
    bottle.response.set_header('Cache-Control', 'no-store')
    bottle.response.set_header('Content-Type', 'audio/wav')
    host = bottle.request.environ.get('REMOTE_ADDR')
    port = bottle.request.environ.get('REMOTE_PORT')
    fs = int(kwargs.get('fs', kwargs.get('rate', 44100)))
    nch = int(kwargs.get('nch', kwargs.get('channels', 1)))
    fmt = kwargs.get('fmt', kwargs.get('format', 'int16'))
    BpC = np.dtype(fmt).itemsize
    BpS = BpC * nch
    Bps = BpS * fs
    bpC = BpC * 8
    dts = kwargs.get('dt', (2 ** 32 - 36 - 1) / Bps)
    print('audio streaming to %s:%d started' % (host, port))
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
    print('audio streaming to %s:%d stopped' % (host, port))


def video_random(fs, width, height, qual=1, **k):
    time.sleep(1 / fs)
    img = np.random.randint(0, 255, (height, width), dtype=np.uint8)
    if cv2 is not None:
        return cv2.imencode(
            '.jpg', img, (cv2.IMWRITE_JPEG_QUALITY, qual)
        )[1].tobytes()
    if tj is None:
        return b''
    depth = 1 if img.ndim == 2 else (img.shape[-1] if img.ndim == 3 else 0)
    tjPF = [0, TJPF_GRAY, TJPF_GRAY, TJPF_BGR, TJPF_BGRA][depth]
    tjSA = [0, TJSAMP_GRAY, TJSAMP_420, TJSAMP_420, TJSAMP_420][depth]
    return tj.encode(img, qual, tjPF, tjSA)


def video_stream(**kwargs):
    if not tj and not cv2:
        raise bottle.HTTPResponse('Video streaming not available', 500)
    kwargs.setdefault('fs', 30)
    kwargs.setdefault('width', 640)
    kwargs.setdefault('height', 480)
    bdary = kwargs.get('boundary', 'FRAME')
    host = bottle.request.environ.get('REMOTE_ADDR')
    port = bottle.request.environ.get('REMOTE_PORT')
    bottle.response.set_header('Cache-Control', 'no-store')
    bottle.response.set_header(
        'Content-Type', 'multipart/x-mixed-replace; boundary=--' + bdary
    )
    print('video streaming to %s:%d started' % (host, port))
    while True:
        try:
            frame = video_random(**kwargs)
            yield '\r\n'.join([
                '--' + bdary,
                'Content-Type: image/jpeg',
                'Content-Length: %d' % len(frame),
                '\r\n'
            ]).encode() + frame
            if 'still' in bottle.request.query:
                break
        except (StopIteration, KeyboardInterrupt, GeneratorExit):
            break
        except Exception as e:
            raise bottle.HTTPResponse(str(e), 500)
    print('video streaming to %s:%d stopped' % (host, port))


def on_media(**k):
    if 'wav' in bottle.request.query:
        return audio_stream(**k)
    if 'mjpg' in bottle.request.query:
        return video_stream(**k)
    return bottle.HTTPResponse('''
        <img id="vplayer" style="cursor:pointer" src="media?mjpg&still">
        <audio id="aplayer" preload="none" src="media?wav" controls
            controlslist="nodownload noremoteplayback noplaybackrate"></audio>
        <script>
            vplayer.addEventListener('click', e => {
                let elem = e.target, tmp = elem.src;
                if (tmp.endsWith('&still')) {
                    elem.src = tmp.substr(0, tmp.length - 6);
                } else {
                    elem.src = '';
                    elem.src = tmp + '&still';
                }
            });
            aplayer.addEventListener('pause', e => {
                let elem = e.target, tmp = elem.src;
                elem.src = '';  // stop buffering after paused
                elem.load();
                elem.src = tmp; // recover streaming source
                // alt.: window.stop();
            });
        </script>
    ''')  # simple page for debugging only


def static_factory(_root):
    '''
    1. auto append ".gz" if resolving file
    2. auto detect "index.html" if resolving directory
    3. fallback to file manager if no "index.html" under dir
    '''
    def on_static(filename, root=_root):
        target = op.join(root, filename.strip('/\\'))
        if op.isdir(target):
            return on_static(op.join(target, 'index.html'))
        if op.isfile(target):
            return bottle.static_file(relpath(target, root), root)
        if op.isfile(target + '.gz'):
            return bottle.static_file(
                relpath(target + '.gz', root), root,
                mimetype=bottle.mimetypes.guess_type(target)[0],
                headers={'Content-Encoding': 'gzip'})
        return bottle.HTTPError(404, 'File not found')
    return on_static


def error_factory(root):
    def on_error(err, on_static=static_factory(root)):
        index = on_static('index.html')
        if isinstance(index, bottle.HTTPError):
            return bottle.Bottle.default_error_handler(None, err)
        return index
    return on_error


def test_apis(apis):
    configs = load_config()
    auth = (configs.get('web.http.name'), configs.get('web.http.pass'))
    ulen, mlen = [max(map(len, i)) for i in zip(*apis)]
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
        return bottle.redirect(urljoin(urlbase, bottle.request.path), 307)
    return wrapper if esphost else lambda: ''


class SSLServer(WSGIServer):
    def get_request(self):
        client, addr = self.socket.accept()
        if client.recv(1, socket.MSG_PEEK) == b'\x16':  # TLS client hello
            app = self.get_app()
            if hasattr(app, 'ssl_opt'):
                client = ssl.wrap_socket(client, **app.ssl_opt)
            self.base_environ['HTTPS'] = 'yes'
        else:
            self.base_environ['HTTPS'] = 'no'
        self.base_environ['REMOTE_PORT'] = addr[1]
        return client, addr


def webserver(args):
    '''
    bottle BaseRequest arguments outline:

                    [CGI FieldStorage]
                             |
                             V
                    bottle.request.POST
                    |                 |
                    V                 V
        bottle.request.files  bottle.request.forms
                                      |
                                      +---> bottle.request.params
                                      V
        [URL Query String] -> bottle.request.query
    '''
    try:
        url = urljoin('http://', args.esphost + '/api/alive')
        assert args.esphost and requests.get(url, timeout=1).ok
        args.esphost = url[:-10]
    except Exception:
        try:
            args.esphost = 'http://' + search(argparse.Namespace(
                service='id', all=False, oneshot=True, timeout=3, quiet=True
            ))
        except Exception:
            if args.test:
                return print('Could not find alive ESP device')

    args.root = op.abspath(args.root)
    if not op.exists(args.root):
        return print('Could not serve at `%s`: no such directory' % args.root)

    def check(username, password):
        print('checking username `%s` password `%s`' % (username, password))
        return True

    api = bottle.Bottle()
    api.route('/alive', 'GET', redirect_esp32(args.esphost))
    api.route('/exec', 'POST', redirect_esp32(args.esphost))
    api.route('/edit', 'GET', lambda: on_edit(args.root))
    api.route('/editu', 'POST', on_editu)
    api.route('/editc', ['GET', 'POST', 'PUT'], on_editc)
    api.route('/editd', ['GET', 'POST', 'DELETE'], on_editd)
    api.route('/config', ['GET', 'POST'], on_config)
    api.route('/update', 'GET', on_update)
    api.route('/update', 'POST', on_editu)
    api.route('/apmode', 'GET', lambda: 'AP interface only')
    api.route('/media', 'GET', on_media)

    if args.test:
        return test_apis([
            (urljoin(args.esphost, '/api' + route.rule), route.method)
            for route in api.routes
        ])

    if not args.quiet:
        print('WebServer running at `%s`' % relpath(args.root, strip=True))
        if not args.static and args.esphost:
            print('Redirect requests to alive ESP32 at', args.esphost)
        elif not args.static:
            print('Simulate ESP32 APIs: cmd/edit/config/update etc.')
    app = bottle.Bottle()
    if op.exists(args.certfile):
        app.ssl_opt = {'server_side': True, 'certfile': args.certfile}
    if not args.static:
        app.mount('/api', api)
    app.route('/', 'GET', lambda: bottle.redirect('index.html'))
    app.route('/auth', ['GET', 'POST'], bottle.auth_basic(check)(lambda: ''))
    app.route('/<filename:path>', 'GET', static_factory(args.root))
    app.error(404)(error_factory(args.root))
    bottle.run(app, host=args.host, port=args.port, quiet=args.quiet,
               debug=True, server='wsgiref', server_class=SSLServer)


def make_parser():
    parser = argparse.ArgumentParser(epilog='see <command> -h for more')
    parser.set_defaults(func=lambda args: parser.print_help())
    parser.add_argument(
        '-q', '--quiet', action='store_true', help='be slient on CLI')
    subparsers = parser.add_subparsers(
        prog='', title='Supported Commands are', metavar='')

    certpem = fromroot('files', 'data', 'server.pem')
    distdir = [
        p for p in [
            fromroot('webpage', 'dist'),
            fromroot('files', 'www'),
            fromroot('files'),
            fromroot()
        ] if op.isdir(p) and os.listdir(p)
    ][0]
    sparser = subparsers.add_parser(
        'serve', help='Python implemented WebServer to debug (see server.h)')
    sparser.add_argument(
        '-H', '--host', default='0.0.0.0', help='host address to listen on')
    sparser.add_argument(
        '-P', '--port', type=int, default=PORT, help='default %d' % PORT)
    sparser.add_argument(
        '--certfile', default=certpem, help='cert file for HTTPS server')
    sparser.add_argument(
        '--esphost', type=str, help='IP address of ESP chip')
    sparser.add_argument(
        '--test', action='store_true', help='run API compatible test')
    sparser.add_argument(
        '--static', action='count', help='disable ESP32 API, only statics')
    sparser.add_argument(
        'root', nargs='?', default=distdir, help='path to static files')
    sparser.set_defaults(func=webserver)

    sparser = subparsers.add_parser(
        'blame', help='Call idf_size.py to analyze memory and flash usage')
    sparser.add_argument(
        '-s', '--sort', default='', help='sort result by field name or index')
    sparser.add_argument(
        '--files', action='store_true', help='list files instead of archives')
    sparser.set_defaults(func=blame)

    sparser = subparsers.add_parser(
        'search', help='Query mDNS (UDP multicast) to find alive ESP32 chip')
    sparser.add_argument(
        '--all', action='store_true', help='print all founded mDNS records')
    sparser.add_argument(
        '--service', default='id', help='service name of records to query')
    sparser.add_argument(
        '--oneshot', action='store_true', help='stop search after found one')
    sparser.add_argument(
        '--timeout', type=float, default=3, help='search duration in sec')
    sparser.set_defaults(func=search)

    nvsfile = fromroot('nvs_flash.csv')
    nvsdist = fromroot('build', 'nvs.bin')
    sparser = subparsers.add_parser(
        'gencfg', help='Generate unique ID with NVS flash template')
    sparser.add_argument(
        '--tpl', default=nvsfile, help='render nvs from template')
    sparser.add_argument(
        '--pack', default=nvsdist, help='package into nvs binary file')
    sparser.add_argument(
        '--flash', metavar='COM', help='flash NVS binary to specified port')
    sparser.add_argument(
        '--offset', type=str, help='nvs partition offset')
    sparser.add_argument(
        '--size', type=str, help='nvs partition size')
    sparser.add_argument(
        '-l', '--len', type=int, default=6, help='length of UID')
    sparser.add_argument(
        '-o', '--out', default=sys.stdout, help='write to file if specified')
    sparser.set_defaults(func=gencfg)

    sparser = subparsers.add_parser(
        'genfont', help='Scan .c files and tree-shake fonts for LVGL')
    sparser.add_argument('font', help='input font file (TTF/WOFF)')
    sparser.add_argument('--bin', action='store_true', help='output binary')
    sparser.add_argument('--bpp', type=int, default=1, help='bits per pixel')
    sparser.add_argument(
        '-s', '--size', type=int, default=12, help='font size in pixels')
    sparser.add_argument('-o', '--out', help='output font path')
    sparser.set_defaults(func=genfont)

    sparser = subparsers.add_parser(
        'gendeps', help='Scan source files to resolve dependencies')
    sparser.set_defaults(func=gendeps)

    sparser = subparsers.add_parser(
        'prebuild', help='Automatically called by CMakeLists.txt')
    sparser.set_defaults(func=prebuild)

    sparser = subparsers.add_parser(
        'sdkconfig', help='Fix sdkconfig.defaults files')
    sparser.set_defaults(func=sdkconfig)

    return parser


def main(args=sys.argv[1:]):
    args, argv = make_parser().parse_known_args(args)
    args.unknown_args = argv
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
