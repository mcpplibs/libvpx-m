// <mwrap/libvpx/macros.hpp> — libvpx's headers as the modules see them, for the
// macros a C++ entity cannot be: vpx_codec_control (token concatenation:
// `vpx_codec_control(ctx, VP8E_SET_CPUUSED, 4)` calls the header's typed
// vpx_codec_control_VP8E_SET_CPUUSED), VPX_CTRL_USE_TYPE, VPX_CTRL_VOID, the
// empty VPX_CTRL_<id> feature tests (`#ifdef VPX_CTRL_VP9E_SET_ROW_MT`), the
// declaration annotations (VPX_INLINE, VPX_DEPRECATED, VPX_UNUSED …).
//
// Include it before `import mcpplibs.mwrap.libvpx;`, rather than the headers
// themselves. Every other macro is defined too; while a macro is defined it
// expands even inside `mcpplibs::mwrap::libvpx::macros::NAME`, and a
// function-like one does not when its name is followed by `)`:
// `(macros::vpx_codec_enc_init)(…)` is the mapped function,
// `vpx_codec_enc_init(…)` the header's macro. libwebm (the webm module) has no
// macros but its include guards and LIBWEBM_DISALLOW_COPY_AND_ASSIGN.
#pragma once
#include <vpx/vpx_integer.h>
#include <vpx/vpx_codec.h>
#include <vpx/vpx_image.h>
#include <vpx/vpx_frame_buffer.h>
#include <vpx/vpx_ext_ratectrl.h>
#include <vpx/vpx_tpl.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8.h>
#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
