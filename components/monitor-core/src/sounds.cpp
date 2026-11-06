#include "monitor/sounds.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace monitor {
namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPeak  = 0.80f;   // of full scale, after mixing

// A buffer of float samples that notes are mixed into, scaled to 16 bits
// at the end
class Synth {
public:
    explicit Synth(uint32_t rate) : rate_(static_cast<float>(rate)) {}

    // A note from `start` lasting `len` seconds: `freq(u, sec)` is its
    // pitch at fraction u of the note (sec seconds into it), `wave(phase,
    // sec)` its shape (-1..1); linear attack and release ramps
    template <class Freq, class Wave>
    void note(float start, float len, float gain, float attack, float release, Freq freq, Wave wave){
        const size_t i0 = index(start);
        const size_t n  = index(len);
        if (n == 0) return;
        if (buf_.size() < i0 + n) buf_.resize(i0 + n, 0.0f);
        float phase = 0.0f;
        for (size_t k = 0; k < n; ++k){
            const float sec = static_cast<float>(k) / rate_;
            const float u   = static_cast<float>(k) / static_cast<float>(n);
            phase += kTwoPi * freq(u, sec) / rate_;
            if (phase >= kTwoPi) phase -= kTwoPi;
            float env = 1.0f;
            if (sec < attack) env = sec / attack;
            const float tail = static_cast<float>(n - k) / rate_;
            if (tail < release) env = std::min(env, tail / release);
            buf_[i0 + k] += gain * env * wave(phase, sec);
        }
    }

    std::vector<int16_t> finish() const {
        std::vector<int16_t> out(buf_.size(), 0);
        float peak = 0.0f;
        for (float v : buf_) peak = std::max(peak, std::fabs(v));
        if (peak <= 0.0f) return out;
        const float scale = kPeak * 32767.0f / peak;
        const size_t fade = index(0.002f);
        const size_t last = buf_.size() - 1;
        for (size_t i = 0; i < buf_.size(); ++i){
            float v = buf_[i] * scale;
            // 2 ms edges, so nothing clicks at either end
            if (fade > 0 && i < fade)         v *= static_cast<float>(i) / static_cast<float>(fade);
            if (fade > 0 && last - i < fade)  v *= static_cast<float>(last - i) / static_cast<float>(fade);
            out[i] = static_cast<int16_t>(std::lround(v));
        }
        return out;
    }

private:
    size_t index(float sec) const { return static_cast<size_t>(sec * rate_ + 0.5f); }

    float rate_;
    std::vector<float> buf_;
};

// ---- pitch ----

// A fixed pitch
auto steady(float f){
    return [f](float, float) { return f; };
}

// From f0 to f1, evenly in pitch; a `curve` under 1 climbs fast, then slows
auto glide(float f0, float f1, float curve = 1.0f){
    return [=](float u, float) { return f0 * std::pow(f1 / f0, std::pow(u, curve)); };
}

// ---- shapes ----

float sine(float p, float) { return std::sin(p); }

// Odd harmonics, edges rounded: a square that does not rasp
float softSquare(float p, float) { return std::tanh(2.2f * std::sin(p)) / std::tanh(2.2f); }

// A sawtooth from its first six partials: a buzz
float buzz(float p, float){
    float s = 0.0f;
    for (int k = 1; k <= 6; ++k) s += std::sin(static_cast<float>(k) * p) / static_cast<float>(k);
    return 0.6f * s;
}

// A computer's voice: a sine with a little of its octave
float chirp(float p, float) { return std::sin(p) + 0.3f * std::sin(2.0f * p); }

// A klaxon: the first four partials, overdriven
float klaxon(float p, float){
    const float x = std::sin(p) + 0.5f * std::sin(2.0f * p) + 0.35f * std::sin(3.0f * p) + 0.2f * std::sin(4.0f * p);
    return std::tanh(1.6f * x);
}

// ---- anime ----

// Three bursts of three sharp beeps, high over a softer octave below
std::vector<int16_t> animeAttention(uint32_t rate){
    Synth s(rate);
    for (int g = 0; g < 3; ++g){
        for (int b = 0; b < 3; ++b){
            const float t = 0.32f * static_cast<float>(g) + 0.075f * static_cast<float>(b);
            s.note(t, 0.05f, 1.0f, 0.003f, 0.012f, steady(2093.0f), softSquare);
            s.note(t, 0.05f, 0.3f, 0.003f, 0.012f, steady(1046.5f), sine);
        }
    }
    return s.finish();
}

// Two wails down, buzzing
std::vector<int16_t> animeFailure(uint32_t rate){
    Synth s(rate);
    for (int i = 0; i < 2; ++i){
        s.note(0.5f * static_cast<float>(i), 0.44f, 1.0f, 0.01f, 0.06f, glide(1250.0f, 420.0f), buzz);
    }
    return s.finish();
}

// ---- lcars ----

// The red-alert klaxon: three whoops climbing from 340 to 1020 Hz, fast at
// first, with a quieter fifth above for the metallic edge
std::vector<int16_t> lcarsAttention(uint32_t rate){
    Synth s(rate);
    for (int i = 0; i < 3; ++i){
        const float t = 0.72f * static_cast<float>(i);
        s.note(t, 0.56f, 1.0f, 0.012f, 0.07f, glide(340.0f, 1020.0f, 0.6f), klaxon);
        s.note(t, 0.56f, 0.22f, 0.012f, 0.07f, glide(510.0f, 1530.0f, 0.6f), sine);
    }
    return s.finish();
}

// "Unable to comply": three notes falling, the last held
std::vector<int16_t> lcarsFailure(uint32_t rate){
    Synth s(rate);
    const float freq[3] = {1175.0f, 880.0f, 587.0f};
    const float len[3]  = {0.07f, 0.07f, 0.2f};
    float t = 0.0f;
    for (int i = 0; i < 3; ++i){
        s.note(t, len[i], 1.0f, 0.004f, i == 2 ? 0.08f : 0.02f, steady(freq[i]), chirp);
        t += len[i] + 0.025f;
    }
    return s.finish();
}

// ---- galactic ----

// Locking on: eight beeps, each sooner and a little higher than the last,
// then a held tone with a fast warble
std::vector<int16_t> galacticAttention(uint32_t rate){
    Synth s(rate);
    const float gaps[8] = {0.20f, 0.17f, 0.145f, 0.12f, 0.10f, 0.085f, 0.072f, 0.062f};
    float t = 0.0f, f = 1150.0f;
    for (float g : gaps){
        s.note(t, 0.035f, 1.0f, 0.003f, 0.012f, steady(f), chirp);
        t += g;
        f += 45.0f;
    }
    auto warble = [](float, float sec) { return 1600.0f * (1.0f + 0.015f * std::sin(kTwoPi * 12.0f * sec)); };
    s.note(t, 0.34f, 1.0f, 0.004f, 0.06f, warble, chirp);
    return s.finish();
}

// Powering down: a falling square that stutters faster as it dies, then
// two blips
std::vector<int16_t> galacticFailure(uint32_t rate){
    Synth s(rate);
    auto stutter = [](float p, float sec) {
        const float trem = 0.5f + 0.5f * std::sin(kTwoPi * (18.0f + 30.0f * sec) * sec);
        return softSquare(p, sec) * (1.0f - 0.5f * trem);
    };
    s.note(0.0f, 0.72f, 1.0f, 0.005f, 0.12f, glide(900.0f, 200.0f, 0.7f), stutter);
    s.note(0.82f, 0.06f, 0.8f, 0.003f, 0.02f, steady(440.0f), softSquare);
    s.note(0.95f, 0.06f, 0.8f, 0.003f, 0.02f, steady(440.0f), softSquare);
    return s.finish();
}

}  // namespace

std::vector<int16_t> renderAlert(Theme theme, Alert alert, uint32_t sampleRate){
    if (sampleRate < 8000) return {};
    const bool failure = alert == Alert::Failure;
    switch (theme){
        case Theme::Anime:    return failure ? animeFailure(sampleRate) : animeAttention(sampleRate);
        case Theme::Lcars:    return failure ? lcarsFailure(sampleRate) : lcarsAttention(sampleRate);
        case Theme::Galactic: return failure ? galacticFailure(sampleRate) : galacticAttention(sampleRate);
        case Theme::Classic:  break;
    }
    return {};
}

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p){
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int sampleAt(const uint8_t* p, uint16_t bits){
    if (bits == 8) return (static_cast<int>(p[0]) - 128) * 256;   // 8-bit WAV is unsigned
    return static_cast<int16_t>(le16(p));
}

}  // namespace

bool decodeWav(const uint8_t* file, size_t size, std::vector<int16_t>& pcm, uint32_t& sampleRate){
    pcm.clear();
    sampleRate = 0;
    if (!file || size < 12 || std::memcmp(file, "RIFF", 4) != 0 || std::memcmp(file + 8, "WAVE", 4) != 0) return false;
    bool haveFormat = false;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t pos = 12;
    while (pos + 8 <= size){
        const uint8_t* chunk = file + pos;
        const size_t len  = le32(chunk + 4);
        const size_t body = pos + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0){
            if (len < 16 || size - body < 16) return false;
            format   = le16(file + body);
            channels = le16(file + body + 2);
            rate     = le32(file + body + 4);
            bits     = le16(file + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format opens the subformat GUID
            if (format == 0xFFFE && len >= 26 && size - body >= 26) format = le16(file + body + 24);
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0){
            if (!haveFormat || format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) ||
                rate < 4000 || rate > 192000) return false;
            const size_t frame  = static_cast<size_t>(channels) * (bits / 8);
            const size_t frames = std::min(len, size - body) / frame;
            if (frames == 0) return false;
            pcm.resize(frames);
            const uint8_t* p = file + body;
            for (size_t i = 0; i < frames; ++i, p += frame){
                int v = sampleAt(p, bits);
                if (channels == 2) v = (v + sampleAt(p + bits / 8, bits)) / 2;
                pcm[i] = static_cast<int16_t>(v);
            }
            sampleRate = rate;
            return true;
        }
        if (len > size - body) break;
        pos = body + len + (len & 1);   // chunks are padded to an even size
    }
    return false;
}

}  // namespace monitor
