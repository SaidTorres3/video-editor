#include "noise_reducer.h"
#include <cmath>
#include <algorithm>

NoiseReducer::NoiseReducer()
    : m_enabled(false), m_sensitivity(0.5f), m_sampleRate(44100), m_channels(2),
      m_windowSize(1024), m_profileFrames(20), m_framesCollected(0),
      m_profileReady(false), m_cfgFwd(nullptr), m_cfgInv(nullptr) {}

NoiseReducer::~NoiseReducer() {
    if (m_cfgFwd) kiss_fftr_free(m_cfgFwd);
    if (m_cfgInv) kiss_fftr_free(m_cfgInv);
}

void NoiseReducer::Initialize(int sampleRate, int channels) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    if (m_cfgFwd) kiss_fftr_free(m_cfgFwd);
    if (m_cfgInv) kiss_fftr_free(m_cfgInv);
    m_cfgFwd = kiss_fftr_alloc(m_windowSize, 0, nullptr, nullptr);
    m_cfgInv = kiss_fftr_alloc(m_windowSize, 1, nullptr, nullptr);
    m_window.resize(m_windowSize);
    for (int i = 0; i < m_windowSize; ++i)
        m_window[i] = 0.5f - 0.5f * cosf(2.0f * static_cast<float>(M_PI) * i / (m_windowSize - 1));
    m_input.resize(m_windowSize);
    m_output.resize(m_windowSize);
    m_freq.resize(m_windowSize / 2 + 1);
    m_noiseMag.assign(m_windowSize / 2 + 1, 0.0f);
    m_framesCollected = 0;
    m_profileReady = false;
}

void NoiseReducer::SetEnabled(bool enabled) { m_enabled = enabled; }
bool NoiseReducer::IsEnabled() const { return m_enabled; }

void NoiseReducer::SetSensitivity(float sensitivity) {
    m_sensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
}

float NoiseReducer::GetSensitivity() const { return m_sensitivity; }

void NoiseReducer::Process(int16_t* samples, int frameCount, int channels) {
    if (!m_enabled || channels != m_channels)
        return;
    int total = frameCount * channels;
    for (int pos = 0; pos + m_windowSize <= total; pos += m_windowSize) {
        ProcessWindow(samples + pos);
    }
}

void NoiseReducer::ProcessWindow(int16_t* samples) {
    for (int i = 0; i < m_windowSize; ++i)
        m_input[i] = samples[i] / 32768.0f * m_window[i];
    kiss_fftr(m_cfgFwd, m_input.data(), m_freq.data());
    for (size_t k = 0; k < m_freq.size(); ++k) {
        float mag = std::sqrt(m_freq[k].r * m_freq[k].r + m_freq[k].i * m_freq[k].i);
        if (!m_profileReady) {
            m_noiseMag[k] += mag;
        } else {
            float threshold = m_noiseMag[k] * (1.0f + m_sensitivity);
            if (mag < threshold) {
                m_freq[k].r = 0.0f;
                m_freq[k].i = 0.0f;
            } else {
                float scale = (mag - threshold) / mag;
                m_freq[k].r *= scale;
                m_freq[k].i *= scale;
            }
        }
    }
    if (!m_profileReady && ++m_framesCollected >= m_profileFrames) {
        for (size_t k = 0; k < m_noiseMag.size(); ++k)
            m_noiseMag[k] /= static_cast<float>(m_profileFrames);
        m_profileReady = true;
    }
    kiss_fftri(m_cfgInv, m_freq.data(), m_output.data());
    for (int i = 0; i < m_windowSize; ++i) {
        float v = m_output[i] / m_windowSize;
        v = std::clamp(v, -1.0f, 1.0f);
        samples[i] = static_cast<int16_t>(v * 32768.0f);
    }
}

