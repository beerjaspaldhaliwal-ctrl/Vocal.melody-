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
        StringArray { "Major", "Minor", "Harmonic minor", "Dorian", "Mixolydian", "Major pentatonic", "Minor pentatonic" }, 0));
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
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { ids::midiOut, 1 }, "MIDI out on playback", false));
    return { p.begin(), p.end() };
}

MelodyLiftProcessor::MelodyLiftProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Params", createLayout())
{
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
            apvts.addParameterListener(wp->paramID, this);
    activeEnd.fill(-1);
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
    return layouts.getMainInputChannelSet() == out;
}

void MelodyLiftProcessor::prepareToPlay(double sampleRate, int)
{
    sr = sampleRate;
    resampler.prepare(sampleRate);
    expectedNext = -1;
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
    bool hasTime = false;
    int64_t timeSamples = 0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples()) { timeSamples = *t; hasTime = true; }
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
    const bool wantMidi = param(ids::midiOut) > 0.5f;
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
    cancelPendingUpdate();
    pendingLevel.store(None);
    restoring = false;
    ++analysisCount;
    publishNotes();
    sendChangeMessage();
}

juce::AudioProcessorEditor* MelodyLiftProcessor::createEditor()
{
    return new MelodyLiftEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodyLiftProcessor();
}
