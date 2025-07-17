#pragma once
#include <vector>
#include <cstdint>
extern "C" {
#include "kiss_fftr.h"
}

class NoiseReducer {
public:
    NoiseReducer();
    ~NoiseReducer();
    void Initialize(int sampleRate, int channels);
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetSensitivity(float sensitivity); // 0..1
    float GetSensitivity() const;
    void Process(int16_t* samples, int frameCount, int channels);
private:
    void ProcessWindow(int16_t* samples);
    bool m_enabled;
    float m_sensitivity;
    int m_sampleRate;
    int m_channels;
    int m_windowSize;
    int m_profileFrames;
    int m_framesCollected;
    bool m_profileReady;
    kiss_fftr_cfg m_cfgFwd;
    kiss_fftr_cfg m_cfgInv;
    std::vector<float> m_window;
    std::vector<float> m_input;
    std::vector<float> m_output;
    std::vector<kiss_fft_cpx> m_freq;
    std::vector<float> m_noiseMag;
};
