#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();

    // Load an OGG file; returns true on success
    bool load(const std::string& path);
    // Unload any loaded audio
    void unload();

    bool hasAudio() const;

    // Playback control
    bool play();
    void pause();
    void stop();
    bool isPlaying() const;

    // Positioning
    double getDurationSeconds() const; // total duration
    double getPositionSeconds() const; // current playhead position
    void seekSeconds(double s);

    // Raw decoded samples (mono, normalized -1..1)
    const std::vector<float>& samples() const { return m_samples; }
    int sampleRate() const { return m_sampleRate; }

private:
    std::vector<float> m_samples; // mono
    int m_sampleRate = 0;

    bool m_playing = false;

// Audio device handle (opaque integer) - always present so header doesn't depend on SDL being available
    // When SDL2 is available, this will store the SDL_AudioDeviceID; otherwise it's unused.
    uintptr_t m_dev = 0;
    std::vector<int16_t> m_s16buffer; // interleaved mono S16
    // Current queued offset in samples (used for seeking)
    size_t m_playOffsetSamples = 0;

    // Helpers
    void clearPlayback();
};
