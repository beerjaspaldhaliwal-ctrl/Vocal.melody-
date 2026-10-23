#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>

namespace
{
constexpr double kMaxCaptureSeconds = 12 * 60;

const juce::StringArray kRangeNames { "Any voice (C2-C6)", "Low voice (A1-F5)", "High voice (C3-F6)" };
const double kRangeHz[3][2] = { { 65, 1100 }, { 55, 700 }, { 130, 1400 } };

juce::StringArray tonicChoices()
{
    juce::StringArray a { "Auto" };
    for (int i = 0; i < 12; ++i) a.add(ml::pitchClassName(i));
    return a;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MelodyLiftProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(juce::roundToInt(v * 100)) + "%"; });
    auto db = AudioParameterFloatAttributes().withLabel("dB");
    auto ms = AudioParameterFloatAttributes().withLabel("ms");

    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::range, 1 }, "Voice range", kRangeNames, 0));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::clarity, 1 }, "Pitch clarity",
                                                      NormalisableRange<float>(0.2f, 0.9f, 0.01f), 0.55f, pct));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::gate, 1 }, "Silence threshold",
                                                      NormalisableRange<float>(-70.0f, -15.0f, 1.0f), -45.0f, db));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::stability, 1 }, "Note stability",
                                                      NormalisableRange<float>(2.0f, 30.0f, 0.5f), 10.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::minNote, 1 }, "Shortest note",
                                                      NormalisableRange<float>(20.0f, 300.0f, 5.0f), 70.0f, ms));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::split, 1 }, "Syllable split",
                                                      NormalisableRange<float>(0.0f, 15.0f, 0.5f), 6.0f, db));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::autoTune, 1 }, "Detect tuning", true));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::tuning, 1 }, "Reference tuning",
                                                      NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("ct")));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::keyTonic, 1 }, "Key", tonicChoices(), 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::scale, 1 }, "Scale",
        StringArray { "Major", "Minor", "Harmonic minor", "Dorian", "Mixolydian", "Major pentatonic", "Minor pentatonic",
                      "Phrygian", "Phrygian dominant" }, 0));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::snap, 1 }, "Snap to scale", false));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID { ids::transpose, 1 }, "Transpose", -24, 24, 0,
                                                    AudioParameterIntAttributes().withLabel("st")));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::grid, 1 }, "Grid",
        StringArray { "Off", "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" }, 4));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::quantize, 1 }, "Quantize", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { ids::strength, 1 }, "Quantize strength",
                                                      NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::bends, 1 }, "Export pitch bends", false));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::bendRange, 1 }, "Bend range",
                                                       StringArray { "+/-2 semitones", "+/-12 semitones" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::midiOut, 2 }, "MIDI out",
                                                       StringArray { "Off", "Vocal notes", "Generated pattern" }, 0));

    // generator
    StringArray styles;
    for (int i = 0; i < ml::gen::NumStyles; ++i) styles.add(ml::gen::styleName(i));
    StringArray keys;
    for (int i = 0; i < 12; ++i) keys.add(ml::pitchClassName(i));
    auto amount = [](const String& id, const String& name, float def) {
        return std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(0.0f, 100.0f, 1.0f), def,
                                                     AudioParameterFloatAttributes().withLabel("%"));
    };
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gStyle, 1 }, "Style", styles, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gKey, 1 }, "Key", keys, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gScale, 1 }, "Scale",
        StringArray { "Style default", "Major", "Minor", "Harmonic minor", "Dorian", "Mixolydian", "Major pentatonic",
                      "Minor pentatonic", "Phrygian", "Phrygian dominant" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gBars, 1 }, "Length", StringArray { "4 bars", "8 bars", "16 bars" }, 1));
    p.push_back(amount(ids::gDensity, "Density", 50));
    p.push_back(amount(ids::gSyncop, "Syncopation", 50));
    p.push_back(amount(ids::gRepeat, "Catchiness", 70));
    p.push_back(amount(ids::gComplex, "Variation", 35));
    p.push_back(amount(ids::gOrnament, "Ornaments", 50));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gRange, 1 }, "Range", StringArray { "Narrow", "Medium", "Wide" }, 1));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::gOctave, 1 }, "Register", StringArray { "Low", "Mid", "High" }, 1));
    p.push_back(amount(ids::gHumanize, "Humanize", 20));
    p.push_back(amount(ids::gSwing, "Swing", 0));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::gPlay, 1 }, "Play pattern with the song", kInstrumentBuild));
    p.push_back(amount(ids::gVolume, "Preview volume", 70));
    return { p.begin(), p.end() };
}

MelodyLiftProcessor::MelodyLiftProcessor()
    : AudioProcessor(kInstrumentBuild ? BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      : BusesProperties()
                                            .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Params", createLayout())
{
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
            apvts.addParameterListener(wp->paramID, this);
    activeEnd.fill(-1);
    for (auto& m : muted) m.store(false);
    auto& rnd = juce::Random::getSystemRandom();
    seeds = { (uint32_t) rnd.nextInt(), (uint32_t) rnd.nextInt(), (uint32_t) rnd.nextInt(), (uint32_t) rnd.nextInt(),
              (uint32_t) rnd.nextInt() };
    runGenerate(true);
    startTimerHz(15);
}

MelodyLiftProcessor::~MelodyLiftProcessor()
{
    alive->store(false);
    ++generation;
    stopTimer();
    cancelPendingUpdate();
    pool.removeAllJobs(true, 10000);
}

bool MelodyLiftProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (kInstrumentBuild) return layouts.getMainInputChannelSet().isDisabled();
    return layouts.getMainInputChannelSet() == out;
}

void MelodyLiftProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    resampler.prepare(sampleRate);
    expectedNext = -1;
    synth.prepare(sampleRate);
    synthBuf.setSize(2, std::max(512, samplesPerBlock));
    genExpected = -1;
    numPendingOffs = 0;
}

float MelodyLiftProcessor::param(const juce::String& id) const
{
    return apvts.getRawParameterValue(id)->load();
}

// ------------------------------------------------------------ audio thread

void MelodyLiftProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch) buffer.clear(ch, 0, n);

    bool playing = false;
    bool hasTime = false, hasPpq = false;
    int64_t timeSamples = 0;
    double ppq = 0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples()) { timeSamples = *t; hasTime = true; }
            if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
            if (auto b = pos->getBpm()) hostBpm.store(*b);
            if (auto ts = pos->getTimeSignature()) { timeSigNum.store(ts->numerator); timeSigDen.store(ts->denominator); }
        }
    }
    hostPlaying.store(playing);
    if (hasTime) playheadSec.store((double) timeSamples / sr);

    // ---- capture: write the vocal at its song position
    if (armed.load() && !capture.empty() && numIn > 0)
    {
        const bool useTransport = hasTime && getPlayHead() != nullptr;
        transportCapture.store(useTransport);
        if (!useTransport || playing)
        {
            const int64_t start = useTransport ? timeSamples : seqPos;
            int64_t mw = maxWritten.load();
            const float scale = 1.0f / (float) numIn;
            for (int i = 0; i < n; ++i)
            {
                float m = 0;
                for (int ch = 0; ch < numIn; ++ch) m += buffer.getSample(ch, i);
                resampler.push(start + i, m * scale, capture.data(), capture.size(), mw);
            }
            maxWritten.store(mw);
            lastWriteMs.store(juce::Time::getMillisecondCounter());
            wroteSinceArm.store(true);
            if (!useTransport) seqPos += n;
        }
    }

    // ---- MIDI out: play the detected notes in time with the song
    const bool wantMidi = (int) param(ids::midiOut) == (int) MidiOutSource::Vocal;
    if (wantMidi && playing && hasTime)
    {
        const juce::SpinLock::ScopedTryLockType lock(rtLock);
        if (lock.isLocked())
        {
            const int64_t bs = timeSamples, be = timeSamples + n;
            if (bs != expectedNext) allNotesOff(midi, 0);
            for (int p = 0; p < 128; ++p)
            {
                if (activeEnd[p] >= 0 && activeEnd[p] < be)
                {
                    midi.addEvent(juce::MidiMessage::noteOff(1, p), (int) juce::jlimit<int64_t>(0, n - 1, activeEnd[p] - bs));
                    activeEnd[p] = -1;
                }
            }
            for (const auto& rn : rtNotes)
            {
                const int64_t s = (int64_t) std::llround(rn.start * sr);
                if (s >= be) break;
                if (s < bs) continue;
                const int off = (int) (s - bs);
                if (activeEnd[rn.pitch] >= 0) midi.addEvent(juce::MidiMessage::noteOff(1, rn.pitch), off);
                midi.addEvent(juce::MidiMessage::noteOn(1, rn.pitch, (juce::uint8) rn.vel), off);
                const int64_t e = std::max(s + 1, (int64_t) std::llround(rn.end * sr));
                if (e < be)
                {
                    midi.addEvent(juce::MidiMessage::noteOff(1, rn.pitch), (int) std::min<int64_t>(n - 1, e - bs));
                    activeEnd[rn.pitch] = -1;
                }
                else
                    activeEnd[rn.pitch] = e;
            }
            expectedNext = be;
        }
    }
    else if (expectedNext >= 0)
    {
        allNotesOff(midi, 0);
        expectedNext = -1;
    }

    renderPattern(buffer, midi, playing, hasPpq, ppq);
}

// Plays the generated pattern: looped in sync with the host (instrument build,
// or "play with song"), or free-running for the preview button. Also feeds
// the MIDI output when it is set to the generated pattern.
void MelodyLiftProcessor::renderPattern(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool playing,
                                        bool hasPpq, double ppq)
{
    const int n = buffer.getNumSamples();
    const bool sync = playing && hasPpq && (kInstrumentBuild || param(ids::gPlay) > 0.5f);
    const bool preview = previewOn.load() && !playing;
    const bool midiActive = (int) param(ids::midiOut) == (int) MidiOutSource::Pattern && playing && hasPpq;
    const bool audible = sync || preview;

    auto flushOffs = [&](bool all) {
        for (int i = 0; i < numPendingOffs;)
        {
            auto& po = pendingOffs[(size_t) i];
            if (all || po.at < genSampleClock + n)
            {
                const int off = all ? 0 : (int) juce::jlimit<int64_t>(0, n - 1, po.at - genSampleClock);
                midi.addEvent(juce::MidiMessage::noteOff(po.channel, po.pitch), off);
                pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
            }
            else
                ++i;
        }
    };

    if (!audible && !midiActive)
    {
        if (genSounding.exchange(false))
        {
            synth.allOff();
            tailSamples = (int64_t) (sr * 0.4); // let the releases ring out
        }
        flushOffs(true);
        genExpected = -1;
        genBeatNow.store(-1.0);
        genSampleClock += n;
        if (tailSamples > 0)
        {
            tailSamples -= n;
            const float gain = param(ids::gVolume) / 100.0f;
            const int len = std::min(n, synthBuf.getNumSamples());
            synthBuf.clear(0, 0, len);
            synthBuf.clear(1, 0, len);
            synth.render(synthBuf.getWritePointer(0), synthBuf.getWritePointer(1), len, gain, nullptr);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.addFrom(ch, 0, synthBuf, std::min(ch, 1), 0, len);
        }
        return;
    }

    const double bpm = std::max(20.0, hostBpm.load());
    const double bps = bpm / 60.0 / sr; // beats per sample
    const double beat0 = (sync || midiActive) ? ppq : previewBeat;
    const double beat1 = beat0 + n * bps;
    if (genExpected >= 0 && std::abs(beat0 - genExpected) > 0.01)
    {
        synth.allOff();
        flushOffs(true);
    }

    bool mutedNow[ml::gen::NumLayers];
    for (int l = 0; l < ml::gen::NumLayers; ++l) mutedNow[l] = muted[(size_t) l].load();

    {
        const juce::SpinLock::ScopedTryLockType lock(genLock);
        if (lock.isLocked() && rtGenBeats > 0)
        {
            const double L = rtGenBeats;
            for (const auto& g : rtGen)
            {
                if (mutedNow[g.layer]) continue;
                const double k0 = std::ceil((beat0 - g.beat) / L - 1e-9);
                for (double t = g.beat + k0 * L; t < beat1; t += L)
                {
                    if (t < beat0 - 1e-9) continue;
                    const int off = juce::jlimit(0, n - 1, (int) ((t - beat0) / bps));
                    const auto lenSamples = (int64_t) std::max(1.0, g.len / bps);
                    if (audible) synth.noteOn(g.layer, g.pitch, g.vel, lenSamples, off);
                    if (midiActive)
                    {
                        const int ch = g.layer + 1;
                        midi.addEvent(juce::MidiMessage::noteOn(ch, g.pitch, (juce::uint8) juce::jlimit(1, 127, g.vel)), off);
                        if (numPendingOffs < (int) pendingOffs.size())
                            pendingOffs[(size_t) numPendingOffs++] = { genSampleClock + off + lenSamples, ch, g.pitch };
                        else
                            midi.addEvent(juce::MidiMessage::noteOff(ch, g.pitch), n - 1);
                    }
                }
            }
            genBeatNow.store(std::fmod(std::max(0.0, beat0), L));
            if (preview && previewBeat + n * bps >= L)
            {
                previewBeat -= L;
                genExpected = beat1 - L;
            }
            else
                genExpected = beat1;
        }
    }
    if (preview) previewBeat = genExpected;
    flushOffs(false);

    if (audible)
    {
        genSounding.store(true);
        const float gain = param(ids::gVolume) / 100.0f;
        const int chunk = synthBuf.getNumSamples();
        for (int start = 0; start < n; start += chunk)
        {
            const int len = std::min(chunk, n - start);
            synthBuf.clear(0, 0, len);
            synthBuf.clear(1, 0, len);
            synth.render(synthBuf.getWritePointer(0), synthBuf.getWritePointer(1), len, gain, mutedNow);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom(ch, start, synthBuf, std::min(ch, 1), 0, len);
        }
    }
    genSampleClock += n;
}

void MelodyLiftProcessor::allNotesOff(juce::MidiBuffer& midi, int offset)
{
    for (int p = 0; p < 128; ++p)
    {
        if (activeEnd[p] >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, p), offset);
            activeEnd[p] = -1;
        }
    }
}

// ---------------------------------------------------------------- status

MelodyLiftProcessor::Status MelodyLiftProcessor::getStatus() const
{
    if (analysing.load()) return Status::Analysing;
    if (armed.load())
    {
        const bool recent = juce::Time::getMillisecondCounter() - lastWriteMs.load() < 300;
        return wroteSinceArm.load() && recent ? Status::Capturing : Status::Armed;
    }
    return Status::Idle;
}

juce::String MelodyLiftProcessor::getStatusText() const
{
    switch (getStatus())
    {
        case Status::Analysing: return analysisLabel;
        case Status::Capturing: return "Capturing the vocal... stop playback when the part is done";
        case Status::Armed: return "Armed: press play in your DAW to capture the vocal";
        case Status::Idle: break;
    }
    if (notes.empty())
        return hasAnalysis() ? "No notes found. Try lowering Pitch clarity or Silence threshold."
                             : "Capture the vocal from your DAW, or load / drop an audio file";
    return sourceName.isNotEmpty() ? sourceName : juce::String("Captured vocal");
}

juce::String MelodyLiftProcessor::takeNotice()
{
    auto n = notice;
    notice.clear();
    return n;
}

void MelodyLiftProcessor::timerCallback()
{
    if (std::abs(hostBpm.load() - genBpm) > 0.01) rescaleForTempo(hostBpm.load());
    // capture ends when the transport stops
    if (armed.load() && wroteSinceArm.load() && transportCapture.load() && !hostPlaying.load()
        && juce::Time::getMillisecondCounter() - lastWriteMs.load() > 400)
    {
        toggleCapture();
    }
    const int st = (int) getStatus();
    if (st != lastStatus || st == (int) Status::Analysing)
    {
        lastStatus = st;
        sendChangeMessage();
    }
}

// --------------------------------------------------------------- capture

void MelodyLiftProcessor::ensureCaptureBuffer()
{
    if (capture.empty()) capture.assign((size_t) (kMaxCaptureSeconds * ml::kAnalysisRate), 0.0f);
}

void MelodyLiftProcessor::toggleCapture()
{
    if (armed.load())
    {
        armed.store(false);
        const int64_t mw = maxWritten.load();
        if (wroteSinceArm.load() && mw > 0)
        {
            std::vector<float> a(capture.begin(), capture.begin() + (size_t) mw + 1);
            sourceName = "Captured vocal";
            startAnalysis(std::move(a), "Analysing the captured vocal...");
        }
        sendChangeMessage();
        return;
    }
    if (analysing.load()) return;
    ensureCaptureBuffer();
    wroteSinceArm.store(false);
    const int64_t mw = maxWritten.load();
    seqPos = mw > 0 ? (int64_t) ((double) (mw + 1) * sr / ml::kAnalysisRate) : 0;
    armed.store(true);
    sendChangeMessage();
}

void MelodyLiftProcessor::clearAll()
{
    armed.store(false);
    ++generation;
    analysing.store(false);
    if (!capture.empty()) std::fill(capture.begin(), capture.end(), 0.0f);
    maxWritten.store(-1);
    wroteSinceArm.store(false);
    audio16k.clear();
    if (!notes.empty()) pushUndo();
    track = {};
    tr = {};
    base.clear();
    notes.clear();
    edited = false;
    sourceName.clear();
    publishNotes();
    sendChangeMessage();
}

// -------------------------------------------------------------- analysis

void MelodyLiftProcessor::startAnalysis(std::vector<float> audio, const juce::String& label)
{
    const int gen = ++generation;
    const int ri = juce::jlimit(0, 2, (int) param(ids::range));
    const double fmin = kRangeHz[ri][0], fmax = kRangeHz[ri][1];
    analysing.store(true);
    progress.store(0);
    analysisLabel = label;
    sendChangeMessage();

    auto aliveFlag = alive;
    pool.addJob([this, aliveFlag, gen, fmin, fmax, audio = std::move(audio)]() mutable {
        std::vector<float> filtered(audio);
        ml::highpass(filtered.data(), filtered.size(), ml::kAnalysisRate, 60);
        auto t = ml::trackPitch(filtered.data(), filtered.size(), ml::kAnalysisRate, fmin, fmax, [&](float f) {
            progress.store(f);
            return aliveFlag->load() && generation.load() == gen;
        });
        if (t.empty()) return;
        juce::MessageManager::callAsync([this, aliveFlag, gen, t = std::move(t), a = std::move(audio)]() mutable {
            if (!aliveFlag->load() || generation.load() != gen) return;
            onAnalysisDone(std::move(t), std::move(a));
        });
    });
}

void MelodyLiftProcessor::loadAudioFile(const juce::File& file)
{
    armed.store(false);
    const int gen = ++generation;
    analysing.store(true);
    progress.store(0);
    analysisLabel = "Reading " + file.getFileName() + "...";
    sendChangeMessage();

    const int ri = juce::jlimit(0, 2, (int) param(ids::range));
    const double fmin = kRangeHz[ri][0], fmax = kRangeHz[ri][1];
    auto aliveFlag = alive;
    pool.addJob([this, aliveFlag, gen, fmin, fmax, file]() {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0)
        {
            juce::MessageManager::callAsync([this, aliveFlag, gen, file] {
                if (!aliveFlag->load() || generation.load() != gen) return;
                analysing.store(false);
                notice = "Could not read " + file.getFileName() + ". Use WAV, AIFF, FLAC, OGG or MP3.";
                sendChangeMessage();
            });
            return;
        }
        const auto len = (int64_t) std::min<double>((double) reader->lengthInSamples, kMaxCaptureSeconds * reader->sampleRate);
        const int chans = (int) juce::jlimit(1u, 2u, reader->numChannels);
        std::vector<float> mono((size_t) len, 0.0f);
        juce::AudioBuffer<float> chunk(chans, 65536);
        for (int64_t pos = 0; pos < len; pos += 65536)
        {
            if (!aliveFlag->load() || generation.load() != gen) return;
            const int num = (int) std::min<int64_t>(65536, len - pos);
            reader->read(&chunk, 0, num, pos, true, chans > 1);
            for (int c = 0; c < chans; ++c)
            {
                const float* d = chunk.getReadPointer(c);
                for (int i = 0; i < num; ++i) mono[(size_t) (pos + i)] += d[i] / (float) chans;
            }
            progress.store(0.3f * (float) pos / (float) len);
        }
        auto audio = ml::resampleToAnalysis(mono.data(), mono.size(), reader->sampleRate);
        std::vector<float> filtered(audio);
        ml::highpass(filtered.data(), filtered.size(), ml::kAnalysisRate, 60);
        auto t = ml::trackPitch(filtered.data(), filtered.size(), ml::kAnalysisRate, fmin, fmax, [&](float f) {
            progress.store(0.3f + 0.7f * f);
            return aliveFlag->load() && generation.load() == gen;
        });
        if (t.empty()) return;
        juce::MessageManager::callAsync([this, aliveFlag, gen, file, t = std::move(t), a = std::move(audio)]() mutable {
            if (!aliveFlag->load() || generation.load() != gen) return;
            // a loaded file replaces any captured audio; it starts at 0:00
            if (!capture.empty()) std::fill(capture.begin(), capture.end(), 0.0f);
            maxWritten.store(-1);
            sourceName = file.getFileName();
            onAnalysisDone(std::move(t), std::move(a));
        });
    });
}

void MelodyLiftProcessor::onAnalysisDone(ml::PitchTrack t, std::vector<float> a)
{
    track = std::move(t);
    audio16k = std::move(a);
    analysing.store(false);
    progress.store(1);
    ++analysisCount;
    runTranscription(true);
}

ml::TranscribeParams MelodyLiftProcessor::transcribeParams() const
{
    ml::TranscribeParams p;
    p.confMin = param(ids::clarity);
    p.gateDb = param(ids::gate);
    p.changeCost = param(ids::stability);
    p.minNoteMs = param(ids::minNote);
    p.splitDb = param(ids::split);
    p.autoTuning = param(ids::autoTune) > 0.5f;
    p.tuningCents = param(ids::tuning);
    return p;
}

void MelodyLiftProcessor::runTranscription(bool replaceNotes)
{
    if (track.empty()) return;
    tr = ml::transcribe(track, transcribeParams());
    base = tr.notes;
    if (replaceNotes) runPostProcess();
}

MelodyLiftProcessor::KeyChoice MelodyLiftProcessor::currentKey() const
{
    KeyChoice k;
    const int t = (int) param(ids::keyTonic);
    if (t == 0)
    {
        k.isAuto = true;
        if (tr.key.tonic >= 0) { k.tonic = tr.key.tonic; k.scale = tr.key.major ? ml::Major : ml::Minor; }
        return k;
    }
    k.isAuto = false;
    k.tonic = t - 1;
    k.scale = (int) param(ids::scale);
    return k;
}

void MelodyLiftProcessor::runPostProcess()
{
    if (track.empty()) return;
    const auto key = currentKey();
    ml::PostParams pp;
    pp.snap = param(ids::snap) > 0.5f;
    pp.tonic = key.tonic;
    pp.scale = key.scale;
    pp.transpose = (int) param(ids::transpose);
    pp.bpm = hostBpm.load();
    pp.grid = (int) param(ids::grid);
    pp.quantize = param(ids::quantize) > 0.5f;
    pp.strength = param(ids::strength) / 100.0;

    const uint32_t now = juce::Time::getMillisecondCounter();
    if (!notes.empty() && now - lastUndoPush > 700) pushUndo();
    lastUndoPush = now;
    if (edited) notice = "Settings changed, so your manual edits were replaced. Undo brings them back.";
    edited = false;
    notes = ml::postProcess(base, pp);
    publishNotes();
    sendChangeMessage();
}

void MelodyLiftProcessor::publishNotes()
{
    std::vector<RtNote> rt;
    rt.reserve(notes.size());
    for (const auto& n : notes)
        rt.push_back({ n.start, n.end, juce::jlimit(0, 127, n.pitch), juce::jlimit(1, 127, n.vel) });
    std::sort(rt.begin(), rt.end(), [](const RtNote& a, const RtNote& b) { return a.start < b.start; });
    const juce::SpinLock::ScopedLockType lock(rtLock);
    rtNotes.swap(rt);
}

// ------------------------------------------------------------- parameters

void MelodyLiftProcessor::parameterChanged(const juce::String& id, float)
{
    if (restoring) return;
    static const juce::StringArray genIds { ids::gStyle, ids::gKey, ids::gScale, ids::gBars, ids::gDensity, ids::gSyncop,
                                            ids::gRepeat, ids::gComplex, ids::gOrnament, ids::gRange, ids::gOctave,
                                            ids::gHumanize, ids::gSwing };
    if (genIds.contains(id))
    {
        if (!suppressGen)
        {
            pendingGen.store(true);
            triggerAsyncUpdate();
        }
        return;
    }
    int level = None;
    if (id == ids::range) level = Pitch;
    else if (id == ids::clarity || id == ids::gate || id == ids::stability || id == ids::minNote
             || id == ids::split || id == ids::autoTune || id == ids::tuning)
        level = Notes;
    else if (id == ids::keyTonic || id == ids::scale || id == ids::snap || id == ids::transpose || id == ids::quantize)
        level = Post;
    else if (id == ids::grid || id == ids::strength)
        level = param(ids::quantize) > 0.5f ? Post : None;

    int cur = pendingLevel.load();
    while (level > cur && !pendingLevel.compare_exchange_weak(cur, level)) {}
    triggerAsyncUpdate();
}

void MelodyLiftProcessor::handleAsyncUpdate()
{
    if (pendingGen.exchange(false)) runGenerate(true);
    const int level = pendingLevel.exchange(None);
    if (level == Pitch)
    {
        if (!audio16k.empty()) startAnalysis(audio16k, "Re-analysing with the new voice range...");
        else if (hasAnalysis()) notice = "The voice range applies to the next capture or file.";
    }
    else if (level == Notes)
        runTranscription(true);
    else if (level == Post)
        runPostProcess();
    sendChangeMessage();
}

// ---------------------------------------------------------------- editing

void MelodyLiftProcessor::pushUndo()
{
    undoStack.push_back(notes);
    if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void MelodyLiftProcessor::beginEdit() { pushUndo(); }

void MelodyLiftProcessor::endEdit()
{
    edited = true;
    std::stable_sort(notes.begin(), notes.end(), [](const ml::Note& a, const ml::Note& b) { return a.start < b.start; });
    publishNotes();
    sendChangeMessage();
}

void MelodyLiftProcessor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back(notes);
    notes = undoStack.back();
    undoStack.pop_back();
    edited = true;
    publishNotes();
    sendChangeMessage();
}

void MelodyLiftProcessor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back(notes);
    notes = redoStack.back();
    redoStack.pop_back();
    edited = true;
    publishNotes();
    sendChangeMessage();
}

// ----------------------------------------------------------------- export

juce::String MelodyLiftProcessor::exportBaseName() const
{
    auto b = sourceName.isNotEmpty() ? juce::File::createLegalFileName(sourceName.upToLastOccurrenceOf(".", false, false))
                                     : juce::String("MelodyLift");
    if (b.isEmpty()) b = "MelodyLift";
    return b + " melody";
}

std::vector<uint8_t> MelodyLiftProcessor::buildMidi() const
{
    auto sorted = notes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ml::Note& a, const ml::Note& b) { return a.start < b.start; });
    const int range = (int) param(ids::bendRange) == 1 ? 12 : 2;
    std::vector<ml::BendPoint> bends;
    const bool withBends = param(ids::bends) > 0.5f && !track.empty();
    if (withBends) bends = ml::buildBendEvents(sorted, tr.prep, track, range);
    return ml::writeMidi(sorted, hostBpm.load(), 480, exportBaseName().toStdString(), withBends ? &bends : nullptr,
                         range, timeSigNum.load(), timeSigDen.load());
}

bool MelodyLiftProcessor::writeMidiFile(const juce::File& f) const
{
    const auto bytes = buildMidi();
    return f.replaceWithData(bytes.data(), bytes.size());
}

juce::File MelodyLiftProcessor::writeMidiToTempFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MelodyLift");
    dir.createDirectory();
    auto f = dir.getChildFile(exportBaseName() + ".mid");
    return writeMidiFile(f) ? f : juce::File();
}

// ------------------------------------------------------------------ state

namespace
{
juce::var floatsToVar(const std::vector<float>& v)
{
    return juce::var(juce::MemoryBlock(v.data(), v.size() * sizeof(float)));
}
std::vector<float> varToFloats(const juce::var& v)
{
    std::vector<float> out;
    if (auto* mb = v.getBinaryData())
    {
        out.resize(mb->getSize() / sizeof(float));
        std::memcpy(out.data(), mb->getData(), out.size() * sizeof(float));
    }
    return out;
}
} // namespace

void MelodyLiftProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root("MelodyLift");
    root.setProperty("version", 1, nullptr);
    root.setProperty("edited", edited, nullptr);
    root.setProperty("source", sourceName, nullptr);
    root.appendChild(apvts.copyState(), nullptr);

    juce::ValueTree nt("Notes");
    for (const auto& n : notes)
    {
        juce::ValueTree c("N");
        c.setProperty("p", n.pitch, nullptr);
        c.setProperty("dp", n.detPitch, nullptr);
        c.setProperty("raw", n.pitchRaw, nullptr);
        c.setProperty("s", n.start, nullptr);
        c.setProperty("e", n.end, nullptr);
        c.setProperty("db", n.db, nullptr);
        c.setProperty("v", n.vel, nullptr);
        c.setProperty("ss", n.srcStart, nullptr);
        c.setProperty("a", n.srcA, nullptr);
        c.setProperty("b", n.srcB, nullptr);
        nt.appendChild(c, nullptr);
    }
    root.appendChild(nt, nullptr);

    if (!track.empty())
    {
        juce::ValueTree tk("Track");
        tk.setProperty("hop", track.hop, nullptr);
        tk.setProperty("sr", track.sr, nullptr);
        tk.setProperty("fmin", track.fmin, nullptr);
        tk.setProperty("fmax", track.fmax, nullptr);
        tk.setProperty("f0", floatsToVar(track.f0), nullptr);
        tk.setProperty("conf", floatsToVar(track.conf), nullptr);
        tk.setProperty("rms", floatsToVar(track.rmsDb), nullptr);
        tk.setProperty("env", floatsToVar(track.envDb), nullptr);
        root.appendChild(tk, nullptr);
    }
    {
        juce::ValueTree g("Generator");
        g.setProperty("seedChords", (juce::int64) seeds.chords, nullptr);
        g.setProperty("seedRhythm", (juce::int64) seeds.rhythm, nullptr);
        g.setProperty("seedPitch", (juce::int64) seeds.pitch, nullptr);
        g.setProperty("seedBass", (juce::int64) seeds.bass, nullptr);
        g.setProperty("seedCounter", (juce::int64) seeds.counter, nullptr);
        int lockMask = 0, muteMask = 0;
        for (int l = 0; l < ml::gen::NumLayers; ++l)
        {
            if (locked[(size_t) l]) lockMask |= 1 << l;
            if (muted[(size_t) l].load()) muteMask |= 1 << l;
        }
        g.setProperty("locked", lockMask, nullptr);
        g.setProperty("muted", muteMask, nullptr);
        g.setProperty("active", activeLayer, nullptr);
        g.setProperty("fromVocal", fromVocal, nullptr);
        g.setProperty("edited", genEdited, nullptr);
        g.setProperty("bpm", genBpm, nullptr);
        g.setProperty("tab", uiTab, nullptr);
        for (int l = 0; l < ml::gen::NumLayers; ++l)
        {
            juce::ValueTree lt("L");
            for (const auto& n : genNotes[(size_t) l])
            {
                juce::ValueTree c("N");
                c.setProperty("p", n.pitch, nullptr);
                c.setProperty("s", n.start, nullptr);
                c.setProperty("e", n.end, nullptr);
                c.setProperty("v", n.vel, nullptr);
                lt.appendChild(c, nullptr);
            }
            g.appendChild(lt, nullptr);
        }
        root.appendChild(g, nullptr);
    }
    juce::MemoryOutputStream mos(destData, false);
    root.writeToStream(mos);
}

void MelodyLiftProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData(data, (size_t) sizeInBytes);
    if (!root.isValid() || !root.hasType("MelodyLift")) return;

    restoring = true;
    auto params = root.getChildWithName(apvts.state.getType());
    if (params.isValid()) apvts.replaceState(params);

    track = {};
    auto tk = root.getChildWithName("Track");
    if (tk.isValid())
    {
        track.hop = (int) tk.getProperty("hop", 160);
        track.sr = (double) tk.getProperty("sr", ml::kAnalysisRate);
        track.fmin = (double) tk.getProperty("fmin", 65.0);
        track.fmax = (double) tk.getProperty("fmax", 1100.0);
        track.f0 = varToFloats(tk.getProperty("f0"));
        track.conf = varToFloats(tk.getProperty("conf"));
        track.rmsDb = varToFloats(tk.getProperty("rms"));
        track.envDb = varToFloats(tk.getProperty("env"));
        const size_t n = track.f0.size();
        if (track.conf.size() != n || track.rmsDb.size() != n) track = {};
        if (track.envDb.size() != n) track.envDb.clear();
    }
    runTranscription(false);

    notes.clear();
    for (const auto& c : root.getChildWithName("Notes"))
    {
        ml::Note n;
        n.id = ml::newNoteId();
        n.pitch = c.getProperty("p", 60);
        n.detPitch = c.getProperty("dp", n.pitch);
        n.pitchRaw = c.getProperty("raw", n.pitch);
        n.start = c.getProperty("s", 0.0);
        n.end = c.getProperty("e", 0.0);
        n.db = c.getProperty("db", 0.0);
        n.vel = c.getProperty("v", 96);
        n.srcStart = c.getProperty("ss", n.start);
        n.srcA = c.getProperty("a", -1);
        n.srcB = c.getProperty("b", -1);
        notes.push_back(n);
    }
    edited = (bool) root.getProperty("edited", false);
    sourceName = root.getProperty("source", "").toString();
    undoStack.clear();
    redoStack.clear();

    auto g = root.getChildWithName("Generator");
    if (g.isValid())
    {
        seeds.chords = (uint32_t) (juce::int64) g.getProperty("seedChords", 1);
        seeds.rhythm = (uint32_t) (juce::int64) g.getProperty("seedRhythm", 2);
        seeds.pitch = (uint32_t) (juce::int64) g.getProperty("seedPitch", 3);
        seeds.bass = (uint32_t) (juce::int64) g.getProperty("seedBass", 4);
        seeds.counter = (uint32_t) (juce::int64) g.getProperty("seedCounter", 5);
        const int lockMask = g.getProperty("locked", 0), muteMask = g.getProperty("muted", 0);
        for (int l = 0; l < ml::gen::NumLayers; ++l)
        {
            locked[(size_t) l] = (lockMask >> l) & 1;
            muted[(size_t) l].store((muteMask >> l) & 1);
        }
        activeLayer = juce::jlimit(0, ml::gen::NumLayers - 1, (int) g.getProperty("active", 0));
        fromVocal = (bool) g.getProperty("fromVocal", false);
        uiTab = (int) g.getProperty("tab", 0);
        runGenerate(false); // rebuilds the chords for display without touching notes
        genBpm = (double) g.getProperty("bpm", hostBpm.load());
        bool any = false;
        for (int l = 0; l < ml::gen::NumLayers && l < g.getNumChildren(); ++l)
        {
            auto& out = genNotes[(size_t) l];
            out.clear();
            for (const auto& c : g.getChild(l))
            {
                ml::Note n;
                n.id = ml::newNoteId();
                n.pitch = n.detPitch = c.getProperty("p", 60);
                n.pitchRaw = n.pitch;
                n.start = n.srcStart = c.getProperty("s", 0.0);
                n.end = c.getProperty("e", 0.0);
                n.vel = c.getProperty("v", 96);
                out.push_back(n);
                any = true;
            }
        }
        if (!any) { genBpm = hostBpm.load(); patternToNotes(); }
        genEdited = (bool) g.getProperty("edited", false);
        genUndoStack.clear();
        genRedoStack.clear();
        publishPattern();
    }
    cancelPendingUpdate();
    pendingLevel.store(None);
    pendingGen.store(false);
    restoring = false;
    ++analysisCount;
    publishNotes();
    sendChangeMessage();
}


// -------------------------------------------------------------- generator

ml::gen::Settings MelodyLiftProcessor::genSettings() const
{
    ml::gen::Settings g;
    g.style = (int) param(ids::gStyle);
    g.tonic = (int) param(ids::gKey);
    const int sc = (int) param(ids::gScale);
    g.scale = sc == 0 ? -1 : sc - 1;
    static const int bars[] = { 4, 8, 16 };
    g.bars = bars[juce::jlimit(0, 2, (int) param(ids::gBars))];
    g.density = param(ids::gDensity) / 100.0;
    g.syncopation = param(ids::gSyncop) / 100.0;
    g.repetition = param(ids::gRepeat) / 100.0;
    g.complexity = param(ids::gComplex) / 100.0;
    g.ornaments = param(ids::gOrnament) / 100.0;
    g.range = (int) param(ids::gRange);
    g.octave = (int) param(ids::gOctave) - 1;
    g.humanize = param(ids::gHumanize) / 100.0;
    g.swing = param(ids::gSwing) / 100.0;
    return g;
}

void MelodyLiftProcessor::runGenerate(bool replaceNotes)
{
    const auto s = genSettings();
    const double bpm = std::max(20.0, hostBpm.load());
    if (fromVocal && !notes.empty())
    {
        std::vector<ml::gen::GNote> mel;
        double end = 0;
        for (const auto& n : notes)
        {
            mel.push_back({ n.start * bpm / 60.0, (n.end - n.start) * bpm / 60.0, n.pitch, n.vel });
            end = std::max(end, n.end * bpm / 60.0);
        }
        std::sort(mel.begin(), mel.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
        pattern = ml::gen::buildAround(mel, end, s, seeds);
    }
    else
    {
        fromVocal = false;
        pattern = ml::gen::generate(s, seeds);
    }
    if (replaceNotes)
    {
        const uint32_t now = juce::Time::getMillisecondCounter();
        if (!genNotes[0].empty() && now - lastGenUndoPush > 700)
        {
            genUndoStack.push_back(genNotes);
            if (genUndoStack.size() > 100) genUndoStack.erase(genUndoStack.begin());
            genRedoStack.clear();
        }
        lastGenUndoPush = now;
        if (genEdited) notice = "Settings changed, so your edits to the pattern were replaced. Undo brings them back.";
        genEdited = false;
        genBpm = bpm;
        patternToNotes();
    }
    publishPattern();
    sendChangeMessage();
}

void MelodyLiftProcessor::patternToNotes()
{
    const double spb = 60.0 / genBpm;
    for (int l = 0; l < ml::gen::NumLayers; ++l)
    {
        auto& out = genNotes[(size_t) l];
        out.clear();
        for (const auto& g : pattern.layers[l])
        {
            ml::Note n;
            n.id = ml::newNoteId();
            n.pitch = n.detPitch = g.pitch;
            n.pitchRaw = g.pitch;
            n.start = n.srcStart = g.beat * spb;
            n.end = (g.beat + g.len) * spb;
            n.vel = g.vel;
            out.push_back(n);
        }
    }
}

void MelodyLiftProcessor::publishPattern()
{
    std::vector<RtGen> rt;
    const double bps = genBpm / 60.0;
    for (int l = 0; l < ml::gen::NumLayers; ++l)
        for (const auto& n : genNotes[(size_t) l])
            rt.push_back({ n.start * bps, (n.end - n.start) * bps, juce::jlimit(0, 127, n.pitch), juce::jlimit(1, 127, n.vel), l });
    std::sort(rt.begin(), rt.end(), [](const RtGen& a, const RtGen& b) { return a.beat < b.beat; });
    const juce::SpinLock::ScopedLockType lock(genLock);
    rtGen.swap(rt);
    rtGenBeats = std::max(1.0, pattern.beats);
}

void MelodyLiftProcessor::rescaleForTempo(double newBpm)
{
    if (newBpm <= 0) return;
    const double f = genBpm / newBpm;
    auto scale = [f](std::array<std::vector<ml::Note>, ml::gen::NumLayers>& layers) {
        for (auto& layer : layers)
            for (auto& n : layer) { n.start *= f; n.end *= f; n.srcStart *= f; }
    };
    scale(genNotes);
    for (auto& u : genUndoStack) scale(u);
    for (auto& r : genRedoStack) scale(r);
    genBpm = newBpm;
    publishPattern();
    sendChangeMessage();
}

void MelodyLiftProcessor::generate(GenAction action)
{
    auto rnd = [] { return (uint32_t) juce::Random::getSystemRandom().nextInt(); };
    switch (action)
    {
        case GenAction::All:
            if (!locked[ml::gen::Chords]) seeds.chords = rnd();
            if (!locked[ml::gen::Melody]) { seeds.rhythm = rnd(); seeds.pitch = rnd(); fromVocal = false; }
            if (!locked[ml::gen::Bass]) seeds.bass = rnd();
            if (!locked[ml::gen::Counter]) seeds.counter = rnd();
            break;
        case GenAction::Hook: seeds.rhythm = rnd(); seeds.pitch = rnd(); fromVocal = false; break;
        case GenAction::Variation: seeds.pitch = rnd(); fromVocal = false; break;
        case GenAction::Chords: seeds.chords = rnd(); break;
        case GenAction::Bass: seeds.bass = rnd(); break;
        case GenAction::Counter: seeds.counter = rnd(); break;
    }
    runGenerate(true);
}

void MelodyLiftProcessor::rerollLayer(int layer)
{
    static const GenAction map[] = { GenAction::Hook, GenAction::Chords, GenAction::Bass, GenAction::Counter };
    generate(map[juce::jlimit(0, 3, layer)]);
}

void MelodyLiftProcessor::useVocalKey()
{
    if (notes.empty())
    {
        notice = "Capture or load a vocal first, then its key can be used here.";
        sendChangeMessage();
        return;
    }
    const auto k = currentKey();
    auto set = [this](const juce::String& id, int index) {
        auto* prm = apvts.getParameter(id);
        prm->setValueNotifyingHost(prm->convertTo0to1((float) index));
    };
    set(ids::gKey, k.tonic);
    set(ids::gScale, k.scale + 1);
}

bool MelodyLiftProcessor::chordsForVocal()
{
    if (notes.empty())
    {
        notice = "Capture or load a vocal first.";
        sendChangeMessage();
        return false;
    }
    suppressGen = true;
    useVocalKey();
    suppressGen = false;
    fromVocal = true;
    runGenerate(true);
    activeLayer = ml::gen::Chords;
    return true;
}

void MelodyLiftProcessor::beginGenEdit()
{
    genUndoStack.push_back(genNotes);
    if (genUndoStack.size() > 100) genUndoStack.erase(genUndoStack.begin());
    genRedoStack.clear();
}

void MelodyLiftProcessor::endGenEdit()
{
    genEdited = true;
    for (auto& layer : genNotes)
        std::stable_sort(layer.begin(), layer.end(), [](const ml::Note& a, const ml::Note& b) { return a.start < b.start; });
    publishPattern();
    sendChangeMessage();
}

void MelodyLiftProcessor::genUndo()
{
    if (genUndoStack.empty()) return;
    genRedoStack.push_back(genNotes);
    genNotes = genUndoStack.back();
    genUndoStack.pop_back();
    genEdited = true;
    publishPattern();
    sendChangeMessage();
}

void MelodyLiftProcessor::genRedo()
{
    if (genRedoStack.empty()) return;
    genUndoStack.push_back(genNotes);
    genNotes = genRedoStack.back();
    genRedoStack.pop_back();
    genEdited = true;
    publishPattern();
    sendChangeMessage();
}

void MelodyLiftProcessor::setPreviewPlaying(bool on)
{
    if (on) previewBeat = 0;
    previewOn.store(on);
    sendChangeMessage();
}

double MelodyLiftProcessor::getPatternPlayheadSeconds() const
{
    const double b = genBeatNow.load();
    return b < 0 ? -1.0 : b * 60.0 / std::max(20.0, hostBpm.load());
}

std::vector<uint8_t> MelodyLiftProcessor::buildPatternMidi(int layer) const
{
    std::vector<ml::gen::MidiTrack> tracks;
    const double bps = genBpm / 60.0;
    for (int l = 0; l < ml::gen::NumLayers; ++l)
    {
        if (layer >= 0 && l != layer) continue;
        ml::gen::MidiTrack t;
        t.name = ml::gen::layerName(l);
        t.channel = l;
        for (const auto& n : genNotes[(size_t) l])
            t.notes.push_back({ n.start * bps, (n.end - n.start) * bps, n.pitch, n.vel });
        tracks.push_back(t);
    }
    return ml::gen::writeMidiTracks(tracks, genBpm);
}

juce::String MelodyLiftProcessor::patternSummary() const
{
    if (!hasPattern()) return {};
    static const char* scaleNames[] = { "major", "minor", "harmonic minor", "dorian", "mixolydian", "major pentatonic",
                                        "minor pentatonic", "phrygian", "phrygian dominant" };
    juce::String s;
    s << (fromVocal ? "Your vocal" : ml::gen::styleName(pattern.style)) << "  |  " << ml::pitchClassName(pattern.tonic) << " "
      << scaleNames[juce::jlimit(0, 8, pattern.scale)] << "  |  ";
    juce::String prev;
    int shown = 0;
    for (const auto& c : pattern.chords)
    {
        if (shown >= 8) { s << " ..."; break; }
        if (c.name == prev && shown > 0) continue;
        s << (shown ? " " : "") << c.name;
        prev = c.name;
        ++shown;
    }
    s << "  |  hook score " << juce::String(pattern.hookScore, 1) << "/10";
    return s;
}

juce::File MelodyLiftProcessor::writePatternMidiToTempFile(int layer) const
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MelodyLift");
    dir.createDirectory();
    juce::String name = juce::String("MelodyLift ") + (fromVocal ? "Vocal" : ml::gen::styleName(pattern.style)) + " "
                        + ml::pitchClassName(pattern.tonic) + " " + (layer < 0 ? juce::String("Full") : juce::String(ml::gen::layerName(layer)));
    auto f = dir.getChildFile(juce::File::createLegalFileName(name) + ".mid");
    return writePatternMidiFile(f, layer) ? f : juce::File();
}

bool MelodyLiftProcessor::writePatternMidiFile(const juce::File& f, int layer) const
{
    const auto bytes = buildPatternMidi(layer);
    return f.replaceWithData(bytes.data(), bytes.size());
}

juce::AudioProcessorEditor* MelodyLiftProcessor::createEditor()
{
    return new MelodyLiftEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodyLiftProcessor();
}
