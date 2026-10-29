#!/usr/bin/env python3
"""Audit the modules against libvpx's and libwebm's public headers (the pinned tags').

- The modules, the controls' unit and the generated tests are what
  tools/gen_exports.py writes.
- Every type, enumerator, exported function and codec interface of libvpx's
  headers is exported by mcpplibs.mwrap.libvpx (VP8's interfaces, declared but
  not built, are listed with their reason), and every typed control adapted.
- Every class, type, enumerator and free function of libwebm's headers is
  exported in its namespace by mcpplibs.mwrap.libvpx.webm, and every
  namespace-scope constant given under mcpplibs::mwrap::libvpx::<namespace>.
- Every macro is mapped in src/macros.cppm, stays in <mwrap/libvpx/macros.hpp>
  with its reason (tools/gen_exports.py's STAY), is a VPX_CTRL_<id> feature
  test, or an include guard.
"""
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_exports import CONSTEXPR, FUNCTIONS, NOT_BUILT, STAY, is_feature_test, is_guard  # noqa: E402
from inventory import c_surface, cpp_surface, exported_symbols, fetch  # noqa: E402


def main():
    root = Path(__file__).resolve().parents[1]
    problems = []
    generated = subprocess.run([sys.executable, str(root / 'tools/gen_exports.py'), '--check'], capture_output=True, text=True)
    if generated.returncode != 0:
        problems.append(generated.stderr.strip() or generated.stdout.strip())
    tops = fetch(root)
    c = c_surface(tops['libvpx'])
    functions, data = exported_symbols(tops['libvpx'])

    text = (root / 'src/libvpx.cppm').read_text(encoding='utf-8')
    exported = set(re.findall(r'export using ::(\w+);', text))
    declared = set(c.types) | set(c.enumerators) | set(c.functions) | set(c.data)
    missing = declared - exported - NOT_BUILT.keys()
    if missing:
        problems.append('src/libvpx.cppm lacks: ' + ', '.join(sorted(missing)))
    built_but_listed = NOT_BUILT.keys() & (set(functions) | set(data))
    if built_but_listed:
        problems.append('listed as not built, yet upstream exports: ' + ', '.join(sorted(built_but_listed)))
    adapted = set(re.findall(r'vpx_codec_control_(\w+)\(', text[text.index('export namespace mcpplibs::mwrap::libvpx {'):]))
    lacking = {k.id for k in c.controls} - adapted
    if lacking:
        problems.append('typed controls not adapted: ' + ', '.join(sorted(lacking)))

    w = cpp_surface(tops['libwebm'])
    webm = (root / 'src/webm.cppm').read_text(encoding='utf-8')
    counts = []
    for name, ns in w.namespaces.items():
        block = re.search(r'export namespace ' + name + r' \{(.*?)\}  // namespace ' + name + r'\n', webm, flags=re.S)
        have = set(re.findall(r'using ::' + name + r'::(\w+);', block.group(1))) if block else set()
        missing = (set(ns.classes) | set(ns.enumerators) | set(ns.functions)) - have
        if missing:
            problems.append(f'src/webm.cppm lacks {name}::' + f', {name}::'.join(sorted(missing)))
        for k in ns.constants:
            if not re.search(rf'export namespace mcpplibs::mwrap::libvpx::{name} \{{[^}}]*inline constexpr auto {k} = ::{name}::{k};', webm):
                problems.append(f'src/webm.cppm does not give the constant {name}::{k}')
        counts.append(f'{name} {len(ns.classes)}/{len(ns.enumerators)}/{len(ns.functions)}/{len(ns.constants)}')

    macros_text = (root / 'src/macros.cppm').read_text(encoding='utf-8')
    body = macros_text[macros_text.index('export namespace mcpplibs::mwrap::libvpx::macros'):]
    mapped = set(re.findall(r'inline constexpr auto (\w+) =', body)) | set(re.findall(r'^(?:constexpr )?[\w:]+ (\w+)\(', body, flags=re.M))
    macros = set(c.macros) | set(w.macros)
    guards = {m for m in macros if is_guard(m)}
    feature_tests = {m for m in macros if is_feature_test(m) and m in c.macros}
    unaccounted = macros - mapped - STAY.keys() - guards - feature_tests
    if unaccounted:
        problems.append('macros neither mapped nor staying: ' + ', '.join(sorted(unaccounted)))
    for table, what in ((STAY.keys(), 'staying'), (FUNCTIONS.keys() | CONSTEXPR.keys(), 'mapped as functions')):
        stale = set(table) - macros
        if stale:
            problems.append(f'{what} but not in the headers: ' + ', '.join(sorted(stale)))
    both = STAY.keys() & mapped
    if both:
        problems.append('listed as staying yet mapped: ' + ', '.join(sorted(both)))
    extra = mapped - macros
    if extra:
        problems.append('mapped but not a macro of the headers: ' + ', '.join(sorted(extra)))
    if problems:
        sys.exit('\n'.join(problems))
    print(f'libvpx: {len(c.types)} types, {len(c.enumerators)} enumerators, {len(functions)} functions, {len(data)} codec interfaces '
          f'exported, {len(NOT_BUILT)} of VP8 not built, {len(c.controls)} typed controls adapted; libwebm (types/enumerators/'
          f'functions/constants): ' + ', '.join(counts) + f'; {len(macros)} macros: {len(macros & mapped)} mapped, '
          f'{len(STAY)} staying, {len(feature_tests)} VPX_CTRL_<id> feature tests, {len(guards)} include guards')


if __name__ == '__main__':
    main()
