#include "music_player.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

// We use stb_vorbis for OGG decoding. The implementation is provided in src/stb_vorbis.c
extern "C" {
    // stb_vorbis_decode_filename: returns number of samples per channel and writes malloc'd interleaved short* to *output
    int stb_vorbis_decode_filename(const char* filename, int* channels, int* sample_rate, short** output);
    // stb_vorbis_decode_memory: same as above but decodes from memory buffer
    int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);
} 

MusicPlayer::MusicPlayer() {}
MusicPlayer::~MusicPlayer() { unload(); }

bool MusicPlayer::load(const std::string& path) {
    unload();
    if (!std::filesystem::exists(path)) return false;

    int channels = 0;
    int sr = 0;
    short* data = nullptr;
    int len = stb_vorbis_decode_filename(path.c_str(), &channels, &sr, &data); // returns samples per channel and sets data
    if (!data || len <= 0 || channels <= 0 || sr <= 0) {
        if (data) free(data);
        return false;
    }

    auto convert_and_free = [&](short* dataPtr, int length, int ch) {
        m_sampleRate = sr;
        m_samples.clear();
        m_samples.reserve((size_t)length);
        for (int i = 0; i < length; ++i) {
            if (ch == 1) {
                m_samples.push_back(dataPtr[i] / 32768.0f);
            } else {
                int idx = i * ch;
                float sum = 0.0f;
                for (int c = 0; c < ch; ++c) sum += dataPtr[idx + c];
                m_samples.push_back((sum / ch) / 32768.0f);
            }
        }
        free(dataPtr);
    };

    convert_and_free(data, len, channels);


#ifdef HAVE_SDL2
    // Fill s16 buffer for playback (mono)
    m_s16buffer.resize(m_samples.size());
    for (size_t i = 0; i < m_samples.size(); ++i) {
        float v = m_samples[i];
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        m_s16buffer[i] = (int16_t)std::lround(v * 32767.0f);
    }
#endif

    m_playOffsetSamples = 0;
    m_playing = false;
    return true;
}

bool MusicPlayer::loadFromMemory(const unsigned char* mem, size_t len) {
    unload();
    if (!mem || len == 0) return false;

    int channels = 0;
    int sr = 0;
    short* data = nullptr;
    int samples = stb_vorbis_decode_memory(mem, (int)len, &channels, &sr, &data);
    if (!data || samples <= 0 || channels <= 0 || sr <= 0) {
        if (data) free(data);
        return false;
    }

    // Convert and free the decoded interleaved s16 buffer
    m_sampleRate = sr;
    m_samples.clear();
    m_samples.reserve((size_t)samples);
    for (int i = 0; i < samples; ++i) {
        if (channels == 1) m_samples.push_back(data[i] / 32768.0f);
        else {
            int idx = i * channels; float sum = 0; for (int c = 0; c < channels; ++c) sum += data[idx + c];
            m_samples.push_back((sum / channels) / 32768.0f);
        }
    }
    free(data);

#ifdef HAVE_SDL2
    // Fill s16 buffer for playback (mono)
    m_s16buffer.resize(m_samples.size());
    for (size_t i = 0; i < m_samples.size(); ++i) {
        float v = m_samples[i];
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        m_s16buffer[i] = (int16_t)std::lround(v * 32767.0f);
    }
#endif

    m_playOffsetSamples = 0;
    m_playing = false;
    return true;
}

void MusicPlayer::unload() {
    stop();
    m_samples.clear();
    m_sampleRate = 0;
#ifdef HAVE_SDL2
    if (m_dev) {
        SDL_CloseAudioDevice(m_dev);
        m_dev = 0;
    }
    m_s16buffer.clear();
#endif
}

bool MusicPlayer::hasAudio() const { return !m_samples.empty() && m_sampleRate > 0; }

bool MusicPlayer::play() {
    if (!hasAudio()) return false;
#ifdef HAVE_SDL2
    fprintf(stderr, "[music] play(): sampleRate=%d samples=%zu offset=%zu\n", m_sampleRate, m_s16buffer.size(), m_playOffsetSamples);
    if (!m_dev) {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = m_sampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 4096;
        want.callback = nullptr;
        m_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (!m_dev) {
            fprintf(stderr, "[music] SDL_OpenAudioDevice failed (mono): %s\n", SDL_GetError());
            // Try stereo as a fallback
            want.channels = 2;
            m_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
            if (!m_dev) {
                fprintf(stderr, "[music] SDL_OpenAudioDevice failed (stereo fallback): %s\n", SDL_GetError());
                return false;
            } else {
                fprintf(stderr, "[music] SDL_OpenAudioDevice succeeded with stereo fallback (dev=%u)\n", m_dev);
            }
        } else {
            fprintf(stderr, "[music] SDL_OpenAudioDevice succeeded (dev=%u)\n", m_dev);
        }
    }
    // Clear any queued audio and queue from current offset
    SDL_ClearQueuedAudio(m_dev);
    size_t remaining = m_s16buffer.size() - m_playOffsetSamples;
    if (remaining == 0) {
        // restart from beginning
        m_playOffsetSamples = 0;
        remaining = m_s16buffer.size();
    }
    if (remaining > 0) {
        const void* ptr = reinterpret_cast<const void*>(m_s16buffer.data() + m_playOffsetSamples);
        Uint32 bytes = (Uint32)(remaining * sizeof(int16_t));
        if (SDL_QueueAudio(m_dev, ptr, bytes) != 0) {
            fprintf(stderr, "[music] SDL_QueueAudio failed: %s\n", SDL_GetError());
            return false;
        } else {
            fprintf(stderr, "[music] queued %u bytes for playback\n", bytes);
        }
    } else {
        fprintf(stderr, "[music] no audio to queue (remaining=0)\n");
    }
    SDL_PauseAudioDevice(m_dev, 0); // start playback
    m_playing = true;
    return true;
#else
    // No audio backend; emulate playback state
    m_playing = true;
    return true;
#endif
}

void MusicPlayer::pause() {
#ifdef HAVE_SDL2
    if (m_dev) {
        // compute how many samples are left in queue and update play offset so resume continues
        Uint32 queued = SDL_GetQueuedAudioSize(m_dev);
        Uint32 totalBytes = (Uint32)(m_s16buffer.size() * sizeof(int16_t));
        // If queued >= totalBytes then nothing has been played yet (played = 0)
        Uint32 played = (queued < totalBytes) ? (totalBytes - queued) : 0;
        m_playOffsetSamples = (size_t)(played / sizeof(int16_t));
        SDL_PauseAudioDevice(m_dev, 1);
    }
    m_playing = false;
#else
    m_playing = false;
#endif
}

void MusicPlayer::stop() {
#ifdef HAVE_SDL2
    if (m_dev) {
        SDL_ClearQueuedAudio(m_dev);
        SDL_PauseAudioDevice(m_dev, 1);
    }
    m_playOffsetSamples = 0;
    m_playing = false;
#else
    m_playOffsetSamples = 0;
    m_playing = false;
#endif
}

bool MusicPlayer::isPlaying() const { return m_playing; }

double MusicPlayer::getDurationSeconds() const {
    if (!hasAudio()) return 0.0;
    return (double)m_samples.size() / (double)m_sampleRate;
}

double MusicPlayer::getPositionSeconds() const {
    if (!hasAudio()) return 0.0;
#ifdef HAVE_SDL2
    if (m_dev) {
        Uint32 queued = SDL_GetQueuedAudioSize(m_dev);
        Uint64 totalBytes = (Uint64)(m_s16buffer.size() * sizeof(int16_t));
        // If queued >= totalBytes then nothing has been played yet (played = 0)
        Uint64 played = (queued < totalBytes) ? (totalBytes - queued) : 0ULL;
        size_t playedSamples = (size_t)(played / sizeof(int16_t));
        return (double)playedSamples / (double)m_sampleRate;
    }
    return (double)m_playOffsetSamples / (double)m_sampleRate;
#else
    // Best-effort: report offset
    return (double)m_playOffsetSamples / (double)m_sampleRate;
#endif
}

void MusicPlayer::seekSeconds(double s) {
    if (!hasAudio()) return;
    if (s < 0) s = 0;
    double dur = getDurationSeconds();
    if (s > dur) s = dur;
    size_t target = (size_t)std::lround(s * m_sampleRate);
    if (target >= m_samples.size()) target = m_samples.size() ? m_samples.size()-1 : 0;
    m_playOffsetSamples = target;
#ifdef HAVE_SDL2
    if (m_dev) {
        SDL_ClearQueuedAudio(m_dev);
        size_t remaining = m_s16buffer.size() - m_playOffsetSamples;
        if (remaining > 0) {
            const void* ptr = m_s16buffer.data() + m_playOffsetSamples;
            Uint32 bytes = (Uint32)(remaining * sizeof(int16_t));
            SDL_QueueAudio(m_dev, ptr, bytes);
        }
        if (m_playing) SDL_PauseAudioDevice(m_dev, 0);
    }
#endif
}

void MusicPlayer::clearPlayback() {
#ifdef HAVE_SDL2
    if (m_dev) {
        SDL_ClearQueuedAudio(m_dev);
        SDL_PauseAudioDevice(m_dev, 1);
    }
#endif
    m_playing = false;
    m_playOffsetSamples = 0;
}
