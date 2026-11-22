#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <vector>

#include "DspUtils.h"

namespace lavc
{
constexpr int kNumHarmonyVoices = 4;

enum class Scale
{
    chromatic, major, minor, harmonicMinor, melodicMinor, majorPentatonic, minorPentatonic, blues,
    dorian, phrygian, lydian, mixolydian, numScales
};

/** Bit i set = pitch class (key + i) belongs to the scale. */
uint16_t scaleMask (Scale s) noexcept;
const char* scaleName (Scale s) noexcept;
const char* keyName (int pitchClass) noexcept;

/** Nearest MIDI note (integer) in key/scale to `midi`. */
int quantizeToScale (float midi, int key, Scale scale) noexcept;
/** Moves `steps` scale degrees from an in-scale note (steps may be negative). */
int scaleStep (int note, int steps, int key, Scale scale) noexcept;

struct HarmonyVoiceParams
{
    bool enabled = false;
    int interval = 0;      // scale degrees (Scale mode) or semitones (Semitone mode)
    float levelDb = -6.0f;
    float pan = 0.0f;      // -1..1
    float formant = 0.0f;  // semitones
};

struct PitchParams
{
    // Tune
    bool tuneEnabled = false;
    int key = 0;
    Scale scale = Scale::chromatic;
    int targetMode = 0;      // 0 = scale, 1 = MIDI notes
    float speedMs = 20.0f;   // retune time (0 = hard tune)
    float toleranceCents = 0.0f;
    float amount = 1.0f;     // 0..1
    float transpose = 0.0f;  // semitones
    float formant = 0.0f;    // semitones
    // Harmonies
    bool harmEnabled = false;
    int harmMode = 0;        // 0 = scale degrees, 1 = semitones, 2 = MIDI chord
    float harmHumanize = 0.3f; // 0..1
    float harmLevelDb = 0.0f;
    std::array<HarmonyVoiceParams, kNumHarmonyVoices> voices {};
};

/** One analysis frame, reported to the UI. */
struct PitchFrame
{
    int64_t position = 0;     // input sample index
    float inputMidi = 0.0f;   // detected pitch (0 = unvoiced)
    float outputMidi = 0.0f;  // corrected lead pitch
    float targetMidi = 0.0f;  // scale / MIDI target note
    float levelDb = -120.0f;
    std::array<float, kNumHarmonyVoices> harmonyMidi {}; // 0 = silent
    bool voiced = false;
};

struct KeyEstimate
{
    int key = 0;
    bool minor = false;
    float confidence = 0.0f; // 0..1
    float seconds = 0.0f;    // amount of voiced material analysed
};

struct PitchListener
{
    virtual ~PitchListener() = default;
    virtual void pitchFrame (const PitchFrame&) {}
};

/**
    Real-time pitch detection (YIN), pitch correction and harmony generation.

    Detection runs on a decimated copy of the signal and is refined at full rate. Shifting uses
    pitch-synchronous overlap-add (TD-PSOLA): grains two periods long are cut around analysis marks
    spaced one period apart and re-spaced at the new period. Formants stay in place because the grain
    content is not resampled; the formant control resamples the grains on purpose.
*/
class PitchEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, bool trackingMode);
    void reset();
    void setParams (const PitchParams& p) noexcept { params = p; }

    /** Total delay of the lead / harmony outputs relative to the input. */
    int getLatency() const noexcept { return contentLatency; }

    void noteOn (int note) noexcept { if (note >= 0 && note < 128) heldNotes.set ((size_t) note); }
    void noteOff (int note) noexcept { if (note >= 0 && note < 128) heldNotes.reset ((size_t) note); }
    void allNotesOff() noexcept { heldNotes.reset(); }

    /** Mono in. Lead out is mono; harmonies are added (panned) into harmL / harmR. voiced[] flags the output. */
    void process (const float* in, float* lead, float* harmL, float* harmR, uint8_t* voiced, int numSamples,
                  PitchListener* listener) noexcept;

    // Key detection ------------------------------------------------------------------------------
    void setKeyLearning (bool on) noexcept { keyLearning = on; }
    void resetKeyHistogram() noexcept { pcHistogram.fill (0.0); }
    KeyEstimate estimateKey() const noexcept;

    float getLastInputMidi() const noexcept { return lastInputMidi; }

private:
    struct Mark
    {
        int64_t pos = 0;
        int period = 0;
        bool voiced = false;
        float srcMidi = 0.0f;
        float leadShift = 0.0f;                       // semitones applied to lead
        std::array<float, kNumHarmonyVoices> harm {}; // semitones relative to source
        std::array<bool, kNumHarmonyVoices> harmOn {};
    };

    struct SynthVoice
    {
        int64_t nextSynth = 0;
        int64_t markIndex = 0; // absolute mark counter of last chosen mark
        float gain = 0.0f;
        std::vector<float> acc, wsum;
        RandomLfo drift { 1u }, timing { 2u };
        float driftCents = 0.0f;
    };

    void analyse (float x) noexcept;
    void runDetection() noexcept;
    void placeMarks() noexcept;
    void computeTargets (Mark& m) noexcept;
    void synthesise (SynthVoice& v, int voiceIndex, int64_t emit) noexcept;
    float readInput (double pos) const noexcept;
    const Mark& markAt (int64_t index) const noexcept { return marks[(size_t) (index & (kMarkCap - 1))]; }

    double sr = 48000.0;
    bool tracking = false;
    PitchParams params;
    std::bitset<128> heldNotes;

    // Input history (full rate)
    std::vector<float> inRing;
    int inMask = 0;
    int64_t tNow = 0; // number of samples written

    // Decimated detection buffer
    Filter24 decimLp;
    int decim = 4, decimCount = 0;
    double decSr = 12000.0;
    std::vector<float> decRing;
    int decMask = 0;
    int64_t decCount = 0;
    int yinW = 200, tauMin = 12, tauMax = 171, hop = 32, hopCount = 0;
    std::vector<float> yinBuf, cmBuf;

    // Current estimate
    float curPeriod = 0.0f; // full-rate samples, 0 = unvoiced
    float curLevelDb = -120.0f;
    std::array<float, 3> recentPeriods {};
    int recentCount = 0;
    float levelAcc = 0.0f;
    int levelCount = 0;

    // Marks
    static constexpr int kMarkCap = 2048;
    std::vector<Mark> marks;
    int64_t numMarks = 0;
    int64_t nextMarkPos = 0;
    int markLag = 0, unvoicedSpacing = 240, maxPeriod = 686, minPeriod = 48;

    // Tuning state
    float corr = 0.0f;
    int lastTarget = -1;
    float lastInputMidi = 0.0f;

    // Synthesis
    int outLatency = 0, analysisLag = 0, contentLatency = 0;
    int outMask = 0;
    SynthVoice lead;
    std::array<SynthVoice, kNumHarmonyVoices> harmonies;
    LatencyDelay dryDelay;
    Smoothed tuneMix;
    std::array<Smoothed, kNumHarmonyVoices> panL, panR;

    // Key histogram
    bool keyLearning = false;
    std::array<double, 12> pcHistogram {};
};
} // namespace lavc
