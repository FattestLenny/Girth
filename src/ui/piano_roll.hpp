#pragma once
#include "imgui.h"
#include <vector>

class VstHost;

class PianoRoll {
  public:
    struct Note {
        int pitch;
        float startBeat;
        float lengthBeats;
    };

    std::vector<Note> notes;
    double bpm    = 120.0;
    float loopEnd = 8.0f;
    bool looping  = true;

    // Call every frame; nowMs = SDL_GetTicks()
    void draw(ImVec2 size, VstHost &host, double nowMs);

    void play();
    void stop(VstHost *host = nullptr);
    void togglePlay(VstHost *host = nullptr) {
        playing ? stop(host) : play();
    }
    bool isPlaying() const {
        return playing;
    }
    double getPlayheadBeat() const {
        return playheadBeat;
    }
    void setPlayheadBeat(double beat) {
        this->playheadBeat = beat;
    }

  private:
    // View state
    float scrollBeat  = -0.5f;
    float scrollPitch = 84.5f; // pitch at top of view
    float hZoom       = 80.0f; // px / beat
    float vZoom       = 14.0f; // px / semitone

    // Playback
    bool playing        = false;
    double playheadBeat = 0.0;
    double lastMs       = -1.0;

    std::vector<int> activeNotes; // pitches currently sounding

    // Drag-to-place state
    Note *dragNote   = nullptr; // note being stretched
    float dragStartX = 0;

    static constexpr float kPianoW  = 52.0f;
    static constexpr float kHeaderH = 20.0f;

    float beatToX(float beat) const {
        return (beat - scrollBeat) * hZoom;
    }
    float pitchToY(float p) const {
        return (scrollPitch - p) * vZoom;
    }
    float xToBeat(float x) const {
        return scrollBeat + x / hZoom;
    }
    float yToPitch(float y) const {
        return scrollPitch - y / vZoom;
    }

    static bool isBlackKey(int pitch) {
        int m = pitch % 12;
        return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
    }

    void updatePlayback(VstHost &host, double nowMs);
    void drawHeader(ImDrawList *dl, ImVec2 org, ImVec2 totalSize);
    void drawPianoKeys(ImDrawList *dl, ImVec2 org, float h);
    void drawGrid(ImDrawList *dl, ImVec2 rOrg, ImVec2 rSize);
    void drawNotes(ImDrawList *dl, ImVec2 rOrg, ImVec2 rSize);
    void drawPlayhead(ImDrawList *dl, ImVec2 rOrg, ImVec2 rSize);
    void handleInput(ImVec2 org, ImVec2 totalSize, VstHost &host);
};
