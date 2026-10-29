#!/usr/bin/env python3
"""Compare examples/vpxwebm, written against import mcpplibs.mwrap.libvpx,
with upstream's own vpxenc and vpxdec built from the same tag.

The tag archives are downloaded once into target/upstream and checked against
upstream/documents.json. Upstream's vpxenc and vpxdec are built by its own
configure and makefiles (generic target, with the system C and C++
compilers; vpxdec reads WebM through its own copy of libwebm in
third_party/); vpxwebm is built with mcpp over compat.libvpx and
compat.libwebm. Then, for a clip generated here (a moving gradient and a
square, 4:2:0):

- for each setting (good quality at constant quality and at a bit rate, with
  and without look-ahead, lossless, real time at a bit rate, four threads with
  tile columns), the two encoders must write the same IVF file, byte for byte
  (vpxenc with --passes=1: vpxwebm encodes in one pass);
- vpxdec must read the WebM file vpxwebm writes (mkvmuxer's), and the two
  decoders must give the same frames from it and from vpxenc's WebM (vpxwebm
  reading it with mkvparser), as raw I420.

Needs `make` and C and C++ compilers: CI runs it on Linux and macOS.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inventory import fetch  # noqa: E402

SETTINGS = [
    ['--good', '--cpu-used', '4', '--end-usage', 'q', '--cq-level', '18', '--lag-in-frames', '0', '--threads', '1'],
    ['--good', '--cpu-used', '2', '--end-usage', 'vbr', '--target-bitrate', '300', '--lag-in-frames', '0', '--threads', '1'],
    ['--good', '--cpu-used', '3', '--end-usage', 'q', '--cq-level', '24', '--threads', '1'],
    ['--good', '--cpu-used', '4', '--lossless', '--lag-in-frames', '0', '--threads', '1'],
    ['--rt', '--cpu-used', '8', '--end-usage', 'cbr', '--target-bitrate', '400', '--lag-in-frames', '0', '--threads', '1'],
    ['--good', '--cpu-used', '1', '--end-usage', 'q', '--cq-level', '30', '--lag-in-frames', '0', '--threads', '4', '--tile-columns', '2'],
]


def vpxenc_args(setting: list[str]) -> list[str]:
    out, i = [], 0
    while i < len(setting):
        s = setting[i]
        if s in ('--good', '--rt'):
            out.append(s)
        elif s == '--lossless':
            out.append('--lossless=1')
        else:
            out.append(f'{s}={setting[i + 1]}')
            i += 1
        i += 1
    return out


def upstream_tools(upstream: Path, cc: str, cxx: str) -> tuple[Path, Path]:
    build = upstream.parent / (upstream.name + '-build')
    if not (build / 'vpxenc').exists() or not (build / 'vpxdec').exists():
        shutil.rmtree(build, ignore_errors=True)
        build.mkdir(parents=True)
        env = dict(os.environ, CC=cc, CXX=cxx, LD=cxx)
        command = [str(upstream / 'configure'), '--target=generic-gnu', '--disable-vp8', '--enable-vp9', '--disable-unit-tests', '--disable-docs']
        print('+', ' '.join(command), flush=True)
        subprocess.run(command, cwd=build, check=True, env=env, stdout=subprocess.DEVNULL)
        command = ['make', '-s', f'-j{os.cpu_count() or 2}']
        print('+', ' '.join(command), flush=True)
        subprocess.run(command, cwd=build, check=True, env=env, stdout=subprocess.DEVNULL)
    return build / 'vpxenc', build / 'vpxdec'


def vpxwebm(example: Path, mcpp: str, toolchain: str | None, profile: str) -> Path:
    command = [mcpp, 'build', '--profile', profile] + (['--toolchain', toolchain] if toolchain else [])
    print('+', ' '.join(command), flush=True)
    subprocess.run(command, cwd=example, check=True)
    found = sorted((p for p in (example / 'target').rglob('vpxwebm*') if p.parent.name == 'bin' and p.is_file() and p.suffix in ('', '.exe')),
                   key=lambda p: p.stat().st_mtime)
    if not found:
        sys.exit('examples/vpxwebm was not built')
    return found[-1]


def clip(path: Path, w: int = 176, h: int = 144, n: int = 40) -> None:
    out = bytearray(f'YUV4MPEG2 W{w} H{h} F30:1 Ip A1:1 C420jpeg\n'.encode())
    for t in range(n):
        out += b'FRAME\n'
        for y in range(h):
            out += bytes(235 if 30 + t * 3 <= x < 70 + t * 3 and 40 <= y < 90 else (x * 2 + y + t * 5) & 255 for x in range(w))
        for y in range(h // 2):
            out += bytes(128 + ((x + t) & 31) - 16 for x in range(w // 2))
        for y in range(h // 2):
            out += bytes(128 - ((y + t) & 31) + 16 for x in range(w // 2))
    path.write_bytes(out)


def run(command: list) -> None:
    result = subprocess.run([str(c) for c in command], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f'{" ".join(map(str, command))} failed:\n{result.stdout}{result.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcpp', default='mcpp')
    parser.add_argument('--toolchain')
    parser.add_argument('--profile', default='release')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    upstream = fetch(root)['libvpx']
    vpxenc, vpxdec = upstream_tools(upstream, args.cc, args.cxx)
    ours = vpxwebm(root / 'examples/vpxwebm', args.mcpp, args.toolchain, args.profile)
    work = root / 'target/conformance'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = work / 'clip.y4m'
    clip(source)
    failures, checks = [], 0

    def compare(a: Path, b: Path, what: str) -> None:
        nonlocal checks
        checks += 1
        if a.read_bytes() != b.read_bytes():
            failures.append(what)
            print('DIFFERENT', what, flush=True)

    for k, setting in enumerate(SETTINGS):
        label = ' '.join(setting)
        theirs, mine = work / f'{k}.vpxenc.ivf', work / f'{k}.ours.ivf'
        run([vpxenc, '--codec=vp9', '--passes=1', *vpxenc_args(setting), '--ivf', '-q', '-o', theirs, source])
        run([ours, 'encode', source, mine, *setting])
        compare(theirs, mine, f'encode {label}')
        print(f'{label}: {theirs.stat().st_size} bytes', flush=True)
        # The WebM files: ours read by vpxdec, vpxenc's read by ours, the same frames out.
        ours_webm, their_webm = work / f'{k}.ours.webm', work / f'{k}.vpxenc.webm'
        run([ours, 'encode', source, ours_webm, *setting])
        run([vpxenc, '--codec=vp9', '--passes=1', *vpxenc_args(setting), '-q', '-o', their_webm, source])
        for webm, who in ((ours_webm, 'vpxwebm'), (their_webm, 'vpxenc')):
            a, b = work / f'{k}.{who}.vpxdec.yuv', work / f'{k}.{who}.ours.yuv'
            run([vpxdec, '--i420', '--rawvideo', '-o', a, webm])
            run([ours, 'decode', webm, b])
            compare(a, b, f'decode {who}\'s WebM, {label}')
    if failures:
        sys.exit(f'{len(failures)} of {checks} comparisons differ from upstream vpxenc/vpxdec')
    print(f'{checks} comparisons: every IVF file byte-identical to upstream vpxenc 1.17.0, every WebM read by both '
          f'(vpxdec and mkvparser) to the same frames ({args.cc})')


if __name__ == '__main__':
    main()
