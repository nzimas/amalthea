// NorniOS: what the plugin DSP bundled as native engines uses of JUCE, and nothing more, so that upstream's DSP
// sources compile unmodified without it: QDelay's (Vendor/qdelay, docs/adr/0053-qdelay.md) and Blackheart's
// (Vendor/blackheart, docs/adr/0055-blackheart.md) and CrushClone's (Vendor/crushclone,
// docs/adr/0061-crushclone.md). Behaviour follows JUCE's own:
//   * Random is JUCE's 48-bit linear congruential generator;
//   * SmoothedValue is JUCE's linear one; Decibels, jlimit and the vector operations compute as JUCE's;
//   * dsp::FFT transforms as JUCE's does: forward unscaled, inverse scaled by 1/N, real-only calls in place;
//   * dsp::StateVariableTPTFilter, dsp::IIR::Filter with the coefficient factories, and dsp::Oversampling
//     (half-band polyphase IIR stages from JUCE's design method) are written from JUCE's formulas;
//   * AudioBuffer, dsp::AudioBlock, ProcessSpec and ProcessContextReplacing carry what the DSP asks of them;
//   * AudioProcessorValueTreeState::Listener is the listener interface QDelay's Delay implements.
#pragma once

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <array>
#include <initializer_list>
#include <map>
#include <type_traits>
#include <limits>
#include <memory>
#include <cctype>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined(__aarch64__) || defined(__arm64__)
#include <arm_neon.h>
#endif

#ifndef DBG
#define DBG(textToWrite) ((void)0)
#define JUCE_VECTOR_CALLTYPE
#endif
#ifndef jassert
#define jassert(x) ((void)0)
#endif
#ifndef jassertfalse
#define jassertfalse ((void)0)
#endif

#define JUCE_DECLARE_NON_COPYABLE(className) \
    className(const className &) = delete;     \
    className &operator=(const className &) = delete;
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(className) JUCE_DECLARE_NON_COPYABLE(className)
#define JUCE_LEAK_DETECTOR(className)
#define JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE(...)
#define JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#define JUCE_BEGIN_IGNORE_WARNINGS_MSVC(...)
#define JUCE_END_IGNORE_WARNINGS_MSVC
#define JUCE_DECLARE_WEAK_REFERENCEABLE(Class)
#define JUCE_UNDENORMALISE(x)
#define JUCE_CONSTEXPR constexpr
#define forcedinline inline

namespace juce {

// juce::String: a std::string that also builds from a number, which is how the plugins' parameter ids are spelled
// ("colony_" + juce::String(n) + "_warp").
class String : public std::string {
public:
    String() = default;
    String(const char *s) : std::string(s == nullptr ? "" : s) {}
    String(const std::string &s) : std::string(s) {}
    String(std::string &&s) : std::string(std::move(s)) {}
    explicit String(int v) : std::string(std::to_string(v)) {}
    String(float v, int decimalPlaces) : std::string(withDecimals(static_cast<double>(v), decimalPlaces)) {}
    String(double v, int decimalPlaces) : std::string(withDecimals(v, decimalPlaces)) {}
    void copyToUTF8(char *destination, size_t maxBytes) const {
        if (destination == nullptr || maxBytes == 0) return;
        const size_t n = std::min(size(), maxBytes - 1);
        std::memcpy(destination, data(), n);
        destination[n] = 0;
    }
    explicit String(double v) : std::string(std::to_string(v)) {}

    // the handful of things JUCE's String does that the gin library and the plugins ask for
    String trim() const {
        const auto first = find_first_not_of(" \t\r\n");
        if (first == npos) return String();
        return String(substr(first, find_last_not_of(" \t\r\n") - first + 1));
    }
    String toLowerCase() const {
        String out(*this);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }
    String toUpperCase() const {
        String out(*this);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return out;
    }
    bool contains(const String &other) const { return find(other) != npos; }
    bool equalsIgnoreCase(const String &other) const { return toLowerCase() == other.toLowerCase(); }
    bool startsWith(const String &other) const { return rfind(other, 0) == 0; }
    bool endsWith(const String &other) const { return size() >= other.size() && compare(size() - other.size(), other.size(), other) == 0; }
    bool isEmpty() const noexcept { return empty(); }
    bool isNotEmpty() const noexcept { return !empty(); }
    int length() const noexcept { return static_cast<int>(size()); }
    float getFloatValue() const { try { return std::stof(*this); } catch (...) { return 0.f; } }
    double getDoubleValue() const { try { return std::stod(*this); } catch (...) { return 0.0; } }
    int getIntValue() const { try { return std::stoi(*this); } catch (...) { return 0; } }
    String upToFirstOccurrenceOf(const String &what, bool includeIt = false) const {
        const auto at = find(what);
        if (at == npos) return *this;
        return String(substr(0, includeIt ? at + what.size() : at));
    }
    String fromFirstOccurrenceOf(const String &what, bool includeIt = false, bool = true) const {
        const auto at = find(what);
        if (at == npos) return String();
        return String(substr(includeIt ? at : at + what.size()));
    }
    String replace(const String &what, const String &with) const {
        String out(*this);
        for (size_t at = out.find(what); at != npos; at = out.find(what, at + with.size()))
            out.std::string::replace(at, what.size(), with);
        return out;
    }
    String substring(int start, int end = -1) const {
        const int n = length();
        start = std::min(std::max(start, 0), n);
        const int stop = end < 0 ? n : std::min(std::max(end, start), n);
        return String(substr(static_cast<size_t>(start), static_cast<size_t>(stop - start)));
    }
    String paddedLeft(char pad, int minimumLength) const {
        String out(*this);
        while (out.length() < minimumLength) out.insert(out.begin(), pad);
        return out;
    }
    static String formatted(const String &format, ...) { return format; }

private:
    static std::string withDecimals(double v, int decimalPlaces) {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(std::max(0, decimalPlaces));
        out << v;
        return out.str();
    }
};
using StringArray = std::vector<String>;

} // namespace juce
// a String is a key in the engines' own tables, so it hashes as the string it is
namespace std {
template <> struct hash<juce::String> {
    size_t operator()(const juce::String &s) const noexcept { return hash<std::string>{}(s); }
};
} // namespace std
namespace juce {

// a colour is the editor's business, and no editor is compiled here; it carries the value so the plugins' constant
// files compile as they are written
class Colour {
public:
    constexpr Colour() = default;
    constexpr explicit Colour(uint32_t argb) : argb_(argb) {}
    constexpr uint32_t getARGB() const noexcept { return argb_; }

private:
    uint32_t argb_{0};
};

template <typename T> struct MathConstants {
    static constexpr T pi = static_cast<T>(3.141592653589793238L);
    static constexpr T twoPi = static_cast<T>(2 * 3.141592653589793238L);
    static constexpr T halfPi = static_cast<T>(3.141592653589793238L / 2);
    static constexpr T euler = static_cast<T>(2.71828182845904523536L);
    static constexpr T sqrt2 = static_cast<T>(1.4142135623730950488L);
};

template <typename T> constexpr T jmin(T a, T b) { return b < a ? b : a; }
template <typename T> constexpr T jmax(T a, T b) { return a < b ? b : a; }
template <typename T> constexpr T jlimit(T lo, T hi, T v) { return v < lo ? lo : (hi < v ? hi : v); }
// jmap as JUCE's: a 0..1 proportion across a range
template <typename T> T jmap(T value0To1, T targetStart, T targetEnd) { return targetStart + value0To1 * (targetEnd - targetStart); }
template <typename T> T jmap(T sourceValue, T sourceStart, T sourceEnd, T targetStart, T targetEnd) {
    return targetStart + ((targetEnd - targetStart) * (sourceValue - sourceStart)) / (sourceEnd - sourceStart);
}

using int64 = int64_t;
using uint64 = uint64_t;
using uint32 = uint32_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using int8 = int8_t;

template <typename... T> void ignoreUnused(T &&...) noexcept {}
// juce::enumerate: walking a range with the index alongside
template <typename Range> auto enumerate(Range &&range, int start = 0) {
    struct Item { int index; decltype(*std::begin(range)) value; };
    std::vector<Item> items;
    int i = start;
    for (auto &&v : range) items.push_back(Item{i++, v});
    return items;
}
template <typename T, int N> constexpr int numElementsInArray(T (&)[N]) noexcept { return N; }
template <typename T> bool exactlyEqual(T a, T b) noexcept { return a == b; }
template <typename T> int roundToInt(T v) noexcept { return static_cast<int>(std::lround(v)); }
// juce::approximatelyEqual: equal to within the representation, which is how the DSP asks "did this parameter move?"
template <typename T> bool approximatelyEqual(T a, T b) noexcept {
    return std::abs(a - b) <= (std::numeric_limits<T>::epsilon() * std::max(std::abs(a), std::abs(b))) ||
           std::abs(a - b) < std::numeric_limits<T>::min();
}
template <typename T, typename U> bool isPositiveAndBelow(T v, U limit) noexcept { return v >= 0 && static_cast<long long>(v) < static_cast<long long>(limit); }

inline int nextPowerOfTwo(int n) noexcept {
    --n;
    n |= (n >> 1); n |= (n >> 2); n |= (n >> 4); n |= (n >> 8); n |= (n >> 16);
    return n + 1;
}

template <typename T> struct DecibelsImpl {
    static T decibelsToGain(T dB, T minusInfinityDb = T(-100)) { return dB > minusInfinityDb ? std::pow(T(10), dB * T(0.05)) : T(); }
    static T gainToDecibels(T gain, T minusInfinityDb = T(-100)) {
        return gain > T() ? std::max(minusInfinityDb, static_cast<T>(std::log10(gain)) * T(20)) : minusInfinityDb;
    }
};
struct Decibels {
    template <typename T> static T decibelsToGain(T dB, T minusInfinityDb = T(-100)) { return DecibelsImpl<T>::decibelsToGain(dB, minusInfinityDb); }
    template <typename T> static T gainToDecibels(T gain, T minusInfinityDb = T(-100)) { return DecibelsImpl<T>::gainToDecibels(gain, minusInfinityDb); }
};

template <typename T> struct Range {
    Range() = default;
    Range(T s, T e) : start(s), end(e) {}
    T getStart() const noexcept { return start; }
    T getEnd() const noexcept { return end; }
    T getLength() const noexcept { return end - start; }
    T start{}, end{};
};

struct FloatVectorOperations {
    static Range<float> findMinAndMax(const float *src, int num) noexcept {
        if (num <= 0) return {};
        float lo = src[0], hi = src[0];
        for (int i = 1; i < num; ++i) { lo = std::min(lo, src[i]); hi = std::max(hi, src[i]); }
        return {lo, hi};
    }
    static void copy(float *d, const float *s, int n) noexcept { std::memcpy(d, s, static_cast<size_t>(n) * sizeof(float)); }
    static void copyWithMultiply(float *d, const float *s, float m, int n) noexcept { for (int i = 0; i < n; ++i) d[i] = s[i] * m; }
    static void addWithMultiply(float *d, const float *s, float m, int n) noexcept { for (int i = 0; i < n; ++i) d[i] += s[i] * m; }
    static void multiply(float *d, float m, int n) noexcept { for (int i = 0; i < n; ++i) d[i] *= m; }
    static void multiply(float *d, const float *s, int n) noexcept { for (int i = 0; i < n; ++i) d[i] *= s[i]; }
    static void add(float *d, const float *s, int n) noexcept { for (int i = 0; i < n; ++i) d[i] += s[i]; }
    static float findMaximum(const float *s, int n) noexcept { float m = n > 0 ? s[0] : 0.f; for (int i = 1; i < n; ++i) m = std::max(m, s[i]); return m; }
    static float findMinimum(const float *s, int n) noexcept { float m = n > 0 ? s[0] : 0.f; for (int i = 1; i < n; ++i) m = std::min(m, s[i]); return m; }
};

// juce::AudioBuffer<float>, owning its channels
template <typename T> class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(int channels, int samples) { setSize(channels, samples); }
    void setSize(int channels, int samples, bool keepExisting = false, bool clearExtra = false, bool avoidReallocating = false) {
        ignoreUnused(clearExtra, avoidReallocating);
        // as JUCE: a buffer already of that shape keeps its storage, so a makeCopyOf on the audio thread allocates
        // once and not every block
        if (channels == channels_ && samples == samples_ && !data_.empty()) return;
        std::vector<std::vector<T>> next(static_cast<size_t>(channels), std::vector<T>(static_cast<size_t>(samples), T()));
        if (keepExisting)
            for (int c = 0; c < std::min(channels, channels_); ++c)
                std::copy_n(data_[static_cast<size_t>(c)].begin(), std::min(samples, samples_), next[static_cast<size_t>(c)].begin());
        data_ = std::move(next);
        channels_ = channels; samples_ = samples;
        
        for (int c = 0; c < channels; ++c) pointers_[static_cast<size_t>(c)] = data_[static_cast<size_t>(c)].data();
    }
    // refers to memory owned elsewhere (the engine's output), as JUCE's constructor from channel pointers does
    AudioBuffer(T *const *channelData, int channels, int samples) { setDataToReferTo(channelData, channels, samples); }
    AudioBuffer(T *const *channelData, int channels, int startSample, int samples) {
        std::array<T*,16> shifted{};
        for (int c = 0; c < channels; ++c) shifted[static_cast<size_t>(c)] = channelData[c] + startSample;
        setDataToReferTo(shifted.data(), channels, samples);
    }
    void setDataToReferTo(T *const *channelData, int channels, int samples) {
        data_.clear();
        channels_ = channels; samples_ = samples;
        std::copy_n(channelData,channels,pointers_.data());
    }
    int getNumChannels() const noexcept { return channels_; }
    int getNumSamples() const noexcept { return samples_; }
    T *getWritePointer(int c) noexcept { return pointers_[static_cast<size_t>(c)]; }
    T *getWritePointer(int c, int start) noexcept { return pointers_[static_cast<size_t>(c)] + start; }
    const T *getReadPointer(int c) const noexcept { return pointers_[static_cast<size_t>(c)]; }
    const T *getReadPointer(int c, int start) const noexcept { return pointers_[static_cast<size_t>(c)] + start; }
    T **getArrayOfWritePointers() noexcept { return pointers_.data(); }
    const T *const *getArrayOfReadPointers() const noexcept { return const_cast<const T *const *>(pointers_.data()); }
    T getSample(int c, int i) const noexcept { return pointers_[static_cast<size_t>(c)][i]; }
    void setSample(int c, int i, T v) noexcept { pointers_[static_cast<size_t>(c)][i] = v; }
    void addSample(int c, int i, T v) noexcept { pointers_[static_cast<size_t>(c)][i] += v; }
    void clear() noexcept { for (int c = 0; c < channels_; ++c) std::fill_n(pointers_[static_cast<size_t>(c)], samples_, T()); }
    void clear(int c, int start, int n) noexcept { std::fill_n(pointers_[static_cast<size_t>(c)] + start, n, T()); }
    void clear(int start, int n) noexcept { for (int c = 0; c < channels_; ++c) clear(c, start, n); }
    void applyGain(int start, int n, T g) noexcept { for (int c = 0; c < channels_; ++c) applyGain(c, start, n, g); }
    void applyGainRamp(int c, int start, int n, T from, T to) noexcept {
        if (n <= 0) return;
        const T step = (to - from) / static_cast<T>(n);
        T *p = pointers_[static_cast<size_t>(c)] + start;
        for (int i = 0; i < n; ++i) { p[i] *= from; from += step; }
    }
    void applyGainRamp(int start, int n, T from, T to) noexcept {
        for (int c = 0; c < channels_; ++c) applyGainRamp(c, start, n, from, to);
    }
    void addFrom(int destChannel, int destStart, const T *src, int n, T gain = T(1)) noexcept {
        T *p = pointers_[static_cast<size_t>(destChannel)] + destStart;
        for (int i = 0; i < n; ++i) p[i] += src[i] * gain;
    }
    void copyFrom(int destChannel, int destStart, const AudioBuffer &src, int srcChannel, int srcStart, int n) noexcept {
        std::memmove(pointers_[static_cast<size_t>(destChannel)] + destStart, src.getReadPointer(srcChannel, srcStart), static_cast<size_t>(n) * sizeof(T));
    }
    void makeCopyOf(const AudioBuffer &other, bool avoidReallocating) { ignoreUnused(avoidReallocating); makeCopyOf(other); }
    void addFromWithRamp(int destChannel, int destStart, const T *src, int n, T startGain, T endGain) noexcept {
        T *d = pointers_[static_cast<size_t>(destChannel)] + destStart;
        const T step = n > 0 ? (endGain - startGain) / static_cast<T>(n) : T();
        for (int i = 0; i < n; ++i) d[i] += src[i] * (startGain + step * static_cast<T>(i));
    }
    void makeCopyOf(const AudioBuffer &other) {
        setSize(other.getNumChannels(), other.getNumSamples());
        for (int c = 0; c < channels_; ++c) std::copy_n(other.getReadPointer(c), samples_, pointers_[static_cast<size_t>(c)]);
    }
    void copyFrom(int destChannel, int destStart, const T *src, int n) noexcept {
        std::memmove(pointers_[static_cast<size_t>(destChannel)] + destStart, src, static_cast<size_t>(n) * sizeof(T));
    }
    void addFrom(int destChannel, int destStart, const AudioBuffer &src, int srcChannel, int srcStart, int n, T gain = T(1)) noexcept {
        T *d = pointers_[static_cast<size_t>(destChannel)] + destStart; const T *s = src.getReadPointer(srcChannel, srcStart);
        for (int i = 0; i < n; ++i) d[i] += s[i] * gain;
    }
    void applyGain(int c, int start, int n, T gain) noexcept { T *d = pointers_[static_cast<size_t>(c)] + start; for (int i = 0; i < n; ++i) d[i] *= gain; }
    void applyGain(T gain) noexcept { for (int c = 0; c < channels_; ++c) applyGain(c, 0, samples_, gain); }
    T getMagnitude(int c, int start, int n) const noexcept {
        T m = T(); const T *d = pointers_[static_cast<size_t>(c)] + start;
        for (int i = 0; i < n; ++i) m = std::max(m, std::abs(d[i]));
        return m;
    }
    T getRMSLevel(int c, int start, int n) const noexcept {
        if (n <= 0) return T();
        double sum = 0; const T *d = pointers_[static_cast<size_t>(c)] + start;
        for (int i = 0; i < n; ++i) sum += static_cast<double>(d[i]) * d[i];
        return static_cast<T>(std::sqrt(sum / n));
    }

private:
    std::vector<std::vector<T>> data_;
    std::array<T *,16> pointers_{};
    int channels_{0}, samples_{0};
};

// juce::SmoothedValue, in both of JUCE's flavours: Linear steps by a difference, Multiplicative by a ratio, which
// is what a resampling ratio and a delay length are smoothed with.
namespace ValueSmoothingTypes {
struct Linear {};
struct Multiplicative {};
} // namespace ValueSmoothingTypes

template <typename T, typename SmoothingType = ValueSmoothingTypes::Linear> class SmoothedValue {
    static constexpr bool multiplicative = std::is_same<SmoothingType, ValueSmoothingTypes::Multiplicative>::value;

public:
    SmoothedValue() = default;
    SmoothedValue(T initial) : current_(initial), target_(initial) {}
    void reset(double sampleRate, double rampLengthSeconds) noexcept { reset(static_cast<int>(std::floor(rampLengthSeconds * sampleRate))); }
    void reset(int numSteps) noexcept { stepsToTarget_ = numSteps; setCurrentAndTargetValue(target_); }
    void setCurrentAndTargetValue(T v) noexcept { target_ = current_ = v; countdown_ = 0; }
    void setTargetValue(T v) noexcept {
        if (v == target_) return;
        if (stepsToTarget_ <= 0) { setCurrentAndTargetValue(v); return; }
        // a ratio cannot reach zero from zero, and JUCE asserts rather than smoothing: jump instead of going NaN
        if (multiplicative && (v == T() || current_ == T())) { setCurrentAndTargetValue(v); return; }
        target_ = v;
        countdown_ = stepsToTarget_;
        setStepSize();
    }
    T getNextValue() noexcept {
        if (!isSmoothing()) return target_;
        --countdown_;
        if (isSmoothing()) setNextValue(); else current_ = target_;
        return current_;
    }
    T skip(int numSamples) noexcept {
        if (numSamples >= countdown_) { setCurrentAndTargetValue(target_); return target_; }
        if (multiplicative) current_ *= static_cast<T>(std::pow(static_cast<double>(step_), static_cast<double>(numSamples)));
        else current_ += step_ * static_cast<T>(numSamples);
        countdown_ -= numSamples;
        return current_;
    }
    bool isSmoothing() const noexcept { return countdown_ > 0; }
    T getCurrentValue() const noexcept { return current_; }
    T getTargetValue() const noexcept { return target_; }

private:
    void setStepSize() noexcept {
        if (multiplicative)
            step_ = static_cast<T>(std::exp((std::log(std::abs(static_cast<double>(target_))) -
                                             std::log(std::abs(static_cast<double>(current_)))) / countdown_));
        else
            step_ = (target_ - current_) / static_cast<T>(countdown_);
    }
    void setNextValue() noexcept {
        if (multiplicative) current_ *= step_; else current_ += step_;
    }
    T current_{}, target_{}, step_{};
    int countdown_{0}, stepsToTarget_{0};
};
template <typename T> using LinearSmoothedValue = SmoothedValue<T>;
using AudioSampleBuffer = AudioBuffer<float>;

// juce::ADSR, as JUCE 7 has it: linear segments at rates worked out from the times, a release that falls from wherever
// the envelope stood at note-off, and new parameters taking effect at once (recalculated rates, and a state skipped when
// its rate has gone to zero).
class ADSR {
public:
    struct Parameters {
        Parameters() = default;
        Parameters(float a, float d, float s, float r) : attack(a), decay(d), sustain(s), release(r) {}
        float attack = 0.1f, decay = 0.1f, sustain = 1.0f, release = 0.1f;
    };

    ADSR() { recalculateRates(); }
    void setParameters(const Parameters &p) { parameters = p; recalculateRates(); }
    const Parameters &getParameters() const noexcept { return parameters; }
    bool isActive() const noexcept { return state != State::idle; }
    void setSampleRate(double sr) noexcept { sampleRate = sr; recalculateRates(); }
    void reset() noexcept { envelopeVal = 0.0f; state = State::idle; }
    void noteOn() noexcept {
        if (attackRate > 0.0f) state = State::attack;
        else if (decayRate > 0.0f) { envelopeVal = 1.0f; state = State::decay; }
        else { envelopeVal = parameters.sustain; state = State::sustain; }
    }
    void noteOff() noexcept {
        if (state != State::idle) {
            if (parameters.release > 0.0f) {
                releaseRate = static_cast<float>(envelopeVal / (parameters.release * sampleRate));
                state = State::release;
            } else {
                reset();
            }
        }
    }
    float getNextSample() noexcept {
        switch (state) {
        case State::idle: return 0.0f;
        case State::attack:
            envelopeVal += attackRate;
            if (envelopeVal >= 1.0f) { envelopeVal = 1.0f; goToNextState(); }
            break;
        case State::decay:
            envelopeVal -= decayRate;
            if (envelopeVal <= parameters.sustain) { envelopeVal = parameters.sustain; goToNextState(); }
            break;
        case State::sustain: envelopeVal = parameters.sustain; break;
        case State::release:
            envelopeVal -= releaseRate;
            if (envelopeVal <= 0.0f) goToNextState();
            break;
        }
        return envelopeVal;
    }

private:
    enum class State { idle, attack, decay, sustain, release };
    void recalculateRates() noexcept {
        auto getRate = [](float distance, float timeInSeconds, double sr) {
            return timeInSeconds > 0.0f ? static_cast<float>(distance / (timeInSeconds * sr)) : -1.0f;
        };
        attackRate = getRate(1.0f, parameters.attack, sampleRate);
        decayRate = getRate(1.0f - parameters.sustain, parameters.decay, sampleRate);
        releaseRate = getRate(parameters.sustain, parameters.release, sampleRate);
        if ((state == State::attack && attackRate <= 0.0f) ||
            (state == State::decay && (decayRate <= 0.0f || envelopeVal <= parameters.sustain)) ||
            (state == State::release && releaseRate <= 0.0f))
            goToNextState();
    }
    void goToNextState() noexcept {
        if (state == State::attack) { state = (decayRate > 0.0f ? State::decay : State::sustain); return; }
        if (state == State::decay) { state = State::sustain; return; }
        if (state == State::release) reset();
    }
    State state = State::idle;
    Parameters parameters;
    double sampleRate = 44100.0;
    float envelopeVal = 0.0f, attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f;
};

class Random {
public:
    Random() {
        seed_ = static_cast<int64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                static_cast<int64_t>(reinterpret_cast<intptr_t>(this));
    }
    explicit Random(int64_t seed) : seed_(seed) {}
    void setSeed(int64_t seed) noexcept { seed_ = seed; }
    int nextInt(int maxValue) noexcept { return static_cast<int>((static_cast<uint64_t>(static_cast<uint32_t>(nextInt())) * static_cast<uint64_t>(maxValue)) >> 32); }
    bool nextBool() noexcept { return (nextInt() & 0x40000000) != 0; }
    int nextInt() noexcept {
        seed_ = static_cast<int64_t>((static_cast<uint64_t>(seed_) * 0x5deece66dULL + 11) & 0xffffffffffffULL);
        return static_cast<int>(seed_ >> 16);
    }
    static Random &getSystemRandom() noexcept {
        static Random shared;
        return shared;
    }
    float nextFloat() noexcept {
        auto result = static_cast<float>(static_cast<uint32_t>(nextInt())) / (static_cast<float>(UINT32_MAX) + 1.0f);
        return result == 1.0f ? 1.0f - std::numeric_limits<float>::epsilon() : result;
    }

private:
    int64_t seed_;
};

// Flush denormals to zero for the scope, as JUCE's does.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#ifdef FE_DFL_DISABLE_DENORMS_ENV
        std::fegetenv(&saved_);
        std::fesetenv(FE_DFL_DISABLE_DENORMS_ENV);
#endif
    }
    ~ScopedNoDenormals() noexcept {
#ifdef FE_DFL_DISABLE_DENORMS_ENV
        std::fesetenv(&saved_);
#endif
    }

private:
#ifdef FE_DFL_DISABLE_DENORMS_ENV
    std::fenv_t saved_;
#endif
};

// juce::Interpolators::Lagrange: JUCE's fifth-order Lagrange resampler, five samples of memory, and the wrapping
// process() a colony resamples the global delay line with.
namespace InterpolatorDetail {
template <int k> struct LagrangeHelper { static void calc(float &a, float b) noexcept { a *= b * (1.0f / k); } };
template <> struct LagrangeHelper<0> { static void calc(float &, float) noexcept {} };
template <int k> inline float calcCoefficient(float input, float offset) noexcept {
    LagrangeHelper<0 - k>::calc(input, -2.0f - offset);
    LagrangeHelper<1 - k>::calc(input, -1.0f - offset);
    LagrangeHelper<2 - k>::calc(input, 0.0f - offset);
    LagrangeHelper<3 - k>::calc(input, 1.0f - offset);
    LagrangeHelper<4 - k>::calc(input, 2.0f - offset);
    return input;
}
struct LagrangeTraits {
    static constexpr int memorySize = 5;
    static float valueAtOffset(const float *inputs, float offset, int index) noexcept {
        float result = 0.0f;
        result += calcCoefficient<0>(inputs[index], offset); if (++index == 5) index = 0;
        result += calcCoefficient<1>(inputs[index], offset); if (++index == 5) index = 0;
        result += calcCoefficient<2>(inputs[index], offset); if (++index == 5) index = 0;
        result += calcCoefficient<3>(inputs[index], offset); if (++index == 5) index = 0;
        result += calcCoefficient<4>(inputs[index], offset);
        return result;
    }
};
} // namespace InterpolatorDetail

template <class Traits, int memorySize> class GenericInterpolator {
public:
    GenericInterpolator() noexcept { reset(); }
    void reset() noexcept {
        indexBuffer_ = 0;
        subSamplePos_ = 1.0;
        std::fill(std::begin(lastInputSamples_), std::end(lastInputSamples_), 0.0f);
    }
    int process(double speedRatio, const float *input, float *output, int numOut) noexcept {
        int numUsed = 0;
        run(speedRatio, output, numOut, [&] { pushSample(input[numUsed++]); });
        return numUsed;
    }
    // the wrapping form: when it runs past what is available it wraps back by wrapAround samples, or feeds zeroes
    int process(double speedRatio, const float *input, float *output, int numOut, int numAvailable, int wrap) noexcept {
        const float *originalIn = input;
        bool exceeded = false;
        run(speedRatio, output, numOut, [&] {
            if (exceeded) { pushSample(0.0f); return; }
            pushSample(*input++);
            if (--numAvailable <= 0) {
                if (wrap > 0) { input -= wrap; numAvailable += wrap; }
                else exceeded = true;
            }
        });
        if (wrap == 0) return static_cast<int>(input - originalIn);
        return (static_cast<int>(input - originalIn) + wrap) % wrap;
    }

private:
    void pushSample(float v) noexcept {
        lastInputSamples_[indexBuffer_] = v;
        if (++indexBuffer_ == memorySize) indexBuffer_ = 0;
    }
    template <typename Push> void run(double speedRatio, float *output, int numOut, Push push) noexcept {
        double pos = subSamplePos_;
        for (int i = 0; i < numOut; ++i) {
            while (pos >= 1.0) { push(); pos -= 1.0; }
            *output++ = Traits::valueAtOffset(lastInputSamples_, static_cast<float>(pos), indexBuffer_);
            pos += speedRatio;
        }
        subSamplePos_ = pos;
    }
    float lastInputSamples_[memorySize];
    double subSamplePos_{1.0};
    int indexBuffer_{0};
};

struct Interpolators {
    using Lagrange = GenericInterpolator<InterpolatorDetail::LagrangeTraits, 5>;
};

// juce::Reverb: Freeverb, with JUCE's own tunings and scale factors
class Reverb {
public:
    struct Parameters {
        float roomSize{0.5f}, damping{0.5f}, wetLevel{0.33f}, dryLevel{0.4f}, width{1.0f}, freezeMode{0.0f};
    };
    Reverb() { setParameters(Parameters()); setSampleRate(44100.0); }
    const Parameters &getParameters() const noexcept { return parameters_; }
    void setParameters(const Parameters &p) {
        const float wetScaleFactor = 3.0f, dryScaleFactor = 2.0f;
        const float wet = p.wetLevel * wetScaleFactor;
        dryGain_.setTargetValue(p.dryLevel * dryScaleFactor);
        wetGain1_.setTargetValue(0.5f * wet * (1.0f + p.width));
        wetGain2_.setTargetValue(0.5f * wet * (1.0f - p.width));
        gain_ = isFrozen(p.freezeMode) ? 0.0f : 0.015f;
        parameters_ = p;
        updateDamping();
    }
    void setSampleRate(double sampleRate) {
        static const short combTunings[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};   // at 44100 Hz
        static const short allPassTunings[] = {556, 441, 341, 225};
        const int stereoSpread = 23;
        const int sr = static_cast<int>(sampleRate);
        for (int i = 0; i < numCombs; ++i) {
            comb_[0][i].setSize((sr * combTunings[i]) / 44100);
            comb_[1][i].setSize((sr * (combTunings[i] + stereoSpread)) / 44100);
        }
        for (int i = 0; i < numAllPasses; ++i) {
            allPass_[0][i].setSize((sr * allPassTunings[i]) / 44100);
            allPass_[1][i].setSize((sr * (allPassTunings[i] + stereoSpread)) / 44100);
        }
        const double smoothTime = 0.01;
        damping_.reset(sampleRate, smoothTime);
        feedback_.reset(sampleRate, smoothTime);
        dryGain_.reset(sampleRate, smoothTime);
        wetGain1_.reset(sampleRate, smoothTime);
        wetGain2_.reset(sampleRate, smoothTime);
    }
    void reset() {
        for (int j = 0; j < numChannels; ++j) {
            for (int i = 0; i < numCombs; ++i) comb_[j][i].clear();
            for (int i = 0; i < numAllPasses; ++i) allPass_[j][i].clear();
        }
    }
    void processStereo(float *left, float *right, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const float input = (left[i] + right[i]) * gain_;
            float outL = 0, outR = 0;
            const float damp = damping_.getNextValue();
            const float feedbck = feedback_.getNextValue();
            for (int j = 0; j < numCombs; ++j) {          // the combs in parallel
                outL += comb_[0][j].process(input, damp, feedbck);
                outR += comb_[1][j].process(input, damp, feedbck);
            }
            for (int j = 0; j < numAllPasses; ++j) {      // the allpasses in series
                outL = allPass_[0][j].process(outL);
                outR = allPass_[1][j].process(outR);
            }
            const float dry = dryGain_.getNextValue(), wet1 = wetGain1_.getNextValue(), wet2 = wetGain2_.getNextValue();
            left[i] = outL * wet1 + outR * wet2 + left[i] * dry;
            right[i] = outR * wet1 + outL * wet2 + right[i] * dry;
        }
    }
    void processMono(float *samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const float input = samples[i] * gain_;
            float output = 0;
            const float damp = damping_.getNextValue(), feedbck = feedback_.getNextValue();
            for (int j = 0; j < numCombs; ++j) output += comb_[0][j].process(input, damp, feedbck);
            for (int j = 0; j < numAllPasses; ++j) output = allPass_[0][j].process(output);
            const float dry = dryGain_.getNextValue(), wet1 = wetGain1_.getNextValue();
            samples[i] = output * wet1 + samples[i] * dry;
        }
    }

private:
    static bool isFrozen(float freezeMode) noexcept { return freezeMode >= 0.5f; }
    void updateDamping() noexcept {
        const float roomScaleFactor = 0.28f, roomOffset = 0.7f, dampScaleFactor = 0.4f;
        if (isFrozen(parameters_.freezeMode)) setDamping(0.0f, 1.0f);
        else setDamping(parameters_.damping * dampScaleFactor, parameters_.roomSize * roomScaleFactor + roomOffset);
    }
    void setDamping(float d, float room) noexcept { damping_.setTargetValue(d); feedback_.setTargetValue(room); }

    class CombFilter {
    public:
        void setSize(int size) {
            if (size != static_cast<int>(buffer_.size())) { bufferIndex_ = 0; buffer_.assign(static_cast<size_t>(size), 0.0f); }
            clear();
        }
        void clear() noexcept { last_ = 0; std::fill(buffer_.begin(), buffer_.end(), 0.0f); }
        float process(float input, float damp, float feedbackLevel) noexcept {
            const float output = buffer_[static_cast<size_t>(bufferIndex_)];
            last_ = (output * (1.0f - damp)) + (last_ * damp);
            buffer_[static_cast<size_t>(bufferIndex_)] = input + (last_ * feedbackLevel);
            bufferIndex_ = (bufferIndex_ + 1) % static_cast<int>(buffer_.size());
            return output;
        }

    private:
        std::vector<float> buffer_;
        int bufferIndex_{0};
        float last_{0.0f};
    };
    class AllPassFilter {
    public:
        void setSize(int size) {
            if (size != static_cast<int>(buffer_.size())) { bufferIndex_ = 0; buffer_.assign(static_cast<size_t>(size), 0.0f); }
            clear();
        }
        void clear() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }
        float process(float input) noexcept {
            const float buffered = buffer_[static_cast<size_t>(bufferIndex_)];
            buffer_[static_cast<size_t>(bufferIndex_)] = input + (buffered * 0.5f);
            bufferIndex_ = (bufferIndex_ + 1) % static_cast<int>(buffer_.size());
            return buffered - input;
        }

    private:
        std::vector<float> buffer_;
        int bufferIndex_{0};
    };

    enum { numCombs = 8, numAllPasses = 4, numChannels = 2 };
    Parameters parameters_;
    float gain_{0.015f};
    CombFilter comb_[numChannels][numCombs];
    AllPassFilter allPass_[numChannels][numAllPasses];
    SmoothedValue<float> damping_, feedback_, dryGain_, wetGain1_, wetGain2_;
};

// The containers, locks and value tree the gin library asks for (Vendor/wavetable/README.md). They carry what its
// DSP does with them and nothing more: gin keeps pools of scratch buffers, arrays of tables and a little state in a
// tree, and it locks the pools.
// juce::ReferenceCountedObject and its pointer: an intrusive count, the object deleted when the last pointer goes
class ReferenceCountedObject {
public:
    void incReferenceCount() noexcept { ++refCount_; }
    void decReferenceCount() noexcept { if (--refCount_ == 0) delete this; }
    int getReferenceCount() const noexcept { return refCount_.load(); }
protected:
    ReferenceCountedObject() = default;
    ReferenceCountedObject(const ReferenceCountedObject &) noexcept {}
    ReferenceCountedObject &operator=(const ReferenceCountedObject &) noexcept { return *this; }
    virtual ~ReferenceCountedObject() = default;
private:
    std::atomic<int> refCount_{0};
};

template <typename ObjectType> class ReferenceCountedObjectPtr {
public:
    using ReferencedType = ObjectType;
    ReferenceCountedObjectPtr() noexcept = default;
    ReferenceCountedObjectPtr(std::nullptr_t) noexcept {}
    ReferenceCountedObjectPtr(ObjectType *o) noexcept : o_(o) { if (o_) o_->incReferenceCount(); }
    ReferenceCountedObjectPtr(ObjectType &o) noexcept : o_(&o) { o_->incReferenceCount(); }
    ReferenceCountedObjectPtr(const ReferenceCountedObjectPtr &p) noexcept : o_(p.o_) { if (o_) o_->incReferenceCount(); }
    ReferenceCountedObjectPtr(ReferenceCountedObjectPtr &&p) noexcept : o_(p.o_) { p.o_ = nullptr; }
    template <typename Other> ReferenceCountedObjectPtr(const ReferenceCountedObjectPtr<Other> &p) noexcept : ReferenceCountedObjectPtr(p.get()) {}
    ~ReferenceCountedObjectPtr() { if (o_) o_->decReferenceCount(); }
    ReferenceCountedObjectPtr &operator=(ObjectType *o) { ReferenceCountedObjectPtr(o).swap(*this); return *this; }
    ReferenceCountedObjectPtr &operator=(const ReferenceCountedObjectPtr &p) { ReferenceCountedObjectPtr(p).swap(*this); return *this; }
    ReferenceCountedObjectPtr &operator=(ReferenceCountedObjectPtr &&p) noexcept { std::swap(o_, p.o_); return *this; }
    void swap(ReferenceCountedObjectPtr &p) noexcept { std::swap(o_, p.o_); }
    ObjectType *get() const noexcept { return o_; }
    ObjectType *operator->() const noexcept { return o_; }
    ObjectType &operator*() const noexcept { return *o_; }
    operator ObjectType *() const noexcept { return o_; }
    void reset() noexcept { ReferenceCountedObjectPtr().swap(*this); }
    bool operator==(std::nullptr_t) const noexcept { return o_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return o_ != nullptr; }
    bool operator==(const ObjectType *o) const noexcept { return o_ == o; }
    bool operator!=(const ObjectType *o) const noexcept { return o_ != o; }
    bool operator==(const ReferenceCountedObjectPtr &p) const noexcept { return o_ == p.o_; }
    bool operator!=(const ReferenceCountedObjectPtr &p) const noexcept { return o_ != p.o_; }
private:
    ObjectType *o_ = nullptr;
};

// the lock JUCE's containers take when none is asked for
struct DummyCriticalSection {
    void enter() const noexcept {}
    void exit() const noexcept {}
};

template <typename T, typename TypeOfCriticalSectionToUse = DummyCriticalSection> class Array {
public:
    Array() = default;
    Array(std::initializer_list<T> init) : items_(init) {}
    int size() const noexcept { return static_cast<int>(items_.size()); }
    bool isEmpty() const noexcept { return items_.empty(); }
    void add(const T &v) { items_.push_back(v); }
    void addIfNotAlreadyThere(const T &v) { if (!contains(v)) items_.push_back(v); }
    void insert(int at, const T &v) { items_.insert(items_.begin() + std::min<int>(at, size()), v); }
    void set(int i, const T &v) { ensure(i); items_[static_cast<size_t>(i)] = v; }
    void setUnchecked(int i, const T &v) { items_[static_cast<size_t>(i)] = v; }
    void sort() { std::sort(items_.begin(), items_.end()); }
    void insertArray(int at, const T *values, int numValues) {
        if (at < 0 || at > size()) at = size();
        items_.insert(items_.begin() + at, values, values + numValues);
    }
    void addArray(const T *values, int numValues) { items_.insert(items_.end(), values, values + numValues); }
    void remove(int i) { if (i >= 0 && i < size()) items_.erase(items_.begin() + i); }
    void removeAllInstancesOf(const T &v) { items_.erase(std::remove(items_.begin(), items_.end(), v), items_.end()); }
    void clear() noexcept { items_.clear(); }
    void clearQuick() noexcept { items_.clear(); }
    void resize(int n) { items_.resize(static_cast<size_t>(std::max(0, n))); }
    void ensureStorageAllocated(int n) {
        if constexpr (!std::is_same<T, bool>::value) items_.reserve(static_cast<size_t>(std::max(0, n)));
    }
    bool contains(const T &v) const { return std::find(items_.begin(), items_.end(), v) != items_.end(); }
    int indexOf(const T &v) const {
        auto it = std::find(items_.begin(), items_.end(), v);
        return it == items_.end() ? -1 : static_cast<int>(it - items_.begin());
    }
    T &getReference(int i) { ensure(i); return items_[static_cast<size_t>(i)]; }
    const T &getReference(int i) const { return items_[static_cast<size_t>(i)]; }
    T getUnchecked(int i) const { return items_[static_cast<size_t>(i)]; }
    T operator[](int i) const { return (i >= 0 && i < size()) ? items_[static_cast<size_t>(i)] : T(); }
    auto begin() noexcept { return items_.begin(); }
    auto end() noexcept { return items_.end(); }
    auto begin() const noexcept { return items_.begin(); }
    auto end() const noexcept { return items_.end(); }
    T getLast() const { return items_.empty() ? T() : items_.back(); }
    T getFirst() const { return items_.empty() ? T() : items_.front(); }
    void removeLast() { if (!items_.empty()) items_.pop_back(); }
    void removeFirstMatchingValue(const T &v) {
        auto it = std::find(items_.begin(), items_.end(), v);
        if (it != items_.end()) items_.erase(it);
    }
    void fill(const T &v) { std::fill(items_.begin(), items_.end(), v); }
    void swapWith(Array &other) noexcept { items_.swap(other.items_); }
    // JUCE's move: the element at from goes to to (or to the end when to is out of range), the rest shift up
    void move(int from, int to) {
        if (from < 0 || from >= size() || from == to) return;
        T v = items_[static_cast<size_t>(from)];
        items_.erase(items_.begin() + from);
        if (to < 0 || to > size()) to = size();
        items_.insert(items_.begin() + to, v);
    }
    template <typename Comparator> int addSorted(Comparator &c, const T &v) {
        int i = 0;
        while (i < size() && c.compareElements(items_[static_cast<size_t>(i)], v) <= 0) ++i;
        items_.insert(items_.begin() + i, v);
        return i;
    }
    int addSorted(const T &v) { int i = 0; while (i < size() && !(v < items_[static_cast<size_t>(i)])) ++i; items_.insert(items_.begin() + i, v); return i; }
    T *data() noexcept { return items_.data(); }


private:
    // std::vector<bool> is a bitfield and hands back a proxy where a reference is asked for, so it is not used here
    using Storage = typename std::conditional<std::is_same<T, bool>::value, std::deque<T>, std::vector<T>>::type;
    void ensure(int i) { if (i >= size()) items_.resize(static_cast<size_t>(i + 1)); }
    Storage items_;
};

// juce::OwnedArray: it owns what is put in it, and deletes it when it goes
template <typename T, typename TypeOfCriticalSectionToUse = DummyCriticalSection> class OwnedArray {
public:
    OwnedArray() = default;
    ~OwnedArray() { clear(); }
    OwnedArray(const OwnedArray &) = delete;
    OwnedArray &operator=(const OwnedArray &) = delete;
    OwnedArray(OwnedArray &&other) noexcept : items_(std::move(other.items_)) { other.items_.clear(); }
    OwnedArray &operator=(OwnedArray &&other) noexcept {
        if (this != &other) { clear(); items_ = std::move(other.items_); other.items_.clear(); }
        return *this;
    }
    int size() const noexcept { return static_cast<int>(items_.size()); }
    bool isEmpty() const noexcept { return items_.empty(); }
    T *add(T *v) { items_.push_back(v); return v; }
    T *insert(int at, T *v) { items_.insert(items_.begin() + std::min<int>(std::max(at, 0), size()), v); return v; }
    T *getUnchecked(int i) const { return items_[static_cast<size_t>(i)]; }
    T *getFirst() const { return items_.empty() ? nullptr : items_.front(); }
    T *getLast() const { return items_.empty() ? nullptr : items_.back(); }
    T *operator[](int i) const { return (i >= 0 && i < size()) ? items_[static_cast<size_t>(i)] : nullptr; }
    int indexOf(const T *v) const {
        for (int i = 0; i < size(); ++i) if (items_[static_cast<size_t>(i)] == v) return i;
        return -1;
    }
    void remove(int i) {
        if (i < 0 || i >= size()) return;
        delete items_[static_cast<size_t>(i)];
        items_.erase(items_.begin() + i);
    }
    void removeObject(const T *v) { remove(indexOf(v)); }
    T *removeAndReturn(int i) {
        if (i < 0 || i >= size()) return nullptr;
        T *v = items_[static_cast<size_t>(i)];
        items_.erase(items_.begin() + i);
        return v;
    }
    void clear() { for (T *v : items_) delete v; items_.clear(); }
    void clearQuick(bool deleteThem = true) { if (deleteThem) clear(); else items_.clear(); }
    void swapWith(OwnedArray &other) noexcept { items_.swap(other.items_); }
    T **begin() noexcept { return items_.data(); }
    T **end() noexcept { return items_.data() + items_.size(); }
    T *const *begin() const noexcept { return items_.data(); }
    T *const *end() const noexcept { return items_.data() + items_.size(); }

private:
    std::vector<T *> items_;
};

// the locks: a recursive one, as JUCE's CriticalSection is, and a spin lock
template <typename LockType> class GenericScopedLock;
template <typename LockType> class GenericScopedTryLock;
class CriticalSection {
public:
    using ScopedLockType = GenericScopedLock<CriticalSection>;
    void enter() const noexcept { mutex_.lock(); }
    void exit() const noexcept { mutex_.unlock(); }
    bool tryEnter() const noexcept { return mutex_.try_lock(); }

private:
    mutable std::recursive_mutex mutex_;
};
class SpinLock {
public:
    using ScopedLockType = GenericScopedLock<SpinLock>;
    void enter() const noexcept { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    void exit() const noexcept { flag_.clear(std::memory_order_release); }
    bool tryEnter() const noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

private:
    mutable std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
template <typename LockType> class GenericScopedLock {
public:
    explicit GenericScopedLock(const LockType &l) noexcept : lock_(l) { lock_.enter(); }
    ~GenericScopedLock() noexcept { lock_.exit(); }
    GenericScopedLock(const GenericScopedLock &) = delete;

private:
    const LockType &lock_;
};
template <typename LockType> class GenericScopedTryLock {
public:
    explicit GenericScopedTryLock(const LockType &l) noexcept : lock_(l), held_(l.tryEnter()) {}
    ~GenericScopedTryLock() noexcept { if (held_) lock_.exit(); }
    bool isLocked() const noexcept { return held_; }

private:
    const LockType &lock_;
    bool held_;
};
using ScopedLock = GenericScopedLock<CriticalSection>;
using ScopedTryLock = GenericScopedTryLock<CriticalSection>;

#define JUCE_DECLARE_SINGLETON(Classname, doNotRecreate)                                   \
    static Classname *getInstance() {                                                       \
        static Classname theOne;                                                            \
        return &theOne;                                                                     \
    }                                                                                       \
    static Classname *getInstanceWithoutCreating() { return getInstance(); }                \
    void clearSingletonInstance() {}
#define JUCE_IMPLEMENT_SINGLETON(Classname)

// a thing JUCE deletes when the app goes; here it simply is what it is
class DeletedAtShutdown {
protected:
    DeletedAtShutdown() = default;
    virtual ~DeletedAtShutdown() = default;
};

// juce::MemoryBlock: a lump of bytes, which is how a wavetable arrives before it is read
class MemoryBlock {
public:
    MemoryBlock() = default;
    MemoryBlock(const void *data, size_t size) : bytes_(static_cast<const char *>(data), static_cast<const char *>(data) + size) {}
    explicit MemoryBlock(size_t size, bool initialiseToZero = false) : bytes_(size, initialiseToZero ? 0 : 0) {}
    void *getData() noexcept { return bytes_.data(); }
    const void *getData() const noexcept { return bytes_.data(); }
    size_t getSize() const noexcept { return bytes_.size(); }
    void setSize(size_t n, bool = false) { bytes_.resize(n); }
    void reset() { bytes_.clear(); }
    bool isEmpty() const noexcept { return bytes_.empty(); }
    void append(const void *data, size_t size) {
        const char *p = static_cast<const char *>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }
    void replaceAll(const void *data, size_t size) { bytes_.assign(static_cast<const char *>(data), static_cast<const char *>(data) + size); }
    bool fromBase64Encoding(const std::string &text) {
        static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        bytes_.clear();
        int value = 0, bits = -8;
        for (char c : text) {
            const auto at = alphabet.find(c);
            if (at == std::string::npos) continue;
            value = (value << 6) + static_cast<int>(at);
            bits += 6;
            if (bits >= 0) { bytes_.push_back(static_cast<char>((value >> bits) & 0xff)); bits -= 8; }
        }
        return true;
    }

private:
    std::vector<char> bytes_;
};

class Identifier {
public:
    Identifier() = default;
    Identifier(const char *s) : name_(s == nullptr ? "" : s) {}
    Identifier(const String &s) : name_(s) {}
    const String &toString() const noexcept { return name_; }
    operator const String &() const noexcept { return name_; }
    bool operator==(const Identifier &o) const noexcept { return name_ == o.name_; }
    bool operator!=(const Identifier &o) const noexcept { return name_ != o.name_; }
    bool operator<(const Identifier &o) const noexcept { return name_ < o.name_; }
    bool isValid() const noexcept { return !name_.empty(); }

private:
    String name_;
};

// juce::var: a number, a flag or a string, which is all gin keeps in a tree
class var {
public:
    var() = default;
    var(int v) : number_(v), kind_(Kind::number) {}
    var(int64_t v) : number_(static_cast<double>(v)), kind_(Kind::number) {}
    var(double v) : number_(v), kind_(Kind::number) {}
    var(float v) : number_(static_cast<double>(v)), kind_(Kind::number) {}
    var(bool v) : number_(v ? 1 : 0), kind_(Kind::flag) {}
    var(const char *v) : text_(v == nullptr ? "" : v), kind_(Kind::text) {}
    var(const String &v) : text_(v), kind_(Kind::text) {}
    operator double() const noexcept { return number_; }
    operator float() const noexcept { return static_cast<float>(number_); }
    operator int() const noexcept { return static_cast<int>(number_); }
    operator long long() const noexcept { return static_cast<long long>(number_); }   // juce::int64 (LibreArp)
    operator bool() const noexcept { return kind_ == Kind::text ? !text_.empty() : number_ != 0; }
    String toString() const { return kind_ == Kind::text ? text_ : String(std::to_string(number_)); }
    operator String() const { return toString(); }
    bool isVoid() const noexcept { return kind_ == Kind::nothing; }
    bool isString() const noexcept { return kind_ == Kind::text; }
    bool operator==(const var &o) const noexcept { return kind_ == o.kind_ && number_ == o.number_ && text_ == o.text_; }
    bool operator!=(const var &o) const noexcept { return !(*this == o); }

private:
    enum class Kind { nothing, number, flag, text };
    double number_{0};
    String text_;
    Kind kind_{Kind::nothing};
};

class UndoManager;   // gin passes nullptr for it, always

// juce::Value: a handle on one property, which gin reads and writes
class Value {
public:
    Value() : held_(std::make_shared<var>()) {}
    explicit Value(const var &v) : held_(std::make_shared<var>(v)) {}
    explicit Value(std::shared_ptr<var> v) : held_(std::move(v)) {}
    var getValue() const { return *held_; }
    void setValue(const var &v) { *held_ = v; }
    operator var() const { return *held_; }
    Value &operator=(const var &v) { *held_ = v; return *this; }
    double getDouble() const { return static_cast<double>(*held_); }
    String toString() const { return held_->toString(); }

private:
    std::shared_ptr<var> held_;
};

// juce::ValueTree: a named node with properties and children, shared by reference as JUCE's is
class ValueTree {
public:
    ValueTree() = default;
    explicit ValueTree(const Identifier &type) : node_(std::make_shared<Node>()) { node_->type = type; }
    bool isValid() const noexcept { return node_ != nullptr; }
    Identifier getType() const { return node_ ? node_->type : Identifier(); }
    bool hasType(const Identifier &t) const { return node_ && node_->type == t; }
    bool hasProperty(const Identifier &name) const { return node_ && node_->properties.count(name.toString()) > 0; }
    var getProperty(const Identifier &name) const {
        if (!node_) return {};
        auto it = node_->properties.find(name.toString());
        return it == node_->properties.end() ? var() : *it->second;
    }
    var getProperty(const Identifier &name, const var &fallback) const {
        return hasProperty(name) ? getProperty(name) : fallback;
    }
    var operator[](const Identifier &name) const { return getProperty(name); }
    ValueTree &setProperty(const Identifier &name, const var &value, UndoManager *) {
        if (!node_) node_ = std::make_shared<Node>();
        auto &slot = node_->properties[name.toString()];
        if (!slot) slot = std::make_shared<var>();
        *slot = value;
        return *this;
    }
    Value getPropertyAsValue(const Identifier &name, UndoManager *) {
        if (!node_) node_ = std::make_shared<Node>();
        auto &slot = node_->properties[name.toString()];
        if (!slot) slot = std::make_shared<var>();
        return Value(slot);
    }
    void removeProperty(const Identifier &name, UndoManager *) { if (node_) node_->properties.erase(name.toString()); }
    int getNumChildren() const { return node_ ? static_cast<int>(node_->children.size()) : 0; }
    ValueTree getChild(int i) const {
        if (!node_ || i < 0 || i >= getNumChildren()) return {};
        return node_->children[static_cast<size_t>(i)];
    }
    ValueTree getChildWithName(const Identifier &t) const {
        for (int i = 0; i < getNumChildren(); ++i) if (getChild(i).hasType(t)) return getChild(i);
        return {};
    }
    void addChild(const ValueTree &child, int index, UndoManager *) {
        if (!node_) node_ = std::make_shared<Node>();
        if (index < 0 || index >= static_cast<int>(node_->children.size())) node_->children.push_back(child);
        else node_->children.insert(node_->children.begin() + index, child);
    }
    void removeAllChildren(UndoManager *) { if (node_) node_->children.clear(); }
    void appendChild(const ValueTree &child, UndoManager *undo) { addChild(child, -1, undo); }
    // NorniOS: no XML (LibreArp's pattern files); a tree writes nothing and reads nothing
    String toXmlString() const { return {}; }
    static ValueTree fromXml(const class XmlElement &) { return {}; }
    ValueTree getOrCreateChildWithName(const Identifier &t, UndoManager *undo) {
        auto found = getChildWithName(t);
        if (found.isValid()) return found;
        ValueTree made(t);
        addChild(made, -1, undo);
        return made;
    }
    const ValueTree *begin() const noexcept { return node_ && !node_->children.empty() ? node_->children.data() : nullptr; }
    const ValueTree *end() const noexcept {
        return node_ && !node_->children.empty() ? node_->children.data() + node_->children.size() : nullptr;
    }
    ValueTree createCopy() const {
        ValueTree copy;
        if (node_) { copy.node_ = std::make_shared<Node>(*node_); }
        return copy;
    }
    bool operator==(const ValueTree &o) const noexcept { return node_ == o.node_; }
    bool operator!=(const ValueTree &o) const noexcept { return node_ != o.node_; }

private:
    struct Node {
        Identifier type;
        std::map<String, std::shared_ptr<var>> properties;
        std::vector<ValueTree> children;
    };
    std::shared_ptr<Node> node_;
};

// MIDI and MPE, as much of them as the gin library's synthesiser asks for (Vendor/wavetable/README.md). NorniOS
// plays a voice from its melody generator or a MIDI keyboard, on one channel, so the zones and per-note channels
// of real MPE are not what this carries: it carries a note with its velocity, pressure, timbre and pitchbend, and
// the note-on and note-off that start and stop it.
class MidiMessage {
public:
    MidiMessage() = default;
    static MidiMessage noteOn(int channel, int note, float velocity) {
        MidiMessage m; m.channel_ = channel; m.note_ = note; m.velocity_ = velocity; m.kind_ = Kind::noteOn; return m;
    }
    static MidiMessage noteOff(int channel, int note) {
        MidiMessage m; m.channel_ = channel; m.note_ = note; m.kind_ = Kind::noteOff; return m;
    }
    static MidiMessage pitchWheel(int channel, int value) {
        MidiMessage m; m.channel_ = channel; m.wheel_ = value; m.kind_ = Kind::pitchWheel; return m;
    }
    static MidiMessage controllerEvent(int channel, int controller, int value) {
        MidiMessage m; m.channel_ = channel; m.controller_ = controller; m.value_ = value; m.kind_ = Kind::controller; return m;
    }
    static MidiMessage allNotesOff(int channel) {
        MidiMessage m; m.channel_ = channel; m.kind_ = Kind::allNotesOff; return m;
    }
    static String getControllerName(int controller) { return "CC " + std::to_string(controller); }
    int getChannel() const noexcept { return channel_; }
    int getNoteNumber() const noexcept { return note_; }
    float getFloatVelocity() const noexcept { return velocity_; }
    int getVelocity() const noexcept { return static_cast<int>(velocity_ * 127.f + 0.5f); }
    int getPitchWheelValue() const noexcept { return wheel_; }
    int getControllerNumber() const noexcept { return controller_; }
    int getControllerValue() const noexcept { return value_; }
    bool isNoteOn(bool = false) const noexcept { return kind_ == Kind::noteOn && velocity_ > 0.f; }
    bool isNoteOff(bool = true) const noexcept { return kind_ == Kind::noteOff || (kind_ == Kind::noteOn && velocity_ <= 0.f); }
    bool isNoteOnOrOff() const noexcept { return kind_ == Kind::noteOn || kind_ == Kind::noteOff; }
    bool isPitchWheel() const noexcept { return kind_ == Kind::pitchWheel; }
    bool isController() const noexcept { return kind_ == Kind::controller; }
    bool isAllNotesOff() const noexcept { return kind_ == Kind::allNotesOff; }
    bool isAllSoundOff() const noexcept { return kind_ == Kind::allNotesOff; }
    bool isAftertouch() const noexcept { return false; }
    bool isSustainPedalOn() const noexcept { return kind_ == Kind::controller && controller_ == 64 && value_ >= 64; }
    bool isSustainPedalOff() const noexcept { return kind_ == Kind::controller && controller_ == 64 && value_ < 64; }
    // none of these ever arrive here: a module is played with notes, a wheel and controllers
    bool isActiveSense() const noexcept { return false; }
    bool isEndOfTrackMetaEvent() const noexcept { return false; }
    bool isFullFrame() const noexcept { return false; }
    bool isKeySignatureMetaEvent() const noexcept { return false; }
    bool isMetaEvent() const noexcept { return false; }
    bool isMidiChannelMetaEvent() const noexcept { return false; }
    bool isMidiClock() const noexcept { return false; }
    bool isMidiMachineControlMessage() const noexcept { return false; }
    bool isMidiStart() const noexcept { return false; }
    bool isMidiStop() const noexcept { return false; }
    bool isProgramChange() const noexcept { return false; }
    bool isQuarterFrame() const noexcept { return false; }
    bool isSongPositionPointer() const noexcept { return false; }
    bool isSysEx() const noexcept { return false; }
    bool isTempoMetaEvent() const noexcept { return false; }
    bool isTextMetaEvent() const noexcept { return false; }
    bool isTimeSignatureMetaEvent() const noexcept { return false; }
    bool isTrackMetaEvent() const noexcept { return false; }
    bool isTrackNameEvent() const noexcept { return false; }
    bool isChannelPressure() const noexcept { return false; }

private:
    enum class Kind { none, noteOn, noteOff, pitchWheel, controller, allNotesOff };
    int channel_{1}, note_{0}, wheel_{8192}, controller_{0}, value_{0};
    float velocity_{0};
    Kind kind_{Kind::none};
};

class MidiBuffer {
public:
    struct Entry {
        MidiMessage message;
        int samplePosition{0};
        MidiMessage getMessage() const { return message; }
    };
    void clear() noexcept { entries_.clear(); }
    void addEvent(const MidiMessage &m, int samplePosition) { entries_.push_back({m, samplePosition}); }
    void addEvents(const MidiBuffer &other, int startSample, int numSamples, int sampleDelta) {
        for (const auto &e : other) {
            if (e.samplePosition < startSample) continue;
            if (numSamples >= 0 && e.samplePosition >= startSample + numSamples) continue;
            entries_.push_back({e.message, e.samplePosition + sampleDelta});
        }
    }
    bool isEmpty() const noexcept { return entries_.empty(); }
    int getNumEvents() const noexcept { return static_cast<int>(entries_.size()); }
    const Entry *begin() const noexcept { return entries_.data(); }
    const Entry *end() const noexcept { return entries_.data() + entries_.size(); }

private:
    std::vector<Entry> entries_;
};

// juce::MPEValue: a value from the wire, read back as a proportion or as a signed amount
class MPEValue {
public:
    MPEValue() = default;
    static MPEValue from7BitInt(int v) { MPEValue m; m.raw_ = jlimit(0, 127, v) << 7; return m; }
    static MPEValue from14BitInt(int v) { MPEValue m; m.raw_ = jlimit(0, 16383, v); return m; }
    static MPEValue centreValue() { return from14BitInt(8192); }
    static MPEValue minValue() { return from14BitInt(0); }
    static MPEValue maxValue() { return from14BitInt(16383); }
    float asUnsignedFloat() const noexcept { return static_cast<float>(raw_) / 16383.f; }
    float asSignedFloat() const noexcept {
        return raw_ >= 8192 ? static_cast<float>(raw_ - 8192) / 8191.f : static_cast<float>(raw_ - 8192) / 8192.f;
    }
    int as14BitInt() const noexcept { return raw_; }
    bool operator==(const MPEValue &o) const noexcept { return raw_ == o.raw_; }

private:
    int raw_{8192};
};

struct MPENote {
    enum KeyState { off = 0, keyDown = 1, sustained = 2, keyDownAndSustained = 3 };
    MPENote() = default;
    MPENote(int channel, int note, MPEValue velocity, MPEValue timbreIn, MPEValue pressureIn, KeyState state)
        : midiChannel(channel), initialNote(note), noteOnVelocity(velocity), timbre(timbreIn), pressure(pressureIn),
          keyState(state) {}
    bool isValid() const noexcept { return noteID != 0; }
    double getFrequencyInHertz(double frequencyOfA = 440.0) const {
        return frequencyOfA * std::pow(2.0, (initialNote + totalPitchbendInSemitones - 69.0) / 12.0);
    }
    bool operator==(const MPENote &o) const noexcept { return noteID == o.noteID; }
    bool operator!=(const MPENote &o) const noexcept { return noteID != o.noteID; }

    uint16 noteID{0};
    uint8 midiChannel{1};
    uint8 initialNote{0};
    MPEValue noteOnVelocity{MPEValue::from7BitInt(100)};
    MPEValue noteOffVelocity{MPEValue::from7BitInt(64)};
    MPEValue pitchbend{MPEValue::centreValue()};
    MPEValue pressure{MPEValue::from7BitInt(0)};
    MPEValue timbre{MPEValue::from7BitInt(64)};
    double totalPitchbendInSemitones{0};
    KeyState keyState{keyDown};
};

class MPEZoneLayout {
public:
    void clearAllZones() {}
    void setLowerZone(int, int = 2, int = 2) {}
    void setUpperZone(int, int = 2, int = 2) {}
};

class MPESynthesiserVoice {
public:
    virtual ~MPESynthesiserVoice() = default;
    virtual void noteStarted() = 0;
    virtual void noteStopped(bool allowTailOff) = 0;
    virtual void notePressureChanged() = 0;
    virtual void notePitchbendChanged() = 0;
    virtual void noteTimbreChanged() = 0;
    virtual void noteKeyStateChanged() = 0;
    virtual void renderNextBlock(AudioBuffer<float> &outputBuffer, int startSample, int numSamples) = 0;
    virtual void setCurrentSampleRate(double newRate) { currentSampleRate = newRate; }
    double getSampleRate() const noexcept { return currentSampleRate; }
    bool isActive() const noexcept { return currentlyPlayingNote.isValid(); }
    bool isPlayingButReleased() const noexcept {
        return isActive() && currentlyPlayingNote.keyState == MPENote::off;
    }
    void clearCurrentNote() noexcept { currentlyPlayingNote = MPENote(); }
    const MPENote &getCurrentlyPlayingNote() const noexcept { return currentlyPlayingNote; }
    bool isCurrentlyPlayingNote(MPENote note) const noexcept {
        return currentlyPlayingNote.isValid() && currentlyPlayingNote.noteID == note.noteID;
    }

    MPENote currentlyPlayingNote;
    double currentSampleRate{0};
    uint32 noteOnTime{0};
};

// juce::MPEInstrument, as much of it as is used here: the notes that are sounding, and who to tell when one comes
// or goes. One channel, no zones — what a keyboard or NorniOS's melody generator sends.
class MPEInstrument {
public:
    enum TrackingMode { lastNotePlayedOnChannel, lowestNoteOnChannel, highestNoteOnChannel, allNotesOnChannel };
    struct Listener {
        virtual ~Listener() = default;
        virtual void noteAdded(MPENote) {}
        virtual void noteReleased(MPENote) {}
        virtual void notePressureChanged(MPENote) {}
        virtual void notePitchbendChanged(MPENote) {}
        virtual void noteTimbreChanged(MPENote) {}
        virtual void noteKeyStateChanged(MPENote) {}
    };
    void addListener(Listener *l) { if (l != nullptr) listeners_.push_back(l); }
    void removeListener(Listener *l) { listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end()); }
    void setZoneLayout(MPEZoneLayout) {}
    void enableLegacyMode(int pitchbendRange = 2, int = 1) { legacyPitchbendRange_ = pitchbendRange; }
    bool isLegacyModeEnabled() const noexcept { return true; }
    int getLegacyModePitchbendRange() const noexcept { return legacyPitchbendRange_; }
    void setPitchbendTrackingMode(TrackingMode) {}

    int getNumPlayingNotes() const noexcept { return static_cast<int>(notes_.size()); }
    MPENote getNote(int index) const {
        return (index >= 0 && index < getNumPlayingNotes()) ? notes_[static_cast<size_t>(index)] : MPENote();
    }

    void noteOn(int channel, int note, MPEValue velocity) {
        MPENote n(channel, note, velocity, MPEValue::from7BitInt(64), MPEValue::from7BitInt(0), MPENote::keyDown);
        n.noteID = ++lastNoteID_;
        n.pitchbend = wheel_;
        n.totalPitchbendInSemitones = wheel_.asSignedFloat() * legacyPitchbendRange_;
        notes_.push_back(n);
        for (auto *l : listeners_) l->noteAdded(n);
    }
    void noteOff(int channel, int note, MPEValue velocity) {
        for (size_t i = notes_.size(); i-- > 0;) {
            if (notes_[i].midiChannel == channel && notes_[i].initialNote == note) {
                MPENote gone = notes_[i];
                gone.noteOffVelocity = velocity;
                gone.keyState = MPENote::off;
                notes_.erase(notes_.begin() + static_cast<long>(i));
                for (auto *l : listeners_) l->noteReleased(gone);
                return;
            }
        }
    }
    void allNotesOff() {
        auto going = notes_;
        notes_.clear();
        for (auto &n : going) {
            n.keyState = MPENote::off;
            for (auto *l : listeners_) l->noteReleased(n);
        }
    }
    void pitchbend(int channel, MPEValue value) {
        wheel_ = value;
        for (auto &n : notes_) {
            if (n.midiChannel != channel) continue;
            n.pitchbend = value;
            n.totalPitchbendInSemitones = value.asSignedFloat() * legacyPitchbendRange_;
            for (auto *l : listeners_) l->notePitchbendChanged(n);
        }
    }

private:
    std::vector<MPENote> notes_;
    std::vector<Listener *> listeners_;
    MPEValue wheel_{MPEValue::centreValue()};
    int legacyPitchbendRange_{2};
    uint16 lastNoteID_{0};
};

// juce::MPESynthesiser, as much as gin's synthesiser builds on: it holds the voices, hands notes to them, and
// renders them into a buffer. Voice allocation and stealing are gin's own, above this.
class MPESynthesiser : public MPEInstrument::Listener {
public:
    MPESynthesiser() { instrument.addListener(this); }
    ~MPESynthesiser() override { instrument.removeListener(this); }

    void addVoice(MPESynthesiserVoice *newVoice) {
        const ScopedLock sl(voicesLock);
        voices.add(newVoice);
        newVoice->setCurrentSampleRate(sampleRate_);
    }
    void clearVoices() { const ScopedLock sl(voicesLock); voices.clear(); }
    int getNumVoices() const noexcept { return voices.size(); }
    MPESynthesiserVoice *getVoice(int index) const { return voices[index]; }

    void setCurrentPlaybackSampleRate(double newRate) {
        const ScopedLock sl(voicesLock);
        sampleRate_ = newRate;
        turnOffAllVoices(false);
        for (auto *v : voices) v->setCurrentSampleRate(newRate);
    }
    double getSampleRate() const noexcept { return sampleRate_; }

    void enableLegacyMode(int pitchbendRange = 2) { instrument.enableLegacyMode(pitchbendRange); }
    void setLegacyModePitchbendRange(int range) { instrument.enableLegacyMode(range); }
    int getLegacyModePitchbendRange() const noexcept { return instrument.getLegacyModePitchbendRange(); }
    void setPitchbendTrackingMode(MPEInstrument::TrackingMode m) { instrument.setPitchbendTrackingMode(m); }
    void setZoneLayout(MPEZoneLayout layout) { instrument.setZoneLayout(layout); }

    virtual void handleMidiEvent(const MidiMessage &m) {
        if (m.isNoteOn()) instrument.noteOn(m.getChannel(), m.getNoteNumber(), MPEValue::from7BitInt(m.getVelocity()));
        else if (m.isNoteOff()) instrument.noteOff(m.getChannel(), m.getNoteNumber(), MPEValue::from7BitInt(64));
        else if (m.isPitchWheel()) instrument.pitchbend(m.getChannel(), MPEValue::from14BitInt(m.getPitchWheelValue()));
        else if (m.isAllNotesOff()) instrument.allNotesOff();
    }

    template <typename FloatType>
    void renderNextBlock(AudioBuffer<FloatType> &outputAudio, const MidiBuffer &midi, int startSample, int numSamples) {
        const ScopedLock sl(voicesLock);
        int at = startSample;
        for (const auto &event : midi) {
            const int until = jlimit(startSample, startSample + numSamples, event.samplePosition);
            if (until > at) { renderVoices(outputAudio, at, until - at); at = until; }
            handleMidiEvent(event.getMessage());
        }
        if (startSample + numSamples > at) renderVoices(outputAudio, at, startSample + numSamples - at);
    }

    virtual void startVoice(MPESynthesiserVoice *voice, MPENote noteToStart) {
        if (voice == nullptr) return;
        voice->currentlyPlayingNote = noteToStart;
        voice->noteOnTime = ++noteOnCounter_;
        voice->noteStarted();
    }
    virtual void stopVoice(MPESynthesiserVoice *voice, MPENote noteToStop, bool allowTailOff) {
        if (voice == nullptr) return;
        voice->currentlyPlayingNote = noteToStop;
        voice->noteStopped(allowTailOff);
    }
    virtual void turnOffAllVoices(bool allowTailOff) {
        for (auto *voice : voices) {
            voice->currentlyPlayingNote.keyState = MPENote::off;
            voice->noteStopped(allowTailOff);
        }
    }
    MPESynthesiserVoice *findFreeVoice(MPENote, bool stealIfNoneAvailable) const {
        for (auto *voice : voices) if (!voice->isActive()) return voice;
        return stealIfNoneAvailable ? findVoiceToSteal(MPENote()) : nullptr;
    }
    virtual MPESynthesiserVoice *findVoiceToSteal(MPENote = MPENote()) const {
        MPESynthesiserVoice *oldest = nullptr;
        for (auto *voice : voices)
            if (voice->isActive() && (oldest == nullptr || voice->noteOnTime < oldest->noteOnTime)) oldest = voice;
        return oldest;
    }

    OwnedArray<MPESynthesiserVoice> voices;
    MPEInstrument instrument;
    CriticalSection voicesLock;

private:
    template <typename FloatType>
    void renderVoices(AudioBuffer<FloatType> &output, int startSample, int numSamples) {
        // only the voices that hold a note, as JUCE's own does: a voice asked to render when it has nothing to
        // play still runs every oscillator it has
        for (auto *voice : voices)
            if (voice->isActive()) voice->renderNextBlock(output, startSample, numSamples);
    }
    double sampleRate_{0};
    uint32 noteOnCounter_{0};
};

// juce::IIRCoefficients and juce::IIRFilter: JUCE's older, direct-form biquad, which the gin library filters with.
// The coefficient formulas are the Audio EQ Cookbook's, as JUCE's are.
class IIRCoefficients {
public:
    IIRCoefficients() { coefficients[0] = 1; for (int i = 1; i < 5; ++i) coefficients[i] = 0; }
    IIRCoefficients(double c0, double c1, double c2, double c3, double c4, double c5) {
        const double a = 1.0 / c3;
        coefficients[0] = static_cast<float>(c0 * a);
        coefficients[1] = static_cast<float>(c1 * a);
        coefficients[2] = static_cast<float>(c2 * a);
        coefficients[3] = static_cast<float>(c4 * a);
        coefficients[4] = static_cast<float>(c5 * a);
    }
    static IIRCoefficients makeLowPass(double sampleRate, double frequency, double Q) {
        const double n = 1.0 / std::tan(MathConstants<double>::pi * frequency / sampleRate);
        const double nSquared = n * n, c1 = 1.0 / (1.0 + Q * n + nSquared);
        return IIRCoefficients(c1 * Q, c1 * 2.0 * Q, c1 * Q, 1.0, c1 * 2.0 * Q * (1.0 - nSquared),
                               c1 * (Q - Q * n + nSquared * Q));
    }
    static IIRCoefficients makeLowPass(double sampleRate, double frequency) {
        return makeLowPass(sampleRate, frequency, 1.0 / MathConstants<double>::sqrt2);
    }
    static IIRCoefficients makeHighPass(double sampleRate, double frequency, double Q) {
        const double n = std::tan(MathConstants<double>::pi * frequency / sampleRate);
        const double nSquared = n * n, c1 = 1.0 / (1.0 + Q * n + nSquared);
        return IIRCoefficients(c1 * Q, c1 * -2.0 * Q, c1 * Q, 1.0, c1 * 2.0 * Q * (nSquared - 1.0),
                               c1 * (Q - Q * n + nSquared * Q));
    }
    static IIRCoefficients makeHighPass(double sampleRate, double frequency) {
        return makeHighPass(sampleRate, frequency, 1.0 / MathConstants<double>::sqrt2);
    }
    static IIRCoefficients makeBandPass(double sampleRate, double frequency, double Q) {
        const double n = 1.0 / std::tan(MathConstants<double>::pi * frequency / sampleRate);
        const double nSquared = n * n, c1 = 1.0 / (1.0 + n / Q + nSquared);
        return IIRCoefficients(c1 * n / Q, 0, -c1 * n / Q, 1.0, c1 * 2.0 * (1.0 - nSquared),
                               c1 * (1.0 - n / Q + nSquared));
    }
    static IIRCoefficients makeBandPass(double sampleRate, double frequency) {
        return makeBandPass(sampleRate, frequency, 1.0 / MathConstants<double>::sqrt2);
    }
    static IIRCoefficients makeNotchFilter(double sampleRate, double frequency, double Q) {
        const double n = 1.0 / std::tan(MathConstants<double>::pi * frequency / sampleRate);
        const double nSquared = n * n, c1 = 1.0 / (1.0 + n / Q + nSquared);
        return IIRCoefficients(c1 * (1.0 + nSquared), 2.0 * c1 * (1.0 - nSquared), c1 * (1.0 + nSquared), 1.0,
                               c1 * 2.0 * (1.0 - nSquared), c1 * (1.0 - n / Q + nSquared));
    }
    static IIRCoefficients makeNotchFilter(double sampleRate, double frequency) {
        return makeNotchFilter(sampleRate, frequency, 1.0 / MathConstants<double>::sqrt2);
    }
    static IIRCoefficients makeAllPass(double sampleRate, double frequency, double Q) {
        const double n = 1.0 / std::tan(MathConstants<double>::pi * frequency / sampleRate);
        const double nSquared = n * n, c1 = 1.0 / (1.0 + n / Q + nSquared);
        return IIRCoefficients(c1 * (1.0 - n / Q + nSquared), c1 * 2.0 * (1.0 - nSquared), 1.0, 1.0,
                               c1 * 2.0 * (1.0 - nSquared), c1 * (1.0 - n / Q + nSquared));
    }
    static IIRCoefficients makeAllPass(double sampleRate, double frequency) {
        return makeAllPass(sampleRate, frequency, 1.0 / MathConstants<double>::sqrt2);
    }
    static IIRCoefficients makeLowShelf(double sampleRate, double cutOffFrequency, double Q, float gainFactor) {
        const double A = jmax(0.0, std::sqrt(static_cast<double>(gainFactor)));
        const double aminus1 = A - 1.0, aplus1 = A + 1.0;
        const double omega = (MathConstants<double>::twoPi * jmax(cutOffFrequency, 2.0)) / sampleRate;
        const double coso = std::cos(omega), beta = std::sin(omega) * std::sqrt(A) / Q;
        const double aminus1TimesCoso = aminus1 * coso;
        return IIRCoefficients(A * (aplus1 - aminus1TimesCoso + beta), A * 2.0 * (aminus1 - aplus1 * coso),
                               A * (aplus1 - aminus1TimesCoso - beta), aplus1 + aminus1TimesCoso + beta,
                               -2.0 * (aminus1 + aplus1 * coso), aplus1 + aminus1TimesCoso - beta);
    }
    static IIRCoefficients makeHighShelf(double sampleRate, double cutOffFrequency, double Q, float gainFactor) {
        const double A = jmax(0.0, std::sqrt(static_cast<double>(gainFactor)));
        const double aminus1 = A - 1.0, aplus1 = A + 1.0;
        const double omega = (MathConstants<double>::twoPi * jmax(cutOffFrequency, 2.0)) / sampleRate;
        const double coso = std::cos(omega), beta = std::sin(omega) * std::sqrt(A) / Q;
        const double aminus1TimesCoso = aminus1 * coso;
        return IIRCoefficients(A * (aplus1 + aminus1TimesCoso + beta), A * -2.0 * (aminus1 + aplus1 * coso),
                               A * (aplus1 + aminus1TimesCoso - beta), aplus1 - aminus1TimesCoso + beta,
                               2.0 * (aminus1 - aplus1 * coso), aplus1 - aminus1TimesCoso - beta);
    }
    static IIRCoefficients makePeakFilter(double sampleRate, double frequency, double Q, float gainFactor) {
        const double A = jmax(0.0, std::sqrt(static_cast<double>(gainFactor)));
        const double omega = (MathConstants<double>::twoPi * jmax(frequency, 2.0)) / sampleRate;
        const double alpha = 0.5 * std::sin(omega) / Q, c2 = -2.0 * std::cos(omega);
        const double alphaTimesA = alpha * A, alphaOverA = alpha / A;
        return IIRCoefficients(1.0 + alphaTimesA, c2, 1.0 - alphaTimesA, 1.0 + alphaOverA, c2, 1.0 - alphaOverA);
    }
    float coefficients[5]{};
};

class IIRFilter {
public:
    IIRFilter() = default;
    void setCoefficients(const IIRCoefficients &newCoefficients) noexcept { coefficients = newCoefficients; }
    void makeInactive() noexcept { coefficients = IIRCoefficients(); }
    void reset() noexcept { v1 = v2 = 0; }
    float processSingleSampleRaw(float in) noexcept {
        const float *c = coefficients.coefficients;
        float out = c[0] * in + v1;
        v1 = c[1] * in - c[3] * out + v2;
        v2 = c[2] * in - c[4] * out;
        return out;
    }
    void processSamples(float *samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) samples[i] = processSingleSampleRaw(samples[i]);
    }
    IIRCoefficients coefficients;

private:
    float v1{0}, v2{0};
};

// juce::Time, for the one thing gin asks of it: how long a block took
class Time {
public:
    static double getMillisecondCounterHiRes() noexcept {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
};

// juce::Point, for the little gin uses of it
template <typename T> struct Point {
    Point() = default;
    Point(T xx, T yy) : x(xx), y(yy) {}
    T getX() const noexcept { return x; }
    T getY() const noexcept { return y; }
    T x{}, y{};
};

// juce::SortedSet: unique elements kept in order (LibreArp's held notes); add() leaves an equal element in place
template <typename ElementType>
class SortedSet {
public:
    SortedSet() { items_.reserve(128); }
    bool add(const ElementType &v) {
        auto it = std::lower_bound(items_.begin(), items_.end(), v);
        if (it != items_.end() && *it == v) return false;
        items_.insert(it, v);
        return true;
    }
    void removeValue(const ElementType &v) {
        auto it = std::lower_bound(items_.begin(), items_.end(), v);
        if (it != items_.end() && *it == v) items_.erase(it);
    }
    int size() const noexcept { return static_cast<int>(items_.size()); }
    bool isEmpty() const noexcept { return items_.empty(); }
    void clear() noexcept { items_.clear(); }
    // as JUCE: an index out of range gives a default element
    ElementType operator[](int i) const { return i >= 0 && i < size() ? items_[static_cast<size_t>(i)] : ElementType(); }
    ElementType getFirst() const { return (*this)[0]; }
    ElementType getLast() const { return (*this)[size() - 1]; }
private:
    std::vector<ElementType> items_;
};

// juce::Atomic: std::atomic with JUCE's get/set/exchange/compareAndSetBool and arithmetic (Stochas)
template <typename Type>
struct Atomic {
    Atomic() noexcept : value(Type()) {}
    Atomic(Type initial) noexcept : value(initial) {}
    Atomic(const Atomic &other) noexcept : value(other.get()) {}
    Atomic &operator=(const Atomic &other) noexcept { value.store(other.get()); return *this; }
    Atomic &operator=(Type v) noexcept { value.store(v); return *this; }
    Type get() const noexcept { return value.load(); }
    void set(Type v) noexcept { value.store(v); }
    Type exchange(Type v) noexcept { return value.exchange(v); }
    bool compareAndSetBool(Type newValue, Type valueToCompare) noexcept { return value.compare_exchange_strong(valueToCompare, newValue); }
    Type operator+=(Type amount) noexcept { return value += amount; }
    Type operator-=(Type amount) noexcept { return value -= amount; }
    Type operator++() noexcept { return ++value; }
    Type operator--() noexcept { return --value; }
    void memoryBarrier() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }
    std::atomic<Type> value;
};

// juce::AbstractFifo: the index arithmetic of a lock-free ring, which gin's audio fifo is built on
class AbstractFifo {
public:
    explicit AbstractFifo(int capacity) : capacity_(capacity) {}
    int getTotalSize() const noexcept { return capacity_; }
    int getFreeSpace() const noexcept { return capacity_ - getNumReady() - 1; }
    int getNumReady() const noexcept {
        const int vs = validStart_.load(), ve = validEnd_.load();
        return vs <= ve ? ve - vs : capacity_ - (vs - ve);
    }
    void reset() noexcept { validEnd_.store(0); validStart_.store(0); }
    void setTotalSize(int newSize) noexcept { capacity_ = newSize; reset(); }
    void prepareToWrite(int numToWrite, int &startIndex1, int &blockSize1, int &startIndex2, int &blockSize2) const noexcept {
        const int ve = validEnd_.load(), vs = validStart_.load();
        int freeSpace = ve >= vs ? capacity_ - (ve - vs) : vs - ve;
        numToWrite = jmin(numToWrite, freeSpace - 1);
        if (numToWrite <= 0) { startIndex1 = startIndex2 = 0; blockSize1 = blockSize2 = 0; return; }
        startIndex1 = ve;
        startIndex2 = 0;
        blockSize1 = jmin(capacity_ - ve, numToWrite);
        numToWrite -= blockSize1;
        blockSize2 = numToWrite <= 0 ? 0 : jmin(numToWrite, capacity_);
    }
    void finishedWrite(int numWritten) noexcept {
        int newEnd = validEnd_.load() + numWritten;
        if (newEnd >= capacity_) newEnd -= capacity_;
        validEnd_.store(newEnd);
    }
    void prepareToRead(int numWanted, int &startIndex1, int &blockSize1, int &startIndex2, int &blockSize2) const noexcept {
        const int ve = validEnd_.load(), vs = validStart_.load();
        int numReady = ve >= vs ? ve - vs : capacity_ - (vs - ve);
        numWanted = jmin(numWanted, numReady);
        if (numWanted <= 0) { startIndex1 = startIndex2 = 0; blockSize1 = blockSize2 = 0; return; }
        startIndex1 = vs;
        startIndex2 = 0;
        blockSize1 = jmin(capacity_ - vs, numWanted);
        numWanted -= blockSize1;
        blockSize2 = numWanted <= 0 ? 0 : jmin(numWanted, capacity_);
    }
    void finishedRead(int numRead) noexcept {
        int newStart = validStart_.load() + numRead;
        if (newStart >= capacity_) newStart -= capacity_;
        validStart_.store(newStart);
    }

private:
    int capacity_;
    std::atomic<int> validStart_{0}, validEnd_{0};
};

// juce::NormalisableRange: a range with a skew, which is how a plugin's parameters are spelled
template <typename T> class NormalisableRange {
public:
    NormalisableRange() = default;
    NormalisableRange(T rangeStart, T rangeEnd, T intervalValue = T(), T skewFactor = T(1), bool useSymmetricSkew = false)
        : start(rangeStart), end(rangeEnd), interval(intervalValue), skew(skewFactor), symmetricSkew(useSymmetricSkew) {}
    T convertTo0to1(T v) const noexcept {
        if (end <= start) return T();
        T proportion = (v - start) / (end - start);
        if (skew != T(1) && proportion > T()) proportion = std::pow(proportion, skew);
        return proportion;
    }
    T convertFrom0to1(T proportion) const noexcept {
        proportion = jlimit(T(), T(1), proportion);
        if (skew != T(1) && proportion > T()) proportion = std::exp(std::log(proportion) / skew);
        return start + (end - start) * proportion;
    }
    T snapToLegalValue(T v) const noexcept {
        if (interval > T()) v = start + interval * std::floor((v - start) / interval + T(0.5));
        return jlimit(start, end, v);
    }
    T getRange() const noexcept { return end - start; }
    // JUCE's: the skew that puts `centre` halfway along the range
    void setSkewForCentre(T centre) noexcept {
        skew = std::log(T(0.5)) / std::log((centre - start) / (end - start));
        symmetricSkew = false;
    }
    T start{}, end{}, interval{}, skew{1};
    bool symmetricSkew{false};
};

// juce::ListenerList, as gin uses it: add, remove, and call them all
template <typename ListenerClass> class ListenerList {
public:
    void add(ListenerClass *l) { if (l != nullptr && !contains(l)) listeners_.push_back(l); }
    void remove(ListenerClass *l) { listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end()); }
    bool contains(ListenerClass *l) const { return std::find(listeners_.begin(), listeners_.end(), l) != listeners_.end(); }
    int size() const noexcept { return static_cast<int>(listeners_.size()); }
    bool isEmpty() const noexcept { return listeners_.empty(); }
    void clear() noexcept { listeners_.clear(); }
    template <typename Callback> void call(Callback &&callback) {
        auto copy = listeners_;
        for (auto *l : copy) callback(*l);
    }
    template <typename Method, typename... Args> void call(Method method, Args &&...args) {
        auto copy = listeners_;
        for (auto *l : copy) (l->*method)(args...);
    }

private:
    std::vector<ListenerClass *> listeners_;
};

// juce::AudioPlayHead: where the host's transport is, which gin's LFOs may follow
template <typename T> class Optional : public std::optional<T> {
public:
    using std::optional<T>::optional;
    Optional(const std::optional<T> &o) : std::optional<T>(o) {}
    bool hasValue() const noexcept { return this->has_value(); }
    const T &operator*() const { return this->value(); }
};

class AudioPlayHead {
public:
    struct PositionInfo {
        Optional<double> getBpm() const { return bpm; }
        Optional<double> getPpqPosition() const { return ppqPosition; }
        Optional<double> getTimeInSeconds() const { return timeInSeconds; }
        struct TimeSignature { int numerator{4}, denominator{4}; };
        Optional<TimeSignature> getTimeSignature() const { return timeSignature; }
        void setTimeSignature(TimeSignature v) { timeSignature = v; }
        TimeSignature timeSignature;
        bool getIsPlaying() const { return isPlaying; }
        void setBpm(std::optional<double> v) { bpm = v; }
        void setPpqPosition(std::optional<double> v) { ppqPosition = v; }
        void setTimeInSeconds(std::optional<double> v) { timeInSeconds = v; }
        void setIsPlaying(bool v) { isPlaying = v; }
        std::optional<double> bpm{120.0}, ppqPosition{0.0}, timeInSeconds{0.0};
        bool isPlaying{false};
    };
    virtual ~AudioPlayHead() = default;
    virtual Optional<PositionInfo> getPosition() const { return position; }
    // the older JUCE API ChowMatrix reads
    struct CurrentPositionInfo {
        double bpm = 120.0, ppqPosition = 0.0, timeInSeconds = 0.0;
        bool isPlaying = false;
    };
    virtual bool getCurrentPosition(CurrentPositionInfo &result) {
        result.bpm = position.bpm.value_or(120.0);
        result.ppqPosition = position.ppqPosition.value_or(0.0);
        result.timeInSeconds = position.timeInSeconds.value_or(0.0);
        result.isPlaying = position.isPlaying;
        return true;
    }
    PositionInfo position;
};

// juce::AudioProcessorParameter, which gin's Parameter is one of
class AudioProcessorParameter {
public:
    enum Category { genericParameter = 0 };
    struct Listener {
        virtual ~Listener() = default;
        virtual void parameterValueChanged(int parameterIndex, float newValue) = 0;
        virtual void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) = 0;
    };
    AudioProcessorParameter() = default;
    virtual ~AudioProcessorParameter() = default;
    virtual float getValue() const = 0;
    virtual void setValue(float newValue) = 0;
    virtual float getDefaultValue() const = 0;
    virtual String getName(int maximumStringLength) const = 0;
    virtual String getLabel() const = 0;
    virtual float getValueForText(const String &text) const = 0;
    virtual String getText(float value, int maximumStringLength) const = 0;
    virtual int getNumSteps() const { return 0x7fffffff; }
    virtual bool isDiscrete() const { return false; }
    virtual bool isBoolean() const { return false; }
    virtual bool isAutomatable() const { return true; }
    virtual bool isMetaParameter() const { return false; }
    virtual bool isOrientationInverted() const { return false; }
    virtual Category getCategory() const { return genericParameter; }
    virtual String getCurrentValueAsText() const { return getText(getValue(), 1024); }
    virtual StringArray getAllValueStrings() const { return {}; }
    void setValueNotifyingHost(float newValue) {
        setValue(newValue);
        for (auto *l : listeners_) l->parameterValueChanged(index_, newValue);
    }
    void beginChangeGesture() { for (auto *l : listeners_) l->parameterGestureChanged(index_, true); }
    void endChangeGesture() { for (auto *l : listeners_) l->parameterGestureChanged(index_, false); }
    void addListener(Listener *l) { if (l != nullptr) listeners_.push_back(l); }
    void removeListener(Listener *l) { listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end()); }
    int getParameterIndex() const noexcept { return index_; }
    void setParameterIndex(int i) noexcept { index_ = i; }

private:
    std::vector<Listener *> listeners_;
    int index_{0};
};
class AudioProcessorParameterWithID : public AudioProcessorParameter {
public:
    AudioProcessorParameterWithID(const String &parameterID, const String &parameterName)
        : paramID(parameterID), name(parameterName) {}
    virtual String getParameterID() const { return paramID; }
    String paramID, name;
};

// a hosted plugin's parameter is one with an id; that is all of it gin uses
class AudioPluginInstance {
public:
    class HostedParameter : public AudioProcessorParameter {
    public:
        HostedParameter() = default;
        explicit HostedParameter(int versionHint) { ignoreUnused(versionHint); }
        virtual String getParameterID() const = 0;
    };
};

// juce::HeapBlock: a block of heap memory, allocated (and zeroed if asked) by its owner
template <typename ElementType, bool throwOnFailure = false> class HeapBlock {
public:
    HeapBlock() = default;
    ~HeapBlock() { std::free(data_); }
    HeapBlock(const HeapBlock &) = delete;
    HeapBlock &operator=(const HeapBlock &) = delete;
    void allocate(size_t n, bool initialiseToZero) {
        bytes_=n*sizeof(ElementType);
        std::free(data_);
        data_ = static_cast<ElementType *>(initialiseToZero ? std::calloc(n, sizeof(ElementType)) : std::malloc(n * sizeof(ElementType)));
    }
    void free() { std::free(data_); data_ = nullptr; }
    void discardZeroPages() noexcept {
#ifdef __linux__
        uintptr_t first=(reinterpret_cast<uintptr_t>(data_)+4095)&~uintptr_t(4095);
        uintptr_t last=(reinterpret_cast<uintptr_t>(data_)+bytes_)&~uintptr_t(4095);
        if(last>first)madvise(reinterpret_cast<void*>(first),last-first,MADV_DONTNEED);
#endif
    }
    ElementType *get() const noexcept { return data_; }
    operator ElementType *() const noexcept { return data_; }
private:
    ElementType *data_ = nullptr;
    size_t bytes_=0;
};

// juce::SharedResourcePointer: one of a thing, shared by everything that holds a pointer to it
template <typename SharedObjectType> class SharedResourcePointer {
public:
    SharedResourcePointer() = default;
    SharedObjectType *operator->() const noexcept { return &get(); }
    SharedObjectType &operator*() const noexcept { return get(); }
    SharedObjectType &get() const noexcept {
        static SharedObjectType theOne;
        return theOne;
    }
};

// juce::ScopedValueSetter: puts a value back when it goes out of scope
template <typename ValueType> class ScopedValueSetter {
public:
    ScopedValueSetter(ValueType &valueToSet, ValueType newValue)
        : value(valueToSet), previous(valueToSet) { valueToSet = newValue; }
    ScopedValueSetter(ValueType &valueToSet, ValueType newValue, ValueType valueWhenDone)
        : value(valueToSet), previous(valueWhenDone) { valueToSet = newValue; }
    ~ScopedValueSetter() { value = previous; }
    ScopedValueSetter(const ScopedValueSetter &) = delete;

private:
    ValueType &value;
    const ValueType previous;
};

// juce::ActionBroadcaster: something that tells whoever is listening that it has changed
class ActionListener {
public:
    virtual ~ActionListener() = default;
    virtual void actionListenerCallback(const String &message) = 0;
};
class ActionBroadcaster {
public:
    virtual ~ActionBroadcaster() = default;
    void addActionListener(ActionListener *l) { if (l != nullptr) listeners_.push_back(l); }
    void removeActionListener(ActionListener *l) {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
    }
    void removeAllActionListeners() { listeners_.clear(); }
    void sendActionMessage(const String &message) const {
        auto copy = listeners_;
        for (auto *l : copy) l->actionListenerCallback(message);
    }

private:
    std::vector<ActionListener *> listeners_;
};

// juce::Timer: gin's parameters use one to let a value settle. Nothing here runs a message loop, so it does not
// tick — what it would have done on the beat, the engine does when the block runs.
class Timer {
public:
    virtual ~Timer() = default;
    virtual void timerCallback() = 0;
    void startTimer(int) noexcept { running_ = true; }
    void startTimerHz(int) noexcept { running_ = true; }
    void stopTimer() noexcept { running_ = false; }
    bool isTimerRunning() const noexcept { return running_; }

private:
    bool running_{false};
};

// Reading audio from bytes, as RipplerX's sampler reads its mallet samples (Vendor/ripplerx/README.md): a stream
// over a block of memory, and a format manager that reads FLAC through dr_flac — the one format a bundled plugin
// asks for — into a reader. A file is only ever a path here, read whole.
extern "C" {
float *drflac_open_memory_and_read_pcm_frames_f32(const void *data, size_t dataSize, unsigned int *channels,
                                                  unsigned int *sampleRate, unsigned long long *totalPCMFrameCount,
                                                  const void *pAllocationCallbacks);
void drflac_free(void *p, const void *pAllocationCallbacks);
}

class InputStream {
public:
    virtual ~InputStream() = default;
    virtual int64 getTotalLength() = 0;
    virtual int read(void *dest, int numBytes) = 0;
    virtual int64 getPosition() = 0;
    int64 getNumBytesRemaining() { return getTotalLength() - getPosition(); }
    double readDouble() {
        double v = 0;
        read(&v, sizeof v);
        return v;
    }
    MemoryBlock readWhole() {
        MemoryBlock all;
        std::vector<char> chunk(65536);
        for (int n; (n = read(chunk.data(), static_cast<int>(chunk.size()))) > 0;) all.append(chunk.data(), static_cast<size_t>(n));
        return all;
    }
};

class MemoryInputStream : public InputStream {
public:
    MemoryInputStream(const void *data, size_t size, bool keepCopy) {
        if (keepCopy) { copy_.assign(static_cast<const char *>(data), static_cast<const char *>(data) + size); data_ = copy_.data(); }
        else data_ = static_cast<const char *>(data);
        size_ = size;
    }
    MemoryInputStream(const MemoryBlock &block, bool keepCopy) : MemoryInputStream(block.getData(), block.getSize(), keepCopy) {}
    int64 getTotalLength() override { return static_cast<int64>(size_); }
    int64 getPosition() override { return static_cast<int64>(at_); }
    int read(void *dest, int numBytes) override {
        const size_t n = std::min(static_cast<size_t>(std::max(numBytes, 0)), size_ - at_);
        std::memcpy(dest, data_ + at_, n);
        at_ += n;
        return static_cast<int>(n);
    }

private:
    const char *data_{nullptr};
    std::vector<char> copy_;
    size_t size_{0}, at_{0};
};

class XmlElement {};
class File;
struct XmlDocument {   // NorniOS: no XML; parsing gives an empty element
    static std::unique_ptr<XmlElement> parse(const File &) { return std::make_unique<XmlElement>(); }
};
class File {
public:
    File() = default;
    bool replaceWithText(const String &text) const {
        FILE *f = std::fopen(path_.c_str(), "wb");
        if (!f) return false;
        const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        std::fclose(f);
        return ok;
    }
    explicit File(const String &path) : path_(path) {}
    bool existsAsFile() const {
        if (FILE *f = std::fopen(path_.c_str(), "rb")) { std::fclose(f); return true; }
        return false;
    }
    std::unique_ptr<InputStream> createInputStream() const {
        std::vector<char> bytes;
        if (FILE *f = std::fopen(path_.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0) { bytes.resize(static_cast<size_t>(n)); if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear(); }
            std::fclose(f);
        }
        if (bytes.empty()) return nullptr;
        return std::make_unique<MemoryInputStream>(bytes.data(), bytes.size(), true);
    }
    const String &getFullPathName() const noexcept { return path_; }

private:
    String path_;
};

class AudioFormatReader {
public:
    unsigned int numChannels{0};
    int64 lengthInSamples{0};
    double sampleRate{0};
    bool read(float *const *dest, int numDestChannels, int64 startSample, int numSamples) {
        for (int c = 0; c < numDestChannels; ++c) {
            const unsigned int src = std::min<unsigned int>(static_cast<unsigned int>(c), numChannels - 1);
            for (int i = 0; i < numSamples; ++i) {
                const int64 at = startSample + i;
                dest[c][i] = at < lengthInSamples ? samples_[static_cast<size_t>(at * numChannels + src)] : 0.f;
            }
        }
        return true;
    }
    std::vector<float> samples_;   // interleaved
};

class AudioFormatManager {
public:
    void registerBasicFormats() {}
    AudioFormatReader *createReaderFor(std::unique_ptr<InputStream> stream) {
        if (stream == nullptr) return nullptr;
        const MemoryBlock bytes = stream->readWhole();
        unsigned int channels = 0, rate = 0;
        unsigned long long frames = 0;
        float *pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.getData(), bytes.getSize(), &channels, &rate,
                                                                &frames, nullptr);
        if (pcm == nullptr || channels == 0) {
            if (pcm != nullptr) drflac_free(pcm, nullptr);
            return nullptr;   // not FLAC: nothing else is read here
        }
        auto *reader = new AudioFormatReader();
        reader->numChannels = channels;
        reader->lengthInSamples = static_cast<int64>(frames);
        reader->sampleRate = rate;
        reader->samples_.assign(pcm, pcm + frames * channels);
        drflac_free(pcm, nullptr);
        return reader;
    }
};

// juce::MidiKeyboardState is the on-screen keyboard's memory of what is held; nothing draws one here
class MidiKeyboardState {
public:
    void processNextMidiBuffer(MidiBuffer &, int, int, bool) {}
};

// juce::AudioProcessorValueTreeState: the plugin's parameters. Upstream's DSP reads them by name, every block, as
// an atomic float, which is all it ever asks of the tree; the engine that hosts the DSP adds them and writes them.
class AudioProcessorValueTreeState {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void parameterChanged(const String &parameterID, float newValue) = 0;
    };
    // AudioProcessorValueTreeState::Parameter, as a plugin's DSP reads one: dereferenced to its value, converted
    // through its range
    struct Parameter {
        Parameter() = default;
        Parameter(NormalisableRange<float> r, float initial) : range(r) { value.store(initial); }
        std::atomic<float> value{0.f};
        NormalisableRange<float> range;
        float defaultValue = 0.f;
        operator float() const noexcept { return value.load(); }
        float get() const noexcept { return value.load(); }
        float convertTo0to1(float v) const noexcept { return range.convertTo0to1(v); }
        float convertFrom0to1(float v) const noexcept { return range.convertFrom0to1(v); }
        const NormalisableRange<float> &getNormalisableRange() const noexcept { return range; }
    };
    struct ParameterLayout {};

    // a parameter as the DSP asks for it: its current value, and the range it moves over
    struct Ranged {
        std::atomic<float> value{0.f};
        NormalisableRange<float> range;
        const NormalisableRange<float> &getNormalisableRange() const noexcept { return range; }
        float convertTo0to1(float v) const noexcept { return range.convertTo0to1(v); }
        void beginChangeGesture() noexcept {}
        void endChangeGesture() noexcept {}
        void setValueNotifyingHost(float normalised) noexcept { value.store(range.convertFrom0to1(normalised)); }
    };

    // built before the audio starts: std::map keeps its nodes put, so the pointers the DSP holds stay good
    void addParameter(const String &parameterID, float initialValue) {
        values_[parameterID].value.store(initialValue, std::memory_order_relaxed);
    }
    void addParameter(const String &parameterID, float initialValue, NormalisableRange<float> range) {
        auto &p = values_[parameterID];
        p.value.store(initialValue, std::memory_order_relaxed);
        p.range = range;
    }
    void setParameter(const String &parameterID, float newValue) {
        auto it = values_.find(parameterID);
        if (it != values_.end()) it->second.value.store(newValue, std::memory_order_relaxed);
    }
    float getParameterValue(const String &parameterID) const {
        auto it = values_.find(parameterID);
        return it == values_.end() ? 0.0f : it->second.value.load(std::memory_order_relaxed);
    }
    Ranged *getParameter(const String &parameterID) noexcept {
        auto it = values_.find(parameterID);
        return it == values_.end() ? nullptr : &it->second;
    }
    std::atomic<float> *getRawParameterValue(const String &parameterID) noexcept {
        auto it = values_.find(parameterID);
        return it == values_.end() ? nullptr : &it->second.value;
    }

private:
    std::map<String, Ranged> values_;
};

namespace dsp {
// juce::dsp::SIMDRegister: a register of four floats (or two doubles), its native value as JUCE has it on ARM — a
// NEON float32x4_t, and for doubles a pair in a struct — so code that reaches for the value's intrinsics still
// compiles; the arithmetic is done a lane at a time, which the compiler vectorises.
template <typename T> struct SIMDNative;
#if defined(__aarch64__) || defined(__arm64__)
template <> struct SIMDNative<float> { using type = float32x4_t; static constexpr size_t lanes = 4; };
#else
template <> struct SIMDNative<float> { struct type { float v[4]; }; static constexpr size_t lanes = 4; };
#endif
template <> struct SIMDNative<double> { struct type { double v[2]; }; static constexpr size_t lanes = 2; };

template <typename Type> struct SIMDRegister {
    using ElementType = Type;
    using vSIMDType = typename SIMDNative<Type>::type;
    static constexpr size_t SIMDNumElements = SIMDNative<Type>::lanes;
    vSIMDType value;

    SIMDRegister() noexcept { std::memset(&value, 0, sizeof value); }
    SIMDRegister(vSIMDType a) noexcept : value(a) {}
    SIMDRegister(Type s) noexcept { for (size_t i = 0; i < SIMDNumElements; ++i) set(i, s); }

    static constexpr size_t size() noexcept { return SIMDNumElements; }
    static SIMDRegister expand(Type s) noexcept { return SIMDRegister(s); }
    static SIMDRegister fromRawArray(const Type *a) noexcept { SIMDRegister r; std::memcpy(&r.value, a, sizeof r.value); return r; }
    void copyToRawArray(Type *a) const noexcept { std::memcpy(a, &value, sizeof value); }
    Type get(size_t i) const noexcept { Type a[SIMDNumElements]; std::memcpy(a, &value, sizeof value); return a[i]; }
    void set(size_t i, Type v) noexcept { Type a[SIMDNumElements]; std::memcpy(a, &value, sizeof value); a[i] = v; std::memcpy(&value, a, sizeof value); }
    Type operator[](size_t i) const noexcept { return get(i); }
    Type sum() const noexcept { Type t = 0; for (size_t i = 0; i < SIMDNumElements; ++i) t += get(i); return t; }

    template <typename F> static SIMDRegister map2(const SIMDRegister &a, const SIMDRegister &b, F f) noexcept {
        Type x[SIMDNumElements], y[SIMDNumElements];
        a.copyToRawArray(x); b.copyToRawArray(y);
        for (size_t i = 0; i < SIMDNumElements; ++i) x[i] = f(x[i], y[i]);
        return fromRawArray(x);
    }
    friend SIMDRegister operator+(const SIMDRegister &a, const SIMDRegister &b) noexcept { return map2(a, b, [](Type x, Type y) { return x + y; }); }
    friend SIMDRegister operator-(const SIMDRegister &a, const SIMDRegister &b) noexcept { return map2(a, b, [](Type x, Type y) { return x - y; }); }
    friend SIMDRegister operator*(const SIMDRegister &a, const SIMDRegister &b) noexcept { return map2(a, b, [](Type x, Type y) { return x * y; }); }
    friend SIMDRegister operator+(const SIMDRegister &a, Type b) noexcept { return a + SIMDRegister(b); }
    friend SIMDRegister operator-(const SIMDRegister &a, Type b) noexcept { return a - SIMDRegister(b); }
    friend SIMDRegister operator*(const SIMDRegister &a, Type b) noexcept { return a * SIMDRegister(b); }
    SIMDRegister operator-() const noexcept { return SIMDRegister(Type(0)) - *this; }
    SIMDRegister &operator+=(const SIMDRegister &o) noexcept { return *this = *this + o; }
    SIMDRegister &operator-=(const SIMDRegister &o) noexcept { return *this = *this - o; }
    SIMDRegister &operator*=(const SIMDRegister &o) noexcept { return *this = *this * o; }
    SIMDRegister &operator*=(Type o) noexcept { return *this = *this * o; }
    static SIMDRegister truncate(const SIMDRegister &a) noexcept { return map2(a, a, [](Type x, Type) { return std::trunc(x); }); }
    static SIMDRegister min(const SIMDRegister &a, const SIMDRegister &b) noexcept { return map2(a, b, [](Type x, Type y) { return std::min(x, y); }); }
    static SIMDRegister max(const SIMDRegister &a, const SIMDRegister &b) noexcept { return map2(a, b, [](Type x, Type y) { return std::max(x, y); }); }
    static SIMDRegister abs(const SIMDRegister &a) noexcept { return map2(a, a, [](Type x, Type) { return std::abs(x); }); }

    // masks, as JUCE's: every bit set where the comparison holds, none where it does not; here in a register of the
    // same type, which the bitwise operators below read as bits
    using vMaskType = SIMDRegister;
    using Bits = typename std::conditional<sizeof(Type) == 4, uint32_t, uint64_t>::type;
    static SIMDRegister fromBits(const Bits *b) noexcept { SIMDRegister r; std::memcpy(&r.value, b, sizeof r.value); return r; }
    template <typename F> static SIMDRegister compare(const SIMDRegister &a, const SIMDRegister &b, F f) noexcept {
        Bits m[SIMDNumElements];
        for (size_t i = 0; i < SIMDNumElements; ++i) m[i] = f(a.get(i), b.get(i)) ? ~Bits(0) : Bits(0);
        return fromBits(m);
    }
    static SIMDRegister lessThan(const SIMDRegister &a, const SIMDRegister &b) noexcept { return compare(a, b, [](Type x, Type y) { return x < y; }); }
    static SIMDRegister greaterThan(const SIMDRegister &a, const SIMDRegister &b) noexcept { return compare(a, b, [](Type x, Type y) { return x > y; }); }
    static SIMDRegister equal(const SIMDRegister &a, const SIMDRegister &b) noexcept { return compare(a, b, [](Type x, Type y) { return x == y; }); }
    static SIMDRegister notEqual(const SIMDRegister &a, const SIMDRegister &b) noexcept { return compare(a, b, [](Type x, Type y) { return x != y; }); }
    template <typename F> static SIMDRegister bits(const SIMDRegister &a, const SIMDRegister &b, F f) noexcept {
        Bits x[SIMDNumElements], y[SIMDNumElements];
        std::memcpy(x, &a.value, sizeof x); std::memcpy(y, &b.value, sizeof y);
        for (size_t i = 0; i < SIMDNumElements; ++i) x[i] = f(x[i], y[i]);
        return fromBits(x);
    }
    friend SIMDRegister operator&(const SIMDRegister &a, const SIMDRegister &b) noexcept { return bits(a, b, [](Bits x, Bits y) { return x & y; }); }
    friend SIMDRegister operator|(const SIMDRegister &a, const SIMDRegister &b) noexcept { return bits(a, b, [](Bits x, Bits y) { return x | y; }); }
    SIMDRegister operator~() const noexcept { return bits(*this, *this, [](Bits x, Bits) { return ~x; }); }
};

namespace SampleTypeHelpers {
    template <typename T, bool = std::is_floating_point<T>::value> struct ElementType { using Type = T; };
    template <typename T> struct ElementType<T, false> { using Type = typename T::value_type; };
    template <typename T> struct ElementType<SIMDRegister<T>, false> { using Type = T; };
}

namespace util {
    template <typename T> inline void snapToZero(T &value) noexcept {
        if (!(value < T(-1.0e-8) || value > T(1.0e-8))) value = T();
    }
}

// the state juce::dsp's processors share, reference-counted
struct ProcessorState : public ReferenceCountedObject {
    using Ptr = ReferenceCountedObjectPtr<ProcessorState>;
};


struct ProcessSpec {
    double sampleRate{44100.0};
    uint32 maximumBlockSize{512};
    uint32 numChannels{2};
};

// juce::dsp::AudioBlock<float>: channel pointers and a length; it owns nothing
template <typename T> class AudioBlock {
public:
    AudioBlock() = default;
    AudioBlock(T *const *channelData, size_t channels, size_t samples) : samples_(samples) { assign(channelData, channels, 0); }
    AudioBlock(T *const *channelData, size_t channels, size_t start, size_t samples) : samples_(samples) { assign(channelData, channels, start); }
    // as JUCE's: channels of `samples` each, carved out of a heap block it (re)allocates, zeroed
    AudioBlock(HeapBlock<char> &heap, size_t channels, size_t samples) : samples_(samples) {
        channels_ = std::min(channels, kMaxChannels);
        heap.allocate(channels_ * samples * sizeof(T) + 16, true);
        T *base = reinterpret_cast<T *>(heap.get());
        for (size_t c = 0; c < channels_; ++c) pointers_[c] = base + c * samples;
    }
    AudioBlock(AudioBuffer<T> &buffer) : samples_(static_cast<size_t>(buffer.getNumSamples())) {
        assign(buffer.getArrayOfWritePointers(), static_cast<size_t>(buffer.getNumChannels()), 0);
    }
    size_t getNumChannels() const noexcept { return channels_; }
    size_t getNumSamples() const noexcept { return samples_; }
    T *getChannelPointer(size_t c) const noexcept { return pointers_[c]; }
    T getSample(int c, int i) const noexcept { return pointers_[c][i]; }
    void setSample(int c, int i, T v) const noexcept { pointers_[c][i] = v; }
    AudioBlock getSingleChannelBlock(size_t c) const noexcept { AudioBlock b; b.channels_ = 1; b.samples_ = samples_; b.pointers_[0] = pointers_[c]; return b; }
    AudioBlock getSubBlock(size_t start, size_t length) const noexcept {
        AudioBlock b; b.channels_ = channels_; b.samples_ = length;
        for (size_t c = 0; c < channels_; ++c) b.pointers_[c] = pointers_[c] + start;
        return b;
    }
    const AudioBlock &clear() const noexcept { for (size_t c = 0; c < channels_; ++c) std::fill_n(pointers_[c], samples_, T()); return *this; }
    const AudioBlock &multiplyBy(T g) const noexcept { for (size_t c = 0; c < channels_; ++c) for (size_t i = 0; i < samples_; ++i) pointers_[c][i] *= g; return *this; }
    // as JUCE's: a smoothed value stepped once a sample, the same step for every channel
    template <typename SmoothingType> const AudioBlock &multiplyBy(SmoothedValue<T, SmoothingType> &value) const noexcept {
        if (!value.isSmoothing()) return multiplyBy(value.getTargetValue());
        for (size_t i = 0; i < samples_; ++i) {
            const T g = value.getNextValue();
            for (size_t c = 0; c < channels_; ++c) pointers_[c][i] *= g;
        }
        return *this;
    }
    const AudioBlock &copyFrom(const AudioBuffer<T> &src, size_t srcPos, size_t dstPos, size_t n) const noexcept {
        for (size_t c = 0; c < channels_ && c < static_cast<size_t>(src.getNumChannels()); ++c)
            std::memmove(pointers_[c] + dstPos, src.getReadPointer(static_cast<int>(c)) + srcPos, n * sizeof(T));
        return *this;
    }
    const AudioBlock &copyFrom(const AudioBlock &src) const noexcept {
        for (size_t c = 0; c < std::min(channels_, src.channels_); ++c) std::memmove(pointers_[c], src.pointers_[c], std::min(samples_, src.samples_) * sizeof(T));
        return *this;
    }

private:
    static constexpr size_t kMaxChannels = 8;
    void assign(T *const *channelData, size_t channels, size_t start) {
        channels_ = std::min(channels, kMaxChannels);
        for (size_t c = 0; c < channels_; ++c) pointers_[c] = channelData[c] + start;
    }
    T *pointers_[kMaxChannels]{};
    size_t channels_{0}, samples_{0};
};

template <typename T> struct ProcessContextReplacing;

// juce::dsp::ProcessContextNonReplacing: input and output in different blocks
template <typename T> struct ProcessContextNonReplacing {
    using SampleType = T;
    ProcessContextNonReplacing(const AudioBlock<T> &input, AudioBlock<T> &output) : input_(input), output_(output) {}
    const AudioBlock<T> &getInputBlock() const noexcept { return input_; }
    AudioBlock<T> &getOutputBlock() const noexcept { return output_; }
    constexpr bool usesSeparateInputAndOutputBlocks() const noexcept { return true; }
    bool isBypassed{false};

private:
    AudioBlock<T> input_;
    AudioBlock<T> &output_;
};

template <typename T> struct ProcessContextReplacing {
    using SampleType = T;
    ProcessContextReplacing(AudioBlock<T> &block) : block_(block) {}
    constexpr bool usesSeparateInputAndOutputBlocks() const noexcept { return false; }
    const AudioBlock<T> &getInputBlock() const noexcept { return block_; }
    AudioBlock<T> &getOutputBlock() const noexcept { return block_; }
    bool isBypassed{false};

private:
    AudioBlock<T> &block_;
};

// juce::dsp::LookupTableTransform: a function sampled over a range and read back with linear interpolation, which
// is how the ladder saturates.
template <typename T> class LookupTableTransform {
public:
    LookupTableTransform() = default;
    // as JUCE's initialise: the function sampled at numPoints over [min, max]
    template <typename Fn> void initialise(Fn fn, T minIn, T maxIn, size_t numPoints) {
        min_ = minIn; max_ = maxIn;
        table_.assign(numPoints + 1, T());
        scaler_ = static_cast<T>(numPoints - 1) / (maxIn - minIn);
        for (size_t i = 0; i < numPoints; ++i)
            table_[i] = fn(jlimit(minIn, maxIn, minIn + static_cast<T>(i) * (maxIn - minIn) / static_cast<T>(numPoints - 1)));
        table_[numPoints] = table_[numPoints - 1];
    }
    T processSample(T value) const noexcept { return (*this)(value); }
    T processSampleUnchecked(T value) const noexcept {
        const T index = (value - min_) * scaler_;
        const size_t i = static_cast<size_t>(index);
        const T frac = index - static_cast<T>(i);
        return table_[i] + frac * (table_[i + 1] - table_[i]);
    }
    template <typename Fn> LookupTableTransform(Fn fn, T minIn, T maxIn, size_t numPoints)
        : min_(minIn), max_(maxIn) {
        table_.resize(numPoints + 1);
        scaler_ = static_cast<T>(numPoints - 1) / (maxIn - minIn);
        for (size_t i = 0; i < numPoints; ++i) table_[i] = fn(minIn + static_cast<T>(i) / scaler_);
        table_[numPoints] = table_[numPoints - 1];
    }
    T operator()(T value) const noexcept {
        const T index = (jlimit(min_, max_, value) - min_) * scaler_;
        const size_t i = static_cast<size_t>(index);
        const T frac = index - static_cast<T>(i);
        return table_[i] + frac * (table_[i + 1] - table_[i]);
    }

private:
    std::vector<T> table_;
    T min_{}, max_{1}, scaler_{1};
};

// juce::dsp::WindowingFunction's tables (the Hann window, unnormalised, as ChowMatrix's reverser asks for)
template <typename T> struct WindowingFunction {
    enum WindowingMethod { rectangular = 0, triangular, hann, hamming, blackman, blackmanHarris, flatTop, kaiser };
    static void fillWindowingTables(T *samples, size_t size, WindowingMethod type, bool normalise = true, T beta = 0) noexcept {
        ignoreUnused(beta);
        for (size_t i = 0; i < size; ++i) {
            const double cos2 = std::cos(2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(size - 1));
            samples[i] = type == hann ? static_cast<T>(0.5 - 0.5 * cos2) : type == hamming ? static_cast<T>(0.54 - 0.46 * cos2) : T(1);
        }
        if (normalise) {
            T sum = 0; for (size_t i = 0; i < size; ++i) sum += samples[i];
            if (sum > 0) for (size_t i = 0; i < size; ++i) samples[i] *= static_cast<T>(size) / sum;
        }
    }
};

enum class LadderFilterMode { LPF12, HPF12, BPF12, LPF24, HPF24, BPF24 };

// juce::dsp::LadderFilter<float>: JUCE's Moog ladder, its coefficients, its output gain of 1.2 and its saturation
// table over [-5, 5] in 128 points
template <typename T> class LadderFilter {
public:
    using Mode = LadderFilterMode;
    LadderFilter() : state_(2) {
        setSampleRate(T(1000));         // as JUCE: an unrealistic default, to catch a missing prepare
        setResonance(T(0));
        setDrive(T(1.2));
        mode_ = Mode::LPF24;
        setMode(Mode::LPF12);
    }
    void setMode(Mode newMode) noexcept {
        if (newMode == mode_) return;
        switch (newMode) {
            case Mode::LPF12: A_ = {{T(0), T(0), T(1), T(0), T(0)}}; comp_ = T(0.5); break;
            case Mode::HPF12: A_ = {{T(1), T(-2), T(1), T(0), T(0)}}; comp_ = T(0); break;
            case Mode::BPF12: A_ = {{T(0), T(0), T(-1), T(1), T(0)}}; comp_ = T(0.5); break;
            case Mode::LPF24: A_ = {{T(0), T(0), T(0), T(0), T(1)}}; comp_ = T(0.5); break;
            case Mode::HPF24: A_ = {{T(1), T(-4), T(6), T(-4), T(1)}}; comp_ = T(0); break;
            case Mode::BPF24: A_ = {{T(0), T(0), T(1), T(-2), T(1)}}; comp_ = T(0.5); break;
        }
        for (auto &a : A_) a *= T(1.2);   // outputGain
        mode_ = newMode;
        reset();
    }
    void prepare(const ProcessSpec &spec) {
        setSampleRate(static_cast<T>(spec.sampleRate));
        state_.assign(std::max<size_t>(spec.numChannels, 1), {});
        reset();
    }
    void reset() noexcept {
        for (auto &st : state_) st.fill(T(0));
        cutoffSmoother_.setCurrentAndTargetValue(cutoffSmoother_.getTargetValue());
        resonanceSmoother_.setCurrentAndTargetValue(resonanceSmoother_.getTargetValue());
    }
    void setCutoffFrequencyHz(T newCutoff) noexcept { cutoffFreqHz_ = newCutoff; updateCutoffFreq(); }
    void setResonance(T newResonance) noexcept { resonance_ = newResonance; updateResonance(); }
    void setDrive(T newDrive) noexcept {
        drive_ = newDrive;
        gain_ = std::pow(drive_, T(-2.642)) * T(0.6103) + T(0.3903);
        drive2_ = drive_ * T(0.04) + T(0.96);
        gain2_ = std::pow(drive2_, T(-2.642)) * T(0.6103) + T(0.3903);
    }
    void setEnabled(bool v) noexcept { enabled_ = v; }
    template <typename Context> void process(const Context &context) noexcept {
        const auto &inputBlock = context.getInputBlock();
        auto &outputBlock = context.getOutputBlock();
        const auto numChannels = outputBlock.getNumChannels();
        const auto numSamples = outputBlock.getNumSamples();
        if (!enabled_ || context.isBypassed) { outputBlock.copyFrom(inputBlock); return; }
        for (size_t n = 0; n < numSamples; ++n) {
            updateSmoothers();
            for (size_t ch = 0; ch < numChannels; ++ch)
                outputBlock.getChannelPointer(ch)[n] = processSample(inputBlock.getChannelPointer(ch)[n], ch);
        }
    }
    T processSample(T inputValue, size_t channelToUse) noexcept {
        auto &st = state_[channelToUse];
        const auto a1 = cutoffTransformValue_;
        const auto g = a1 * T(-1) + T(1);
        const auto b0 = g * T(0.76923076923);
        const auto b1 = g * T(0.23076923076);
        const auto dx = gain_ * saturationLUT_(drive_ * inputValue);
        const auto a = dx + scaledResonanceValue_ * T(-4) * (gain2_ * saturationLUT_(drive2_ * st[4]) - dx * comp_);
        const auto b = b1 * st[0] + a1 * st[1] + b0 * a;
        const auto c = b1 * st[1] + a1 * st[2] + b0 * b;
        const auto d = b1 * st[2] + a1 * st[3] + b0 * c;
        const auto e = b1 * st[3] + a1 * st[4] + b0 * d;
        st[0] = a; st[1] = b; st[2] = c; st[3] = d; st[4] = e;
        return a * A_[0] + b * A_[1] + c * A_[2] + d * A_[3] + e * A_[4];
    }
    void updateSmoothers() noexcept {
        cutoffTransformValue_ = cutoffSmoother_.getNextValue();
        scaledResonanceValue_ = resonanceSmoother_.getNextValue();
    }

private:
    void setSampleRate(T newValue) noexcept {
        cutoffFreqScaler_ = T(-2.0 * MathConstants<double>::pi) / newValue;
        const T smootherRampTimeSec = T(0.05);
        cutoffSmoother_.reset(newValue, smootherRampTimeSec);
        resonanceSmoother_.reset(newValue, smootherRampTimeSec);
        updateCutoffFreq();
    }
    void updateCutoffFreq() noexcept { cutoffSmoother_.setTargetValue(std::exp(cutoffFreqHz_ * cutoffFreqScaler_)); }
    void updateResonance() noexcept { resonanceSmoother_.setTargetValue(jmap(resonance_, T(0.1), T(1.0))); }

    static constexpr size_t numStates = 5;
    T drive_{}, drive2_{}, gain_{}, gain2_{}, comp_{};
    std::vector<std::array<T, numStates>> state_;
    std::array<T, numStates> A_{};
    SmoothedValue<T> cutoffSmoother_, resonanceSmoother_;
    T cutoffTransformValue_{}, scaledResonanceValue_{};
    LookupTableTransform<T> saturationLUT_{[](T x) { return std::tanh(x); }, T(-5), T(5), 128};
    T cutoffFreqHz_{T(200)};
    T resonance_{};
    T cutoffFreqScaler_{};
    Mode mode_{Mode::LPF24};
    bool enabled_{true};
};

// juce::dsp::BallisticsFilter<float>: the attack/release envelope the compressor rectifies with
template <typename T> class BallisticsFilter {
public:
    enum class LevelCalculationType { peak, RMS };
    BallisticsFilter() { setAttackTime(attackTime_); setReleaseTime(releaseTime_); }
    void setAttackTime(T attackTimeMs) { attackTime_ = attackTimeMs; cteAT_ = calculateLimitedCte(attackTime_); }
    void setReleaseTime(T releaseTimeMs) { releaseTime_ = releaseTimeMs; cteRL_ = calculateLimitedCte(releaseTime_); }
    void setLevelCalculationType(LevelCalculationType t) { levelType_ = t; reset(); }
    void prepare(const ProcessSpec &spec) {
        expFactor_ = -2.0 * MathConstants<double>::pi * 1000.0 / spec.sampleRate;
        setAttackTime(attackTime_);
        setReleaseTime(releaseTime_);
        yold_.resize(std::max<size_t>(spec.numChannels, 1));
        reset();
    }
    void reset() { reset(T(0)); }
    void reset(T initialValue) { for (auto &old : yold_) old = initialValue; }
    T processSample(int channel, T inputValue) {
        if (levelType_ == LevelCalculationType::RMS) inputValue *= inputValue;
        else inputValue = std::abs(inputValue);
        const T cte = (inputValue > yold_[static_cast<size_t>(channel)] ? cteAT_ : cteRL_);
        const T result = inputValue + cte * (yold_[static_cast<size_t>(channel)] - inputValue);
        yold_[static_cast<size_t>(channel)] = result;
        return levelType_ == LevelCalculationType::RMS ? std::sqrt(result) : result;
    }

private:
    T calculateLimitedCte(T timeMs) const noexcept {
        return timeMs < T(1.0e-3) ? T(0) : static_cast<T>(std::exp(expFactor_ / timeMs));
    }
    std::vector<T> yold_;
    double expFactor_{-2.0 * MathConstants<double>::pi * 1000.0 / 44100.0};
    T attackTime_{T(1)}, releaseTime_{T(100)}, cteAT_{}, cteRL_{};
    LevelCalculationType levelType_{LevelCalculationType::peak};
};

// juce::dsp::Compressor<float>: a ballistics filter with a peak rectifier, then a VCA
template <typename T> class Compressor {
public:
    Compressor() { update(); }
    void setThreshold(T newThreshold) { thresholddB_ = newThreshold; update(); }
    void setRatio(T newRatio) { ratio_ = newRatio; update(); }
    void setAttack(T newAttack) { attackTime_ = newAttack; update(); }
    void setRelease(T newRelease) { releaseTime_ = newRelease; update(); }
    void prepare(const ProcessSpec &spec) { envelopeFilter_.prepare(spec); update(); reset(); }
    void reset() { envelopeFilter_.reset(); }
    template <typename Context> void process(const Context &context) noexcept {
        const auto &inputBlock = context.getInputBlock();
        auto &outputBlock = context.getOutputBlock();
        const auto numChannels = outputBlock.getNumChannels();
        const auto numSamples = outputBlock.getNumSamples();
        if (context.isBypassed) { outputBlock.copyFrom(inputBlock); return; }
        for (size_t ch = 0; ch < numChannels; ++ch) {
            auto *in = inputBlock.getChannelPointer(ch);
            auto *out = outputBlock.getChannelPointer(ch);
            for (size_t i = 0; i < numSamples; ++i) out[i] = processSample(static_cast<int>(ch), in[i]);
        }
    }
    T processSample(int channel, T inputValue) {
        const auto env = envelopeFilter_.processSample(channel, inputValue);
        const auto gain = (env < threshold_) ? T(1.0) : std::pow(env * thresholdInverse_, ratioInverse_ - T(1.0));
        return gain * inputValue;
    }

private:
    void update() {
        threshold_ = Decibels::decibelsToGain(thresholddB_, T(-200.0));
        thresholdInverse_ = T(1.0) / threshold_;
        ratioInverse_ = T(1.0) / ratio_;
        envelopeFilter_.setAttackTime(attackTime_);
        envelopeFilter_.setReleaseTime(releaseTime_);
    }
    BallisticsFilter<T> envelopeFilter_;
    T thresholddB_{T(0)}, ratio_{T(1)}, attackTime_{T(1)}, releaseTime_{T(100)};
    T threshold_{}, thresholdInverse_{}, ratioInverse_{};
};

// juce::dsp::Reverb: the Freeverb above, over a block
class Reverb {
public:
    using Parameters = juce::Reverb::Parameters;
    const Parameters &getParameters() const noexcept { return reverb_.getParameters(); }
    void setParameters(const Parameters &p) { reverb_.setParameters(p); }
    bool isEnabled() const noexcept { return enabled_; }
    void setEnabled(bool v) noexcept { enabled_ = v; }
    void prepare(const ProcessSpec &spec) { reverb_.setSampleRate(spec.sampleRate); }
    void reset() noexcept { reverb_.reset(); }
    template <typename Context> void process(const Context &context) noexcept {
        const auto &inputBlock = context.getInputBlock();
        auto &outputBlock = context.getOutputBlock();
        const auto numIn = inputBlock.getNumChannels(), numOut = outputBlock.getNumChannels();
        const auto numSamples = outputBlock.getNumSamples();
        outputBlock.copyFrom(inputBlock);
        if (!enabled_ || context.isBypassed) return;
        if (numIn == 1 && numOut == 1) reverb_.processMono(outputBlock.getChannelPointer(0), static_cast<int>(numSamples));
        else if (numIn == 2 && numOut == 2)
            reverb_.processStereo(outputBlock.getChannelPointer(0), outputBlock.getChannelPointer(1), static_cast<int>(numSamples));
    }

private:
    juce::Reverb reverb_;
    bool enabled_{true};
};

enum class StateVariableTPTFilterType { lowpass, bandpass, highpass };

// juce::dsp::StateVariableTPTFilter<float>
template <typename T> class StateVariableTPTFilter {
public:
    using Type = StateVariableTPTFilterType;
    StateVariableTPTFilter() { update(); }
    void setType(Type t) { type_ = t; }
    void setCutoffFrequency(T hz) { cutoff_ = hz; update(); }
    void setResonance(T r) { resonance_ = r; update(); }
    Type getType() const noexcept { return type_; }
    T getCutoffFrequency() const noexcept { return cutoff_; }
    T getResonance() const noexcept { return resonance_; }
    void prepare(const ProcessSpec &spec) {
        sampleRate_ = spec.sampleRate;
        s1_.assign(spec.numChannels, T()); s2_.assign(spec.numChannels, T());
        update();
    }
    void reset() { reset(T()); }
    void reset(T v) { std::fill(s1_.begin(), s1_.end(), v); std::fill(s2_.begin(), s2_.end(), v); }
    void snapToZero() noexcept {
        for (auto *v : {&s1_, &s2_}) for (auto &e : *v) if (!(e < T(-1.0e-8) || e > T(1.0e-8))) e = T();
    }
    template <typename Context> void process(const Context &context) noexcept {
        const auto &in = context.getInputBlock();
        auto &out = context.getOutputBlock();
        const auto channels = out.getNumChannels(), n = out.getNumSamples();
        if (context.isBypassed) { out.copyFrom(in); return; }
        for (size_t c = 0; c < channels; ++c) {
            const T *src = in.getChannelPointer(c); T *dst = out.getChannelPointer(c);
            for (size_t i = 0; i < n; ++i) dst[i] = processSample(static_cast<int>(c), src[i]);
        }
        snapToZero();
    }
    T processSample(int channel, T x) noexcept {
        auto &ls1 = s1_[static_cast<size_t>(channel)];
        auto &ls2 = s2_[static_cast<size_t>(channel)];
        const T yHP = h_ * (x - ls1 * (g_ + R2_) - ls2);
        const T yBP = yHP * g_ + ls1; ls1 = yHP * g_ + yBP;
        const T yLP = yBP * g_ + ls2; ls2 = yBP * g_ + yLP;
        switch (type_) {
        case Type::lowpass: return yLP;
        case Type::bandpass: return yBP;
        case Type::highpass: return yHP;
        }
        return yLP;
    }

private:
    void update() {
        g_ = static_cast<T>(std::tan(3.141592653589793238 * cutoff_ / sampleRate_));
        R2_ = static_cast<T>(1.0 / resonance_);
        h_ = static_cast<T>(1.0 / (1.0 + R2_ * g_ + g_ * g_));
    }
    Type type_{Type::lowpass};
    T cutoff_{T(1000)}, resonance_{static_cast<T>(1.0 / 1.4142135623730951)};
    T g_{}, h_{}, R2_{};
    double sampleRate_{44100.0};
    std::vector<T> s1_{2}, s2_{2};
};

// juce::dsp::FirstOrderTPTFilter<float>: one-pole topology-preserving filter, lowpass, highpass or allpass
enum class FirstOrderTPTFilterType { lowpass, highpass, allpass };
template <typename T> class FirstOrderTPTFilter {
public:
    using Type = FirstOrderTPTFilterType;
    FirstOrderTPTFilter() { update(); }
    void setType(Type t) { type_ = t; }
    void setCutoffFrequency(T hz) { cutoff_ = hz; update(); }
    T getCutoffFrequency() const noexcept { return cutoff_; }
    void prepare(const ProcessSpec &spec) { sampleRate_ = spec.sampleRate; s1_.assign(spec.numChannels, T()); update(); reset(); }
    void reset() { std::fill(s1_.begin(), s1_.end(), T()); }
    T processSample(int channel, T x) noexcept {
        auto &s = s1_[static_cast<size_t>(channel)];
        const T v = G_ * (x - s);
        const T y = v + s;
        s = y + v;
        switch (type_) {
        case Type::lowpass: return y;
        case Type::highpass: return x - y;
        case Type::allpass: return 2 * y - x;
        }
        return y;
    }

private:
    void update() {
        const T g = static_cast<T>(std::tan(3.141592653589793238 * cutoff_ / sampleRate_));
        G_ = g / (1 + g);
    }
    Type type_{Type::lowpass};
    T cutoff_{T(1000)}, G_{};
    double sampleRate_{44100.0};
    std::vector<T> s1_{2};
};

// juce::dsp::Phaser<float>, as JUCE 7 has it: six first-order allpass stages whose cutoff a sine LFO sweeps on a
// log scale around a centre frequency, updated every fourth sample, with feedback and a linear dry/wet mix, the LFO's
// depth, the feedback and the mix each smoothed over 50 ms.
template <typename T> class Phaser {
public:
    Phaser() { for (auto &f : filters_) f.setType(FirstOrderTPTFilterType::allpass); }
    void setRate(T hz) { rate_ = hz; update(); }
    void setDepth(T d) { depth_ = d; update(); }
    void setCentreFrequency(T hz) { centre_ = hz; update(); }
    void setFeedback(T f) { feedback_ = f; update(); }
    void setMix(T m) { mix_ = m; update(); }

    void prepare(const ProcessSpec &spec) {
        sampleRate_ = spec.sampleRate;
        for (auto &f : filters_) f.prepare(spec);
        dry_.reset(spec.sampleRate, 0.05);
        wet_.reset(spec.sampleRate, 0.05);
        oscFrequency_.reset(spec.sampleRate / kMaxUpdateCounter, 0.05);
        oscVolume_.reset(spec.sampleRate / kMaxUpdateCounter, 0.05);
        feedbackVolume_.assign(spec.numChannels, SmoothedValue<T>());
        for (auto &fv : feedbackVolume_) fv.reset(spec.sampleRate, 0.05);
        lastOutput_.assign(spec.numChannels, T());
        freq_.assign(spec.maximumBlockSize / kMaxUpdateCounter + 2, T());
        dryCopy_.assign(spec.numChannels, std::vector<T>(spec.maximumBlockSize, T()));
        reset();
        update();
    }
    void reset() {
        std::fill(lastOutput_.begin(), lastOutput_.end(), T());
        for (auto &f : filters_) f.reset();
        phase_ = 0;
        updateCounter_ = 0;
    }

    template <typename Context> void process(const Context &context) noexcept {
        const auto &in = context.getInputBlock();
        auto &out = context.getOutputBlock();
        const size_t channels = out.getNumChannels(), n = out.getNumSamples();
        for (size_t c = 0; c < channels && c < dryCopy_.size(); ++c)
            std::copy(in.getChannelPointer(c), in.getChannelPointer(c) + std::min(n, dryCopy_[c].size()), dryCopy_[c].begin());

        int numSamplesDown = 0;
        int counter = updateCounter_;
        for (size_t i = 0; i < n; ++i) {
            if (counter == 0) ++numSamplesDown;
            if (++counter == kMaxUpdateCounter) counter = 0;
        }
        const T lo = T(20), hi = static_cast<T>(std::min(20000.0, 0.49 * sampleRate_));
        for (int i = 0; i < numSamplesDown && i < static_cast<int>(freq_.size()); ++i) {
            // juce::dsp::Oscillator: the generator reads the phase less pi, and the phase advances after
            const T f = oscFrequency_.getNextValue();
            const T osc = std::sin(phase_ - static_cast<T>(3.141592653589793238));
            phase_ += static_cast<T>(2.0 * 3.141592653589793238 * f / (sampleRate_ / kMaxUpdateCounter));
            while (phase_ >= static_cast<T>(2.0 * 3.141592653589793238)) phase_ -= static_cast<T>(2.0 * 3.141592653589793238);
            const T lfo = jlimit(T(0), T(1), osc * oscVolume_.getNextValue() + normCentre_);
            freq_[static_cast<size_t>(i)] = lo * std::pow(hi / lo, lfo);
        }

        const T currentFrequency = filters_[0].getCutoffFrequency();
        for (size_t c = 0; c < channels; ++c) {
            counter = updateCounter_;
            int k = 0;
            const T *src = in.getChannelPointer(c);
            T *dst = out.getChannelPointer(c);
            for (size_t i = 0; i < n; ++i) {
                T output = src[i] - lastOutput_[c];
                if (i == 0 && counter != 0) for (auto &f : filters_) f.setCutoffFrequency(currentFrequency);
                if (counter == 0) { for (auto &f : filters_) f.setCutoffFrequency(freq_[static_cast<size_t>(k)]); ++k; }
                for (auto &f : filters_) output = f.processSample(static_cast<int>(c), output);
                dst[i] = output;
                lastOutput_[c] = output * feedbackVolume_[c].getNextValue();
                if (++counter == kMaxUpdateCounter) counter = 0;
            }
        }
        // juce::dsp::DryWetMixer, linear rule
        for (size_t i = 0; i < n; ++i) {
            const T d = dry_.getNextValue(), w = wet_.getNextValue();
            for (size_t c = 0; c < channels && c < dryCopy_.size(); ++c) {
                T *dst = out.getChannelPointer(c);
                dst[i] = dst[i] * w + dryCopy_[c][i] * d;
            }
        }
        updateCounter_ = static_cast<int>((static_cast<size_t>(updateCounter_) + n) % kMaxUpdateCounter);
    }

private:
    static constexpr int kMaxUpdateCounter = 4;
    void update() {
        oscFrequency_.setTargetValue(rate_);
        oscVolume_.setTargetValue(depth_ * static_cast<T>(0.5));
        dry_.setTargetValue(T(1) - mix_);
        wet_.setTargetValue(mix_);
        for (auto &fv : feedbackVolume_) fv.setTargetValue(feedback_);
        const T lo = T(20), hi = static_cast<T>(std::min(20000.0, 0.49 * sampleRate_));
        normCentre_ = std::log10(centre_ / lo) / std::log10(hi / lo);
    }
    std::array<FirstOrderTPTFilter<T>, 6> filters_;
    SmoothedValue<T> oscFrequency_{T(1)}, oscVolume_{T(0.25)}, dry_{T(0.5)}, wet_{T(0.5)};
    std::vector<SmoothedValue<T>> feedbackVolume_{SmoothedValue<T>(), SmoothedValue<T>()};
    std::vector<T> lastOutput_{T(), T()}, freq_;
    std::vector<std::vector<T>> dryCopy_;
    T rate_{T(1)}, depth_{T(0.5)}, feedback_{T(0)}, mix_{T(0.5)}, centre_{T(1300)}, normCentre_{T(0.5)};
    T phase_{T(0)};
    int updateCounter_{0};
    double sampleRate_{44100.0};
};

// juce::dsp::Gain<float>: a gain in linear terms, ramped over a duration when it changes
template <typename FloatType> class Gain {
public:
    void setGainLinear(FloatType g) noexcept { gain_.setTargetValue(g); }
    void setGainDecibels(FloatType dB) noexcept { setGainLinear(Decibels::decibelsToGain<FloatType>(dB, FloatType(-100))); }
    FloatType getGainLinear() const noexcept { return gain_.getTargetValue(); }
    FloatType getGainDecibels() const noexcept { return Decibels::gainToDecibels<FloatType>(getGainLinear(), FloatType(-100)); }
    void setRampDurationSeconds(double s) noexcept { if (rampSeconds_ != s) { rampSeconds_ = s; reset(); } }
    bool isSmoothing() const noexcept { return gain_.isSmoothing(); }
    void prepare(const ProcessSpec &spec) noexcept { sampleRate_ = spec.sampleRate; reset(); }
    void reset() noexcept { if (sampleRate_ > 0) gain_.reset(sampleRate_, rampSeconds_); }
    FloatType processSample(FloatType s) noexcept { return s * gain_.getNextValue(); }
    template <typename Context> void process(const Context &context) noexcept {
        const auto &in = context.getInputBlock();
        auto &out = context.getOutputBlock();
        const size_t channels = out.getNumChannels(), n = out.getNumSamples();
        if (context.isBypassed) { gain_.skip(static_cast<int>(n)); if (context.usesSeparateInputAndOutputBlocks()) out.copyFrom(in); return; }
        for (size_t i = 0; i < n; ++i) {
            const FloatType g = gain_.getNextValue();
            for (size_t c = 0; c < channels; ++c) out.getChannelPointer(c)[i] = in.getChannelPointer(c)[i] * g;
        }
    }

private:
    SmoothedValue<FloatType> gain_{FloatType(0)};   // as JUCE: silent until a gain is set, then ramped to it
    double sampleRate_ = 0, rampSeconds_ = 0;
};

namespace IIR {

// juce::dsp::IIR::Coefficients<float>: a0 divided out, { b0, b1, b2, a1, a2 } (or { b0, b1, a1 })
template <typename T> struct Coefficients {
    using Ptr = std::shared_ptr<Coefficients>;
    std::vector<T> coefficients;
    Coefficients(){coefficients.reserve(5);}
    // Explicit per-thread destination lets native callers update owned coefficients in place.
    inline static thread_local Ptr destination{};
    struct Reuse {Ptr previous;Reuse(Ptr p):previous(destination){destination=std::move(p);}~Reuse(){destination=std::move(previous);}};
    size_t getFilterOrder() const noexcept { return (coefficients.size() - 1) / 2; }
    const T *getRawCoefficients() const noexcept { return coefficients.data(); }
    static Ptr fromSix(T b0, T b1, T b2, T a0, T a1, T a2) {
        auto c = destination ? destination : std::make_shared<Coefficients>();
        const T inv = a0 != T() ? T(1) / a0 : T();
        c->coefficients = {b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv};
        return c;
    }
    static Ptr makeHighPass(double sampleRate, T frequency, T Q = static_cast<T>(1.0 / 1.4142135623730951)) {
        const T n = std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        const T n2 = n * n, invQ = T(1) / Q, c1 = T(1) / (T(1) + invQ * n + n2);
        return fromSix(c1, c1 * T(-2), c1, T(1), c1 * T(2) * (n2 - T(1)), c1 * (T(1) - invQ * n + n2));
    }
    // the shelves, as JUCE's ArrayCoefficients writes them
    static Ptr makeLowShelf(double sampleRate, T cutOffFrequency, T Q, T gainFactor) {
        const T A = jmax(T(), std::sqrt(gainFactor));
        const T aminus1 = A - 1, aplus1 = A + 1;
        const T omega = (2 * static_cast<T>(3.141592653589793238) * jmax(cutOffFrequency, T(2))) / static_cast<T>(sampleRate);
        const T coso = std::cos(omega), beta = std::sin(omega) * std::sqrt(A) / Q;
        const T aminus1TimesCoso = aminus1 * coso;
        return fromSix(A * (aplus1 - aminus1TimesCoso + beta), A * 2 * (aminus1 - aplus1 * coso), A * (aplus1 - aminus1TimesCoso - beta),
                       aplus1 + aminus1TimesCoso + beta, -2 * (aminus1 + aplus1 * coso), aplus1 + aminus1TimesCoso - beta);
    }
    static Ptr makeHighShelf(double sampleRate, T cutOffFrequency, T Q, T gainFactor) {
        const T A = jmax(T(), std::sqrt(gainFactor));
        const T aminus1 = A - 1, aplus1 = A + 1;
        const T omega = (2 * static_cast<T>(3.141592653589793238) * jmax(cutOffFrequency, T(2))) / static_cast<T>(sampleRate);
        const T coso = std::cos(omega), beta = std::sin(omega) * std::sqrt(A) / Q;
        const T aminus1TimesCoso = aminus1 * coso;
        return fromSix(A * (aplus1 + aminus1TimesCoso + beta), A * -2 * (aminus1 + aplus1 * coso), A * (aplus1 + aminus1TimesCoso - beta),
                       aplus1 - aminus1TimesCoso + beta, 2 * (aminus1 - aplus1 * coso), aplus1 - aminus1TimesCoso - beta);
    }
    // the rest of JUCE's factories, as its ArrayCoefficients writes them (Frequalizer asks for all of them)
    static Ptr fromFour(T b0, T b1, T a0, T a1) {
        auto c = destination ? destination : std::make_shared<Coefficients>();
        const T inv = a0 != T() ? T(1) / a0 : T();
        c->coefficients = {b0 * inv, b1 * inv, a1 * inv};
        return c;
    }
    static Ptr makeFirstOrderLowPass(double sampleRate, T frequency) {
        const T n = std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        return fromFour(n, n, n + 1, n - 1);
    }
    static Ptr makeFirstOrderHighPass(double sampleRate, T frequency) {
        const T n = std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        return fromFour(1, -1, n + 1, n - 1);
    }
    static Ptr makeFirstOrderAllPass(double sampleRate, T frequency) {
        const T n = std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        return fromFour(n - 1, n + 1, n + 1, n - 1);
    }
    static Ptr makeBandPass(double sampleRate, T frequency, T Q = static_cast<T>(1.0 / 1.4142135623730951)) {
        const T n = T(1) / std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        const T n2 = n * n, invQ = T(1) / Q, c1 = T(1) / (T(1) + invQ * n + n2);
        return fromSix(c1 * n * invQ, 0, -c1 * n * invQ, 1, c1 * 2 * (T(1) - n2), c1 * (T(1) - invQ * n + n2));
    }
    static Ptr makeNotch(double sampleRate, T frequency, T Q = static_cast<T>(1.0 / 1.4142135623730951)) {
        const T n = T(1) / std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        const T n2 = n * n, invQ = T(1) / Q, c1 = T(1) / (T(1) + n * invQ + n2);
        const T b0 = c1 * (T(1) + n2), b1 = 2 * c1 * (T(1) - n2);
        return fromSix(b0, b1, b0, 1, b1, c1 * (T(1) - n * invQ + n2));
    }
    static Ptr makeAllPass(double sampleRate, T frequency, T Q = static_cast<T>(1.0 / 1.4142135623730951)) {
        const T n = T(1) / std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        const T n2 = n * n, invQ = T(1) / Q, c1 = T(1) / (T(1) + invQ * n + n2);
        const T b0 = c1 * (T(1) - n * invQ + n2), b1 = c1 * 2 * (T(1) - n2);
        return fromSix(b0, b1, 1, 1, b1, b0);
    }
    static Ptr makePeakFilter(double sampleRate, T frequency, T Q, T gainFactor) {
        const T A = jmax(T(), std::sqrt(gainFactor));
        const T omega = (2 * static_cast<T>(3.141592653589793238) * jmax(frequency, T(2))) / static_cast<T>(sampleRate);
        const T alpha = std::sin(omega) / (Q * 2), c2 = -2 * std::cos(omega);
        const T alphaTimesA = alpha * A, alphaOverA = alpha / A;
        return fromSix(1 + alphaTimesA, c2, 1 - alphaTimesA, 1 + alphaOverA, c2, 1 - alphaOverA);
    }
    // JUCE's Coefficients::getMagnitudeForFrequency
    double getMagnitudeForFrequency(double frequency, double sampleRate) const noexcept {
        const std::complex<double> j(0, 1);
        const size_t order = getFilterOrder();
        std::complex<double> numerator = 0.0, denominator = 1.0, factor = 1.0;
        const std::complex<double> jw = std::exp(-2.0 * 3.141592653589793238 * frequency * j / sampleRate);
        for (size_t n = 0; n <= order; ++n) { numerator += static_cast<double>(coefficients[n]) * factor; factor *= jw; }
        factor = jw;
        for (size_t n = order + 1; n <= 2 * order; ++n) { denominator += static_cast<double>(coefficients[n]) * factor; factor *= jw; }
        return std::abs(numerator / denominator);
    }
    static Ptr makeLowPass(double sampleRate, T frequency, T Q = static_cast<T>(1.0 / 1.4142135623730951)) {
        const T n = T(1) / std::tan(static_cast<T>(3.141592653589793238) * frequency / static_cast<T>(sampleRate));
        const T n2 = n * n, invQ = T(1) / Q, c1 = T(1) / (T(1) + invQ * n + n2);
        return fromSix(c1, c1 * T(2), c1, T(1), c1 * T(2) * (T(1) - n2), c1 * (T(1) - invQ * n + n2));
    }
};

// juce::dsp::IIR::Filter<float>: transposed direct form II
template <typename T> class Filter {
public:
    using CoefficientsPtr = typename Coefficients<T>::Ptr;
    CoefficientsPtr coefficients;
    Filter() : coefficients(Coefficients<T>::fromSix(1, 0, 0, 1, 0, 0)) { reset(); }
    void prepare(const ProcessSpec &) noexcept { reset(); }
    void reset() { reset(T()); }
    void reset(T v) { order_ = coefficients->getFilterOrder(); state_.assign(order_ + 1, v); }
    T processSample(T x) noexcept {
        if (order_ != coefficients->getFilterOrder()) reset();
        const T *c = coefficients->getRawCoefficients();
        const T y = c[0] * x + state_[0];
        for (size_t j = 0; j + 1 < order_; ++j) state_[j] = c[j + 1] * x - c[order_ + j + 1] * y + state_[j + 1];
        state_[order_ - 1] = c[order_] * x - c[order_ * 2] * y;
        return y;
    }
    void snapToZero() noexcept { for (auto &e : state_) if (!(e < T(-1.0e-8) || e > T(1.0e-8))) e = T(); }
    template <typename Context> void process(const Context &context) noexcept {
        auto &out = context.getOutputBlock();
        const T *src = context.getInputBlock().getChannelPointer(0); T *dst = out.getChannelPointer(0);
        for (size_t i = 0; i < out.getNumSamples(); ++i) dst[i] = context.isBypassed ? src[i] : processSample(src[i]);
        snapToZero();
    }

private:
    size_t order_{2};
    std::vector<T> state_;
};

} // namespace IIR

// juce::dsp::Oversampling<float> with filterHalfBandPolyphaseIIR: `factor` stages of 2x, each a pair of polyphase
// allpass chains designed as FilterDesign::designIIRLowpassHalfBandPolyphaseAllpassMethod does, with JUCE's figures
// for each stage; with integer latency, the first-order Thiran allpass that rounds the latency up.
template <typename T> class Oversampling {
public:
    enum FilterType { filterHalfBandPolyphaseIIR = 0, filterHalfBandFIREquiripple, numFilterTypes };

    Oversampling(size_t numChannels, size_t factor, FilterType, bool isMaxQuality = true, bool useIntegerLatency = false)
        : channels_(numChannels), integerLatency_(useIntegerLatency) {
        for (size_t n = 0; n < factor; ++n) {
            const double twUp = (isMaxQuality ? 0.10 : 0.12) * (n == 0 ? 0.5 : 1.0);
            const double twDown = (isMaxQuality ? 0.12 : 0.15) * (n == 0 ? 0.5 : 1.0);
            const double dbUp = (isMaxQuality ? -90.0 : -70.0) + (isMaxQuality ? 10.0 : 8.0) * static_cast<double>(n);
            const double dbDown = (isMaxQuality ? -75.0 : -60.0) + (isMaxQuality ? 10.0 : 8.0) * static_cast<double>(n);
            stages_.emplace_back(new Stage(numChannels, twUp, dbUp, twDown, dbDown));
        }
        factor_ = static_cast<size_t>(1) << factor;
    }

    size_t getOversamplingFactor() const noexcept { return factor_; }
    T getLatencyInSamples() const noexcept { const T u = uncompensated(); return integerLatency_ ? u + fractional_ : u; }

    void initProcessing(size_t maxBlock) {
        size_t n = maxBlock;
        for (auto &s : stages_) { n *= 2; s->init(n); }
        updateFractional();
        reset();
    }
    void reset() noexcept {
        for (auto &s : stages_) s->reset();
        thiranX_.assign(channels_, T()); thiranY_.assign(channels_, T());
    }

    AudioBlock<T> processSamplesUp(const AudioBlock<T> &input) noexcept {
        AudioBlock<T> block = input;
        for (auto &s : stages_) { s->up(block); block = s->block(block.getNumSamples() * 2); }
        return block;
    }
    void processSamplesDown(AudioBlock<T> &output) noexcept {
        if (stages_.empty()) return;
        size_t n = output.getNumSamples();
        for (size_t k = 0; k + 1 < stages_.size(); ++k) n *= 2;
        for (size_t k = stages_.size() - 1; k > 0; --k) {
            auto lower = stages_[k - 1]->block(n);
            stages_[k]->down(lower);
            n /= 2;
        }
        stages_[0]->down(output);
        if (integerLatency_ && fractional_ > T()) {
            const T alpha = (T(1) - fractional_) / (T(1) + fractional_);
            for (size_t c = 0; c < std::min(channels_, output.getNumChannels()); ++c) {
                T *d = output.getChannelPointer(c); T x1 = thiranX_[c], y1 = thiranY_[c];
                for (size_t i = 0; i < output.getNumSamples(); ++i) { const T x = d[i]; const T y = x1 + alpha * (x - y1); x1 = x; y1 = y; d[i] = y; }
                thiranX_[c] = x1; thiranY_[c] = y1;
            }
        }
    }

private:
    struct Stage {
        Stage(size_t channels, double twUp, double dbUp, double twDown, double dbDown) : channels_(channels) {
            latency_ = design(twUp, dbUp, up_, directUp_) + design(twDown, dbDown, down_, directDown_);
            v1Up_.assign(channels, std::vector<T>(up_.size(), T()));
            v1Down_.assign(channels, std::vector<T>(down_.size(), T()));
            delayDown_.assign(channels, T());
        }
        void init(size_t samples) {
            buffer_.assign(channels_, std::vector<T>(samples, T()));
            pointers_.resize(channels_);
            for (size_t c = 0; c < channels_; ++c) pointers_[c] = buffer_[c].data();
        }
        void reset() {
            for (auto &v : v1Up_) std::fill(v.begin(), v.end(), T());
            for (auto &v : v1Down_) std::fill(v.begin(), v.end(), T());
            for (auto &b : buffer_) std::fill(b.begin(), b.end(), T());
            std::fill(delayDown_.begin(), delayDown_.end(), T());
        }
        AudioBlock<T> block(size_t samples) { return AudioBlock<T>(pointers_.data(), channels_, samples); }
        void up(const AudioBlock<T> &in) noexcept {
            const int stages = static_cast<int>(up_.size());
            for (size_t c = 0; c < std::min(channels_, in.getNumChannels()); ++c) {
                T *out = buffer_[c].data(); T *lv1 = v1Up_[c].data(); const T *src = in.getChannelPointer(c);
                for (size_t i = 0; i < in.getNumSamples(); ++i) {
                    T input = src[i];
                    for (int s = 0; s < directUp_; ++s) { const T a = up_[static_cast<size_t>(s)], o = a * input + lv1[s]; lv1[s] = input - a * o; input = o; }
                    out[i << 1] = input;
                    input = src[i];
                    for (int s = directUp_; s < stages; ++s) { const T a = up_[static_cast<size_t>(s)], o = a * input + lv1[s]; lv1[s] = input - a * o; input = o; }
                    out[(i << 1) + 1] = input;
                }
                snap(v1Up_[c]);
            }
        }
        void down(AudioBlock<T> &outBlock) noexcept {
            const int stages = static_cast<int>(down_.size());
            for (size_t c = 0; c < std::min(channels_, outBlock.getNumChannels()); ++c) {
                const T *buf = buffer_[c].data(); T *lv1 = v1Down_[c].data(); T *out = outBlock.getChannelPointer(c);
                T delay = delayDown_[c];
                for (size_t i = 0; i < outBlock.getNumSamples(); ++i) {
                    T input = buf[i << 1];
                    for (int s = 0; s < directDown_; ++s) { const T a = down_[static_cast<size_t>(s)], o = a * input + lv1[s]; lv1[s] = input - a * o; input = o; }
                    const T direct = input;
                    input = buf[(i << 1) + 1];
                    for (int s = directDown_; s < stages; ++s) { const T a = down_[static_cast<size_t>(s)], o = a * input + lv1[s]; lv1[s] = input - a * o; input = o; }
                    out[i] = (delay + direct) * T(0.5);
                    delay = input;
                }
                delayDown_[c] = delay;
                snap(v1Down_[c]);
            }
        }
        static void snap(std::vector<T> &v) noexcept { for (auto &x : v) if (!(x < T(-1.0e-8) || x > T(1.0e-8))) x = T(); }
        // the direct path's allpass coefficients followed by the delayed path's; returns the delay near DC
        static double design(double transitionWidth, double stopbandDb, std::vector<T> &coeffs, int &directStages) {
            const double pi = 3.141592653589793238;
            const double wt = 2.0 * pi * transitionWidth;
            const double ds = std::pow(10.0, stopbandDb * 0.05);
            const double k = std::pow(std::tan((pi - wt) / 4), 2.0);
            const double kp = std::sqrt(1.0 - k * k);
            const double e = (1 - std::sqrt(kp)) / (1 + std::sqrt(kp)) * 0.5;
            const double q = e + 2 * std::pow(e, 5.0) + 15 * std::pow(e, 9.0) + 150 * std::pow(e, 13.0);
            const double k1 = ds * ds / (1 - ds * ds);
            int n = static_cast<int>(std::lround(std::ceil(std::log(k1 * k1 / 16) / std::log(q))));
            if (n % 2 == 0) ++n;
            if (n == 1) n = 3;
            const int N = (n - 1) / 2;
            std::vector<double> ai;
            for (int i = 1; i <= N; ++i) {
                double num = 0.0, delta = 1.0; int m = 0;
                while (std::abs(delta) > 1e-100) { delta = std::pow(-1, m) * std::pow(q, m * (m + 1)) * std::sin((2 * m + 1) * pi * i / static_cast<double>(n)); num += delta; m++; }
                num *= 2 * std::pow(q, 0.25);
                double den = 0.0; delta = 1.0; m = 1;
                while (std::abs(delta) > 1e-100) { delta = std::pow(-1, m) * std::pow(q, m * m) * std::cos(m * 2.0 * pi * i / static_cast<double>(n)); den += delta; ++m; }
                den = 1 + 2 * den;
                const double wi = num / den;
                const double api = std::sqrt((1 - wi * wi * k) * (1 - wi * wi / k)) / (1 + wi * wi);
                ai.push_back((1 - api) / (1 + api));
            }
            std::vector<T> direct, delayed;
            for (int i = 0; i < N; i += 2) direct.push_back(static_cast<T>(ai[static_cast<size_t>(i)]));
            for (int i = 1; i < N; i += 2) delayed.push_back(static_cast<T>(ai[static_cast<size_t>(i)]));
            coeffs = direct; coeffs.insert(coeffs.end(), delayed.begin(), delayed.end());
            directStages = static_cast<int>(direct.size());
            const double w = 2.0 * pi * 0.0001;
            const std::complex<double> zi = std::polar(1.0, -w);
            auto allpass = [&](double a) { return (a + zi) / (1.0 + a * zi); };
            std::complex<double> h1{1.0, 0.0}, h2 = zi;
            for (T a : direct) h1 *= allpass(a);
            for (T a : delayed) h2 *= allpass(a);
            return -std::arg(h1 + h2) / w;
        }
        size_t channels_;
        double latency_{0};
        std::vector<T> up_, down_;
        int directUp_{0}, directDown_{0};
        std::vector<std::vector<T>> v1Up_, v1Down_, buffer_;
        std::vector<T *> pointers_;
        std::vector<T> delayDown_;
    };

    T uncompensated() const noexcept {
        double latency = 0, order = 1;
        for (auto &s : stages_) { order *= 2; latency += s->latency_ / order; }
        return static_cast<T>(latency);
    }
    void updateFractional() noexcept {
        const T u = uncompensated();
        fractional_ = T(1) - (u - std::floor(u));
        if (std::abs(fractional_ - T(1)) < T(1e-6)) fractional_ = T();
        else if (fractional_ < T(0.618)) fractional_ += T(1);
    }

    size_t channels_, factor_{1};
    bool integerLatency_;
    T fractional_{};
    std::vector<std::unique_ptr<Stage>> stages_;
    std::vector<T> thiranX_, thiranY_;
};


// A radix-2 FFT with JUCE's conventions. The pitch shifter uses it to line up its crossfades (Pitcher.cpp).
template <typename T> using Complex = std::complex<T>;

class FFT {
public:
    explicit FFT(int order) : size_(1 << order), scratch_(static_cast<size_t>(1) << order) {
        rev_.resize(static_cast<size_t>(size_));
        for (int i = 0; i < size_; ++i) {
            int r = 0;
            for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev_[static_cast<size_t>(i)] = r;
        }
        twiddles_.resize(static_cast<size_t>(size_ / 2));
        for (int k = 0; k < size_ / 2; ++k) {
            const double a = -2.0 * 3.141592653589793238 * k / size_;
            twiddles_[static_cast<size_t>(k)] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
    }
    int getSize() const noexcept { return size_; }

    // the complex transform, as JUCE's perform is: N bins in, N bins out, and an inverse scaled by 1/N
    void perform(const Complex<float> *input, Complex<float> *output, bool inverse) const noexcept {
        for (int i = 0; i < size_; ++i) scratch_[static_cast<size_t>(i)] = input[i];
        transform(scratch_.data(), inverse);
        const float scale = inverse ? 1.0f / static_cast<float>(size_) : 1.0f;
        for (int i = 0; i < size_; ++i) output[i] = scratch_[static_cast<size_t>(i)] * scale;
    }

    // d holds 2N floats: N real samples in, N interleaved complex bins out
    void performRealOnlyForwardTransform(float *d, bool = false) const noexcept {
        for (int i = 0; i < size_; ++i) scratch_[static_cast<size_t>(i)] = {d[i], 0.f};
        transform(scratch_.data(), false);
        for (int i = 0; i < size_; ++i) {
            d[2 * i] = scratch_[static_cast<size_t>(i)].real();
            d[2 * i + 1] = scratch_[static_cast<size_t>(i)].imag();
        }
    }
    // d holds N interleaved complex bins in (the upper half is taken from the lower); real parts out in d[0..N),
    // imaginary parts in d[N..2N)
    void performRealOnlyInverseTransform(float *d) const noexcept {
        for (int i = 0; i < size_; ++i) scratch_[static_cast<size_t>(i)] = {d[2 * i], d[2 * i + 1]};
        for (int i = size_ >> 1; i < size_; ++i) scratch_[static_cast<size_t>(i)] = std::conj(scratch_[static_cast<size_t>(size_ - i)]);
        transform(scratch_.data(), true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (int i = 0; i < size_; ++i) {
            d[i] = scratch_[static_cast<size_t>(i)].real() * scale;
            d[i + size_] = scratch_[static_cast<size_t>(i)].imag() * scale;
        }
    }

private:
    void transform(std::complex<float> *x, bool inverse) const noexcept {
        for (int i = 0; i < size_; ++i) {
            const int j = rev_[static_cast<size_t>(i)];
            if (j > i) std::swap(x[i], x[j]);
        }
        for (int len = 2; len <= size_; len <<= 1) {
            const int half = len >> 1, step = size_ / len;
            for (int i = 0; i < size_; i += len) {
                for (int k = 0; k < half; ++k) {
                    auto w = twiddles_[static_cast<size_t>(k * step)];
                    if (inverse) w = std::conj(w);
                    const auto t = x[i + k + half] * w;
                    x[i + k + half] = x[i + k] - t;
                    x[i + k] += t;
                }
            }
        }
    }

    int size_;
    std::vector<int> rev_;
    std::vector<std::complex<float>> twiddles_;
    mutable std::vector<std::complex<float>> scratch_;
};

} // namespace dsp
// A plugin's hand-off to the message thread. Here there is none between the engine and its parameters, so what it
// is asked to do happens at once: it is only ever a parameter being set, which is a store.
struct MessageManager {
    template <typename F> static void callAsync(F &&f) { f(); }
};

} // namespace juce

using namespace juce;
