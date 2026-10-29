# Using mcpplibs.mwrap.libvpx

`import mcpplibs.mwrap.libvpx;` declares libvpx 1.17.0's API as its headers do, and libwebm 1.0.0.32's classes in their namespaces. Types, enumerators and functions are the upstream entities themselves, so a translation unit may include a header too. The libraries are `compat.libvpx` 1.17.0 and `compat.libwebm` 1.0.0.32, built from the same tags.

## Modules

| Module | What it holds |
|---|---|
| `mcpplibs.mwrap.libvpx` | `vpx/vpx_codec.h`, `vpx_image.h`, `vpx_frame_buffer.h`, `vpx_encoder.h` (with `vpx_ext_ratectrl.h`, `vpx_tpl.h`), `vpx_decoder.h`, `vp8.h`, `vp8cx.h`, `vp8dx.h`: 133 types (`vpx_codec_enc_cfg_t`, `vpx_codec_ctx_t`, `vpx_image_t`, `vpx_codec_cx_pkt_t`, `vpx_rational_t`, `vpx_svc_layer_id_t` …), 192 enumerators (`VPX_CODEC_OK`, `VPX_IMG_FMT_I420`, `VPX_Q`, `VPX_CBR`, `VP8E_SET_CPUUSED`, `VP9E_SET_ROW_MT`, `VPX_CS_BT_709`, `VPX_CR_STUDIO_RANGE` …), 35 functions and the two VP9 interfaces (`vpx_codec_vp9_cx`, `vpx_codec_vp9_dx`); 86 typed controls adapted; re-exports the two below |
| `mcpplibs.mwrap.libvpx.webm` | libwebm's `mkvmuxer` (Segment, VideoTrack, AudioTrack, Frame, Colour, Cues, IMkvWriter, MkvWriter … and the element writers), `mkvparser` (Segment, Tracks, Cluster, Block, BlockEntry, Colour, IMkvReader, MkvReader …) and `libwebm` (the `MkvId` element ids, `CopyColour`, `ParseVpxCodecPrivate` …) |
| `mcpplibs.mwrap.libvpx.macros` | the macros, in `mcpplibs::mwrap::libvpx::macros` |

VP8 is not built (compat.libvpx is configured `--disable-vp8`): `vpx_codec_vp8_cx`, `vpx_codec_vp8_dx` and their `_algo` objects, which `vp8cx.h` and `vp8dx.h` declare, are not exported. The VP8 control ids are, as they are shared with VP9.

## The typed controls

In C, `vpx_codec_control(ctx, VP8E_SET_CPUUSED, 4)` is a macro that becomes `vpx_codec_control_VP8E_SET_CPUUSED(ctx, VP8E_SET_CPUUSED, 4)`, a static function the header defines for each control (`VPX_CTRL_USE_TYPE`) so the argument's type is checked. Internal linkage cannot be exported, and token concatenation is not C++: each of the 86 is adapted with its name and signature in `mcpplibs::mwrap::libvpx`, calling the variadic `vpx_codec_control_` as upstream's body does (`[[deprecated]]` where upstream deprecates it).

```cpp
namespace vpx = mcpplibs::mwrap::libvpx;
vpx::vpx_codec_control_VP8E_SET_CPUUSED(&ctx, VP8E_SET_CPUUSED, 6);
vpx::vpx_codec_control_VP9E_SET_ROW_MT(&ctx, VP9E_SET_ROW_MT, 1u);
vpx::vpx_codec_control_VP9E_SET_COLOR_SPACE(&ctx, VP9E_SET_COLOR_SPACE, VPX_CS_BT_709);
```

A translation unit that includes `<mwrap/libvpx/macros.hpp>` before the import has the macro too: `vpx_codec_control(&ctx, VP8E_SET_CPUUSED, 6)` calls the header's static function, the same control.

## The macros

`mcpplibs.mwrap.libvpx.macros` maps 74 macros in `mcpplibs::mwrap::libvpx::macros`:

- constants of the type their expression has: the ABI versions, `VPX_CODEC_CAP_*`, `VPX_CODEC_USE_*`, `VPX_FRAME_IS_KEY` and the other packet flags, `VPX_EFLAG_FORCE_KF`, `VP8_EFLAG_*`, `VPX_DL_REALTIME` / `_GOOD_QUALITY` / `_BEST_QUALITY`, the layer limits (`VPX_TS_MAX_LAYERS` …), `VPX_IMG_FMT_PLANAR` and the image format bits, `VPX_PLANE_Y` / `_U` / `_V` / `_ALPHA` …;
- functions: `vpx_codec_enc_init`, `vpx_codec_enc_init_multi`, `vpx_codec_dec_init` (each passes the ABI version the headers were written for to its `_ver` function), `vpx_codec_version_major` / `_minor` / `_patch`;
- constexpr functions: `VPX_VERSION_MAJOR(v)`, `VPX_VERSION_MINOR(v)`, `VPX_VERSION_PATCH(v)`.

What stays a macro, in `<mwrap/libvpx/macros.hpp>`: `vpx_codec_control` (token concatenation), `VPX_CTRL_USE_TYPE`, `VPX_CTRL_USE_TYPE_DEPRECATED`, `VPX_CTRL_VOID`, the 86 empty `VPX_CTRL_<id>` feature tests (`#ifdef VPX_CTRL_VP9E_SET_ROW_MT`), the declaration annotations (`VPX_INLINE`, `VPX_FORCE_INLINE`, `VPX_DEPRECATED`, `VPX_DECLSPEC_DEPRECATED`, `VPX_UNUSED`), libwebm's `LIBWEBM_DISALLOW_COPY_AND_ASSIGN` and the include guards. While a macro is defined it expands even inside `macros::NAME`; a function-like one does not when its name is followed by `)`, so `(macros::vpx_codec_enc_init)(…)` is the mapped function beside the header's macro.

## libwebm's constants

libwebm's headers define a few constants at namespace scope (`mkvmuxer::kMaxTrackNumber`, `kEbmlUnknownValue`, `kMaxBlockTimecode`; `mkvparser::E_PARSE_FAILED`, `E_FILE_FORMAT_INVALID`, `E_BUFFER_NOT_FULL`). In C++ a namespace-scope `const` has internal linkage and cannot be exported; their values are under the same names in `mcpplibs::mwrap::libvpx::mkvmuxer` and `mcpplibs::mwrap::libvpx::mkvparser`. Class constants (`mkvmuxer::Tracks::kVp9CodecId`, `mkvmuxer::Colour::kBt709` …) come with their classes.

## Encoding into WebM, in brief

```cpp
import std;
import mcpplibs.mwrap.libvpx;
namespace m = mcpplibs::mwrap::libvpx::macros;
namespace vpx = mcpplibs::mwrap::libvpx;

// A seekable writer in memory: mkvmuxer goes back for the sizes, cues and duration.
class Memory final : public mkvmuxer::IMkvWriter { /* Write, Position, Position(pos), Seekable, ElementStartNotify */ };

vpx_codec_enc_cfg_t cfg;
vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0);
cfg.g_w = 1280; cfg.g_h = 720; cfg.g_timebase = {1, 60};
cfg.rc_end_usage = VPX_Q;                            // constant quality …
vpx_codec_ctx_t enc;
m::vpx_codec_enc_init(&enc, vpx_codec_vp9_cx(), &cfg, 0);
vpx::vpx_codec_control_VP8E_SET_CQ_LEVEL(&enc, VP8E_SET_CQ_LEVEL, 20u);   // … at this level
vpx::vpx_codec_control_VP8E_SET_CPUUSED(&enc, VP8E_SET_CPUUSED, 5);

Memory out;
mkvmuxer::Segment segment;
segment.Init(&out);
segment.set_mode(mkvmuxer::Segment::kFile);
auto track = segment.AddVideoTrack(1280, 720, 1);
auto* video = static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(track));
video->set_codec_id(mkvmuxer::Tracks::kVp9CodecId);
video->set_uid(1);                                   // reproducible: mkvmuxer's own is random

// for each frame i: vpx_codec_encode(&enc, &img, i, 1, 0, m::VPX_DL_GOOD_QUALITY), then for each packet:
//     segment.AddFrame(buf, size, track, pts * 1'000'000'000 / 60, (flags & m::VPX_FRAME_IS_KEY) != 0);
// at the end: vpx_codec_encode(&enc, nullptr, -1, 1, 0, …) until no packet comes, then segment.Finalize().
```

Reading back: `mkvparser::EBMLHeader::Parse`, `mkvparser::Segment::CreateInstance` over an `IMkvReader`, `Load`, then the clusters' blocks' frames (`GetFirst`, `GetNext`, `Block::GetFrame(k).Read`), each given to `vpx_codec_decode`; `vpx_codec_get_frame` gives the pictures. [examples/vpxwebm](../examples/vpxwebm/src/main.cpp) does both.

## Determinism and speed

The codec is libvpx's portable C (no SIMD): with one thread the encoder writes the same bytes on every target — the tests pin each file's FNV-1a on Linux, macOS, Windows and the Web — and those bytes are upstream `vpxenc`'s for the same settings (tools/run_conformance.py). VP9 decoding is exact by specification. Speed is the C reference's: real-time speeds (`VPX_DL_REALTIME`, `VP8E_SET_CPUUSED` 6–9) and `VP9E_SET_ROW_MT` with threads are the fast paths; SIMD (the assembly and intrinsics behind upstream's run-time dispatch) is a later revision of compat.libvpx.

## Platforms

Linux (x86_64, arm64), macOS (arm64), Windows (x86_64, clang) and wasm32-emscripten. On Emscripten without its pthreads, `CONFIG_MULTITHREAD` is 0: asking for threads is accepted and the work runs in turn on the calling thread.
