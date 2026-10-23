#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "Synth.h"
#include "core/Generator.h"
#include "core/MelodyCore.h"

#if defined(JucePlugin_IsSynth) && JucePlugin_IsSynth
constexpr bool kInstrumentBuild = true;
#else
constexpr bool kInstrumentBuild = false;
#endif

namespace ids
{
inline const juce::String range = "range", clarity = "clarity", gate = "gate", stability = "stability",
                          minNote = "minNote", split = "split", autoTune = "autoTune", tuning = "tuning",
                          keyTonic = "keyTonic", scale = "scale", snap = "snap", transpose = "transpose",
                          grid = "grid", quantize = "quantize", strength = "strength", bends = "bends",
                          bendRange = "bendRange", midiOut = "midiOut";
// generator
inline const juce::String gStyle = "gStyle", gKey = "gKey", gScale = "gScale", gBars = "gBars",
                          gDensity = "gDensity", gSyncop = "gSyncop", gRepeat = "gRepeat", gComplex = "gComplex",
                          gOrnament = "gOrnament", gRange = "gRange", gOctave = "gOctave", gHumanize = "gHumanize",
                          gSwing = "gSwing", gPlay = "gPlay", gVolume = "gVolume";
}

/** What the MIDI output plays during host playback. */
enum class MidiOutSource { Off = 0, Vocal, Pattern };

/**
 * Two jobs in one plugin:
 *  - Generator: writes pop chord progressions, hooks, basslines and counter
 *    melodies (core/Generator), previews them with a small synth, and plays
 *    them in sync with the host.
 *  - Vocal to MIDI: captures the vocal from the track it is inserted on
 *    (positioned on the song timeline) and transcribes it to notes.
 *
 * Threading: everything under "model" lives on the message thread. The audio
 * thread only writes the capture buffer and reads a published copy of the
 * notes for MIDI output.
 */
class MelodyLiftProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater,
                            private juce::Timer
{
public:
    MelodyLiftProcessor();
    ~MelodyLiftProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MelodyLift"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ------------------------------------------------- message-thread API
    enum class Status { Idle, Armed, Capturing, Analysing };
    Status getStatus() const;
    float getProgress() const { return progress.load(); }
    juce::String getStatusText() const;

    void toggleCapture();
    bool isArmed() const { return armed.load(); }
    void loadAudioFile(const juce::File& file);
    void clearAll();

    const std::vector<ml::Note>& getNotes() const { return notes; }
    std::vector<ml::Note>& editNotes() { return notes; }
    const ml::PitchTrack& getTrack() const { return track; }
    const ml::Transcription& getTranscription() const { return tr; }
    bool hasAnalysis() const { return !track.empty(); }
    struct KeyChoice { int tonic = 0; int scale = ml::Major; bool isAuto = true; };
    KeyChoice currentKey() const;
    juce::String getSourceName() const { return sourceName; }
    /** Increments whenever a new analysis or project state arrives. */
    int getAnalysisCount() const { return analysisCount; }

    // editing (the piano roll calls these around its changes)
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    std::vector<uint8_t> buildMidi() const;
    juce::File writeMidiToTempFile() const;
    bool writeMidiFile(const juce::File& f) const;
    juce::String exportBaseName() const;

    // host transport, mirrored for the UI
    double getHostBpm() const { return hostBpm.load(); }
    int getTimeSigNum() const { return timeSigNum.load(); }
    int getTimeSigDen() const { return timeSigDen.load(); }
    double getPlayheadSeconds() const { return playheadSec.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    /** A short message for the editor to show once (e.g. edits replaced). */
    juce::String takeNotice();

    // ------------------------------------------------------- generator API
    enum class GenAction { All, Hook, Variation, Chords, Bass, Counter };
    void generate(GenAction action);
    void rerollLayer(int layer);
    const ml::gen::Pattern& getPattern() const { return pattern; }
    const std::vector<ml::Note>& genLayer(int layer) const { return genNotes[(size_t) layer]; }
    std::vector<ml::Note>& editGenLayer(int layer) { return genNotes[(size_t) layer]; }
    bool hasPattern() const { return !pattern.chords.empty(); }
    bool isPatternFromVocal() const { return fromVocal; }
    bool isLayerLocked(int l) const { return locked[(size_t) l]; }
    void setLayerLocked(int l, bool on) { locked[(size_t) l] = on; sendChangeMessage(); }
    bool isLayerMuted(int l) const { return muted[(size_t) l].load(); }
    void setLayerMuted(int l, bool on) { muted[(size_t) l].store(on); sendChangeMessage(); }
    int getActiveLayer() const { return activeLayer; }
    void setActiveLayer(int l) { activeLayer = juce::jlimit(0, ml::gen::NumLayers - 1, l); sendChangeMessage(); }
    void useVocalKey();
    /** Builds chords, bass and counter around the captured vocal melody. */
    bool chordsForVocal();
    void beginGenEdit();
    void endGenEdit();
    void genUndo();
    void genRedo();
    bool canGenUndo() const { return !genUndoStack.empty(); }
    bool canGenRedo() const { return !genRedoStack.empty(); }
    void setPreviewPlaying(bool on);
    bool isPreviewPlaying() const { return previewOn.load(); }
    /** Position inside the pattern in seconds, for the playhead (-1 when idle). */
    double getPatternPlayheadSeconds() const;
    double patternSeconds() const { return pattern.beats * 60.0 / genBpm; }
    /** layer < 0 = all layers as a multi-track file. */
    std::vector<uint8_t> buildPatternMidi(int layer) const;
    juce::File writePatternMidiToTempFile(int layer) const;
    bool writePatternMidiFile(const juce::File& f, int layer) const;
    juce::String patternSummary() const;
    int getUiTab() const { return uiTab; }
    void setUiTab(int t) { uiTab = t; }

private:
    void parameterChanged(const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void ensureCaptureBuffer();
    ml::gen::Settings genSettings() const;
    void runGenerate(bool replaceNotes);
    void patternToNotes();
    void publishPattern();
    void rescaleForTempo(double newBpm);
    void renderPattern(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool playing, bool hasPpq, double ppq);
    void startAnalysis(std::vector<float> audio16k, const juce::String& label);
    void onAnalysisDone(ml::PitchTrack t, std::vector<float> audio16k);
    void runTranscription(bool replaceNotes);
    void runPostProcess();
    void publishNotes();
    void pushUndo();
    ml::TranscribeParams transcribeParams() const;
    float param(const juce::String& id) const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ----- model (message thread)
    ml::PitchTrack track;
    ml::Transcription tr;
    std::vector<ml::Note> base, notes;
    std::vector<float> audio16k; // kept so a voice-range change can re-track
    std::vector<std::vector<ml::Note>> undoStack, redoStack;
    bool edited = false;
    juce::String sourceName, notice;
    bool restoring = false;
    int lastStatus = -1;
    int analysisCount = 0;
    uint32_t lastUndoPush = 0;

    // ----- generator model (message thread)
    ml::gen::Pattern pattern;
    ml::gen::Seeds seeds;
    std::array<std::vector<ml::Note>, ml::gen::NumLayers> genNotes;
    std::array<bool, ml::gen::NumLayers> locked {};
    std::array<std::atomic<bool>, ml::gen::NumLayers> muted {};
    std::vector<std::array<std::vector<ml::Note>, ml::gen::NumLayers>> genUndoStack, genRedoStack;
    int activeLayer = 0;
    bool fromVocal = false, genEdited = false;
    double genBpm = 120.0;
    int uiTab = 0;
    uint32_t lastGenUndoPush = 0;

    // ----- generator playback (audio thread reads)
    struct RtGen { double beat, len; int pitch, vel, layer; };
    juce::SpinLock genLock;
    std::vector<RtGen> rtGen;
    double rtGenBeats = 32;
    std::atomic<bool> previewOn { false };
    std::atomic<bool> genSounding { false };
    double previewBeat = 0, genExpected = -1;
    std::atomic<double> genBeatNow { -1.0 };
    PreviewSynth synth;
    juce::AudioBuffer<float> synthBuf;
    struct PendingOff { int64_t at; int channel, pitch; };
    std::array<PendingOff, 128> pendingOffs {};
    int numPendingOffs = 0;
    int64_t genSampleClock = 0;
    int64_t tailSamples = 0;

    // ----- background analysis
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads(1).withThreadName("MelodyLift analysis") };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::atomic<int> generation { 0 };
    std::atomic<bool> analysing { false };
    std::atomic<float> progress { 0.0f };
    juce::String analysisLabel;

    enum Level { None = 0, Post = 1, Notes = 2, Pitch = 3 };
    std::atomic<int> pendingLevel { None };
    std::atomic<bool> pendingGen { false };
    bool suppressGen = false;

    // ----- capture (audio thread writes)
    std::vector<float> capture;
    std::atomic<bool> armed { false };
    std::atomic<int64_t> maxWritten { -1 };
    std::atomic<uint32_t> lastWriteMs { 0 };
    std::atomic<bool> wroteSinceArm { false };
    std::atomic<bool> transportCapture { false };
    ml::PositionalResampler resampler;
    int64_t seqPos = 0;
    double sr = 44100.0;

    // ----- host transport mirror
    std::atomic<double> hostBpm { 120.0 }, playheadSec { 0.0 };
    std::atomic<int> timeSigNum { 4 }, timeSigDen { 4 };
    std::atomic<bool> hostPlaying { false };

    // ----- MIDI output (audio thread reads)
    struct RtNote { double start, end; int pitch, vel; };
    juce::SpinLock rtLock;
    std::vector<RtNote> rtNotes;
    std::array<int64_t, 128> activeEnd {};
    int64_t expectedNext = -1;
    void allNotesOff(juce::MidiBuffer& midi, int offset);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodyLiftProcessor)
};
