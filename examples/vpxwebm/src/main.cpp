// vpxwebm — VP9 in WebM (or IVF) through import mcpplibs.mwrap.libvpx.
//
//     vpxwebm encode <in.y4m> <out.webm|out.ivf> [--good | --rt] [--cpu-used N] [--end-usage q|vbr|cbr]
//                    [--cq-level N] [--target-bitrate KBPS] [--lag-in-frames N] [--threads N] [--tile-columns N] [--lossless]
//     vpxwebm decode <in.webm|in.ivf> <out.yuv>      every frame as raw I420
//     vpxwebm info   <in.webm>                       the tracks, frames and duration
//
// Encoding takes the Y4M's size and frame rate (the time base is the frame
// rate's inverse, as vpxenc's) and sets only the controls given, so the IVF it
// writes for a set of vpxenc's options is the one vpxenc writes for them
// (tools/run_conformance.py).
import std;
import mcpplibs.mwrap.libvpx;

namespace m = mcpplibs::mwrap::libvpx::macros;
namespace vpx = mcpplibs::mwrap::libvpx;

namespace {

[[noreturn]] void fail(std::string_view what) {
    std::println(std::cerr, "vpxwebm: {}", what);
    std::exit(1);
}

std::vector<unsigned char> read_file(const std::string& name) {
    std::ifstream in(name, std::ios::binary);
    if (!in) fail("cannot read " + name);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_file(const std::string& name, std::span<const unsigned char> bytes) {
    std::ofstream out(name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) fail("cannot write " + name);
}

// ── Y4M in ──

struct Y4m {
    int                                     width = 0, height = 0, rate = 30, scale = 1;
    std::vector<std::vector<unsigned char>> frames;  // I420
};

Y4m read_y4m(const std::string& name) {
    const auto b = read_file(name);
    const std::string_view text(reinterpret_cast<const char*>(b.data()), b.size());
    const auto eol = text.find('\n');
    if (!text.starts_with("YUV4MPEG2 ") || eol == std::string_view::npos) fail(name + " is not a Y4M file");
    Y4m y;
    for (auto part : std::views::split(text.substr(10, eol - 10), ' ')) {
        const std::string tag(part.begin(), part.end());
        if (tag.empty()) continue;
        if (tag[0] == 'W') y.width = std::stoi(tag.substr(1));
        if (tag[0] == 'H') y.height = std::stoi(tag.substr(1));
        if (tag[0] == 'F') y.rate = std::stoi(tag.substr(1)), y.scale = std::stoi(tag.substr(tag.find(':') + 1));
        if (tag[0] == 'C' && !tag.starts_with("C420")) fail("only 4:2:0 Y4M is read");
    }
    const std::size_t size = static_cast<std::size_t>(y.width) * y.height + 2 * static_cast<std::size_t>((y.width + 1) / 2) * ((y.height + 1) / 2);
    for (std::size_t at = eol + 1; at < text.size();) {
        at = text.find('\n', at) + 1;
        if (at == 0 || text.size() - at < size) fail(name + ": a frame cut short");
        y.frames.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
    }
    return y;
}

// ── encoding ──

struct Options {
    vpx_enc_deadline_t                 deadline = m::VPX_DL_GOOD_QUALITY;
    std::optional<int>                 cpu_used, cq_level, tile_columns;
    std::optional<unsigned>            lag, threads, bitrate;
    std::optional<vpx_rc_mode>         end_usage;
    bool                               lossless = false;
};

struct Packet {
    std::vector<unsigned char> bytes;
    std::int64_t               pts = 0;
    bool                       key = false;
};

std::vector<Packet> encode(const Y4m& y, const Options& o) {
    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) fail("no default configuration");
    cfg.g_w = static_cast<unsigned>(y.width);
    cfg.g_h = static_cast<unsigned>(y.height);
    cfg.g_timebase = {y.scale, y.rate};
    if (o.lag) cfg.g_lag_in_frames = *o.lag;
    if (o.threads) cfg.g_threads = *o.threads;
    if (o.end_usage) cfg.rc_end_usage = *o.end_usage;
    if (o.bitrate) cfg.rc_target_bitrate = *o.bitrate;
    vpx_codec_ctx_t ctx;
    if (m::vpx_codec_enc_init(&ctx, vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) fail(std::string("encoder: ") + vpx_codec_error(&ctx));
    if (o.cpu_used) vpx::vpx_codec_control_VP8E_SET_CPUUSED(&ctx, VP8E_SET_CPUUSED, *o.cpu_used);
    if (o.cq_level) vpx::vpx_codec_control_VP8E_SET_CQ_LEVEL(&ctx, VP8E_SET_CQ_LEVEL, static_cast<unsigned>(*o.cq_level));
    if (o.tile_columns) vpx::vpx_codec_control_VP9E_SET_TILE_COLUMNS(&ctx, VP9E_SET_TILE_COLUMNS, *o.tile_columns);
    if (o.lossless) vpx::vpx_codec_control_VP9E_SET_LOSSLESS(&ctx, VP9E_SET_LOSSLESS, 1u);
    std::vector<Packet> out;
    auto drain = [&] {
        vpx_codec_iter_t it = nullptr;
        while (const vpx_codec_cx_pkt_t* p = vpx_codec_get_cx_data(&ctx, &it))
            if (p->kind == VPX_CODEC_CX_FRAME_PKT) {
                const auto* b = static_cast<const unsigned char*>(p->data.frame.buf);
                out.push_back({{b, b + p->data.frame.sz}, p->data.frame.pts, (p->data.frame.flags & m::VPX_FRAME_IS_KEY) != 0});
            }
    };
    vpx_image_t img;
    for (std::size_t i = 0; i < y.frames.size(); ++i) {
        vpx_img_wrap(&img, VPX_IMG_FMT_I420, static_cast<unsigned>(y.width), static_cast<unsigned>(y.height), 1,
                     const_cast<unsigned char*>(y.frames[i].data()));
        if (vpx_codec_encode(&ctx, &img, static_cast<vpx_codec_pts_t>(i), 1, 0, o.deadline) != VPX_CODEC_OK)
            fail(std::string("encoding: ") + vpx_codec_error(&ctx));
        drain();
    }
    for (bool more = true; more;) {
        if (vpx_codec_encode(&ctx, nullptr, -1, 1, 0, o.deadline) != VPX_CODEC_OK) fail("flushing");
        const std::size_t before = out.size();
        drain();
        more = out.size() != before;
    }
    vpx_codec_destroy(&ctx);
    return out;
}

void put32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i)));
}

std::vector<unsigned char> ivf(const Y4m& y, const std::vector<Packet>& packets) {
    std::vector<unsigned char> b{'D', 'K', 'I', 'F', 0, 0, 32, 0, 'V', 'P', '9', '0'};
    b.push_back(static_cast<unsigned char>(y.width)), b.push_back(static_cast<unsigned char>(y.width >> 8));
    b.push_back(static_cast<unsigned char>(y.height)), b.push_back(static_cast<unsigned char>(y.height >> 8));
    put32(b, static_cast<std::uint32_t>(y.rate));
    put32(b, static_cast<std::uint32_t>(y.scale));
    put32(b, static_cast<std::uint32_t>(packets.size()));
    put32(b, 0);
    for (const Packet& p : packets) {
        put32(b, static_cast<std::uint32_t>(p.bytes.size()));
        put32(b, static_cast<std::uint32_t>(p.pts));
        put32(b, static_cast<std::uint32_t>(static_cast<std::uint64_t>(p.pts) >> 32));
        b.insert(b.end(), p.bytes.begin(), p.bytes.end());
    }
    return b;
}

class FileWriter final : public mkvmuxer::IMkvWriter {
public:
    std::vector<unsigned char> bytes;
    std::int64_t               at = 0;
    mkvmuxer::int32 Write(const void* buf, mkvmuxer::uint32 len) override {
        if (bytes.size() < static_cast<std::size_t>(at) + len) bytes.resize(static_cast<std::size_t>(at) + len);
        std::memcpy(bytes.data() + at, buf, len);
        at += len;
        return 0;
    }
    mkvmuxer::int64 Position() const override { return at; }
    mkvmuxer::int32 Position(mkvmuxer::int64 position) override {
        at = position;
        return 0;
    }
    bool Seekable() const override { return true; }
    void ElementStartNotify(mkvmuxer::uint64, mkvmuxer::int64) override {}
};

std::vector<unsigned char> webm(const Y4m& y, const std::vector<Packet>& packets) {
    FileWriter w;
    mkvmuxer::Segment segment;
    if (!segment.Init(&w)) fail("muxer");
    segment.set_mode(mkvmuxer::Segment::kFile);
    segment.OutputCues(true);
    segment.GetSegmentInfo()->set_writing_app("vpxwebm");
    segment.GetSegmentInfo()->set_muxing_app("vpxwebm");
    const auto track = segment.AddVideoTrack(y.width, y.height, 1);
    auto* v = static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(track));
    v->set_codec_id(mkvmuxer::Tracks::kVp9CodecId);
    v->set_uid(1);  // reproducible: mkvmuxer's own is random
    v->set_frame_rate(static_cast<double>(y.rate) / y.scale);
    segment.CuesTrack(track);
    for (const Packet& p : packets) {
        const auto ns = static_cast<std::uint64_t>(p.pts) * 1'000'000'000ull * static_cast<std::uint64_t>(y.scale) / static_cast<std::uint64_t>(y.rate);
        if (!segment.AddFrame(p.bytes.data(), p.bytes.size(), track, ns, p.key)) fail("adding a frame");
    }
    if (!segment.Finalize()) fail("finishing the file");
    return w.bytes;
}

// ── decoding ──

class MemoryReader final : public mkvparser::IMkvReader {
public:
    explicit MemoryReader(const std::vector<unsigned char>& b) : bytes(b) {}
    int Read(long long pos, long len, unsigned char* buf) override {
        if (pos < 0 || len < 0 || static_cast<std::size_t>(pos + len) > bytes.size()) return -1;
        std::memcpy(buf, bytes.data() + pos, static_cast<std::size_t>(len));
        return 0;
    }
    int Length(long long* total, long long* available) override {
        if (total) *total = static_cast<long long>(bytes.size());
        if (available) *available = static_cast<long long>(bytes.size());
        return 0;
    }
    const std::vector<unsigned char>& bytes;
};

struct Stream {
    std::vector<std::vector<unsigned char>> frames;
    long long                               width = 0, height = 0, duration_ns = 0;
    std::string                             codec;
};

Stream read_webm(const std::vector<unsigned char>& file) {
    Stream s;
    MemoryReader reader(file);
    long long pos = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(&reader, pos) != 0) fail("not a WebM file");
    mkvparser::Segment* raw = nullptr;
    if (mkvparser::Segment::CreateInstance(&reader, pos, raw) != 0 || !raw) fail("no segment");
    std::unique_ptr<mkvparser::Segment> segment(raw);
    if (segment->Load() < 0) fail("the segment does not load");
    const mkvparser::Tracks* tracks = segment->GetTracks();
    const mkvparser::Track* video = nullptr;
    for (unsigned long i = 0; i < tracks->GetTracksCount(); ++i)
        if (tracks->GetTrackByIndex(i)->GetType() == mkvparser::Track::kVideo) video = tracks->GetTrackByIndex(i);
    if (!video) fail("no video track");
    s.codec = video->GetCodecId();
    s.width = static_cast<const mkvparser::VideoTrack*>(video)->GetWidth();
    s.height = static_cast<const mkvparser::VideoTrack*>(video)->GetHeight();
    s.duration_ns = segment->GetDuration();
    for (const mkvparser::Cluster* c = segment->GetFirst(); c && !c->EOS(); c = segment->GetNext(c)) {
        const mkvparser::BlockEntry* e = nullptr;
        if (c->GetFirst(e) != 0) break;
        while (e && !e->EOS()) {
            const mkvparser::Block* b = e->GetBlock();
            if (b->GetTrackNumber() == video->GetNumber())
                for (int k = 0; k < b->GetFrameCount(); ++k) {
                    const mkvparser::Block::Frame& f = b->GetFrame(k);
                    std::vector<unsigned char> data(static_cast<std::size_t>(f.len));
                    if (f.Read(&reader, data.data()) != 0) fail("reading a frame");
                    s.frames.push_back(std::move(data));
                }
            if (c->GetNext(e, e) != 0) break;
        }
    }
    return s;
}

Stream read_ivf(const std::vector<unsigned char>& file) {
    Stream s;
    auto u32 = [&](std::size_t at) { return std::uint32_t(file[at]) | std::uint32_t(file[at + 1]) << 8 | std::uint32_t(file[at + 2]) << 16 | std::uint32_t(file[at + 3]) << 24; };
    if (file.size() < 32 || std::memcmp(file.data(), "DKIF", 4) != 0) fail("not an IVF file");
    s.codec = "V_VP9";
    s.width = file[12] | file[13] << 8;
    s.height = file[14] | file[15] << 8;
    for (std::size_t at = 32; at + 12 <= file.size();) {
        const std::size_t n = u32(at);
        if (at + 12 + n > file.size()) fail("an IVF frame cut short");
        s.frames.emplace_back(file.begin() + static_cast<std::ptrdiff_t>(at + 12), file.begin() + static_cast<std::ptrdiff_t>(at + 12 + n));
        at += 12 + n;
    }
    return s;
}

std::vector<unsigned char> decode(const Stream& s) {
    std::vector<unsigned char> out;
    vpx_codec_dec_cfg_t cfg{};
    vpx_codec_ctx_t ctx;
    if (m::vpx_codec_dec_init(&ctx, vpx_codec_vp9_dx(), &cfg, 0) != VPX_CODEC_OK) fail("decoder");
    for (const auto& f : s.frames) {
        if (vpx_codec_decode(&ctx, f.data(), static_cast<unsigned>(f.size()), nullptr, 0) != VPX_CODEC_OK)
            fail(std::string("decoding: ") + vpx_codec_error(&ctx));
        vpx_codec_iter_t it = nullptr;
        while (vpx_image_t* img = vpx_codec_get_frame(&ctx, &it))
            for (int plane = 0; plane < 3; ++plane) {
                const unsigned w = plane ? (img->d_w + 1) / 2 : img->d_w, h = plane ? (img->d_h + 1) / 2 : img->d_h;
                for (unsigned y = 0; y < h; ++y) {
                    const unsigned char* row = img->planes[plane] + static_cast<std::ptrdiff_t>(y) * img->stride[plane];
                    out.insert(out.end(), row, row + w);
                }
            }
    }
    vpx_codec_destroy(&ctx);
    return out;
}

Stream read_stream(const std::string& name) {
    const auto b = read_file(name);
    return name.ends_with(".ivf") ? read_ivf(b) : read_webm(b);
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> a(argv + 1, argv + argc);
    if (a.size() >= 3 && a[0] == "encode") {
        Options o;
        for (std::size_t i = 3; i < a.size(); ++i) {
            const std::string& s = a[i];
            auto next = [&] {
                if (i + 1 >= a.size()) fail(s + " takes a value");
                return a[++i];
            };
            if (s == "--good") o.deadline = m::VPX_DL_GOOD_QUALITY;
            else if (s == "--rt") o.deadline = m::VPX_DL_REALTIME;
            else if (s == "--cpu-used") o.cpu_used = std::stoi(next());
            else if (s == "--cq-level") o.cq_level = std::stoi(next());
            else if (s == "--tile-columns") o.tile_columns = std::stoi(next());
            else if (s == "--lag-in-frames") o.lag = static_cast<unsigned>(std::stoul(next()));
            else if (s == "--threads") o.threads = static_cast<unsigned>(std::stoul(next()));
            else if (s == "--target-bitrate") o.bitrate = static_cast<unsigned>(std::stoul(next()));
            else if (s == "--end-usage") {
                const std::string u = next();
                o.end_usage = u == "q" ? VPX_Q : u == "cbr" ? VPX_CBR : u == "cq" ? VPX_CQ : VPX_VBR;
            } else if (s == "--lossless") o.lossless = true;
            else fail("unknown option " + s);
        }
        const Y4m y = read_y4m(a[1]);
        const auto packets = encode(y, o);
        const auto bytes = a[2].ends_with(".ivf") ? ivf(y, packets) : webm(y, packets);
        write_file(a[2], bytes);
        std::println("{} frames, {} packets, {} bytes", y.frames.size(), packets.size(), bytes.size());
        return 0;
    }
    if (a.size() == 3 && a[0] == "decode") {
        const Stream s = read_stream(a[1]);
        const auto yuv = decode(s);
        write_file(a[2], yuv);
        std::println("{} frames, {} bytes of I420", s.frames.size(), yuv.size());
        return 0;
    }
    if (a.size() == 2 && a[0] == "info") {
        const Stream s = read_stream(a[1]);
        std::println("{} {} x {}, {} frames, {:.3f} s", s.codec, s.width, s.height, s.frames.size(), static_cast<double>(s.duration_ns) / 1e9);
        return 0;
    }
    std::println(std::cerr, "vpxwebm encode <in.y4m> <out.webm|out.ivf> [options] | decode <in.webm|in.ivf> <out.yuv> | info <in.webm>");
    return 2;
}
