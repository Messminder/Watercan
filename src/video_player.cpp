#include "video_player.h"
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <condition_variable>
#include <GL/gl.h>

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

#ifdef HAVE_FFMPEG
#include <libswresample/swresample.h>
#endif

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}
#endif

struct VideoPlayer::Impl {
#ifdef HAVE_FFMPEG
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    int videoStream = -1;
    SwsContext* sws = nullptr;

    // Audio support
    int audioStream = -1;
    AVCodecContext* dec_audio = nullptr;
    SwrContext* swr = nullptr; // reserved for future resampling, not guaranteed available
#ifdef HAVE_SDL2
    SDL_AudioDeviceID sdlDev = 0;
#endif

    // In-process audio bookkeeping
    int outSampleRate = 0;
    int outChannels = 0;
    int outBytesPerSample = 0; // bytes per sample per channel (eg 2 for S16)
    std::atomic<uint64_t> totalQueuedSamples{0}; // total samples queued to SDL since open
    double firstAudioPts = -1.0; // pts of first enqueued audio frame
    std::mutex audioMutex;

    // Preload buffer: decoded audio is accumulated here _before_ the user presses play
    std::vector<uint8_t> preloadedAudio;
    std::atomic<uint64_t> preloadedSamples{0};


    std::thread decodeThread;
    std::atomic<bool> threadRunning{false};
    std::atomic<bool> playing{false};
    std::mutex frameMutex;
    // Frame queue (max 8 frames buffer)
    struct FrameData {
        std::vector<unsigned char> data;
        double pts;
    };
    std::deque<FrameData> frameQueue;
    static constexpr size_t MAX_FRAME_QUEUE = 8;
    int frameW = 0;
    int frameH = 0;
    double framePts = 0.0;
    double lastFramePts = 0.0; // used for pacing
    double lastDisplayedPts = -1.0; // pts of last frame we actually displayed
    std::chrono::steady_clock::time_point lastDisplayTime; // wall-clock when we displayed it
    // GL texture
    unsigned int textureId = 0;
    int texWidth = 0;
    int texHeight = 0;
    std::atomic<bool> newFrameAvailable{false};
    std::condition_variable_any cv;

    // Playback timing
    std::chrono::steady_clock::time_point playStart;
    std::atomic<bool> hasPlayStart{false};

    // path provided to open()
    std::string path;

    // External audio fallback (uses ffplay if SDL or in-process audio not available)
    bool externalAudioRunning = false;
    std::string externalAudioCmd;
#endif
};

VideoPlayer::VideoPlayer() : m_impl(new Impl()) {}
VideoPlayer::~VideoPlayer() { close(); delete m_impl; }

bool VideoPlayer::open(const std::string& path) {
#ifdef HAVE_FFMPEG
    close();
    avformat_network_init();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) {
        fprintf(stderr, "[video] avformat_open_input failed for %s\n", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "[video] find_stream_info failed\n");
        avformat_close_input(&fmt);
        return false;
    }
    int vs = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vs = (int)i; }
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { m_impl->audioStream = (int)i; }
    }
    if (vs < 0) {
        fprintf(stderr, "[video] no video stream\n");
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecParameters* cp = fmt->streams[vs]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
    if (!codec) {
        fprintf(stderr, "[video] decoder not found\n");
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    if (!dec) { avformat_close_input(&fmt); return false; }
    if (avcodec_parameters_to_context(dec, cp) < 0) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }
    if (avcodec_open2(dec, codec, NULL) < 0) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    // If there is an audio stream, try to open its decoder and set up SDL/audio resampler
    if (m_impl->audioStream >= 0) {
        AVCodecParameters* ap = fmt->streams[m_impl->audioStream]->codecpar;
        const AVCodec* acodec = avcodec_find_decoder(ap->codec_id);
        if (acodec) {
            m_impl->dec_audio = avcodec_alloc_context3(acodec);
            if (!(m_impl->dec_audio && avcodec_parameters_to_context(m_impl->dec_audio, ap) >= 0 && avcodec_open2(m_impl->dec_audio, acodec, NULL) >= 0)) {
                if (m_impl->dec_audio) { avcodec_free_context(&m_impl->dec_audio); m_impl->dec_audio = nullptr; }
            }
        }

#ifdef HAVE_SDL2
        // If we successfully opened an audio decoder, attempt to initialize SDL device
        if (m_impl->dec_audio) {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
            int desired_rate = m_impl->dec_audio->sample_rate > 0 ? m_impl->dec_audio->sample_rate : 48000;
            // Determine channel count from codecpar if available
            AVCodecParameters* ap_audio = fmt->streams[m_impl->audioStream]->codecpar;
            int desired_ch = 2;
#if defined(AV_CODEC_PARAMETERS_HAS_CHANNELS)
            if (ap_audio && ap_audio->channels > 0) desired_ch = ap_audio->channels;
#elif defined(AVCODEC_HAVE_CH_LAYOUT)
            if (ap_audio) desired_ch = av_channel_layout_nb_channels(&ap_audio->ch_layout);
#endif
            SDL_AudioSpec want, have;
            SDL_zero(want);
            want.freq = desired_rate;
            want.format = AUDIO_S16SYS;
            want.channels = desired_ch;
            want.samples = 4096;
            want.callback = nullptr;
            SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (dev != 0) {
                m_impl->sdlDev = dev;
                m_impl->outSampleRate = have.freq;
                m_impl->outChannels = have.channels;
                m_impl->outBytesPerSample = SDL_AUDIO_BITSIZE(have.format) / 8;
                m_impl->totalQueuedSamples = 0;
                m_impl->firstAudioPts = -1.0;
            } else {
                fprintf(stderr, "[video] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            }
        }
#endif
    }

    m_impl->fmt = fmt;
    m_impl->dec = dec;
    m_impl->videoStream = vs;
    m_impl->sws = nullptr;
    m_impl->threadRunning = true;
    m_impl->playing = false;
    m_impl->lastFramePts = 0.0;
    m_impl->path = path;
    m_impl->hasPlayStart = false;

    // Start decode thread
    m_impl->decodeThread = std::thread([this](){
        AVPacket* pkt = av_packet_alloc();
        AVFrame frame; memset(&frame, 0, sizeof(frame));
        AVFrame rgba; memset(&rgba, 0, sizeof(rgba));
        while (m_impl->threadRunning) {
            if (av_read_frame(m_impl->fmt, pkt) < 0) {
                // EOF - sleep a bit and break
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                break;
            }
            if (pkt->stream_index == m_impl->videoStream) {
                if (avcodec_send_packet(m_impl->dec, pkt) == 0) {
                    while (avcodec_receive_frame(m_impl->dec, &frame) == 0) {
                        int w = frame.width;
                        int h = frame.height;
                        // setup sws if needed
                        if (!m_impl->sws || m_impl->frameW != w || m_impl->frameH != h) {
                            if (m_impl->sws) sws_freeContext(m_impl->sws);
                            m_impl->sws = sws_getContext(w, h, (AVPixelFormat)frame.format,
                                w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                            if (!m_impl->sws) {
                                fprintf(stderr, "[video] sws_getContext failed\n");
                                continue;
                            }
                            m_impl->frameW = w; m_impl->frameH = h;
                        }
                        // allocate rgba frame buffer
                        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
                        std::vector<unsigned char> buf(numBytes);
                        uint8_t* dst[4] = { buf.data(), NULL, NULL, NULL };
                        int dstLines[4] = { 4*w, 0,0,0 };
                        sws_scale(m_impl->sws, frame.data, frame.linesize, 0, h, dst, dstLines);

                        // compute pts in seconds
                        double pts = (frame.pts == AV_NOPTS_VALUE) ? 0.0 : frame.pts * av_q2d(m_impl->fmt->streams[m_impl->videoStream]->time_base);
                        m_impl->lastFramePts = pts;

                        // Push frame to queue - wait if full (but with timeout to not block audio)
                        bool pushed = false;
                        for (int retry = 0; retry < 50 && m_impl->threadRunning && !pushed; ++retry) {
                            {
                                std::lock_guard<std::mutex> lk(m_impl->frameMutex);
                                if (m_impl->frameQueue.size() < Impl::MAX_FRAME_QUEUE) {
                                    Impl::FrameData fd;
                                    fd.data.swap(buf);
                                    fd.pts = pts;
                                    m_impl->frameQueue.push_back(std::move(fd));
                                    m_impl->newFrameAvailable = true;
                                    pushed = true;
                                }
                            }
                            if (!pushed) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                            }
                        }
                        // If still not pushed after timeout, drop this frame (better than blocking audio)
                        if (!pushed) {
                            // Frame dropped due to full queue
                        }
                    }
                }
            } else if (m_impl->audioStream >= 0 && pkt->stream_index == m_impl->audioStream && m_impl->dec_audio) {
                if (avcodec_send_packet(m_impl->dec_audio, pkt) == 0) {
                    AVFrame af; memset(&af, 0, sizeof(af));
                    while (avcodec_receive_frame(m_impl->dec_audio, &af) == 0) {
                        // compute pts for audio frame
                        double apts = (af.pts == AV_NOPTS_VALUE) ? 0.0 : af.pts * av_q2d(m_impl->fmt->streams[m_impl->audioStream]->time_base);
#ifdef HAVE_SDL2
                        if (m_impl->sdlDev) {
                            // Basic conversions for common formats (avoid swresample dependency):
                            int ch = m_impl->outChannels > 0 ? m_impl->outChannels : 2;
                            int nb_samples = af.nb_samples;

                            auto enqueue_or_preload = [&](const void* data, int bytes){
                                std::lock_guard<std::mutex> al(m_impl->audioMutex);
                                if (!m_impl->playing) {
                                    // append to preload buffer (limit preload to ~2 sec)
                                    int maxPreload = m_impl->outSampleRate * m_impl->outChannels * 2 * 2;
                                    if ((int)m_impl->preloadedAudio.size() < maxPreload) {
                                        size_t old = m_impl->preloadedAudio.size();
                                        m_impl->preloadedAudio.resize(old + bytes);
                                        memcpy(m_impl->preloadedAudio.data() + old, data, bytes);
                                        m_impl->preloadedSamples += (uint64_t)nb_samples;
                                        if (m_impl->firstAudioPts < 0.0) m_impl->firstAudioPts = apts;
                                    }
                                } else {
                                    // flush any preload first
                                    if (!m_impl->preloadedAudio.empty()) {
                                        SDL_QueueAudio(m_impl->sdlDev, m_impl->preloadedAudio.data(), (int)m_impl->preloadedAudio.size());
                                        m_impl->totalQueuedSamples += m_impl->preloadedSamples.load();
                                        m_impl->preloadedAudio.clear(); m_impl->preloadedSamples = 0;
                                    }
                                    // Always queue audio - never drop frames (SDL consumes at correct rate)
                                    SDL_QueueAudio(m_impl->sdlDev, data, bytes);
                                    m_impl->totalQueuedSamples += (uint64_t)nb_samples;
                                    if (m_impl->firstAudioPts < 0.0) m_impl->firstAudioPts = apts;
                                }
                            };

                            // Interleaved S16
                            if (af.format == AV_SAMPLE_FMT_S16) {
                                int bytes = nb_samples * ch * 2;
                                enqueue_or_preload(af.data[0], bytes);
                            }
                            // Planar S16: interleave
                            else if (af.format == AV_SAMPLE_FMT_S16P) {
                                std::vector<int16_t> out(nb_samples * ch);
                                for (int i = 0; i < nb_samples; ++i) {
                                    for (int c = 0; c < ch; ++c) {
                                        int16_t* plane = (int16_t*)af.data[c];
                                        out[i*ch + c] = plane[i];
                                    }
                                }
                                enqueue_or_preload(out.data(), (int)out.size() * 2);
                            }
                            // Planar float -> S16 interleaved
                            else if (af.format == AV_SAMPLE_FMT_FLTP) {
                                std::vector<int16_t> out(nb_samples * ch);
                                for (int i = 0; i < nb_samples; ++i) {
                                    for (int c = 0; c < ch; ++c) {
                                        float* plane = (float*)af.data[c];
                                        int v = (int)std::lround(plane[i] * 32767.0f);
                                        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
                                        out[i*ch + c] = (int16_t)v;
                                    }
                                }
                                enqueue_or_preload(out.data(), (int)out.size() * 2);
                            }
                            // Interleaved float
                            else if (af.format == AV_SAMPLE_FMT_FLT) {
                                float* src = (float*)af.data[0];
                                std::vector<int16_t> out(nb_samples * ch);
                                for (int i = 0; i < nb_samples * ch; ++i) {
                                    int v = (int)std::lround(src[i] * 32767.0f);
                                    if (v > 32767) v = 32767; if (v < -32768) v = -32768;
                                    out[i] = (int16_t)v;
                                }
                                enqueue_or_preload(out.data(), (int)out.size() * 2);
                            } else {
                                // Unsupported format for in-process audio; drop (external ffplay fallback may be used)
                            }
                        }
#endif
                    }
                }
            }
            m_impl->cv.notify_all();

            // If not playing, block here until play is called; wait in small chunks so audio packets
            // can still be processed when playing is true.
            if (!m_impl->playing) {
                while (m_impl->threadRunning && !m_impl->playing) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            av_packet_unref(pkt);
        } // end while(m_impl->threadRunning)
        // stack AVFrame usage; no free required
        (void)frame; (void)rgba;
        av_packet_free(&pkt);
    });
    return true;
#else
    (void)path; fprintf(stderr, "[video] FFmpeg not available at compile time\n"); return false;
#endif
}

void VideoPlayer::close() {
#ifdef HAVE_FFMPEG
    if (!m_impl) return;
    m_impl->threadRunning = false;
    if (m_impl->decodeThread.joinable()) m_impl->decodeThread.join();
    if (m_impl->sws) { sws_freeContext(m_impl->sws); m_impl->sws = nullptr; }
    if (m_impl->swr) { m_impl->swr = nullptr; }
    if (m_impl->dec_audio) { avcodec_free_context(&m_impl->dec_audio); m_impl->dec_audio = nullptr; }
    if (m_impl->dec) { avcodec_free_context(&m_impl->dec); m_impl->dec = nullptr; }
    if (m_impl->fmt) { avformat_close_input(&m_impl->fmt); m_impl->fmt = nullptr; }
#ifdef HAVE_SDL2
    if (m_impl->sdlDev) { SDL_CloseAudioDevice(m_impl->sdlDev); m_impl->sdlDev = 0; }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
    // delete GL texture
    if (m_impl->textureId) { glDeleteTextures(1, &m_impl->textureId); m_impl->textureId = 0; }
    m_impl->frameQueue.clear();
    // Ensure external audio process is killed if it was started
    if (m_impl->externalAudioRunning) {
        system("pkill -f 'ffplay -nodisp' >/dev/null 2>&1 || true");
        m_impl->externalAudioRunning = false;
    }
#ifdef HAVE_SDL2
    // Close and cleanup in-process audio if active
    if (m_impl->sdlDev) {
        SDL_ClearQueuedAudio(m_impl->sdlDev);
        SDL_CloseAudioDevice(m_impl->sdlDev);
        m_impl->sdlDev = 0;
    }
    if (m_impl->swr) { /* swr not used in this build path */ m_impl->swr = nullptr; }
    m_impl->totalQueuedSamples = 0;
    m_impl->firstAudioPts = -1.0;
#endif
#else
    (void)m_impl;
#endif
}

bool VideoPlayer::isOpen() const {
#ifdef HAVE_FFMPEG
    return m_impl && m_impl->fmt != nullptr;
#else
    return false;
#endif
}

void VideoPlayer::play() {
#ifdef HAVE_FFMPEG
    if (!m_impl) return; 
    m_impl->playing = true;
    m_impl->lastDisplayedPts = -1.0; // reset frame pacing
#ifdef HAVE_SDL2
    if (m_impl->sdlDev) {
        // Wait for a small preload buffer if available to reduce jitter (default 200ms)
        int preload_ms = 200;
        if (const char* env = getenv("WATERCAN_PRELOAD_MS")) { try { preload_ms = std::max(0, std::stoi(env)); } catch(...) {} }
        if (m_impl->outSampleRate > 0 && m_impl->preloadedSamples.load() == 0) {
            int waited = 0;
            int minSamples = (m_impl->outSampleRate * preload_ms) / 1000;
            while (waited < 500 && m_impl->preloadedSamples.load() < (uint64_t)minSamples) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); waited += 10;
            }
        }
        // Flush any preloaded audio into SDL so playback starts smoothly
        {
            std::lock_guard<std::mutex> al(m_impl->audioMutex);
            if (!m_impl->preloadedAudio.empty()) {
                SDL_QueueAudio(m_impl->sdlDev, m_impl->preloadedAudio.data(), (int)m_impl->preloadedAudio.size());
                m_impl->totalQueuedSamples += m_impl->preloadedSamples.load();
                m_impl->preloadedAudio.clear();
                m_impl->preloadedSamples = 0;
            }
        }
        SDL_PauseAudioDevice(m_impl->sdlDev, 0); // resume audio playback
    }
#endif

    // If we have an audio stream but no in-process audio, try external ffplay fallback,
    // Launch ffplay first so we can align video timing to when audio is actually started.
    if (m_impl->audioStream >= 0 && !m_impl->sdlDev && !m_impl->externalAudioRunning) {
        // check if ffplay is available
        if (system("command -v ffplay >/dev/null 2>&1") == 0) {
            // launch ffplay to play audio-only in background; keep running until file closed/paused
            std::string p = m_impl->path.empty() ? std::string("../res/clippy.mp4") : m_impl->path;
            // sanitize path: remove control characters (< 0x20) and escape double quotes
            std::string p_sanitized; p_sanitized.reserve(p.size());
            for (unsigned char uc : p) {
                if (uc < 0x20) continue; // strip control chars including CR/LF
                char c = (char)uc;
                if (c == '"') { p_sanitized += '\\'; p_sanitized += '"'; }
                else p_sanitized += c;
            }
            std::string cmd = "ffplay -nodisp -autoexit -loglevel error \"" + p_sanitized + "\" >/dev/null 2>&1 &";
            int ret = system(cmd.c_str()); (void)ret;
            m_impl->externalAudioRunning = true;
            m_impl->externalAudioCmd = cmd;
        }
    }

    // If we started external audio, wait briefly for at least one decoded frame so
    // we can align video playStart to when audio actually begins (reduces A/V lag).
    if (m_impl->externalAudioRunning) {
        int waited = 0;
        while (waited < 250) { // wait up to 250ms
            {
                std::lock_guard<std::mutex> lk(m_impl->frameMutex);
                if (m_impl->newFrameAvailable && !m_impl->frameQueue.empty()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
    }

    // establish playStart based on current latest frame PTS to sync wall-clock; read pts under lock
    {
        auto now = std::chrono::steady_clock::now();
        double pts = 0.0;
        {
            std::lock_guard<std::mutex> lk(m_impl->frameMutex);
            pts = m_impl->framePts;
        }
        // If we're using external audio (ffplay) we apply a fixed small offset so the video
        // is shifted slightly earlier to match ffplay's startup latency. This is an empirical
        // correction; can be tuned later or replaced by in-process audio.
        // Allow an environment override for quick tuning without rebuild
        int ffplay_audio_lead_ms = 160; // default lead (ms) to compensate ffplay startup latency
        if (const char* env = getenv("WATERCAN_FFPLAY_LEAD_MS")) {
            try { ffplay_audio_lead_ms = std::max(0, std::stoi(env)); } catch(...) { }
        }
        if (m_impl->externalAudioRunning && !m_impl->sdlDev) {
            m_impl->playStart = now - std::chrono::milliseconds((int)(pts * 1000.0)) - std::chrono::milliseconds(ffplay_audio_lead_ms);
        } else {
            m_impl->playStart = now - std::chrono::milliseconds((int)(pts * 1000.0));
        }
        m_impl->hasPlayStart = true;
    }
    m_impl->cv.notify_all();
#else
    (void)m_impl;
#endif
}
void VideoPlayer::pause() {
#ifdef HAVE_FFMPEG
    if (!m_impl) return;
    m_impl->playing = false; m_impl->hasPlayStart = false;
    m_impl->lastDisplayedPts = -1.0; // reset frame pacing
    // pause SDL audio device if in use
#ifdef HAVE_SDL2
    if (m_impl->sdlDev) {
        SDL_PauseAudioDevice(m_impl->sdlDev, 1);
        // Clear queued audio so next play starts fresh
        SDL_ClearQueuedAudio(m_impl->sdlDev);
        m_impl->totalQueuedSamples = 0;
        m_impl->firstAudioPts = -1.0;
    }
#endif
    // stop external audio if running
    if (m_impl->externalAudioRunning) {
        // best-effort: kill ffplay instances playing our file
        system("pkill -f 'ffplay -nodisp' >/dev/null 2>&1 || true");
        m_impl->externalAudioRunning = false;
    }
#else
    (void)m_impl;
#endif
}
bool VideoPlayer::isPlaying() const {
#ifdef HAVE_FFMPEG
    return m_impl && m_impl->playing;
#else
    return false;
#endif
}

bool VideoPlayer::update() {
#ifdef HAVE_FFMPEG
    if (!m_impl) return false;
    
    std::vector<unsigned char> frameCopy;
    int w = 0, h = 0;
    double pts = 0.0;
    {
        std::lock_guard<std::mutex> lk(m_impl->frameMutex);
        if (!m_impl->newFrameAvailable || m_impl->frameQueue.empty()) return false;
        
        // Peek at front frame PTS for pacing decision
        pts = m_impl->frameQueue.front().pts;
        
        // Sync video to audio clock when available (audio plays at real-time)
        if (m_impl->playing) {
            double audioClock = -1.0;
#ifdef HAVE_SDL2
            if (m_impl->sdlDev && m_impl->firstAudioPts >= 0.0 && m_impl->outSampleRate > 0) {
                int queuedBytes = SDL_GetQueuedAudioSize(m_impl->sdlDev);
                int bytesPerFrame = m_impl->outBytesPerSample * m_impl->outChannels;
                uint64_t queuedSamplesNow = (bytesPerFrame > 0) ? (uint64_t)(queuedBytes / bytesPerFrame) : 0;
                uint64_t totalQueued = m_impl->totalQueuedSamples.load();
                uint64_t playedSamples = (totalQueued > queuedSamplesNow) ? (totalQueued - queuedSamplesNow) : 0;
                // Audio clock = first audio PTS + time played
                audioClock = m_impl->firstAudioPts + (double)playedSamples / (double)m_impl->outSampleRate;
            }
#endif
            if (audioClock >= 0.0) {
                // Sync video to audio: display frame when audio clock reaches its PTS
                if (pts > audioClock + 0.040) { // 40ms lookahead tolerance
                    return false; // frame is ahead of audio, wait
                }
                // If frame is way behind audio (>200ms), skip it later by not waiting
            } else if (m_impl->lastDisplayedPts >= 0.0) {
                // Fallback to wall-clock pacing if no audio clock
                auto now = std::chrono::steady_clock::now();
                double wallElapsed = std::chrono::duration<double>(now - m_impl->lastDisplayTime).count();
                double videoElapsed = pts - m_impl->lastDisplayedPts;
                
                // If there's a large gap (frames were dropped), resync immediately
                if (videoElapsed > 0.5) {
                    m_impl->lastDisplayedPts = pts;
                    m_impl->lastDisplayTime = now;
                    videoElapsed = 0.0;
                }
                
                if (videoElapsed > wallElapsed + 0.008) {
                    return false;
                }
            }
        }
        
        // Take the front frame from queue
        frameCopy = std::move(m_impl->frameQueue.front().data);
        pts = m_impl->frameQueue.front().pts;
        m_impl->frameQueue.pop_front();
        if (m_impl->frameQueue.empty()) {
            m_impl->newFrameAvailable = false;
        }
        w = m_impl->frameW;
        h = m_impl->frameH;
        m_impl->framePts = pts;
    }
    
    // Update display timing
    m_impl->lastDisplayedPts = pts;
    m_impl->lastDisplayTime = std::chrono::steady_clock::now();

    // upload to texture
    if (!m_impl->textureId) {
        glGenTextures(1, &m_impl->textureId);
        glBindTexture(GL_TEXTURE_2D, m_impl->textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameCopy.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        m_impl->texWidth = w; m_impl->texHeight = h;
    } else {
        // Resize texture if needed
        if (m_impl->texWidth != w || m_impl->texHeight != h) {
            glBindTexture(GL_TEXTURE_2D, m_impl->textureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameCopy.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            m_impl->texWidth = w; m_impl->texHeight = h;
        } else {
            glBindTexture(GL_TEXTURE_2D, m_impl->textureId);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, frameCopy.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    return true;
#else
    (void)m_impl; return false;
#endif
}

unsigned int VideoPlayer::getTextureId() const {
#ifdef HAVE_FFMPEG
    if (!m_impl) return 0; return m_impl->textureId;
#else
    (void)m_impl; return 0;
#endif
}
