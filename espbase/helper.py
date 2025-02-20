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
import gzip
import json
import time
import socket
import struct
import argparse
import tempfile
import threading
import os.path as op
from io import StringIO
from posixpath import join as urljoin
from wsgiref.simple_server import WSGIServer

# requirements.txt: bottle, requests, zeroconf
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

# these are default values
__basedir__ = op.dirname(op.abspath(__file__))
__codedir__ = op.join(__basedir__, 'main')
__cmkfile__ = op.join(__basedir__, 'CMakeLists.txt')
__partcsv__ = op.join(__basedir__, 'partitions.csv')
__nvsfile__ = op.join(__basedir__, 'nvs_flash.csv')
__nvsdist__ = op.join(__basedir__, 'build', 'nvs.bin')
__certpem__ = op.join(__basedir__, 'files', 'data', 'server.pem')
__lvgldir__ = op.join(__basedir__, 'managed_components', 'lvgl__lvgl')
__distdir__ = (tuple(filter(
    lambda p: op.isdir(p) and os.listdir(p),
    [
        op.join(__basedir__, 'webpage', 'dist'),
        op.join(__basedir__, 'files', 'www'),
        op.join(__basedir__, 'files'),
    ]
)) or [__basedir__])[0]


PORT = 9999


def random_id(len=8):
    #  from random import choice
    #  from string import hexdigits
    #  return ''.join([choice(hexdigits) for i in range(len)])
    from uuid import uuid4
    return uuid4().hex[:len].upper()


def project_name():
    try:
        with open(__cmkfile__) as f:
            content = f.read()
        return re.findall(r'project\((\w+)[ \)]', content)[0]
    except Exception:
        return 'testing'


def firmware_url(filename='app.bin', port=PORT):
    host = socket.gethostbyname(socket.gethostname())
    return urljoin('https://%s:%d' % (host, port), filename)


def process_nvs(args):
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        return
    try:
        offset, size = '0xB000', '0x4000'
        with open(__partcsv__, 'r') as f:
            for line in f.readlines():
                if line.startswith('#') or not line.strip():
                    continue
                if 'data' not in line or 'nvs' not in line:
                    continue
                chunks = [a.strip(',') for a in line.split()]
                offset, size = chunks[3:5]
                break
    except Exception as e:
        print('Parse', __partcsv__, 'failed:', e)
    argv_backup = sys.argv[:]
    try:
        for folder in (
            'components/nvs_flash/nvs_partition_generator',
            'components/esptool_py/esptool',
        ):
            sys.path.append(op.join(idf_path, folder))
        from nvs_partition_gen import main as nvs_gen
        from esptool import _main as nvs_flash
        if op.isdir(op.dirname(__nvsdist__)):
            dist = __nvsdist__
        else:
            dist = tempfile.mktemp()
        sys.argv[1:] = ['generate', args.out, dist, size]
        nvs_gen()
        if args.flash:
            sys.argv[1:] = ['-p', args.flash, 'write_flash', offset, dist]
            nvs_flash()
    except Exception as e:
        return print('Generate NVS partition binary failed:', e)
    finally:
        if op.exists(args.out):
            os.remove(args.out)
        sys.argv = argv_backup


def genid(args):
    if op.exists(args.tpl):
        with open(args.tpl, 'r') as f:
            prefix = project_name()
            data = f.read() \
                .replace('{NAME}', prefix) \
                .replace('{UID}', random_id(args.len)) \
                .replace('{URL}', firmware_url(prefix + '.bin')) \
                .replace('\n\n', '\n') \
                .replace(' ', '')
    else:
        data = random_id(args.len)
    if args.pack and 'IDF_PATH' in os.environ:
        args.out = tempfile.mktemp()
    if hasattr(args.out, 'name'):
        filename = args.out.name
    else:
        filename = str(args.out).strip('<>')
    if not args.quiet:
        print('Writing NVS information to `%s`' % filename)
    if hasattr(args.out, 'write'):
        args.out.write(data)
    else:
        with open(args.out, 'w') as f:
            f.write(data)
    if args.pack:
        process_nvs(args)


def font_output(args):
    if args.out:
        out = args.out
    elif args.bin:
        out = op.join(__basedir__, 'files', 'data', 'chinese')
    else:
        out = op.join(__basedir__, 'main', 'scnfont.c')
    dirname, basename = op.split(out)
    if args.bin and not basename.startswith('lv_font'):
        basename = 'lv_font_' + basename
    if args.bin and not basename.endswith('bin'):
        basename += '_%d.bin' % args.size
    return op.join(dirname, basename)


def font_icon(args):
    if not op.exists(__lvgldir__):
        return ''
    fontdir = op.join(__lvgldir__, 'scripts', 'built_in_font')
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
    for root, dirs, files in os.walk(__codedir__):
        for fn in filter(lambda fn: fn.endswith(('.c', '.cpp')), files):
            with open(op.join(root, fn), encoding='utf-8') as f:
                symbols.update(re.findall(r'[\u4E00-\u9FA5]', f.read()))
    if not symbols:
        return print('No chinese characters found. Skip')
    if not op.exists(args.font):
        return print('Font file `%s` not found' % args.font)
    args.out = font_output(args)
    cmd = (
        'lv_font_conv --bpp %d --size %d --no-kerning --format %s -o "%s"'
        ' --font "%s" --symbols "%s" -r 0x3000-0x301F %s'  # chinese characters
    ) % (
        args.bpp, args.size, 'bin' if args.bin else 'lvgl', args.out,
        args.font, ''.join(sorted(symbols)), font_icon(args)
    )
    print('Executing command', cmd)
    rc = os.system(cmd)
    if rc == 0 and not args.bin:
        font_postproc(args.out)


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
        if info.name.startswith(devname):
            return info.parsed_addresses()[0]
    return  # not found


def searchesp(args):
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


def edit_get(root):
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))
    if 'list' in bottle.request.query:
        name = bottle.request.query.get('list').strip('/')
        path = op.join(root, name)
        if not op.exists(path):
            bottle.abort(404)
        elif op.isfile(path):
            bottle.abort(403)
        d = []
        for fn in os.listdir(path):
            fpath = op.join(path, fn)
            d.append({
                'name': fn,
                'type': 'folder' if op.isdir(fpath) else 'file',
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


def edit_create():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def edit_delete():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def edit_upload():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('header', dict(bottle.request.headers))
    print('params', dict(bottle.request.params))
    for idx, file in enumerate(bottle.request.files.values()):
        print('file', idx, vars(file), dict(file.headers))
    time.sleep(0.5)


def config_load():
    configs = []
    try:
        args = make_parser().parse_args(['--quiet', 'genid'])
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


def config_handler():
    print(bottle.request.path)
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))
    if not hasattr(config_handler, 'data'):
        config_handler.data = config_load()
    if bottle.request.method == 'GET':
        return config_handler.data
    data = bottle.request.params.get('json', {})
    if isinstance(data, str):
        data = json.loads(data)
    config_handler.data.update(data)


def update_handler():
    if 'raw' in bottle.request.params:
        return 'Raw updation HTML'
    return bottle.redirect('/updation')  # vue.js router


def static_assets(filename, root, fileman=False, redirect=False):
    '''
    1. auto append ".gz" if resolving file
    2. auto detect "index.html" if resolving directory
    3. fallback to file manager if no "index.html" under dir
    '''
    target = op.join(root, filename.strip('/\\'))
    index = op.join(target, 'index.html')
    if op.isfile(target):
        return bottle.static_file(relpath(target, root), root)
    if op.isfile(target + '.gz'):
        return bottle.static_file(relpath(target + '.gz', root), root)
    if not op.exists(target):
        return bottle.HTTPError(404, 'File not found')
    if not op.isdir(target):
        return bottle.HTTPError(404, 'Path not found')
    if (
        bottle.request.query.get('auto', True)
        and any([index, index + '.gz'].map(op.isfile))
    ):
        if redirect:
            fn = op.join(bottle.request.path, filename, 'index.html')
            return bottle.redirect(fn)
        if not op.isfile(index):
            index += '.gz'
        return bottle.static_file(relpath(index, root), root)
    if not hasattr(static_assets, 'tpl'):
        p1 = op.abspath(op.join(root, '**', 'fileman*', 'index*.html*'))
        p2 = op.abspath(op.join(root, '**', 'fileman*.html*'))
        tpls = (glob.glob(p1, recursive=True) + glob.glob(p2, recursive=True))
        if tpls:
            print('Using template %s' % relpath(tpls[0], strip=True))
            with open(tpls[0], 'rb') as f:
                bstr = f.read()
            if tpls[0].endswith('.gz'):
                bstr = gzip.decompress(bstr)
            static_assets.tpl = bstr.decode('utf-8')
        else:
            static_assets.tpl = None
    if not fileman or not static_assets.tpl:
        return bottle.HTTPError(404, 'Could not serve directory')
    return static_assets.tpl.replace('%ROOT%', bottle.request.path)


def test_apis(apis):
    configs = config_load()
    auth = (configs.get('web.http.name'), configs.get('web.http.pass'))
    ulen, mlen = [max(map(len, i)) for i in zip(*apis)]
    for url, method in apis:
        print(method.rjust(mlen), url.ljust(ulen), end=' ')
        try:
            kwargs = {'timeout': 1, 'allow_redirects': False}
            resp = requests.request(method, url, **kwargs)
            if resp.status_code in [301, 303, 307, 401]:
                kwargs['allow_redirects'] = True
                kwargs['auth'] = auth
                print(resp.status_code, end=' - ')
                resp = requests.request(method, url, **kwargs)
            print(resp.status_code, resp.reason)
        except Exception:
            print('408 - TIMEOUT')


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
        return client, addr


def find_esphost(args):
    try:
        assert args.esphost  # TODO: UDP scan esphost
        url = 'http://%s:%d/api/alive' % (args.esphost, args.port)
        assert requests.get(url, timeout=1).ok
        return url[:-10]
    except Exception:
        pass


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
        url = urljoin('http://', args.esphost, '/api/alive')
        assert requests.get(url, timeout=1).ok
        args.esphost = url[:-10]
    except Exception:
        try:
            args.esphost = 'http://' + searchesp(argparse.Namespace(
                service='id', all=False, oneshot=True, timeout=1, quiet=True
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

    def redirect_esp32():
        target = args.esphost + bottle.request.path
        print('Redirect to %s %s' % (target, dict(bottle.request.params)))
        return bottle.redirect(target)

    api = bottle.Bottle()
    api.route('/alive', 'GET', redirect_esp32 if args.esphost else lambda: '')
    api.route('/exec', 'POST', redirect_esp32 if args.esphost else lambda: '')
    api.route('/edit', 'GET', lambda: edit_get(args.root))
    api.route('/editu', 'POST', edit_upload)
    api.route('/editc', ['GET', 'POST', 'PUT'], edit_create)
    api.route('/editd', ['GET', 'POST', 'DELETE'], edit_delete)
    api.route('/config', ['GET', 'POST'], config_handler)
    api.route('/update', 'GET', update_handler)
    api.route('/update', 'POST', edit_upload)
    api.route('/apmode', 'GET', lambda: 'AP interface only')

    if args.test:
        return test_apis([
            (args.esphost + '/api' + r.rule, r.method) for r in api.routes
        ])

    app = bottle.Bottle()
    if op.exists(args.certfile):
        app.ssl_opt = {'server_side': True, 'certfile': args.certfile}
    if not args.quiet:
        print('WebServer running at `%s`' % relpath(args.root, strip=True))
    if not args.static:
        if not args.quiet:
            if args.esphost:
                print('Redirect requests to alive ESP32 at', args.esphost)
            else:
                print('Simulate ESP32 APIs: cmd/edit/config/update etc.')
        app.mount('/api', api)
    app.route('/', 'GET', lambda: bottle.redirect('index.html'))
    app.route('/auth', ['GET', 'POST'], bottle.auth_basic(check)(lambda: ''))
    app.route('/<fn:path>', 'GET', lambda fn: static_assets(fn, args.root))
    bottle.run(app, host=args.host, port=args.port, quiet=args.quiet,
               debug=True, server='wsgiref', server_class=SSLServer)


def make_parser():
    parser = argparse.ArgumentParser(epilog='see <command> -h for more')
    parser.set_defaults(func=lambda args: parser.print_help())
    parser.add_argument(
        '-q', '--quiet', action='store_true', help='be slient on CLI')
    subparsers = parser.add_subparsers(
        prog='', title='Supported Commands are', metavar='')

    sparser = subparsers.add_parser(
        'serve', help='Python implemented WebServer to debug (see server.h)')
    sparser.add_argument(
        '-H', '--host', default='0.0.0.0', help='host address to listen on')
    sparser.add_argument(
        '-P', '--port', type=int, default=PORT, help='default %d' % PORT)
    sparser.add_argument(
        '--certfile', default=__certpem__, help='cert file for HTTPS server')
    sparser.add_argument(
        '--esphost', type=str, help='IP address of ESP chip')
    sparser.add_argument(
        '--test', action='store_true', help='run API compatible test')
    sparser.add_argument(
        '--static', action='count', help='disable ESP32 API, only statics')
    sparser.add_argument(
        'root', nargs='?', default=__distdir__, help='path to static files')
    sparser.set_defaults(func=webserver)

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
    sparser.set_defaults(func=searchesp)

    sparser = subparsers.add_parser(
        'genid', help='Generate unique ID with NVS flash template')
    sparser.add_argument(
        '--pack', action='store_true', help='package into nvs binary file')
    sparser.add_argument(
        '--flash', metavar='COM', help='flash NVS binary to specified port')
    sparser.add_argument(
        '-l', '--len', type=int, default=6, help='length of UID')
    sparser.add_argument(
        '-o', '--out', default=sys.stdout, help='write to file if specified')
    sparser.add_argument(
        '-t', '--tpl', default=__nvsfile__, help='render nvs from template')
    sparser.set_defaults(func=genid)

    sparser = subparsers.add_parser(
        'genfont', help='Scan .c files and tree-shake fonts for LVGL')
    sparser.add_argument('font', help='input font file (TTF/WOFF)')
    sparser.add_argument('--bin', action='store_true', help='output binary')
    sparser.add_argument('--bpp', type=int, default=1, help='bits per pixel')
    sparser.add_argument(
        '-s', '--size', type=int, default=12, help='font size in pixels')
    sparser.add_argument('-o', '--out', help='output font path')
    sparser.set_defaults(func=genfont)

    return parser


def main(args=sys.argv[1:]):
    args = make_parser().parse_args(args)
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
