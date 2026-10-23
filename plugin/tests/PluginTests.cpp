// End-to-end tests for the plugin processor and editor, driven the way a DAW
// drives them: capture during playback, MIDI out, export, state restore.
// Usage: MelodyLiftPluginTests [screenshot.png]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginProcessor.h"

#include <cstdio>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct SynthNote { int pitch; double start, dur; };

struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

int failures = 0;
void check(bool ok, const juce::String& what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (!ok) ++failures;
}

std::vector<float> synth(const std::vector<SynthNote>& mel, double total)
{
    std::vector<float> x((size_t) (total * kRate), 0.0f);
    for (const auto& n : mel)
    {
        const double f0 = ml::midiToHz(n.pitch);
        double phase = 0;
        const auto a = (size_t) (n.start * kRate), len = (size_t) (n.dur * kRate);
        for (size_t i = 0; i < len && a + i < x.size(); ++i)
        {
            const double t = i / kRate;
            const double f = f0 * std::pow(2.0, 0.3 * std::sin(2 * juce::MathConstants<double>::pi * 5.5 * t) * std::min(1.0, t / 0.25) / 12);
            phase += 2 * juce::MathConstants<double>::pi * f / kRate;
            const double env = std::min(1.0, t / 0.02) * std::min(1.0, (n.dur - t) / 0.03) * 0.5;
            double v = 0;
            for (int h = 1; h <= 12; ++h) v += std::sin(h * phase) / std::pow(h, 1.1);
            x[a + i] += (float) (env * v * 0.35);
        }
    }
    return x;
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

bool waitFor(const std::function<bool()>& cond, int timeoutMs)
{
    for (int t = 0; t < timeoutMs; t += 20)
    {
        if (cond()) return true;
        pump(20);
    }
    return cond();
}

/** Plays `audio` through the processor from song position 0; returns MIDI note-ons (sample, pitch). */
std::vector<std::pair<int64_t, int>> play(MelodyLiftProcessor& proc, FakePlayHead& ph, const std::vector<float>& audio)
{
    std::vector<std::pair<int64_t, int>> ons;
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    ph.info.setIsPlaying(true);
    for (int64_t pos = 0; pos < (int64_t) audio.size(); pos += kBlock)
    {
        const int n = (int) std::min<int64_t>(kBlock, (int64_t) audio.size() - pos);
        buf.setSize(2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) buf.setSample(ch, i, audio[(size_t) (pos + i)]);
        ph.info.setTimeInSamples(pos);
        ph.info.setTimeInSeconds(pos / kRate);
        midi.clear();
        proc.processBlock(buf, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn()) ons.push_back({ pos + m.samplePosition, m.getMessage().getNoteNumber() });
        if ((pos / kBlock) % 64 == 0) pump(1);
    }
    ph.info.setIsPlaying(false);
    buf.clear();
    for (int i = 0; i < 4; ++i) { midi.clear(); proc.processBlock(buf, midi); }
    return ons;
}

void setParam(MelodyLiftProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

bool notesMatch(const std::vector<ml::Note>& got, const std::vector<SynthNote>& want, juce::String& why)
{
    if (got.size() != want.size()) { why = "got " + juce::String((int) got.size()) + " notes"; return false; }
    for (size_t i = 0; i < want.size(); ++i)
    {
        if (got[i].pitch != want[i].pitch) { why = "note " + juce::String((int) i) + " pitch " + juce::String(got[i].pitch); return false; }
        if (std::abs(got[i].start - want[i].start) > 0.06) { why = "note " + juce::String((int) i) + " start " + juce::String(got[i].start, 3); return false; }
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    // A phrase that starts at bar 2 of a 120 bpm song (2.0 s), like a vocal stem would.
    std::vector<SynthNote> mel;
    const int pitches[] = { 64, 64, 65, 67, 67, 65, 64, 62, 60, 60, 62, 64 };
    for (int i = 0; i < 12; ++i) mel.push_back({ pitches[i], 2.0 + i * 0.5, 0.42 });
    const auto audio = synth(mel, 9.0);

    auto proc = std::make_unique<MelodyLiftProcessor>();
    FakePlayHead ph;
    ph.info.setBpm(120.0);
    ph.info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
    proc->setPlayHead(&ph);
    proc->setRateAndBufferSizeDetails(kRate, kBlock);
    proc->prepareToPlay(kRate, kBlock);

    // 1. capture during playback; analysis starts by itself when playback stops
    proc->toggleCapture();
    check(proc->isArmed(), "capture arms");
    play(*proc, ph, audio);
    const bool done = waitFor([&] { return !proc->isArmed() && proc->getStatus() == MelodyLiftProcessor::Status::Idle && !proc->getNotes().empty(); }, 20000);
    check(done, "capture stops with the transport and analysis finishes");
    juce::String why;
    check(notesMatch(proc->getNotes(), mel, why), "captured notes match the sung melody on the song timeline " + why);

    // 2. MIDI out plays the notes in sync on the next playback
    setParam(*proc, ids::midiOut, 1.0f);
    pump(50);
    const auto ons = play(*proc, ph, std::vector<float>(audio.size(), 0.0f));
    bool midiOk = ons.size() == mel.size();
    for (size_t i = 0; midiOk && i < mel.size(); ++i)
        midiOk = ons[i].second == mel[i].pitch && std::abs(ons[i].first / kRate - proc->getNotes()[i].start) < 0.002;
    check(midiOk, "MIDI out sends " + juce::String((int) ons.size()) + " note-ons in time");
    setParam(*proc, ids::midiOut, 0.0f);

    // 3. exported .mid lines up with bars (bar 2 = tick 1920 at 480 ppq)
    const auto file = proc->writeMidiToTempFile();
    juce::MidiFile mf;
    juce::FileInputStream in(file);
    const bool parsed = in.openedOk() && mf.readFrom(in);
    int noteOns = 0;
    double firstTick = -1;
    if (parsed)
        for (const auto* e : *mf.getTrack(0))
            if (e->message.isNoteOn()) { if (firstTick < 0) firstTick = e->message.getTimeStamp(); ++noteOns; }
    check(parsed && noteOns == (int) mel.size(), "MIDI file has " + juce::String(noteOns) + " notes (" + file.getFullPathName() + ")");
    check(std::abs(firstTick - 1920) <= 30, "first note lands on bar 2 (tick " + juce::String(firstTick) + ")");

    // 4. a settings change re-transcribes; quantize snaps to the 1/16 grid
    setParam(*proc, ids::quantize, 1.0f);
    pump(100);
    bool quantized = !proc->getNotes().empty();
    for (const auto& n : proc->getNotes())
        quantized = quantized && std::abs(n.start / 0.125 - std::round(n.start / 0.125)) < 1e-6;
    check(quantized, "quantize puts every note on the 1/16 grid");
    setParam(*proc, ids::transpose, 12.0f);
    pump(100);
    check(!proc->getNotes().empty() && proc->getNotes()[0].pitch == 76, "transpose +12");

    // 5. edits + undo
    proc->beginEdit();
    proc->editNotes().erase(proc->editNotes().begin());
    proc->endEdit();
    check(proc->getNotes().size() == mel.size() - 1, "edit removes a note");
    proc->undo();
    check(proc->getNotes().size() == mel.size(), "undo restores it");

    // 6. project save / reload keeps notes and analysis
    juce::MemoryBlock state;
    proc->getStateInformation(state);
    auto proc2 = std::make_unique<MelodyLiftProcessor>();
    proc2->setStateInformation(state.getData(), (int) state.getSize());
    pump(100);
    bool same = proc2->getNotes().size() == proc->getNotes().size() && proc2->hasAnalysis();
    for (size_t i = 0; same && i < proc->getNotes().size(); ++i)
        same = proc2->getNotes()[i].pitch == proc->getNotes()[i].pitch && std::abs(proc2->getNotes()[i].start - proc->getNotes()[i].start) < 1e-9;
    check(same, "state restore keeps " + juce::String((int) proc2->getNotes().size()) + " notes and the pitch analysis");
    check((int) proc2->apvts.getRawParameterValue(ids::transpose)->load() == 12, "state restore keeps parameters");

    // 7. loading an audio file (placed at 0:00)
    setParam(*proc, ids::transpose, 0.0f);
    setParam(*proc, ids::quantize, 0.0f);
    const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("melodylift-test.wav");
    {
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(wav), 44100, 1, 16, {}, 0));
        std::vector<SynthNote> m2 { { 57, 0.5, 0.4 }, { 60, 1.0, 0.4 }, { 64, 1.5, 0.4 }, { 69, 2.0, 0.6 } };
        const auto a48 = synth(m2, 3.0);
        std::vector<float> a44;
        for (size_t i = 0; i * kRate / 44100 < a48.size(); ++i) a44.push_back(a48[(size_t) (i * kRate / 44100)]);
        const float* chans[] = { a44.data() };
        w->writeFromFloatArrays(chans, 1, (int) a44.size());
    }
    proc->loadAudioFile(wav);
    const bool loaded = waitFor([&] { return proc->getStatus() == MelodyLiftProcessor::Status::Idle && proc->getSourceName() == wav.getFileName(); }, 20000);
    std::vector<SynthNote> want2 { { 57, 0.5, 0.4 }, { 60, 1.0, 0.4 }, { 64, 1.5, 0.4 }, { 69, 2.0, 0.6 } };
    check(loaded && notesMatch(proc->getNotes(), want2, why), "loading a WAV file transcribes it " + why);

    // 8. generator: a pattern is ready as soon as the plugin opens
    auto gen = std::make_unique<MelodyLiftProcessor>();
    FakePlayHead gph;
    gph.info.setBpm(120.0);
    gen->setPlayHead(&gph);
    gen->setRateAndBufferSizeDetails(kRate, kBlock);
    gen->prepareToPlay(kRate, kBlock);
    bool full = gen->hasPattern();
    for (int l = 0; l < ml::gen::NumLayers; ++l) full = full && !gen->genLayer(l).empty();
    check(full, "generator has chords, hook, bass and counter on open");

    // locked layers survive Generate
    const auto melodyBefore = gen->genLayer(ml::gen::Melody);
    const auto bassBefore = gen->genLayer(ml::gen::Bass);
    gen->setLayerLocked(ml::gen::Melody, true);
    gen->setLayerLocked(ml::gen::Chords, true);
    gen->generate(MelodyLiftProcessor::GenAction::All);
    bool sameHook = melodyBefore.size() == gen->genLayer(ml::gen::Melody).size();
    for (size_t i = 0; sameHook && i < melodyBefore.size(); ++i)
        sameHook = melodyBefore[i].pitch == gen->genLayer(ml::gen::Melody)[i].pitch;
    bool newBass = bassBefore.size() != gen->genLayer(ml::gen::Bass).size();
    for (size_t i = 0; !newBass && i < bassBefore.size(); ++i)
        newBass = bassBefore[i].pitch != gen->genLayer(ml::gen::Bass)[i].pitch || bassBefore[i].start != gen->genLayer(ml::gen::Bass)[i].start;
    check(sameHook, "locked hook is kept by Generate");
    check(newBass, "unlocked bass is rewritten by Generate");
    gen->setLayerLocked(ml::gen::Melody, false);
    gen->setLayerLocked(ml::gen::Chords, false);

    // switching genre re-renders the same seeds in the new style
    setParam(*gen, ids::gStyle, (float) ml::gen::PunjabiPop);
    pump(100);
    check(gen->patternSummary().contains("Punjabi"), "genre change: " + gen->patternSummary());

    // preview makes sound while the host is stopped
    auto rms = [&](int blocks) {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        double sum = 0;
        gph.info.setIsPlaying(false);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            gen->processBlock(buf, midi);
            sum += buf.getRMSLevel(0, 0, kBlock);
        }
        return sum / blocks;
    };
    check(rms(20) < 1e-6, "silent until preview starts");
    gen->setPreviewPlaying(true);
    const double loud = rms(400);
    check(loud > 0.01, "preview plays the pattern (rms " + juce::String(loud, 4) + ")");
    gen->setPreviewPlaying(false);
    rms(60);
    check(rms(20) < 1e-4, "preview stops");

    // "play with song" (always on in the instrument build) sounds when the host plays
    setParam(*gen, ids::gPlay, 1.0f);
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        double sum = 0;
        gph.info.setIsPlaying(true);
        for (int b = 0; b < 400; ++b)
        {
            gph.info.setTimeInSamples((int64_t) b * kBlock);
            gph.info.setPpqPosition(b * kBlock / kRate * 2.0);
            buf.clear();
            gen->processBlock(buf, midi);
            sum += buf.getRMSLevel(0, 0, kBlock);
        }
        gph.info.setIsPlaying(false);
        check(sum / 400 > 0.01, "pattern plays along with the song");
    }
    setParam(*gen, ids::gPlay, 0.0f);
    rms(60);

    // MIDI out plays every layer on its own channel in sync with the host
    setParam(*gen, ids::midiOut, 2.0f);
    pump(50);
    std::array<int, 17> perChannel {};
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        const double seconds = gen->patternSeconds();
        gph.info.setIsPlaying(true);
        for (int64_t pos = 0; pos < (int64_t) (seconds * kRate); pos += kBlock)
        {
            gph.info.setTimeInSamples(pos);
            gph.info.setPpqPosition(pos / kRate * 2.0);
            buf.clear();
            midi.clear();
            gen->processBlock(buf, midi);
            for (const auto m : midi)
                if (m.getMessage().isNoteOn()) ++perChannel[(size_t) m.getMessage().getChannel()];
        }
        gph.info.setIsPlaying(false);
        buf.clear();
        midi.clear();
        gen->processBlock(buf, midi);
    }
    bool channelsOk = true;
    for (int l = 0; l < ml::gen::NumLayers; ++l)
        channelsOk = channelsOk && perChannel[(size_t) l + 1] == (int) gen->genLayer(l).size();
    check(channelsOk, "MIDI out: melody " + juce::String(perChannel[1]) + ", chords " + juce::String(perChannel[2]) + ", bass "
                          + juce::String(perChannel[3]) + ", counter " + juce::String(perChannel[4]) + " note-ons");
    setParam(*gen, ids::midiOut, 0.0f);

    // tempo change keeps the pattern on the beat
    const double firstEnd = gen->genLayer(ml::gen::Bass).back().end;
    gph.info.setBpm(60.0);
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        gen->processBlock(buf, midi);
    }
    pump(200);
    check(std::abs(gen->genLayer(ml::gen::Bass).back().end - firstEnd * 2) < 1e-6, "half tempo doubles the note times");
    gph.info.setBpm(120.0);
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        gen->processBlock(buf, midi);
    }
    pump(200);

    // multi-track export
    {
        const auto f = gen->writePatternMidiToTempFile(-1);
        juce::MidiFile mf;
        juce::FileInputStream in(f);
        check(in.openedOk() && mf.readFrom(in) && mf.getNumTracks() == 5, "pattern exports as a 5-track MIDI file (" + f.getFileName() + ")");
    }

    // save / reload keeps the pattern, edits and locks
    gen->beginGenEdit();
    gen->editGenLayer(ml::gen::Melody).front().pitch += 1;
    gen->endGenEdit();
    gen->setLayerLocked(ml::gen::Bass, true);
    juce::MemoryBlock gstate;
    gen->getStateInformation(gstate);
    auto gen2 = std::make_unique<MelodyLiftProcessor>();
    gen2->setStateInformation(gstate.getData(), (int) gstate.getSize());
    bool sameState = gen2->patternSummary() == gen->patternSummary() && gen2->isLayerLocked(ml::gen::Bass);
    for (int l = 0; sameState && l < ml::gen::NumLayers; ++l)
    {
        sameState = gen2->genLayer(l).size() == gen->genLayer(l).size();
        for (size_t i = 0; sameState && i < gen->genLayer(l).size(); ++i)
            sameState = gen2->genLayer(l)[i].pitch == gen->genLayer(l)[i].pitch && std::abs(gen2->genLayer(l)[i].start - gen->genLayer(l)[i].start) < 1e-9;
    }
    check(sameState, "generator state survives save / reload, including edits");

    // chords for the captured vocal
    check(proc->chordsForVocal() && proc->isPatternFromVocal() && proc->getPattern().chords.size() >= 2
              && proc->genLayer(ml::gen::Melody).size() == proc->getNotes().size(),
          "chords for vocal: " + proc->patternSummary());

    // 9. editor opens on both tabs; save screenshots if asked
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(gen->createEditor());
        check(ed != nullptr && ed->getWidth() > 0, "editor opens");
        if (argc > 1 && ed != nullptr)
        {
            pump(200);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            const juce::File out { juce::String(argv[1]).replace(".png", "-gen.png") };
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            std::printf("     screenshot: %s\n", out.getFullPathName().toRawUTF8());
        }
    }
    {
        proc->setStateInformation(state.getData(), (int) state.getSize());
        proc->setUiTab(1);
        setParam(*proc, ids::transpose, 0.0f);
        setParam(*proc, ids::quantize, 0.0f);
        pump(200);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        if (argc > 1 && ed != nullptr)
        {
            pump(100);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            const juce::File out { juce::String(argv[1]) };
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            std::printf("     screenshot: %s\n", out.getFullPathName().toRawUTF8());
        }
    }
    gen2.reset();
    gen.reset();
    proc2.reset();
    proc.reset();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
