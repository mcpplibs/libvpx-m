#!/usr/bin/env python3
"""The public surface of libvpx's and libwebm's headers, from the pinned tags.

Shared by tools/gen_exports.py (which writes the modules) and
tools/check_exports.py (which audits them against it). The headers come from
the upstream tag archives, downloaded once into target/upstream and checked
against upstream/documents.json: the same bytes compat.libvpx and
compat.libwebm build.

libvpx (C): the headers upstream installs (libs.mk's INSTALL-LIBS for a VP9
encoder and decoder build) — vpx/vpx_integer.h, vpx_codec.h, vpx_image.h,
vpx_frame_buffer.h, vpx_encoder.h with vpx_ext_ratectrl.h and vpx_tpl.h,
vpx_decoder.h, vp8.h, vp8cx.h, vp8dx.h. The functions are the symbols
upstream exports (vpx/exports_com, exports_enc, exports_dec and vp9's
exports_enc, exports_dec), which the headers declare.

libwebm (C++): the headers of upstream's `webm` library — mkvmuxer/
mkvmuxertypes.h, mkvmuxer.h, mkvmuxerutil.h, mkvwriter.h; mkvparser/
mkvparser.h, mkvreader.h; common/webmids.h, hdr_util.h, file_util.h — by
namespace (mkvmuxer, mkvparser, libwebm).
"""
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import sys
import tarfile
import urllib.request

LIBVPX_HEADERS = ['vpx/vpx_integer.h', 'vpx/vpx_codec.h', 'vpx/vpx_image.h', 'vpx/vpx_frame_buffer.h', 'vpx/vpx_ext_ratectrl.h',
                  'vpx/vpx_tpl.h', 'vpx/vpx_encoder.h', 'vpx/vpx_decoder.h', 'vpx/vp8.h', 'vpx/vp8cx.h', 'vpx/vp8dx.h']
LIBVPX_EXPORTS = ['vpx/exports_com', 'vpx/exports_enc', 'vpx/exports_dec', 'vp9/exports_enc', 'vp9/exports_dec']
LIBWEBM_HEADERS = ['mkvmuxer/mkvmuxertypes.h', 'mkvmuxer/mkvmuxer.h', 'mkvmuxer/mkvmuxerutil.h', 'mkvmuxer/mkvwriter.h',
                   'mkvparser/mkvparser.h', 'mkvparser/mkvreader.h', 'common/webmids.h', 'common/hdr_util.h', 'common/file_util.h']


def fetch(root: Path) -> dict[str, Path]:
    """The two extracted archives, by library."""
    pins = json.loads((root / 'upstream/documents.json').read_text(encoding='utf-8'))
    cache = root / 'target/upstream'
    cache.mkdir(parents=True, exist_ok=True)
    out = {}
    for lib, pin in pins['upstream'].items():
        archive = cache / f'{lib}.tar.gz'
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
            print('+ fetch', pin['url'], flush=True, file=sys.stderr)
            with urllib.request.urlopen(pin['url'], timeout=120) as response:
                archive.write_bytes(response.read())
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != pin['sha256']:
            sys.exit(f'{lib} archive checksum {digest} != {pin["sha256"]}')
        with tarfile.open(archive) as tar:
            top = tar.getmembers()[0].name.split('/')[0]
            if not (cache / top).exists():
                tar.extractall(cache, filter='data')
        out[lib] = cache / top
    return out


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def logical_lines(text: str) -> list[str]:
    return strip_comments(text).replace('\\\n', ' ').split('\n')


def preprocess(text: str) -> tuple[str, list[str]]:
    """The C text without preprocessor lines and without `#ifdef __cplusplus`
    blocks (as a C compiler reads it), and the macros it defines."""
    out, macros, stack = [], [], []
    for line in logical_lines(text):
        s = line.strip()
        if s.startswith('#'):
            d = re.match(r'#\s*(\w+)\s*(.*)', s)
            word, rest = (d.group(1), d.group(2)) if d else ('', '')
            if word in ('if', 'ifdef', 'ifndef'):
                stack.append(word == 'ifdef' and rest.strip() == '__cplusplus' or word == 'if' and re.fullmatch(r'defined\s*\(?\s*__cplusplus\s*\)?', rest.strip()) is not None)
            elif word in ('else', 'elif') and stack:
                stack[-1] = False if stack[-1] else stack[-1]
            elif word == 'endif' and stack:
                stack.pop()
            elif word == 'define' and not any(stack):
                d2 = re.match(r'(\w+)(\()?', rest)
                macros.append(d2.group(1) + ('()' if d2.group(2) else ''))
            continue
        if any(stack):
            continue
        out.append(line)
    return '\n'.join(out), macros


def top_level(text: str) -> list[str]:
    """Top-level statements: text up to a `;` at brace depth 0 (a struct's body
    included), and macro invocations standing alone (`VPX_CTRL_USE_TYPE(…)`)."""
    out, cur, depth = [], [], 0
    i = 0
    while i < len(text):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        cur.append(c)
        if depth == 0 and c == ';':
            out.append(''.join(cur).strip())
            cur = []
        elif depth == 0 and c == ')' and re.match(r'\s*VPX_CTRL_\w+\s*\(', ''.join(cur)):
            out.append(''.join(cur).strip())
            cur = []
        i += 1
    return [s for s in out if s]


@dataclass
class Control:
    id: str
    type: str | None   # None: VPX_CTRL_VOID
    deprecated: bool


@dataclass
class CSurface:
    types: list[str] = field(default_factory=list)          # typedef names and struct/enum tags
    enumerators: list[str] = field(default_factory=list)
    functions: list[str] = field(default_factory=list)      # declared, with external linkage
    data: list[str] = field(default_factory=list)           # extern variables
    controls: list[Control] = field(default_factory=list)   # VPX_CTRL_USE_TYPE / _DEPRECATED / VPX_CTRL_VOID
    macros: dict[str, str] = field(default_factory=dict)    # name -> header
    function_like: set[str] = field(default_factory=set)    # macros that take arguments


def c_surface(top: Path) -> CSurface:
    s = CSurface()
    for h in LIBVPX_HEADERS:
        text, macros = preprocess((top / h).read_text(encoding='utf-8'))
        for m in macros:
            if m.endswith('()'):
                m = m[:-2]
                s.function_like.add(m)
            s.macros.setdefault(m, h)
        for st in top_level(text):
            st = re.sub(r'^extern\s+"C"\s*\{', '', st).strip()
            m = re.match(r'VPX_CTRL_(USE_TYPE|USE_TYPE_DEPRECATED|VOID)\s*\(\s*(\w+)\s*(?:,\s*(.+?))?\s*\)$', st, flags=re.S)
            if m:
                s.controls.append(Control(m.group(2), m.group(3) and ' '.join(m.group(3).split()), m.group(1) == 'USE_TYPE_DEPRECATED'))
                continue
            # Only what is at the statement's outermost level: in C++ a struct or
            # enum defined inside another struct is that struct's member.
            outer = re.match(r'(?:typedef\s+)?(struct|enum)\s*(\w*)\s*\{', st)
            if outer:
                if outer.group(2):
                    s.types.append(outer.group(2))
                if outer.group(1) == 'enum':
                    body = st[outer.end():st.rindex('}')]
                    for e in body.split(','):
                        e = e.strip()
                        if e:
                            s.enumerators.append(re.match(r'(\w+)', e).group(1))
            flat = ' '.join(re.sub(r'\{.*\}', ' { } ', st, flags=re.S).split())
            if flat.startswith('typedef'):
                fp = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', flat)
                name = fp.group(1) if fp else re.search(r'(\w+)\s*(?:\[[^\]]*\])?\s*;$', flat).group(1)
                s.types.append(name)
            elif flat.startswith('extern ') and '(' not in flat:
                s.data.append(re.search(r'(\w+)\s*;$', flat).group(1))
            elif '(' in flat and not flat.startswith(('static', 'struct', 'enum')) and '{' not in flat:
                fn = re.search(r'(\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:\w+\s*(?:\([^()]*\))?\s*)*;$', flat)
                if fn:
                    s.functions.append(fn.group(1))
    s.types = sorted(set(s.types))
    s.enumerators = sorted(set(s.enumerators))
    s.functions = sorted(set(s.functions))
    s.data = sorted(set(s.data))
    return s


def exported_symbols(top: Path) -> tuple[list[str], list[str]]:
    """What upstream's export lists name: functions (text) and data."""
    text, data = [], []
    for f in LIBVPX_EXPORTS:
        for line in (top / f).read_text(encoding='utf-8').splitlines():
            kind, _, name = line.partition(' ')
            (data if kind == 'data' else text).append(name.strip())
    return sorted(text), sorted(data)


# ── libwebm (C++) ────────────────────────────────────────────────────────


@dataclass
class Namespace:
    classes: list[str] = field(default_factory=list)      # classes, structs, typedefs, enums
    enumerators: list[str] = field(default_factory=list)  # of unscoped namespace-level enums
    functions: list[str] = field(default_factory=list)    # free functions (and templates)
    constants: dict[str, str] = field(default_factory=dict)  # name -> type (namespace-scope const: internal linkage)


@dataclass
class CppSurface:
    namespaces: dict[str, Namespace] = field(default_factory=dict)
    macros: dict[str, str] = field(default_factory=dict)


def cpp_surface(top: Path) -> CppSurface:
    s = CppSurface()
    for h in LIBWEBM_HEADERS:
        lines = logical_lines((top / h).read_text(encoding='utf-8'))
        body = []
        for line in lines:
            st = line.strip()
            if st.startswith('#'):
                d = re.match(r'#\s*define\s+(\w+)', st)
                if d:
                    s.macros.setdefault(d.group(1), h)
                continue
            body.append(line)
        text = '\n'.join(body)
        # Namespace blocks at file level (libwebm does not nest them).
        for m in re.finditer(r'\bnamespace\s+(\w+)\s*\{', text):
            name = m.group(1)
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                i += 1
            ns = s.namespaces.setdefault(name, Namespace())
            for st in top_level(text[m.end():i - 1]):
                flat = ' '.join(re.sub(r'\{.*\}', ' { } ', st, flags=re.S).split())
                flat = re.sub(r'^template\s*<[^>]*>\s*', '', flat)
                if re.match(r'(class|struct)\s+\w+\s*;$', flat):
                    continue  # a forward declaration
                c = re.match(r'(?:class|struct)\s+(\w+)\b', flat)
                if c and '{' in flat:
                    ns.classes.append(c.group(1))
                    continue
                e = re.match(r'enum\s+(?:class\s+)?(\w+)\s*(?::\s*[\w:]+\s*)?\{', flat)
                if e:
                    ns.classes.append(e.group(1))
                    if not flat.startswith('enum class'):
                        inner = re.search(r'\benum\b[^{]*\{(.*)\}', st, flags=re.S).group(1)
                        for item in inner.split(','):
                            item = item.strip()
                            if item:
                                ns.enumerators.append(re.match(r'(\w+)', item).group(1))
                    continue
                if flat.startswith('typedef') or flat.startswith('using '):
                    t = re.search(r'(\w+)\s*;$', flat) if flat.startswith('typedef') else re.match(r'using\s+(\w+)\s*=', flat)
                    ns.classes.append(t.group(1))
                    continue
                k = re.match(r'(?:static\s+)?const(?:expr)?\s+([\w:]+)\s+(\w+)\s*=', flat)
                if k:
                    ns.constants[k.group(2)] = k.group(1)
                    continue
                if '(' in flat:
                    f = re.search(r'([\w~]+)\s*\(', re.sub(r'<[^<>]*>', '', flat))
                    if f and not re.search(r'\b\w+::\w+\s*\(', flat):  # not an out-of-class member definition
                        ns.functions.append(f.group(1))
    for ns in s.namespaces.values():
        ns.classes = sorted(set(ns.classes))
        ns.enumerators = sorted(set(ns.enumerators))
        ns.functions = sorted(set(ns.functions))
    return s


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    tops = fetch(root)
    c = c_surface(tops['libvpx'])
    text, data = exported_symbols(tops['libvpx'])
    print(f'libvpx: {len(c.types)} types, {len(c.enumerators)} enumerators, {len(c.functions)} functions, {len(c.data)} data, '
          f'{len(c.controls)} controls, {len(c.macros)} macros; exported: {len(text)} functions, {len(data)} data')
    print('  declared, not exported:', sorted(set(c.functions) - set(text)))
    print('  exported, not declared:', sorted(set(text) - set(c.functions)))
    print('  data declared:', c.data)
    w = cpp_surface(tops['libwebm'])
    for name, ns in w.namespaces.items():
        print(f'libwebm {name}: {len(ns.classes)} types, {len(ns.enumerators)} enumerators, {len(ns.functions)} functions, constants {ns.constants}')
    print('  macros:', sorted(w.macros))
