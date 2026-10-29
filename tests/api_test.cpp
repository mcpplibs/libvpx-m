// The API through the modules alone: VP9 frames encoded (good quality and real
// time, one thread and four, lossless), muxed into a WebM with mkvmuxer (a VP9
// track with its colour, an Opus track's header, cues, the duration), parsed
// back with mkvparser, decoded and compared — every frame back, at a PSNR a
// correct codec reaches; lossless, every pixel. One thread, the same settings:
// the same bytes twice; each file's bytes pinned (the codec's portable C and
// the muxer with its track UID given write the same bytes on every target).
import std;
import mcpplibs.mwrap.libvpx;

namespace m = mcpplibs::mwrap::libvpx::macros;
namespace vpx = mcpplibs::mwrap::libvpx;

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    std::println("{:<72} {}", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}

constexpr int W = 256, H = 144, FRAMES = 30;

// A gradient that moves and a square that crosses it.
void draw(vpx_image_t& img, int t) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int v = (x * 2 + y + t * 5) & 255;
            if (x >= 30 + t * 6 && x < 70 + t * 6 && y >= 50 && y < 90) v = 235;
            img.planes[m::VPX_PLANE_Y][y * img.stride[m::VPX_PLANE_Y] + x] = static_cast<unsigned char>(v);
        }
    for (int y = 0; y < H / 2; ++y)
        for (int x = 0; x < W / 2; ++x) {
            img.planes[m::VPX_PLANE_U][y * img.stride[m::VPX_PLANE_U] + x] = static_cast<unsigned char>(128 + ((x + t) & 31) - 16);
            img.planes[m::VPX_PLANE_V][y * img.stride[m::VPX_PLANE_V] + x] = static_cast<unsigned char>(128 - ((y + t) & 31) + 16);
        }
}

struct Packet {
    std::vector<unsigned char> bytes;
    std::int64_t               pts = 0;
    bool                       key = false;
};

std::vector<Packet> encode(vpx_enc_deadline_t deadline, int speed, bool lossless, unsigned threads) {
    std::vector<Packet> out;
    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) return out;
    cfg.g_w = W;
    cfg.g_h = H;
    cfg.g_timebase = {1, 30};
    cfg.g_threads = threads;
    cfg.g_lag_in_frames = 0;
    cfg.rc_end_usage = VPX_Q;
    vpx_codec_ctx_t ctx;
    if (m::vpx_codec_enc_init(&ctx, vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) return out;
    vpx::vpx_codec_control_VP8E_SET_CPUUSED(&ctx, VP8E_SET_CPUUSED, speed);
    vpx::vpx_codec_control_VP8E_SET_CQ_LEVEL(&ctx, VP8E_SET_CQ_LEVEL, 18u);
    vpx::vpx_codec_control_VP9E_SET_COLOR_SPACE(&ctx, VP9E_SET_COLOR_SPACE, VPX_CS_BT_709);
    if (lossless) vpx::vpx_codec_control_VP9E_SET_LOSSLESS(&ctx, VP9E_SET_LOSSLESS, 1u);
    if (threads > 1) vpx::vpx_codec_control_VP9E_SET_TILE_COLUMNS(&ctx, VP9E_SET_TILE_COLUMNS, 2);
    vpx_image_t img;
    vpx_img_alloc(&img, VPX_IMG_FMT_I420, W, H, 16);
    auto drain = [&] {
        vpx_codec_iter_t it = nullptr;
        while (const vpx_codec_cx_pkt_t* p = vpx_codec_get_cx_data(&ctx, &it)) {
            if (p->kind != VPX_CODEC_CX_FRAME_PKT) continue;
            const auto* b = static_cast<const unsigned char*>(p->data.frame.buf);
            out.push_back({{b, b + p->data.frame.sz}, p->data.frame.pts, (p->data.frame.flags & m::VPX_FRAME_IS_KEY) != 0});
        }
    };
    for (int t = 0; t < FRAMES; ++t) {
        draw(img, t);
        if (vpx_codec_encode(&ctx, &img, t, 1, 0, deadline) != VPX_CODEC_OK) return {};
        drain();
    }
    vpx_codec_encode(&ctx, nullptr, FRAMES, 1, 0, deadline);
    drain();
    vpx_img_free(&img);
    vpx_codec_destroy(&ctx);
    return out;
}

class MemoryWriter final : public mkvmuxer::IMkvWriter {
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

// A WebM of the packets (30 fps: frame i at i/30 s), track UIDs given so the bytes are reproducible.
std::vector<unsigned char> mux(const std::vector<Packet>& packets) {
    MemoryWriter w;
    mkvmuxer::Segment segment;
    if (!segment.Init(&w)) return {};
    segment.set_mode(mkvmuxer::Segment::kFile);
    segment.OutputCues(true);
    const auto video = segment.AddVideoTrack(W, H, 1);
    auto* v = static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(video));
    v->set_codec_id(mkvmuxer::Tracks::kVp9CodecId);
    v->set_uid(1);
    mkvmuxer::Colour colour;
    colour.set_matrix_coefficients(mkvmuxer::Colour::kBt709);
    colour.set_range(mkvmuxer::Colour::kBroadcastRange);
    v->SetColour(colour);
    segment.CuesTrack(video);
    for (const Packet& p : packets)
        if (!segment.AddFrame(p.bytes.data(), p.bytes.size(), video, static_cast<std::uint64_t>(p.pts) * 1'000'000'000 / 30, p.key)) return {};
    segment.set_duration(1000.0 * FRAMES / 30);
    if (!segment.Finalize()) return {};
    return w.bytes;
}

// The frames of a WebM's VP9 track, in order.
std::vector<std::vector<unsigned char>> demux(const std::vector<unsigned char>& file, int& track_width, bool& bt709) {
    std::vector<std::vector<unsigned char>> frames;
    MemoryReader reader(file);
    long long pos = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(&reader, pos) != 0) return frames;
    mkvparser::Segment* raw = nullptr;
    if (mkvparser::Segment::CreateInstance(&reader, pos, raw) != 0 || !raw) return frames;
    std::unique_ptr<mkvparser::Segment> segment(raw);
    if (segment->Load() < 0) return frames;
    const auto* track = static_cast<const mkvparser::VideoTrack*>(segment->GetTracks()->GetTrackByNumber(1));
    if (!track || std::string_view(track->GetCodecId()) != "V_VP9") return frames;
    track_width = static_cast<int>(track->GetWidth());
    bt709 = track->GetColour() && track->GetColour()->matrix_coefficients == mkvmuxer::Colour::kBt709;
    for (const mkvparser::Cluster* c = segment->GetFirst(); c && !c->EOS(); c = segment->GetNext(c)) {
        const mkvparser::BlockEntry* e = nullptr;
        if (c->GetFirst(e) != 0) break;
        while (e && !e->EOS()) {
            const mkvparser::Block::Frame& f = e->GetBlock()->GetFrame(0);
            std::vector<unsigned char> data(static_cast<std::size_t>(f.len));
            if (f.Read(&reader, data.data()) != 0) return {};
            frames.push_back(std::move(data));
            if (c->GetNext(e, e) != 0) break;
        }
    }
    return frames;
}

struct Decoded {
    int    frames = 0;
    double worst = 1e9;  // luma PSNR, dB
    bool   exact = true;
};

Decoded decode(const std::vector<std::vector<unsigned char>>& frames, unsigned threads) {
    Decoded d;
    vpx_codec_dec_cfg_t cfg{};
    cfg.threads = threads;
    vpx_codec_ctx_t ctx;
    if (m::vpx_codec_dec_init(&ctx, vpx_codec_vp9_dx(), &cfg, 0) != VPX_CODEC_OK) return d;
    vpx_image_t ref;
    vpx_img_alloc(&ref, VPX_IMG_FMT_I420, W, H, 16);
    for (const auto& f : frames) {
        if (vpx_codec_decode(&ctx, f.data(), static_cast<unsigned>(f.size()), nullptr, 0) != VPX_CODEC_OK) break;
        vpx_codec_iter_t it = nullptr;
        while (vpx_image_t* img = vpx_codec_get_frame(&ctx, &it)) {
            draw(ref, d.frames);
            double se = 0;
            for (int plane = 0; plane < 3; ++plane)
                for (int y = 0; y < (plane ? H / 2 : H); ++y)
                    for (int x = 0; x < (plane ? W / 2 : W); ++x) {
                        const int a = img->planes[plane][y * img->stride[plane] + x], b = ref.planes[plane][y * ref.stride[plane] + x];
                        d.exact = d.exact && a == b;
                        if (plane == 0) se += double(a - b) * (a - b);
                    }
            const double mse = se / (W * H);
            d.worst = std::min(d.worst, mse == 0 ? 99.0 : 10 * std::log10(255.0 * 255.0 / mse));
            ++d.frames;
        }
    }
    vpx_img_free(&ref);
    vpx_codec_destroy(&ctx);
    return d;
}

std::uint64_t fnv(const std::vector<unsigned char>& b) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : b) h = (h ^ c) * 0x100000001b3ull;
    return h;
}

}  // namespace

int main() {
    check(std::string_view(vpx_codec_version_str()) == "v1.17.0" && m::VPX_VERSION_MAJOR(vpx_codec_version()) == 1 &&
              m::VPX_VERSION_MINOR(vpx_codec_version()) == 17,
          "libvpx v1.17.0");
    // libwebm 1.0.0.32's parser and muxer report their own versions.
    int major = 0, minor = 0, build = 0, revision = 0;
    mkvparser::GetVersion(major, minor, build, revision);
    mkvmuxer::int32 mmajor = 0, mminor = 0, mbuild = 0, mrevision = 0;
    mkvmuxer::GetVersion(&mmajor, &mminor, &mbuild, &mrevision);
    check(major == 1 && minor == 1 && build == 4 && revision == 0 && mmajor == 0 && mminor == 3 && mbuild == 4 && mrevision == 0,
          "libwebm 1.0.0.32: mkvparser 1.1.4.0, mkvmuxer 0.3.4.0");
    check((vpx_codec_get_caps(vpx_codec_vp9_cx()) & m::VPX_CODEC_CAP_ENCODER) != 0 && (vpx_codec_get_caps(vpx_codec_vp9_dx()) & m::VPX_CODEC_CAP_DECODER) != 0,
          "the VP9 encoder and decoder interfaces");

    struct Case {
        std::string_view   name;
        vpx_enc_deadline_t deadline;
        int                speed;
        bool               lossless;
        unsigned           threads;
        double             floor;
        std::uint64_t      pinned;  // the WebM's FNV-1a: the encoder's bytes, the same on every target
    };
    for (const Case& k : {Case{"good quality", static_cast<vpx_enc_deadline_t>(m::VPX_DL_GOOD_QUALITY), 4, false, 1, 38, 0xc19238c601547696},
                          Case{"real time, speed 8", static_cast<vpx_enc_deadline_t>(m::VPX_DL_REALTIME), 8, false, 1, 30, 0xe8396a4100a874f6},
                          Case{"four threads, tile columns", static_cast<vpx_enc_deadline_t>(m::VPX_DL_GOOD_QUALITY), 5, false, 4, 36, 0x2c42d979708eaf23},
                          Case{"lossless", static_cast<vpx_enc_deadline_t>(m::VPX_DL_GOOD_QUALITY), 4, true, 1, 99, 0xeed41b6461e2327a}}) {
        const auto packets = encode(k.deadline, k.speed, k.lossless, k.threads);
        check(packets.size() == FRAMES && packets.front().key, std::format("{}: a packet a frame, the first a key frame", k.name));
        const auto file = mux(packets);
        int width = 0;
        bool bt709 = false;
        const auto frames = demux(file, width, bt709);
        check(frames.size() == FRAMES && width == W && bt709, std::format("{}: muxed into WebM and parsed back ({} bytes)", k.name, file.size()));
        bool same = frames.size() == packets.size();
        for (std::size_t i = 0; same && i < frames.size(); ++i) same = frames[i] == packets[i].bytes;
        check(same, std::format("{}: every frame's bytes through the container", k.name));
        const Decoded d = decode(frames, k.threads > 1 ? 2 : 1);
        std::println("    {}: worst PSNR {:.2f} dB, stream {:016x}", k.name, d.worst, fnv(file));
        check(d.frames == FRAMES && d.worst >= k.floor && (!k.lossless || d.exact), std::format("{}: decoded back, at least {} dB", k.name, k.floor));
        if (k.threads == 1 && !k.lossless) check(file == mux(encode(k.deadline, k.speed, k.lossless, k.threads)), std::format("{}: the same bytes twice", k.name));
        check(fnv(file) == k.pinned, std::format("{}: the bytes pinned ({:016x})", k.name, k.pinned));
    }
    std::println("{}", failures ? "FAILED" : "all passed");
    return failures;
}
