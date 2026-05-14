#pragma once

#include <memory>
#include <string>
#include <vector>

struct VstParam {
    std::string name;
    uint32_t id;
    float value; // normalized 0..1
};

class VstHost {
  public:
    VstHost();
    ~VstHost();

    bool load(const std::string &path, double sampleRate, int blockSize);
    void unload();

    // Call from audio thread only
    void process(float *outLeft, float *outRight, int frames);

    // Thread-safe: call from any thread
    void noteOn(int pitch, float velocity, int channel = 0);
    void noteOff(int pitch, int channel = 0);

    bool openEditor();
    void closeEditor();
    bool isEditorOpen() const;

    bool isLoaded() const;
    const std::string &getPluginName() const;
    std::vector<VstParam> getParams() const;
    void setParam(uint32_t paramId, float normalizedValue);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
