extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <SDL3/SDL.h>
#include <vector>
#include "videodev.h"

namespace videodev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define VD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

/* The SDL user event code the window passes on when a stub movie ends. */
static const int MOVIE_ENDED = 0x464;

struct Decoded {
    std::vector<unsigned char> rgba;
    int    width = 0, height = 0;
    double pts = 0;       // seconds from the start
    bool   valid = false;
};

struct PlayerState {
    bool              show = false;
    AVFormatContext  *format = NULL;
    AVCodecContext   *vctx = NULL, *actx = NULL;
    int               vstream = -1, astream = -1;
    SwsContext       *sws = NULL;
    SwrContext       *swr = NULL;
    AVPacket         *packet = NULL;
    AVFrame          *frame = NULL;
    SDL_AudioStream  *audio = NULL;
    bool              eof = false;
    Decoded           current, next;   // shown; and decoded, not yet due
    bool              fresh = false;   // current not yet handed out
    bool              running = false, started = false;
    Uint64            startTicks = 0, pausedAt = 0;

    ~PlayerState();
    bool open(const char *path);
    bool decodeNext();
    void audioFrame(AVFrame *f);
};

PlayerState::~PlayerState()
{
    if (audio)  SDL_DestroyAudioStream(audio);
    sws_freeContext(sws);
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&vctx);
    avcodec_free_context(&actx);
    avformat_close_input(&format);
}

static AVCodecContext *open_decoder(AVFormatContext *fmt, int index)
{
    const AVCodec *codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
    if (!codec) return NULL;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) return NULL;
    if (avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar) < 0
        || avcodec_open2(ctx, codec, NULL) < 0)
        avcodec_free_context(&ctx);
    return ctx;
}

bool PlayerState::open(const char *path)
{
    if (avformat_open_input(&format, path, NULL, NULL) < 0
        || avformat_find_stream_info(format, NULL) < 0) {
        VD_LOG("videodev: can't open '%s'\n", path);
        return false;
    }
    vstream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    astream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (vstream < 0 || !(vctx = open_decoder(format, vstream))) {
        VD_LOG("videodev: no playable video in '%s'\n", path);
        return false;
    }
    if (astream >= 0 && (actx = open_decoder(format, astream))) {
        SDL_AudioSpec spec = {};
        spec.format   = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq     = actx->sample_rate;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, actx->sample_rate,
                                &actx->ch_layout, actx->sample_fmt, actx->sample_rate,
                                0, NULL) < 0 || swr_init(swr) < 0) {
            swr_free(&swr);
        } else {
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                              &spec, NULL, NULL);
        }
    }
    packet = av_packet_alloc();
    frame  = av_frame_alloc();
    return packet && frame;
}

/* Converts a decoded audio frame to 16-bit stereo and queues it. */
void PlayerState::audioFrame(AVFrame *f)
{
    if (!swr || !audio) return;
    int max = swr_get_out_samples(swr, f->nb_samples);
    std::vector<Uint8> out((size_t)max * 4);
    Uint8 *outp = out.data();
    int n = swr_convert(swr, &outp, max, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n > 0)
        SDL_PutAudioStreamData(audio, out.data(), n * 4);
}

/* Decodes up to the next video frame into `next`, queueing sound on the way.
 * False at the end of the movie. */
bool PlayerState::decodeNext()
{
    for (;;) {
        // Frames already decoded and waiting inside the codec come first.
        int r = avcodec_receive_frame(vctx, frame);
        if (r == 0) {
            next.width = frame->width;
            next.height = frame->height;
            next.rgba.resize((size_t)frame->width * frame->height * 4);
            sws = sws_getCachedContext(sws, frame->width, frame->height,
                                       (AVPixelFormat)frame->format, frame->width,
                                       frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                       NULL, NULL, NULL);
            uint8_t *dst[1] = { next.rgba.data() };
            int dstStride[1] = { frame->width * 4 };
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
            int64_t t = frame->best_effort_timestamp;
            next.pts = t == AV_NOPTS_VALUE ? next.pts
                     : t * av_q2d(format->streams[vstream]->time_base);
            next.valid = true;
            av_frame_unref(frame);
            return true;
        }
        if (r != AVERROR(EAGAIN))
            return false;  // drained (EOF) or failed

        if (eof) return false;
        r = av_read_frame(format, packet);
        if (r < 0) {
            eof = true;
            avcodec_send_packet(vctx, NULL);  // flush
            continue;
        }
        if (packet->stream_index == vstream) {
            avcodec_send_packet(vctx, packet);
        } else if (packet->stream_index == astream && actx
                   && avcodec_send_packet(actx, packet) == 0) {
            while (avcodec_receive_frame(actx, frame) == 0) {
                audioFrame(frame);
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }
}

Player::Player() : state_(new PlayerState()), playing_(false)
{
}

Player::~Player()
{
    VD_LOG("videodev: teardown\n");
    delete state_;
}

bool Player::load(void *, const char *path, bool show)
{
    VD_LOG("videodev: load(path=\"%s\" show=%d)\n", path ? path : "(null)", (int)show);
    PlayerState *s = state_;
    s->show = show;
    if (!show) {
        playing_ = true;
        return true;
    }
    if (!s->open(path) || !s->decodeNext()) {
        VD_LOG("videodev: can't play '%s'\n", path);
        return false;
    }
    playing_ = true;
    return true;
}

void Player::play()
{
    VD_LOG("videodev: play\n");
    PlayerState *s = state_;
    if (!s->show) {
        SDL_Event e = {};
        e.type = SDL_EVENT_USER;
        e.user.code = MOVIE_ENDED;
        SDL_PushEvent(&e);
        return;
    }
    if (s->running)
        return;
    Uint64 now = SDL_GetTicks();
    if (s->started) {
        s->startTicks += now - s->pausedAt;  // the pause does not count
    } else {
        s->startTicks = now;
        s->started = true;
    }
    s->running = true;
    if (s->audio)
        SDL_ResumeAudioStreamDevice(s->audio);
}

void Player::pause()
{
    VD_LOG("videodev: pause\n");
    PlayerState *s = state_;
    if (!s->show || !s->running)
        return;
    s->running = false;
    s->pausedAt = SDL_GetTicks();
    if (s->audio)
        SDL_PauseAudioStreamDevice(s->audio);
}

void Player::skip()
{
    VD_LOG("videodev: skip\n");
    playing_ = false;
    if (state_->audio) {
        SDL_DestroyAudioStream(state_->audio);
        state_->audio = NULL;
    }
}

bool Player::update()
{
    PlayerState *s = state_;
    if (!playing_ || !s->show || !s->running)
        return false;
    const double now = (double)(SDL_GetTicks() - s->startTicks) / 1000.0;

    // Take every decoded frame that is due; the last one is the one to show.
    bool changed = false;
    while (s->next.valid && s->next.pts <= now) {
        std::swap(s->current, s->next);
        s->next.valid = false;
        changed = true;
        if (!s->decodeNext())
            break;
    }
    // The last frame is held for a moment, then the movie is over.
    if (!s->next.valid && s->eof && now > s->current.pts + 0.1) {
        skip();
        return false;
    }
    return changed;
}

const unsigned char *Player::frame(int *width, int *height) const
{
    const Decoded &d = state_->current;
    if (d.rgba.empty())
        return NULL;
    *width = d.width;
    *height = d.height;
    return d.rgba.data();
}

bool Player::handleWindowMessage(unsigned msg, unsigned long, long)
{
    if (msg != (unsigned)MOVIE_ENDED)
        return false;
    playing_ = false;
    return true;
}

}  // namespace videodev
