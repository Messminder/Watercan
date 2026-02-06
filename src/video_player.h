#pragma once
#include <string>

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool open(const std::string& path); // open file (does not start playing)
    void close();
    bool isOpen() const;

    void play();
    void pause();
    bool isPlaying() const;

    // Call from main thread each frame to upload decoded frame to GL texture
    // and perform maintenance; returns true if a new texture is available
    bool update();

    // Returns GL texture id to render with ImGui::Image (0 if none)
    unsigned int getTextureId() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};