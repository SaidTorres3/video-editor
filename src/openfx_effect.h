#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OpenFxParameter
{
    std::string name;
    std::string label;
    std::string value;
    std::string hint;
};

// A CPU Filter-context host using the upstream OpenFX HostSupport library.
// Full-frame RGBA8, constant parameters, one source; no GPU or temporal fetch.
class OpenFxEffect
{
  public:
    OpenFxEffect(const std::wstring &binary, int pluginIndex, int width, int height,
                 double frameRate, double duration, double pixelAspect, std::atomic<bool> &cancel);
    ~OpenFxEffect();
    OpenFxEffect(const OpenFxEffect &) = delete;
    std::vector<std::string> PluginLabels() const;
    std::vector<OpenFxParameter> Parameters() const;
    void SetParameter(const std::string &name, const std::string &value);
    void Render(uint8_t *rgba, int stride, double timeInFrames);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
