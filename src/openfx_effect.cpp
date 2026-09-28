#include "openfx_effect.h"
#include "debug_log.h"
#include "ofxhPluginCache.h"
#include "ofxhImageEffect.h"
#include "ofxhImageEffectAPI.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace
{
namespace IE = OFX::Host::ImageEffect;
namespace Param = OFX::Host::Param;
std::mutex hostMutex; // Upstream HostSupport has a process-wide host pointer.
void CheckOfx(OfxStatus status, const char *action)
{
    if (status != kOfxStatOK && status != kOfxStatReplyDefault)
        throw std::runtime_error(std::string("OpenFX ") + action + " failed (" +
                                 std::to_string(status) + ").");
}
OfxStatus Message(const char *type, const char *format, va_list args)
{
    char buffer[2048] = {};
    vsnprintf(buffer, sizeof(buffer), format, args);
    DebugLog(std::string("OpenFX: ") + buffer);
    return type && std::strcmp(type, kOfxMessageQuestion) == 0 ? kOfxStatReplyNo : kOfxStatOK;
}
const std::string byteDepth = kOfxBitDepthByte, rgbaComponents = kOfxImageComponentRGBA;
const std::string progressive = kOfxImageFieldNone, premult = kOfxImagePreMultiplied;

class Value : public Param::Instance, public Param::KeyframeParam
{
  public:
    int dimensions = 0;
    bool integer = false, string = false;
    std::vector<double> numbers;
    std::string text;
    Value(Param::Descriptor &d, Param::SetInstance *owner) : Instance(d, owner)
    {
        const auto &t = d.getType();
        string = t == kOfxParamTypeString;
        integer = t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean ||
                  t == kOfxParamTypeChoice || t == kOfxParamTypeInteger2D ||
                  t == kOfxParamTypeInteger3D;
        dimensions =
            t == kOfxParamTypeRGBA ? 4
            : (t == kOfxParamTypeRGB || t == kOfxParamTypeDouble3D || t == kOfxParamTypeInteger3D)
                ? 3
            : (t == kOfxParamTypeDouble2D || t == kOfxParamTypeInteger2D) ? 2
                                                                          : 1;
        auto &p = d.getProperties();
        if (string)
            text = p.getStringProperty(kOfxParamPropDefault);
        else
            for (int i = 0; i < dimensions; ++i)
                numbers.push_back(integer ? p.getIntProperty(kOfxParamPropDefault, i)
                                          : p.getDoubleProperty(kOfxParamPropDefault, i));
        getProperties().setIntProperty(kOfxParamPropAnimates, 0);
    }
    OfxStatus getV(va_list args) override
    {
        if (string)
            *va_arg(args, char **) = const_cast<char *>(text.c_str());
        else
            for (double v : numbers)
            {
                if (integer)
                    *va_arg(args, int *) = static_cast<int>(v);
                else
                    *va_arg(args, double *) = v;
            }
        return kOfxStatOK;
    }
    OfxStatus getV(OfxTime, va_list args) override
    {
        return getV(args);
    }
    OfxStatus setV(va_list args) override
    {
        if (string)
        {
            const char *v = va_arg(args, const char *);
            text = v ? v : "";
        }
        else
            for (double &v : numbers)
                v = integer ? va_arg(args, int) : va_arg(args, double);
        return kOfxStatOK;
    }
    OfxStatus setV(OfxTime, va_list) override
    {
        return kOfxStatErrUnsupported;
    }
    OfxStatus getNumKeys(unsigned int &keys) const override
    {
        keys = 0;
        return kOfxStatOK;
    }
    OfxStatus deleteAllKeys() override
    {
        return kOfxStatOK;
    }
    OfxStatus copyFrom(const Param::Instance &other, OfxTime, const OfxRangeD *) override
    {
        auto *value = dynamic_cast<const Value *>(&other);
        if (!value || value->getType() != getType())
            return kOfxStatErrValue;
        text = value->text;
        numbers = value->numbers;
        return kOfxStatOK;
    }
    OfxStatus deriveV(OfxTime, va_list args) override
    {
        if (string || integer)
            return kOfxStatErrUnsupported;
        for (int i = 0; i < dimensions; ++i)
            *va_arg(args, double *) = 0;
        return kOfxStatOK;
    }
    OfxStatus integrateV(OfxTime a, OfxTime b, va_list args) override
    {
        if (string || integer)
            return kOfxStatErrUnsupported;
        for (double v : numbers)
            *va_arg(args, double *) = v * (b - a);
        return kOfxStatOK;
    }
    std::string Display() const
    {
        if (string)
            return text;
        std::ostringstream out;
        out << std::setprecision(12);
        for (size_t i = 0; i < numbers.size(); ++i)
        {
            if (i)
                out << ' ';
            out << numbers[i];
        }
        return out.str();
    }
    void Set(const std::string &value)
    {
        if (string)
        {
            text = value;
            return;
        }
        std::istringstream input(value);
        auto changed = numbers;
        for (int i = 0; i < dimensions; ++i)
        {
            double v;
            if (!(input >> v) || !std::isfinite(v) || (integer && std::trunc(v) != v))
                throw std::runtime_error("Invalid value for " + getLabel());
            if (integer)
            {
                int low = getProperties().getIntProperty(kOfxParamPropMin, i);
                int high = getProperties().getIntProperty(kOfxParamPropMax, i);
                if (getType() == kOfxParamTypeBoolean)
                {
                    low = 0;
                    high = 1;
                }
                if (getType() == kOfxParamTypeChoice)
                {
                    low = 0;
                    high = getProperties().getDimension(kOfxParamPropChoiceOption) - 1;
                }
                if (v < low || v > high)
                    throw std::runtime_error("Value out of range for " + getLabel());
            }
            else
            {
                double low = getProperties().getDoubleProperty(kOfxParamPropMin, i);
                double high = getProperties().getDoubleProperty(kOfxParamPropMax, i);
                if (v < low || v > high)
                    throw std::runtime_error("Value out of range for " + getLabel());
            }
            changed[i] = v;
        }
        std::string extra;
        if (input >> extra)
            throw std::runtime_error("Too many values for " + getLabel());
        numbers = std::move(changed);
    }
};

struct FrameState
{
    int width, height;
    double rate, duration, aspect;
    std::atomic<bool> &cancel;
    double time = 0;
    uint8_t *source = nullptr;
    uint8_t *output = nullptr;
    int stride = 0;
};
class Clip : public IE::ClipInstance
{
    FrameState &state;
    bool output;

  public:
    Clip(IE::Instance *instance, IE::ClipDescriptor *descriptor, FrameState &s)
        : ClipInstance(instance, *descriptor), state(s),
          output(descriptor->getName() == kOfxImageEffectOutputClipName)
    {
    }
    const std::string &getUnmappedBitDepth() const override
    {
        return byteDepth;
    }
    const std::string &getUnmappedComponents() const override
    {
        return rgbaComponents;
    }
    const std::string &getPremult() const override
    {
        return premult;
    }
    const std::string &getFieldOrder() const override
    {
        return progressive;
    }
    double getAspectRatio() const override
    {
        return state.aspect;
    }
    double getFrameRate() const override
    {
        return state.rate;
    }
    double getUnmappedFrameRate() const override
    {
        return state.rate;
    }
    void getFrameRange(double &start, double &end) const override
    {
        start = 0;
        end = std::max(0.0, state.duration * state.rate - 1);
    }
    void getUnmappedFrameRange(double &a, double &b) const override
    {
        getFrameRange(a, b);
    }
    bool getConnected() const override
    {
        return output || getName() == kOfxImageEffectSimpleSourceClipName;
    }
    bool getContinuousSamples() const override
    {
        return false;
    }
    OfxRectD getRegionOfDefinition(OfxTime) const override
    {
        return {0, 0, state.width * state.aspect, static_cast<double>(state.height)};
    }
    IE::Image *getImage(OfxTime time, const OfxRectD *) override
    {
        if (!getConnected() || !state.source || std::abs(time - state.time) > 1e-5 ||
            state.cancel.load())
            return nullptr;
        OfxRectI bounds = {0, 0, state.width, state.height};
        // OFX's origin is bottom-left; FFmpeg buffers run from top to bottom.
        auto *data = (output ? state.output : state.source) + (state.height - 1) * state.stride;
        return new IE::Image(*this, 1, 1, data, bounds, bounds, -state.stride, progressive,
                             getName() + std::to_string(time));
    }
};
class Instance : public IE::Instance
{
  public:
    FrameState &state;
    Instance(IE::ImageEffectPlugin *p, IE::Descriptor &d, const std::string &context, FrameState &s)
        : IE::Instance(p, d, context, false), state(s)
    {
    }
    const std::string &getDefaultOutputFielding() const override
    {
        return progressive;
    }
    IE::ClipInstance *newClipInstance(IE::Instance *instance, IE::ClipDescriptor *d, int) override
    {
        return new Clip(instance, d, state);
    }
    OfxStatus vmessage(const char *t, const char *, const char *f, va_list a) override
    {
        return Message(t, f, a);
    }
    OfxStatus setPersistentMessage(const char *t, const char *id, const char *f, va_list a) override
    {
        return vmessage(t, id, f, a);
    }
    OfxStatus clearPersistentMessage() override
    {
        return kOfxStatOK;
    }
    void getProjectSize(double &x, double &y) const override
    {
        x = state.width * state.aspect;
        y = state.height;
    }
    void getProjectExtent(double &x, double &y) const override
    {
        getProjectSize(x, y);
    }
    void getProjectOffset(double &x, double &y) const override
    {
        x = y = 0;
    }
    double getProjectPixelAspectRatio() const override
    {
        return state.aspect;
    }
    double getEffectDuration() const override
    {
        return state.duration * state.rate;
    }
    double getFrameRate() const override
    {
        return state.rate;
    }
    double getFrameRecursive() const override
    {
        return state.time;
    }
    void getRenderScaleRecursive(double &x, double &y) const override
    {
        x = y = 1;
    }
    int abort() override
    {
        return state.cancel.load();
    }
    Param::Instance *newParam(const std::string &, Param::Descriptor &d) override
    {
        auto &t = d.getType();
        if (t == kOfxParamTypeGroup)
            return new Param::GroupInstance(d, this);
        if (t == kOfxParamTypePage)
            return new Param::PageInstance(d, this);
        if (t == kOfxParamTypePushButton)
            return new Param::Instance(d, this);
        if (t == kOfxParamTypeInteger || t == kOfxParamTypeDouble || t == kOfxParamTypeBoolean ||
            t == kOfxParamTypeChoice || t == kOfxParamTypeRGB || t == kOfxParamTypeRGBA ||
            t == kOfxParamTypeDouble2D || t == kOfxParamTypeDouble3D ||
            t == kOfxParamTypeInteger2D || t == kOfxParamTypeInteger3D || t == kOfxParamTypeString)
            return new Value(d, this);
        throw std::runtime_error("Unsupported OpenFX parameter: " + t);
    }
    OfxStatus editBegin(const std::string &) override
    {
        return kOfxStatOK;
    }
    OfxStatus editEnd() override
    {
        return kOfxStatOK;
    }
    void progressStart(const std::string &, const std::string &) override
    {
    }
    void progressEnd() override
    {
    }
    bool progressUpdate(double) override
    {
        return !state.cancel.load();
    }
    double timeLineGetTime() override
    {
        return state.time;
    }
    void timeLineGotoTime(double) override
    {
    }
    void timeLineGetBounds(double &a, double &b) override
    {
        a = 0;
        b = std::max(0.0, getEffectDuration() - 1);
    }
};
class Host : public IE::Host
{
  public:
    Host()
    {
        _properties.setStringProperty(kOfxPropName, "org.video-editor.openfx");
        _properties.setStringProperty(kOfxPropLabel, "Video Editor");
        _properties.setIntProperty(kOfxPropAPIVersion, 1, 0);
        _properties.setIntProperty(kOfxPropAPIVersion, 4, 1);
        _properties.setIntProperty(kOfxImageEffectHostPropIsBackground, 1);
        _properties.setIntProperty(kOfxImageEffectInstancePropSequentialRender, 1);
        _properties.setStringProperty(kOfxImageEffectPropSupportedContexts,
                                      kOfxImageEffectContextFilter, 0);
        _properties.setStringProperty(kOfxImageEffectPropSupportedComponents,
                                      kOfxImageComponentRGBA, 0);
        _properties.setStringProperty(kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthByte, 0);
        for (const char *name :
             {kOfxImageEffectPropSupportsOverlays, kOfxImageEffectPropSupportsMultiResolution,
              kOfxImageEffectPropSupportsTiles, kOfxImageEffectPropTemporalClipAccess,
              kOfxImageEffectPropSupportsMultipleClipDepths,
              kOfxImageEffectPropSupportsMultipleClipPARs, kOfxParamHostPropSupportsCustomInteract,
              kOfxParamHostPropSupportsStringAnimation, kOfxParamHostPropSupportsChoiceAnimation,
              kOfxParamHostPropSupportsBooleanAnimation, kOfxParamHostPropSupportsCustomAnimation})
            _properties.setIntProperty(name, 0);
    }
    IE::Instance *newInstance(void *data, IE::ImageEffectPlugin *p, IE::Descriptor &d,
                              const std::string &c) override
    {
        return new Instance(p, d, c, *static_cast<FrameState *>(data));
    }
    IE::Descriptor *makeDescriptor(IE::ImageEffectPlugin *p) override
    {
        return new IE::Descriptor(p);
    }
    IE::Descriptor *makeDescriptor(const IE::Descriptor &d, IE::ImageEffectPlugin *p) override
    {
        return new IE::Descriptor(d, p);
    }
    IE::Descriptor *makeDescriptor(const std::string &path, IE::ImageEffectPlugin *p) override
    {
        return new IE::Descriptor(path, p);
    }
    OfxStatus vmessage(const char *t, const char *, const char *f, va_list a) override
    {
        return Message(t, f, a);
    }
    OfxStatus setPersistentMessage(const char *t, const char *id, const char *f, va_list a) override
    {
        return vmessage(t, id, f, a);
    }
    OfxStatus clearPersistentMessage() override
    {
        return kOfxStatOK;
    }
};
} // namespace

struct OpenFxEffect::Impl
{
    std::unique_lock<std::mutex> lock{hostMutex};
    Host host;
    IE::PluginCache api{host};
    OFX::Host::PluginCache cache;
    FrameState state;
    std::unique_ptr<OFX::Host::PluginBinary> binary;
    std::unique_ptr<Instance> instance;
    std::vector<uint8_t> output;
    bool begun = false;
    Impl(int w, int h, double rate, double duration, double aspect, std::atomic<bool> &cancel)
        : state{w, h, rate, duration, aspect, cancel}
    {
        api.registerInCache(cache);
    }
    ~Impl()
    {
        if (begun)
            instance->endRenderAction(0, state.duration * state.rate, 1, false, {1, 1}, true,
                                      false);
    }
};

OpenFxEffect::OpenFxEffect(const std::wstring &file, int pluginIndex, int w, int h, double rate,
                           double duration, double aspect, std::atomic<bool> &cancel)
    : impl(std::make_unique<Impl>(w, h, rate, duration, aspect, cancel))
{
    if (w <= 0 || h <= 0 || rate <= 0 || aspect <= 0)
        throw std::runtime_error("Invalid video format for OpenFX.");
    // Upstream Windows loader accepts ANSI paths; reject lossy conversion explicitly.
    BOOL lossy = FALSE;
    int count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, file.data(), (int)file.size(),
                                    nullptr, 0, nullptr, &lossy);
    std::string path(count, '\0');
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, file.data(), (int)file.size(), path.data(),
                        count, nullptr, &lossy);
    if (lossy)
        throw std::runtime_error(
            "Move the OpenFX bundle to a path supported by the Windows system code page.");
    auto bundle = std::filesystem::path(path).parent_path().parent_path().parent_path().string();
    impl->binary = std::make_unique<OFX::Host::PluginBinary>(path, bundle, &impl->cache);
    if (impl->binary->isInvalid() || impl->binary->getNPlugins() == 0)
        throw std::runtime_error("No OpenFX image effects found in this Windows x64 binary.");
    if (pluginIndex < 0)
        return;
    if (pluginIndex >= impl->binary->getNPlugins())
        throw std::runtime_error("Invalid OpenFX effect selection.");
    auto *plugin = dynamic_cast<IE::ImageEffectPlugin *>(&impl->binary->getPlugin(pluginIndex));
    if (!plugin || !plugin->getPluginHandle() ||
        !plugin->getContexts().count(kOfxImageEffectContextFilter))
        throw std::runtime_error("This effect does not support the OpenFX Filter context.");
    auto *desc = plugin->getContext(kOfxImageEffectContextFilter);
    if (!desc)
        throw std::runtime_error("OpenFX effect description failed.");
    bool byteSupported = false;
    auto &props = desc->getProps();
    for (int i = 0; i < props.getDimension(kOfxImageEffectPropSupportedPixelDepths); ++i)
        byteSupported |=
            props.getStringProperty(kOfxImageEffectPropSupportedPixelDepths, i) == byteDepth;
    if (!byteSupported || props.getIntProperty(kOfxImageEffectPropTemporalClipAccess))
        throw std::runtime_error(
            "This host supports CPU RGBA 8-bit effects without temporal frame access.");
    if (!desc->getClips().count(kOfxImageEffectSimpleSourceClipName) ||
        !desc->getClips().count(kOfxImageEffectOutputClipName))
        throw std::runtime_error("The effect must expose Source and Output clips.");
    for (const auto &clip : desc->getClips())
    {
        if (clip.first != kOfxImageEffectSimpleSourceClipName &&
            clip.first != kOfxImageEffectOutputClipName &&
            !clip.second->getProps().getIntProperty(kOfxImageClipPropOptional))
            throw std::runtime_error("This effect requires additional input clips.");
    }
    impl->instance.reset(new Instance(plugin, *desc, kOfxImageEffectContextFilter, impl->state));
    CheckOfx(impl->instance->populate(), "populate instance");
    CheckOfx(impl->instance->createInstanceAction(), "create instance");
    if (!impl->instance->getClipPreferences())
        throw std::runtime_error("OpenFX clip preferences are incompatible.");
    for (const char *name : {kOfxImageEffectSimpleSourceClipName, kOfxImageEffectOutputClipName})
    {
        auto *clip = impl->instance->getClip(name);
        if (clip->getPixelDepth() != byteDepth || clip->getComponents() != rgbaComponents)
            throw std::runtime_error(
                "The effect requires an unsupported image format; only RGBA8 is available.");
    }
}
OpenFxEffect::~OpenFxEffect() = default;
std::vector<std::string> OpenFxEffect::PluginLabels() const
{
    std::vector<std::string> labels;
    for (int i = 0; i < impl->binary->getNPlugins(); ++i)
        labels.push_back(impl->binary->getPlugin(i).getIdentifier());
    return labels;
}
std::vector<OpenFxParameter> OpenFxEffect::Parameters() const
{
    std::vector<OpenFxParameter> result;
    if (!impl->instance)
        return result;
    for (const auto &pair : impl->instance->getParams())
    {
        auto *value = dynamic_cast<Value *>(pair.second);
        if (!value || value->getProperties().getIntProperty(kOfxParamPropSecret))
            continue;
        std::string hint = value->getProperties().getStringProperty(kOfxParamPropHint);
        if (value->getType() == kOfxParamTypeChoice)
        {
            for (int i = 0; i < value->getProperties().getDimension(kOfxParamPropChoiceOption); ++i)
                hint += " " + std::to_string(i) + "=" +
                        value->getProperties().getStringProperty(kOfxParamPropChoiceOption, i);
        }
        else if (value->dimensions > 1)
            hint += " Enter " + std::to_string(value->dimensions) + " space-separated numbers.";
        result.push_back({pair.first, value->getLabel(), value->Display(), hint});
    }
    return result;
}
void OpenFxEffect::SetParameter(const std::string &name, const std::string &text)
{
    auto params = impl->instance->getParams();
    auto found = params.find(name);
    auto *value = found == params.end() ? nullptr : dynamic_cast<Value *>(found->second);
    if (!value)
        throw std::runtime_error("Unknown OpenFX parameter.");
    value->Set(text);
    CheckOfx(impl->instance->beginInstanceChangedAction(kOfxChangeUserEdited),
             "begin parameter change");
    CheckOfx(impl->instance->paramInstanceChangedAction(name, kOfxChangeUserEdited, 0, {1, 1}),
             "change parameter");
    CheckOfx(impl->instance->endInstanceChangedAction(kOfxChangeUserEdited),
             "end parameter change");
}
void OpenFxEffect::Render(uint8_t *rgba, int stride, double time)
{
    if (!impl->instance)
        throw std::runtime_error("No OpenFX effect selected.");
    auto &state = impl->state;
    if (!rgba || stride < state.width * 4 || !std::isfinite(time))
        throw std::runtime_error("Invalid OpenFX input frame.");
    state.time = time;
    state.source = rgba;
    state.stride = stride;
    impl->output.assign(static_cast<size_t>(stride) * state.height, 0);
    state.output = impl->output.data();
    if (!impl->begun)
    {
        CheckOfx(impl->instance->beginRenderAction(0, state.duration * state.rate, 1, false, {1, 1},
                                                   true, false),
                 "begin render");
        impl->begun = true;
    }
    OfxStatus status = impl->instance->renderAction(
        time, progressive, {0, 0, state.width, state.height}, {1, 1}, true, false, false);
    state.source = nullptr;
    if (status != kOfxStatOK)
        throw std::runtime_error("OpenFX render failed (" + std::to_string(status) + ").");
    std::memcpy(rgba, impl->output.data(), impl->output.size());
}
