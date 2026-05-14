#include "piano_roll.hpp"
#include "vst/vst_host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void PianoRoll::play()
{
    if (playing) return;
    lastMs  = -1.0;
    playing = true;
}

void PianoRoll::stop(VstHost* host)
{
    playing = false;
    if (host) {
        for (int p : activeNotes) host->noteOff(p);
    }
    activeNotes.clear();
    lastMs = -1.0;
}

void PianoRoll::updatePlayback(VstHost& host, double nowMs)
{
    if (!playing) return;

    if (lastMs < 0.0) { lastMs = nowMs; return; }

    double elapsed    = (nowMs - lastMs) * 0.001;
    lastMs            = nowMs;
    double beatsPerSec = bpm / 60.0;

    double prevBeat = playheadBeat;
    double newBeat  = prevBeat + elapsed * beatsPerSec;

    // Loop
    if (looping && loopEnd > 0.0f && newBeat >= static_cast<double>(loopEnd)) {
        for (int p : activeNotes) host.noteOff(p);
        activeNotes.clear();
        newBeat  = std::fmod(newBeat, static_cast<double>(loopEnd));
        prevBeat = 0.0;
    }

    // Fire note-on / note-off events for notes crossing this window
    for (auto& note : notes) {
        double ns = note.startBeat;
        double ne = note.startBeat + note.lengthBeats;

        if (ns >= prevBeat && ns < newBeat) {
            // Start note only if not already active
            if (std::find(activeNotes.begin(), activeNotes.end(), note.pitch) ==
                activeNotes.end()) {
                host.noteOn(note.pitch, 0.75f);
                activeNotes.push_back(note.pitch);
            }
        }
        if (ne >= prevBeat && ne < newBeat) {
            host.noteOff(note.pitch);
            activeNotes.erase(
                std::remove(activeNotes.begin(), activeNotes.end(), note.pitch),
                activeNotes.end());
        }
    }

    playheadBeat = newBeat;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

void PianoRoll::drawHeader(ImDrawList* dl, ImVec2 org, ImVec2 totalSize)
{
    float headerRight = org.x + totalSize.x;

    // Background
    dl->AddRectFilled({org.x + kPianoW, org.y},
                      {headerRight,      org.y + kHeaderH},
                      IM_COL32(38, 38, 42, 255));

    // Loop shading
    if (loopEnd > 0.0f) {
        float x0 = org.x + kPianoW + beatToX(0.0f);
        float x1 = org.x + kPianoW + beatToX(loopEnd);
        x0 = std::max(x0, org.x + kPianoW);
        x1 = std::min(x1, headerRight);
        if (x1 > x0)
            dl->AddRectFilled({x0, org.y}, {x1, org.y + kHeaderH},
                              IM_COL32(60, 60, 90, 80));
    }

    // Beat labels
    float rollW = totalSize.x - kPianoW;
    float endBeat = xToBeat(rollW);

    float beatStep = 1.0f;
    if (hZoom < 20.0f) beatStep = 8.0f;
    else if (hZoom < 35.0f) beatStep = 4.0f;
    else if (hZoom < 60.0f) beatStep = 2.0f;

    float first = std::ceil(scrollBeat / beatStep) * beatStep;
    for (float b = first; b <= endBeat + beatStep; b += beatStep) {
        float x = org.x + kPianoW + beatToX(b);
        if (x < org.x + kPianoW || x > headerRight) continue;

        dl->AddLine({x, org.y + kHeaderH * 0.5f}, {x, org.y + kHeaderH},
                    IM_COL32(70, 70, 75, 255));

        char label[16];
        int bar  = static_cast<int>(b / 4.0f) + 1;
        int beat = static_cast<int>(std::fmod(b, 4.0f)) + 1;
        if (beatStep >= 4.0f)
            std::snprintf(label, sizeof(label), "%d", bar);
        else
            std::snprintf(label, sizeof(label), "%d.%d", bar, beat);

        dl->AddText({x + 3.0f, org.y + 3.0f}, IM_COL32(160, 160, 165, 255), label);
    }

    // Playhead triangle in header
    float ph = org.x + kPianoW + beatToX(static_cast<float>(playheadBeat));
    if (ph >= org.x + kPianoW && ph <= headerRight) {
        ImVec2 pts[3] = {
            {ph,        org.y + 2.0f},
            {ph - 5.0f, org.y + kHeaderH - 2.0f},
            {ph + 5.0f, org.y + kHeaderH - 2.0f}
        };
        dl->AddTriangleFilled(pts[0], pts[1], pts[2], IM_COL32(255, 90, 90, 220));
    }
}

// ---------------------------------------------------------------------------

void PianoRoll::drawPianoKeys(ImDrawList* dl, ImVec2 org, float h)
{
    float x0 = org.x, x1 = org.x + kPianoW;

    // Background
    dl->AddRectFilled(org, {x1, org.y + h}, IM_COL32(32, 32, 32, 255));

    int topP = static_cast<int>(std::ceil(scrollPitch)) + 1;
    int botP = static_cast<int>(yToPitch(h)) - 1;
    topP = std::clamp(topP, 0, 127);
    botP = std::clamp(botP, 0, 127);

    for (int p = botP; p <= topP; ++p) {
        float cy  = org.y + pitchToY(static_cast<float>(p));
        float yTop = cy - vZoom * 0.5f;
        float yBot = cy + vZoom * 0.5f;
        yTop = std::max(yTop, org.y);
        yBot = std::min(yBot, org.y + h);
        if (yBot <= yTop) continue;

        bool black = isBlackKey(p);
        float keyW = black ? kPianoW * 0.62f : kPianoW - 1.0f;
        ImU32 fill = black ? IM_COL32(28, 28, 28, 255) : IM_COL32(215, 215, 215, 255);
        dl->AddRectFilled({x0, yTop}, {x0 + keyW, yBot}, fill);
        dl->AddLine({x0, yBot}, {x0 + kPianoW, yBot}, IM_COL32(70, 70, 70, 255));

        // C label
        if (p % 12 == 0 && vZoom >= 7.0f) {
            char lbl[6];
            std::snprintf(lbl, sizeof(lbl), "C%d", p / 12 - 1);
            float ty = cy - ImGui::GetTextLineHeight() * 0.5f;
            ty = std::clamp(ty, org.y, org.y + h - ImGui::GetTextLineHeight());
            dl->AddText({x0 + 2.0f, ty}, IM_COL32(50, 50, 50, 255), lbl);
        }
    }

    // Right border
    dl->AddLine({x1, org.y}, {x1, org.y + h}, IM_COL32(60, 60, 60, 255));
}

// ---------------------------------------------------------------------------

void PianoRoll::drawGrid(ImDrawList* dl, ImVec2 rOrg, ImVec2 rSize)
{
    float endBeat = xToBeat(rSize.x);
    int   topP    = static_cast<int>(std::ceil(scrollPitch)) + 1;
    int   botP    = static_cast<int>(yToPitch(rSize.y)) - 1;
    topP = std::clamp(topP, 0, 127);
    botP = std::clamp(botP, 0, 127);

    // Horizontal row fills
    for (int p = botP; p <= topP; ++p) {
        float cy   = rOrg.y + pitchToY(static_cast<float>(p));
        float yTop = std::max(cy - vZoom * 0.5f, rOrg.y);
        float yBot = std::min(cy + vZoom * 0.5f, rOrg.y + rSize.y);
        if (yBot <= yTop) continue;

        ImU32 fill = isBlackKey(p) ? IM_COL32(33, 33, 36, 255)
                   : (p % 12 == 0) ? IM_COL32(48, 48, 56, 255)
                                   : IM_COL32(42, 42, 45, 255);
        dl->AddRectFilled({rOrg.x, yTop}, {rOrg.x + rSize.x, yBot}, fill);

        if (!isBlackKey(p))
            dl->AddLine({rOrg.x, yBot}, {rOrg.x + rSize.x, yBot},
                        IM_COL32(52, 52, 52, 255));
    }

    // Subdivision lines
    float sub = 0.25f;
    if (hZoom < 25.0f)      sub = 2.0f;
    else if (hZoom < 40.0f) sub = 1.0f;
    else if (hZoom < 70.0f) sub = 0.5f;

    float first = std::ceil(scrollBeat / sub) * sub;
    for (float b = first; b <= endBeat; b += sub) {
        float x = rOrg.x + beatToX(b);
        if (x < rOrg.x || x > rOrg.x + rSize.x) continue;

        float fmod4 = std::fmod(b, 4.0f);
        float fmod1 = std::fmod(b, 1.0f);
        bool isBar  = fmod4 < 0.001f || fmod4 > 3.999f;
        bool isBeat = fmod1 < 0.001f || fmod1 > 0.999f;

        ImU32 col = isBar  ? IM_COL32(85, 85, 90, 255)
                  : isBeat ? IM_COL32(65, 65, 68, 255)
                           : IM_COL32(52, 52, 55, 255);
        dl->AddLine({x, rOrg.y}, {x, rOrg.y + rSize.y}, col);
    }

    // Loop end marker
    if (loopEnd > 0.0f) {
        float x = rOrg.x + beatToX(loopEnd);
        if (x >= rOrg.x && x <= rOrg.x + rSize.x)
            dl->AddLine({x, rOrg.y}, {x, rOrg.y + rSize.y},
                        IM_COL32(100, 100, 200, 180), 1.5f);
    }
}

// ---------------------------------------------------------------------------

void PianoRoll::drawNotes(ImDrawList* dl, ImVec2 rOrg, ImVec2 rSize)
{
    float endBeat = xToBeat(rSize.x);

    for (auto& note : notes) {
        if (note.startBeat + note.lengthBeats < scrollBeat) continue;
        if (note.startBeat > endBeat) continue;

        float x0 = rOrg.x + beatToX(note.startBeat);
        float x1 = rOrg.x + beatToX(note.startBeat + note.lengthBeats) - 1.0f;
        float cy  = rOrg.y + pitchToY(static_cast<float>(note.pitch));
        float y0  = cy - vZoom * 0.5f + 0.5f;
        float y1  = cy + vZoom * 0.5f - 0.5f;

        x0 = std::max(x0, rOrg.x);
        x1 = std::min(x1, rOrg.x + rSize.x);
        y0 = std::max(y0, rOrg.y);
        y1 = std::min(y1, rOrg.y + rSize.y);
        if (x1 - x0 < 1.0f) x1 = x0 + 1.0f;
        if (y1 - y0 < 1.0f) y1 = y0 + 1.0f;

        bool active = std::find(activeNotes.begin(), activeNotes.end(), note.pitch)
                      != activeNotes.end();

        ImU32 fill   = active ? IM_COL32(110, 230, 110, 255)
                              : IM_COL32(72,  165,  72,  255);
        ImU32 border = active ? IM_COL32(160, 255, 160, 255)
                              : IM_COL32(50,  110,  50,  255);

        dl->AddRectFilled({x0, y0}, {x1, y1}, fill, 2.0f);
        dl->AddRect      ({x0, y0}, {x1, y1}, border, 2.0f);
    }
}

// ---------------------------------------------------------------------------

void PianoRoll::drawPlayhead(ImDrawList* dl, ImVec2 rOrg, ImVec2 rSize)
{
    float x = rOrg.x + beatToX(static_cast<float>(playheadBeat));
    if (x >= rOrg.x && x <= rOrg.x + rSize.x)
        dl->AddLine({x, rOrg.y}, {x, rOrg.y + rSize.y},
                    IM_COL32(255, 80, 80, 200), 2.0f);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void PianoRoll::handleInput(ImVec2 org, ImVec2 totalSize, VstHost& host)
{
    if (!ImGui::IsItemHovered()) { dragNote = nullptr; return; }

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   mouse  = io.MousePos;
    float    mx     = mouse.x - org.x;   // relative to whole widget
    float    my     = mouse.y - org.y;

    float rollX = mx - kPianoW;          // relative to roll content
    float rollY = my - kHeaderH;
    float rollW = totalSize.x - kPianoW;
    float rollH = totalSize.y - kHeaderH;

    bool inRoll   = rollX >= 0 && rollY >= 0 && rollX < rollW && rollY < rollH;
    bool inHeader = my >= 0 && my < kHeaderH && mx > kPianoW;

    // ── Scroll / zoom ──────────────────────────────────────────────────────
    if (io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            // Horizontal zoom centred on cursor
            float beatUnder = xToBeat(rollX);
            hZoom *= std::pow(1.12f, io.MouseWheel);
            hZoom  = std::clamp(hZoom, 10.0f, 600.0f);
            scrollBeat = beatUnder - rollX / hZoom;
            scrollBeat = std::max(scrollBeat, -0.5f);
        } else if (io.KeyShift) {
            // Horizontal pan
            scrollBeat -= io.MouseWheel * 2.0f;
            scrollBeat  = std::max(scrollBeat, -0.5f);
        } else {
            // Vertical zoom when Alt held, else vertical pan
            if (io.KeyAlt) {
                float pitchUnder = yToPitch(rollY);
                vZoom *= std::pow(1.12f, io.MouseWheel);
                vZoom  = std::clamp(vZoom, 4.0f, 40.0f);
                scrollPitch = pitchUnder + rollY / vZoom;
            } else {
                scrollPitch -= io.MouseWheel * 3.0f;
            }
            scrollPitch = std::clamp(scrollPitch, 12.0f, 127.0f);
        }
    }

    // ── Middle-mouse drag: pan ─────────────────────────────────────────────
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 d = io.MouseDelta;
        scrollBeat  -= d.x / hZoom;
        scrollPitch += d.y / vZoom;
        scrollBeat  = std::max(scrollBeat, -0.5f);
        scrollPitch = std::clamp(scrollPitch, 12.0f, 127.0f);
    }

    // ── Click in header: set playhead ─────────────────────────────────────
    if (inHeader && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        double newBeat = static_cast<double>(xToBeat(rollX));
        newBeat = std::max(0.0, newBeat);
        // Stop active notes before jump
        for (int p : activeNotes) host.noteOff(p);
        activeNotes.clear();
        playheadBeat = newBeat;
    }
    // Drag playhead in header
    if (inHeader && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        double newBeat = static_cast<double>(xToBeat(rollX));
        newBeat = std::max(0.0, newBeat);
        for (int p : activeNotes) host.noteOff(p);
        activeNotes.clear();
        playheadBeat = newBeat;
    }

    // ── Left-click in roll: place note ────────────────────────────────────
    if (inRoll && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float snap = io.KeyShift ? 0.125f : 0.25f;
        float beat  = std::round(xToBeat(rollX) / snap) * snap;
        beat  = std::max(beat, 0.0f);
        int pitch = static_cast<int>(std::round(yToPitch(rollY)));
        pitch = std::clamp(pitch, 0, 127);

        // Don't add if overlapping an existing note at same pitch
        bool overlap = false;
        for (auto& n : notes) {
            if (n.pitch == pitch &&
                beat < n.startBeat + n.lengthBeats &&
                beat + 0.25f > n.startBeat) {
                overlap = true; break;
            }
        }
        if (!overlap) {
            notes.push_back({pitch, beat, 0.25f});
            dragNote   = &notes.back();
            dragStartX = rollX;
        }
    }

    // Drag to extend placed note
    if (dragNote && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float snap   = io.KeyShift ? 0.125f : 0.25f;
        float endBt  = std::round(xToBeat(rollX) / snap) * snap;
        float len    = endBt - dragNote->startBeat;
        dragNote->lengthBeats = std::max(snap, len);
    } else {
        dragNote = nullptr;
    }

    // ── Right-click in roll: delete note ──────────────────────────────────
    if (inRoll && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        float beat = xToBeat(rollX);
        int pitch  = static_cast<int>(std::round(yToPitch(rollY)));

        for (auto it = notes.begin(); it != notes.end(); ++it) {
            if (it->pitch == pitch &&
                beat >= it->startBeat &&
                beat <  it->startBeat + it->lengthBeats) {
                host.noteOff(it->pitch);
                activeNotes.erase(
                    std::remove(activeNotes.begin(), activeNotes.end(), it->pitch),
                    activeNotes.end());
                notes.erase(it);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main draw entry
// ---------------------------------------------------------------------------

void PianoRoll::draw(ImVec2 size, VstHost& host, double nowMs)
{
    updatePlayback(host, nowMs);

    ImVec2     org = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Capture input via InvisibleButton
    ImGui::InvisibleButton("##pianoroll", size,
        ImGuiButtonFlags_MouseButtonLeft  |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    handleInput(org, size, host);

    // Clip to our area
    dl->PushClipRect(org, {org.x + size.x, org.y + size.y}, true);

    // Full background
    dl->AddRectFilled(org, {org.x + size.x, org.y + size.y},
                      IM_COL32(40, 40, 43, 255));

    ImVec2 rollOrg  = {org.x + kPianoW, org.y + kHeaderH};
    ImVec2 rollSize = {size.x - kPianoW, size.y - kHeaderH};

    drawGrid     (dl, rollOrg, rollSize);
    drawNotes    (dl, rollOrg, rollSize);
    drawPlayhead (dl, rollOrg, rollSize);
    drawPianoKeys(dl, {org.x, org.y + kHeaderH}, size.y - kHeaderH);
    drawHeader   (dl, org, size);

    // Corner fill over the piano+header intersection
    dl->AddRectFilled(org, {org.x + kPianoW, org.y + kHeaderH},
                      IM_COL32(30, 30, 30, 255));

    dl->PopClipRect();
}
