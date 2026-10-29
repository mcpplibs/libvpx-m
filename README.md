# libvpx

> libvpx, the VP9 video codec, and libwebm, the WebM container, through C++23 modules.

**English** · [简体中文](README.zh.md) · [Upstream](https://chromium.googlesource.com/webm/libvpx) · [libwebm](https://github.com/webmproject/libwebm) · [Releases](https://github.com/mcpplibs/libvpx-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![libvpx](https://img.shields.io/badge/libvpx-BSD--3--Clause-blue.svg)](upstream/libvpx/LICENSE)
[![libwebm](https://img.shields.io/badge/libwebm-BSD--3--Clause-blue.svg)](upstream/libwebm/LICENSE.TXT)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/libvpx-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/libvpx-m/actions/workflows/ci.yml)

`mcpplibs.mwrap.libvpx` 1.17.0 encodes and decodes VP9, the royalty-free
video of the Web, and writes and reads WebM, the container every browser
plays. The upstream APIs are unchanged: `vpx_codec_enc_config_default`,
`vpx_codec_encode`, `vpx_codec_get_cx_data`, `vpx_codec_decode`,
`vpx_codec_get_frame`, `vpx_img_alloc` … are libvpx's globals, and
`mkvmuxer::Segment`, `mkvparser::Segment`, `mkvmuxer::IMkvWriter`,
`mkvparser::IMkvReader` … are libwebm's classes in their own namespaces. The
libraries are `compat.libvpx` 1.17.0 and `compat.libwebm` 1.0.0.32. The
headers' typed controls (`vpx_codec_control_VP8E_SET_CPUUSED` …) are adapted
under the same names, and the macros are constants and functions. On every
CI run the files written are byte-identical to upstream's own `vpxenc`, and
`vpxdec` reads them to the same frames.

## Modules

- `import mcpplibs.mwrap.libvpx;`: the encoder and decoder (`vpx/vpx_encoder.h`, `vpx/vpx_decoder.h`, `vpx/vp8cx.h`, `vpx/vp8dx.h` …), re-exporting the two below
- `import mcpplibs.mwrap.libvpx.webm;`: libwebm's `mkvmuxer`, `mkvparser` and `libwebm` namespaces
- `import mcpplibs.mwrap.libvpx.macros;`: `VPX_DL_REALTIME`, `VPX_FRAME_IS_KEY`, `VPX_CODEC_CAP_ENCODER`, `vpx_codec_enc_init` … in `mcpplibs::mwrap::libvpx::macros`

> More Modules API -> [Usage and API](docs/usage.md)

## Getting started

```sh
mcpp new clip --template mcpplibs.mwrap.libvpx@1.17.0
cd clip
mcpp run -- clip.webm
```

The default template encodes two seconds of moving pictures, muxes them into
a WebM in memory, parses it back, decodes every frame and prints the file's
size and the worst PSNR; given a file name it also writes the WebM.

```toml
[dependencies]
"mcpplibs.mwrap.libvpx" = "1.17.0"
```

```cpp
import std;
import mcpplibs.mwrap.libvpx;

namespace m = mcpplibs::mwrap::libvpx::macros;

int main() {
    vpx_codec_enc_cfg_t cfg;
    vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0);
    cfg.g_w = 320;
    cfg.g_h = 180;
    cfg.g_timebase = {1, 30};
    vpx_codec_ctx_t encoder;
    m::vpx_codec_enc_init(&encoder, vpx_codec_vp9_cx(), &cfg, 0);
    mcpplibs::mwrap::libvpx::vpx_codec_control_VP8E_SET_CPUUSED(&encoder, VP8E_SET_CPUUSED, 6);
    vpx_image_t img;
    vpx_img_alloc(&img, VPX_IMG_FMT_I420, 320, 180, 16);
    // … fill img.planes, then vpx_codec_encode(&encoder, &img, pts, 1, 0, m::VPX_DL_GOOD_QUALITY)
    vpx_img_free(&img);
    vpx_codec_destroy(&encoder);
    std::println("{}", vpx_codec_version_str());
}
```

## The libraries

`compat.libvpx` builds the 101 C files upstream's own `configure
--target=generic-gnu --disable-vp8 --enable-vp9` compiles: libvpx's portable
C, without SIMD, assembler or run-time CPU detection, so one configuration
builds on x86-64 and aarch64, Linux, macOS, Windows and wasm32-emscripten. One
thread (the default) gives the same bytes on every target; more threads tile
the work (on Emscripten without pthreads, in turn on one). VP8 is not built.
`compat.libwebm` builds upstream's `webm` library (mkvmuxer, mkvparser and
the common helpers). mkvmuxer gives a track a random UID unless one is set
(`Track::set_uid`): set it for reproducible files. Details and limits:
[docs/usage.md](docs/usage.md).

## Examples

- [examples/vpxwebm](examples/vpxwebm): encodes Y4M to VP9 in WebM or IVF, decodes WebM or IVF to raw I420 and
  describes a WebM, written against the module; [tools/run_conformance.py](tools/run_conformance.py) compares it with
  upstream's `vpxenc` and `vpxdec`.

## Build and testing

Linux (x86_64 GCC and LLVM, arm64 GCC), macOS, Windows and wasm32-emscripten
(under node) CI build the modules and run the tests in the dev and release
profiles: VP9 at good quality, real time, four threads and lossless, muxed
into WebM, parsed and decoded back, each file's bytes pinned; every macro
mapping against the raw headers; every exported function linked. The native
rows also run the export audit ([tools/check_exports.py](tools/check_exports.py):
the generated modules are fresh, every declaration exported, every macro
mapped or accounted for) and, but on Windows, the conformance run against
upstream's tools. A separate job scaffolds the template as an installed
package and runs it.

## License

[Apache-2.0](LICENSE) for the wrapper, by mcpp-community; libvpx and libwebm
remain BSD-3-Clause with the WebM project's patent grant
([upstream/libvpx/LICENSE](upstream/libvpx/LICENSE),
[upstream/libvpx/PATENTS](upstream/libvpx/PATENTS),
[upstream/libwebm/LICENSE.TXT](upstream/libwebm/LICENSE.TXT),
[upstream/libwebm/PATENTS.TXT](upstream/libwebm/PATENTS.TXT)).
