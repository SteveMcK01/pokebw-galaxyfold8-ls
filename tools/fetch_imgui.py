#!/usr/bin/env python3
"""
tools/fetch_imgui.py -- Dear ImGui for the Android overlay, from the official
release, into a cache outside the repository.

The last line the script prints is the source folder, for
EIDOS_HOST_IMGUI_ROOT.

usage: fetch_imgui.py [--cache DIR]
"""

import hashlib
import os
import sys
import tarfile
import urllib.request

VERSION = '1.93.0'
ARCHIVE = 'imgui-%s.tar.gz' % VERSION
URL = 'https://github.com/ocornut/imgui/archive/refs/tags/v%s.tar.gz' % VERSION
SHA256 = 'f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee'
FOLDER = 'imgui-%s' % VERSION


def cache_dir(argv):
    if '--cache' in argv:
        return argv[argv.index('--cache') + 1]
    env = os.environ.get('EIDOS_IMGUI_CACHE')
    if env:
        return env
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        return os.path.join(base, 'eidos', 'imgui')
    if sys.platform == 'darwin':
        return os.path.expanduser('~/Library/Caches/eidos/imgui')
    base = os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache')
    return os.path.join(base, 'eidos', 'imgui')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    cache = cache_dir(argv)
    downloads = os.path.join(cache, 'downloads')
    os.makedirs(downloads, exist_ok=True)
    archive = os.path.join(downloads, ARCHIVE)
    if not os.path.exists(archive) or sha256(archive) != SHA256:
        print('fetch_imgui: downloading', URL)
        tmp = archive + '.part'
        urllib.request.urlretrieve(URL, tmp)
        got = sha256(tmp)
        if got != SHA256:
            os.remove(tmp)
            sys.exit('fetch_imgui: %s has SHA-256 %s, expected %s' % (ARCHIVE, got, SHA256))
        os.replace(tmp, archive)
    root = os.path.join(cache, FOLDER)
    if not os.path.isdir(root):
        print('fetch_imgui: unpacking', ARCHIVE)
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                if member.name.startswith('/') or '..' in member.name.split('/'):
                    sys.exit('fetch_imgui: unsafe path in the archive: %s' % member.name)
            t.extractall(cache)
    print(root.replace('\\', '/'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
