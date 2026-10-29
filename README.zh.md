# libvpx

> 通过 C++23 模块使用 VP9 视频编解码库 libvpx 与 WebM 容器库 libwebm。

[English](README.md) · **简体中文** · [Upstream](https://chromium.googlesource.com/webm/libvpx) · [libwebm](https://github.com/webmproject/libwebm) · [Releases](https://github.com/mcpplibs/libvpx-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![libvpx](https://img.shields.io/badge/libvpx-BSD--3--Clause-blue.svg)](upstream/libvpx/LICENSE)
[![libwebm](https://img.shields.io/badge/libwebm-BSD--3--Clause-blue.svg)](upstream/libwebm/LICENSE.TXT)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/libvpx-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/libvpx-m/actions/workflows/ci.yml)

`mcpplibs.mwrap.libvpx` 1.17.0 编码和解码 VP9（Web 上免版税的视频格式），并写入和读取
WebM（所有浏览器都能播放的容器）。上游 API 保持不变：`vpx_codec_enc_config_default`、
`vpx_codec_encode`、`vpx_codec_get_cx_data`、`vpx_codec_decode`、`vpx_codec_get_frame`、
`vpx_img_alloc` … 是 libvpx 的全局名；`mkvmuxer::Segment`、`mkvparser::Segment`、
`mkvmuxer::IMkvWriter`、`mkvparser::IMkvReader` … 是 libwebm 在各自命名空间里的类。
库由 `compat.libvpx` 1.17.0 与 `compat.libwebm` 1.0.0.32 提供。头文件中的类型化控制函数
（`vpx_codec_control_VP8E_SET_CPUUSED` …）以同名适配，宏映射为常量和函数。每次 CI 都验证：
写出的文件与上游自己的 `vpxenc` 逐字节相同，`vpxdec` 读出的帧也相同。

## 模块

- `import mcpplibs.mwrap.libvpx;`：编码器与解码器（`vpx/vpx_encoder.h`、`vpx/vpx_decoder.h`、`vpx/vp8cx.h`、`vpx/vp8dx.h` …），并重新导出下面两个模块
- `import mcpplibs.mwrap.libvpx.webm;`：libwebm 的 `mkvmuxer`、`mkvparser` 与 `libwebm` 命名空间
- `import mcpplibs.mwrap.libvpx.macros;`：`VPX_DL_REALTIME`、`VPX_FRAME_IS_KEY`、`VPX_CODEC_CAP_ENCODER`、`vpx_codec_enc_init` …，位于 `mcpplibs::mwrap::libvpx::macros`

> 更多模块 API -> [用法与 API](docs/usage.md)

## 快速开始

```sh
mcpp new clip --template mcpplibs.mwrap.libvpx@1.17.0
cd clip
mcpp run -- clip.webm
```

默认模板把两秒的运动画面编码，在内存中封装成 WebM，再解析回来、逐帧解码，打印文件大小和最差的
PSNR；给出文件名时同时写出 WebM。

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
    // … 填好 img.planes，再 vpx_codec_encode(&encoder, &img, pts, 1, 0, m::VPX_DL_GOOD_QUALITY)
    vpx_img_free(&img);
    vpx_codec_destroy(&encoder);
    std::println("{}", vpx_codec_version_str());
}
```

## 库

`compat.libvpx` 构建上游自己的 `configure --target=generic-gnu --disable-vp8 --enable-vp9`
所编译的 101 个 C 源文件：libvpx 的可移植 C 实现，无 SIMD、无汇编、无运行时 CPU 检测，同一配置在
x86-64 与 aarch64、Linux、macOS、Windows 和 wasm32-emscripten 上构建。单线程（默认）在所有目标上
得到相同的字节；多线程按块分担（不带 pthreads 的 Emscripten 上在一个线程里依次执行）。不构建 VP8。
`compat.libwebm` 构建上游的 `webm` 库（mkvmuxer、mkvparser 与公共辅助代码）。mkvmuxer 不设置时会给
轨道随机的 UID（`Track::set_uid`）：需要可重现的文件时请设置它。细节与限制见
[docs/usage.md](docs/usage.md)。

## 示例

- [examples/vpxwebm](examples/vpxwebm)：把 Y4M 编码成 WebM 或 IVF 中的 VP9，把 WebM 或 IVF 解码成原始 I420，
  描述 WebM 文件，基于模块编写；[tools/run_conformance.py](tools/run_conformance.py) 将它与上游的 `vpxenc`、
  `vpxdec` 对照。

## 构建与测试

Linux（x86_64 GCC 与 LLVM、arm64 GCC）、macOS、Windows 与 wasm32-emscripten（node 下运行）的 CI 在
dev 与 release 配置下构建模块并运行测试：好质量、实时、四线程与无损的 VP9 封装进 WebM、解析并解码
回来，每个文件的字节固定；每个宏映射与原始头文件对照；每个导出函数都能链接。原生行还运行导出审计
（[tools/check_exports.py](tools/check_exports.py)：生成的模块是最新的、每个声明都已导出、每个宏都有着落），
除 Windows 外还运行与上游工具的一致性对照。另有一个作业把模板当作已安装的包生成并运行。

## 许可

封装部分为 [Apache-2.0](LICENSE)，作者 mcpp-community；libvpx 与 libwebm 仍为 BSD-3-Clause，并附 WebM
项目的专利授权（[upstream/libvpx/LICENSE](upstream/libvpx/LICENSE)、
[upstream/libvpx/PATENTS](upstream/libvpx/PATENTS)、
[upstream/libwebm/LICENSE.TXT](upstream/libwebm/LICENSE.TXT)、
[upstream/libwebm/PATENTS.TXT](upstream/libwebm/PATENTS.TXT)）。
