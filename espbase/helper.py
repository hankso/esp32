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
import sys
import glob
import gzip
import json
import time
import socket
import argparse
import tempfile
import posixpath
import os.path as op
from io import StringIO

# requirements.txt: bottle
try:
    import bottle
except Exception:
    bottle = None

# these are default values
__basedir__ = op.dirname(op.abspath(__file__))
__distdir__ = op.join(__basedir__, 'webpage', 'dist')
__cmkfile__ = op.join(__basedir__, 'CMakeLists.txt')
__partcsv__ = op.join(__basedir__, 'partitions.csv')
__nvsfile__ = op.join(__basedir__, 'nvs_flash.csv')
__nvsdist__ = op.join(__basedir__, 'build', 'nvs.bin')


def _random_id(len=8):
    #  from random import choice
    #  from string import hexdigits
    #  return ''.join([choice(hexdigits) for i in range(len)])
    from uuid import uuid4
    return uuid4().hex[:len].upper()


def _project_name():
    try:
        with open(__cmkfile__) as f:
            content = f.read()
        return re.findall(r'project\((\w+)\)', content)[0]
    except Exception:
        return 'testing'


def _firmware_url(filename='app.bin', port=8080):
    host = socket.gethostbyname(socket.gethostname())
    return posixpath.join('http://%s:%d' % (host, port), filename)


def _process_nvs(args):
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
    except Exception:
        pass
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
        return print('Generate NVS partition binary failed: %s' % e)
    finally:
        if op.exists(args.out):
            os.remove(args.out)
        sys.argv = argv_backup


def genid(args):
    if op.exists(args.tpl):
        with open(args.tpl, 'r') as f:
            prefix = _project_name()
            data = f.read().replace('{NAME}', prefix)
            data = data.replace('{UID}', _random_id(args.len))
            data = data.replace('{URL}', _firmware_url(prefix + '.bin'))
            data = data.replace('\n\n', '\n')   # strip null lines
            data = data.replace(' ', '')        # strip white spaces
    else:
        data = _random_id(args.len)
    if args.pack and 'IDF_PATH' in os.environ:
        args.out = tempfile.mktemp()
    if hasattr(args.out, 'name'):
        filename = args.out.name
    else:
        filename = str(args.out).strip('<>')
    print('Writing NVS information to `%s`' % filename)
    if hasattr(args.out, 'write'):
        args.out.write(data)
    else:
        with open(args.out, 'w') as f:
            f.write(data)
    if args.pack:
        _process_nvs(args)


def _relpath(p, ref='.', strip=False):
    p = op.relpath(p, ref)
    if not strip:
        return p
    if (len(p.split(op.sep)) > 3 and len(p) > 30) or len(p) > 80:
        m = re.findall(r'\w+%s(.+)%s\w+' % tuple([op.sep] * 2), p)
        p = p.replace((m or [''])[0], '...')
    return p


def _absjoin(*a): return op.abspath(op.join(*a))


def edit_get(root):
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
    else:
        return bottle.redirect('/ap/editor.html')


def edit_create():
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def edit_delete():
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))


def edit_upload():
    print('method', bottle.request.method)
    print('header', dict(bottle.request.headers))
    print('params', dict(bottle.request.params))
    for idx, file in enumerate(bottle.request.files.values()):
        print('file', idx, vars(file), dict(file.headers))
    time.sleep(1)


def config_init():
    configs = []
    try:
        args = make_parser().parse_args(['genid'])
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
    except Exception:
        pass
    return dict(configs)


def config():
    print('method', bottle.request.method)
    print('params', dict(bottle.request.params))
    if not hasattr(config, 'data'):
        config.data = config_init()
    if bottle.request.method == 'GET':
        return config.data
    try:
        data = bottle.request.params.get('json', {})
        if isinstance(data, str):
            data = json.loads(data)
        config.data.update(data)
    except Exception:
        pass


def static_factory(filename, root, auto=True, fileman=False, redirect=False):
    '''
    1. auto append ".gz" if resolving file
    2. auto detect "index.html" if resolving directory
    3. fallback to file manager if no "index.html" under dir
    '''
    target = op.join(root, filename.strip('/\\'))
    index = op.join(target, 'index.html')
    if op.isfile(target):
        return bottle.static_file(_relpath(target, root), root)
    if op.isfile(target + '.gz'):
        return bottle.static_file(_relpath(target + '.gz', root), root)
    if not op.exists(target):
        return bottle.HTTPError(404, 'File not found')
    if not op.isdir(target):
        return bottle.HTTPError(404, 'Path not found')
    if auto and any([index, index + '.gz'].map(op.isfile)):
        if redirect:
            fn = op.join(bottle.request.path, filename, 'index.html')
            return bottle.redirect(fn)
        if not op.isfile(index):
            index += '.gz'
        return bottle.static_file(_relpath(index, root), root)
    if not hasattr(static_factory, 'tpl'):
        p1 = _absjoin(root, '**', 'fileman*', 'index*.html*')
        p2 = _absjoin(root, '**', 'fileman*.html*')
        tpls = (glob.glob(p1, recursive=True) + glob.glob(p2, recursive=True))
        if tpls:
            print('Using template %s' % _relpath(tpls[0], strip=True))
            with open(tpls[0], 'rb') as f:
                bstr = f.read()
            if tpls[0].endswith('.gz'):
                bstr = gzip.decompress(bstr)
            static_factory.tpl = bstr.decode('utf-8')
        else:
            static_factory.tpl = None
    if not fileman or not static_factory.tpl:
        return bottle.HTTPError(404, 'Cannot serve directory')
    return static_factory.tpl.replace('%ROOT%', bottle.request.path)


def webserver(args):
    '''
    bottle BaseRequest arguments outline:

        [CGI FieldStorage] -> bottle.request.POST
                             /          |
                           |/           V
        bottle.request.files  bottle.request.forms
                                        |
                                        +---------> bottle.request.params
                                        |
        [URL Query String] -> bottle.request.query
    '''
    args.root = op.abspath(args.root)
    if not (op.exists(args.root)):
        return print('Cannot serve at `%s`: dirctory not found' % args.root)
    if not args.quiet:
        print('WebServer running at `%s`' % _relpath(args.root, strip=True))

    def redirect_esp32():
        target = 'http://' + args.esphost + bottle.request.path
        print('Redirect to %s %s' % (target, dict(bottle.request.params)))
        return bottle.redirect(target, 308)

    def static_assets(filename='/'):
        return static_factory(
            filename, args.root, bottle.request.query.get('auto', True))

    app = bottle.Bottle()
    if not args.static:
        if not args.quiet:
            print('Will simulate ESP32 APIs: edit/config/assets etc.')
        app.route('/cmd', 'POST', redirect_esp32)
        app.route('/edit', 'GET', lambda: edit_get(args.root))
        app.route('/editu', 'POST', edit_upload)
        app.route('/editc', ['GET', 'POST', 'PUT'], edit_create)
        app.route('/editd', ['GET', 'POST', 'DELETE'], edit_delete)
        app.route('/update', 'POST', edit_upload)
        app.route('/config', ['GET', 'POST'], config)
        app.route('/apmode', 'GET', lambda: None)
    app.route('/', 'GET', lambda: bottle.redirect('index.html'))
    app.route('/<filename:path>', 'GET', static_assets)
    bottle.run(
        app, reload=True, quiet=args.quiet, host=args.host, port=args.port
    )


def make_parser():
    parser = argparse.ArgumentParser(epilog='see <command> -h for more')
    parser.set_defaults(func=lambda args: parser.print_help())
    subparsers = parser.add_subparsers(
        prog='', title='Supported Commands are', metavar='')

    sparser = subparsers.add_parser(
        'serve', help='Python implemented WebServer to debug (bottle needed)')
    sparser.add_argument(
        '-H', '--host', default='0.0.0.0', help='Host address to listen on')
    sparser.add_argument(
        '-P', '--port', type=int, default=8080, help='default port 8080')
    sparser.add_argument(
        '-q', '--quiet', action='store_true', help='be slient on CLI')
    sparser.add_argument(
        '--esphost', default='10.0.0.112', help='IP address of ESP chip')
    sparser.add_argument(
        '--static', action='count', help='disable ESP32 API, only statics')
    sparser.add_argument(
        'root', nargs='?', default=__distdir__, help='path to static files')
    sparser.set_defaults(func=webserver)

    sparser = subparsers.add_parser(
        'genid', help='Generate unique ID in NVS flash for each firmware')
    sparser.add_argument(
        '--pack', action='store_true', help='package into nvs binary file')
    sparser.add_argument(
        '--flash', metavar='COM', help='Flash NVS binary to specified port')
    sparser.add_argument(
        '-l', '--len', type=int, default=6, help='length of UID')
    sparser.add_argument(
        '-o', '--out', default=sys.stdout, help='write to file if specified')
    sparser.add_argument(
        '-f', '--tpl', default=__nvsfile__, help='render nvs from template')
    sparser.set_defaults(func=genid)

    return parser


def main(args=sys.argv[1:]):
    args = make_parser().parse_args(args)
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
