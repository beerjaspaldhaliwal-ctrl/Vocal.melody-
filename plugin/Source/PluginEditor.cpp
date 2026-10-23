#include "PluginEditor.h"

namespace
{
constexpr int kTopH = 52, kBottomH = 30, kPanelW = 280, kGap = 8;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;aif;aiff;flac;ogg;mp3");
}
} // namespace

// ------------------------------------------------------------ drag chip

void DragMidiChip::paint(juce::Graphics& g)
{
    const bool on = !proc.getNotes().empty();
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    juce::Path p;
    p.addRoundedRectangle(r, 5.0f);
    const float dash[] = { 4.0f, 3.0f };
    juce::Path dashed;
    juce::PathStrokeType(1.2f).createDashedStroke(dashed, p, dash, 2);
    g.setColour(on ? theme::accent : theme::muted.withAlpha(0.5f));
    g.fillPath(dashed);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(on ? "Drag MIDI" : "Drag MIDI", getLocalBounds(), juce::Justification::centred);
}

void DragMidiChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4 || proc.getNotes().empty()) return;
    const auto f = proc.writeMidiToTempFile();
    if (!f.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this,
                                                               [this] { dragging = false; });
}

// -------------------------------------------------------- controls panel

ControlsPanel::ControlsPanel(MelodyLiftProcessor& p) : proc(p)
{
    header("Detection");
    combo(ids::range, "Voice range");
    slider(ids::clarity, "Pitch clarity", "Raise if breaths or reverb become notes");
    slider(ids::gate, "Silence threshold");
    slider(ids::stability, "Note stability", "Higher keeps vibrato in one note");
    slider(ids::minNote, "Shortest note");
    slider(ids::split, "Syllable split", "Splits repeated notes. 0 = off");
    header("Pitch & key");
    toggle(ids::autoTune, "Detect tuning from the singer");
    slider(ids::tuning, "Reference tuning");
    combo(ids::keyTonic, "Key");
    combo(ids::scale, "Scale");
    toggle(ids::snap, "Snap notes to the scale");
    slider(ids::transpose, "Transpose");
    header("Timing");
    combo(ids::grid, "Grid");
    toggle(ids::quantize, "Quantize detected notes");
    slider(ids::strength, "Quantize strength");
    header("Output");
    toggle(ids::bends, "Pitch bends in MIDI (vibrato, slides)");
    combo(ids::bendRange, "Bend range");
    toggle(ids::midiOut, "Send notes to MIDI out on playback");
    refreshEnablement();
}

void ControlsPanel::header(const juce::String& title)
{
    Row r;
    r.title = title;
    r.header = true;
    rows.push_back(std::move(r));
}

void ControlsPanel::slider(const juce::String& id, const juce::String& label, const juce::String& help)
{
    Row r;
    r.label = std::make_unique<juce::Label>(juce::String(), label);
    r.label->setFont(juce::FontOptions(13.0f));
    auto s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
    sliderAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *s));
    if (auto* prm = proc.apvts.getParameter(id))
    {
        s->textFromValueFunction = [prm](double v) {
            return prm->getText(prm->convertTo0to1((float) v), 8) + (prm->getLabel().isNotEmpty() ? " " + prm->getLabel() : "");
        };
        s->updateText();
    }
    addAndMakeVisible(*r.label);
    addAndMakeVisible(*s);
    byId[id] = s.get();
    r.control = std::move(s);
    r.help = help;
    rows.push_back(std::move(r));
}

void ControlsPanel::combo(const juce::String& id, const juce::String& label)
{
    Row r;
    r.label = std::make_unique<juce::Label>(juce::String(), label);
    r.label->setFont(juce::FontOptions(13.0f));
    auto c = std::make_unique<juce::ComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(id)))
        c->addItemList(choice->choices, 1);
    comboAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, id, *c));
    addAndMakeVisible(*r.label);
    addAndMakeVisible(*c);
    byId[id] = c.get();
    r.control = std::move(c);
    rows.push_back(std::move(r));
}

void ControlsPanel::toggle(const juce::String& id, const juce::String& label)
{
    Row r;
    auto b = std::make_unique<juce::ToggleButton>(label);
    buttonAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *b));
    b->onStateChange = [this] { refreshEnablement(); };
    addAndMakeVisible(*b);
    byId[id] = b.get();
    r.control = std::move(b);
    r.fullWidth = true;
    rows.push_back(std::move(r));
}

void ControlsPanel::refreshEnablement()
{
    auto get = [this](const juce::String& id) { return proc.apvts.getRawParameterValue(id)->load(); };
    auto enable = [this](const juce::String& id, bool on) {
        if (auto it = byId.find(id); it != byId.end()) it->second->setEnabled(on);
    };
    enable(ids::tuning, get(ids::autoTune) < 0.5f);
    enable(ids::scale, get(ids::keyTonic) > 0.5f);
    enable(ids::strength, get(ids::quantize) > 0.5f);
    enable(ids::bendRange, get(ids::bends) > 0.5f);
}

void ControlsPanel::resized()
{
    const int w = getWidth() - 28;
    int y = 4;
    headerPos.clear();
    for (auto& r : rows)
    {
        if (r.header)
        {
            y += 8;
            headerPos.push_back({ y, r.title });
            y += 26;
            continue;
        }
        if (r.fullWidth)
        {
            r.control->setBounds(10, y, w + 8, 24);
            y += 28;
            continue;
        }
        r.label->setBounds(10, y, w, 18);
        y += 18;
        r.control->setBounds(14, y, w, 24);
        y += 30;
        if (r.help.isNotEmpty()) y += 14;
    }
    contentHeight = y + 10;
}

void ControlsPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    bool first = true;
    for (const auto& [y, title] : headerPos)
    {
        if (!first)
        {
            g.setColour(theme::line);
            g.fillRect(0, y - 6, getWidth(), 1);
        }
        first = false;
        g.setColour(theme::muted);
        g.drawText(title.toUpperCase(), 12, y, getWidth() - 24, 22, juce::Justification::centredLeft);
    }
    g.setFont(juce::FontOptions(11.0f));
    for (const auto& r : rows)
    {
        if (r.help.isEmpty() || !r.control) continue;
        g.setColour(theme::muted);
        g.drawText(r.help, 14, r.control->getBottom() + 1, getWidth() - 28, 14, juce::Justification::centredLeft);
    }
}

// ----------------------------------------------------------------- editor

MelodyLiftEditor::MelodyLiftEditor(MelodyLiftProcessor& p)
    : AudioProcessorEditor(&p), proc(p), dragChip(p), roll(p), controls(p)
{
    setLookAndFeel(&lnf);

    for (auto* b : { &captureBtn, &loadBtn, &clearBtn, &undoBtn, &redoBtn, &fitBtn, &saveBtn })
        addAndMakeVisible(*b);
    saveBtn.getProperties().set("primary", true);
    saveBtn.setColour(juce::TextButton::textColourOffId, theme::accentFg);
    captureBtn.setTooltip("Arm, then press play in your DAW. Capture stops when playback stops.");
    loadBtn.setTooltip("Open a vocal file (WAV, AIFF, FLAC, OGG, MP3). It is placed at the start of the song.");
    clearBtn.setTooltip("Remove the captured audio and all notes");
    fitBtn.setTooltip("Zoom to fit all notes");
    saveBtn.setTooltip("Save the melody as a .mid file");

    captureBtn.onClick = [this] { proc.toggleCapture(); refresh(); };
    loadBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Open a vocal", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                      "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto f = fc.getResult();
                                 if (f.existsAsFile()) proc.loadAudioFile(f);
                             });
    };
    clearBtn.onClick = [this] { proc.clearAll(); };
    undoBtn.onClick = [this] { proc.undo(); };
    redoBtn.onClick = [this] { proc.redo(); };
    fitBtn.onClick = [this] { roll.fitToNotes(); };
    saveBtn.onClick = [this] {
        if (proc.getNotes().empty()) return;
        chooser = std::make_unique<juce::FileChooser>("Save MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(proc.exportBaseName() + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& fc) {
                                 auto f = fc.getResult();
                                 if (f == juce::File()) return;
                                 if (!f.hasFileExtension("mid")) f = f.withFileExtension("mid");
                                 showNotice(proc.writeMidiFile(f) ? "Saved " + f.getFileName() : "Could not write " + f.getFullPathName());
                             });
    };

    addAndMakeVisible(dragChip);
    for (auto* l : { &statusLabel, &statsLabel, &hintLabel, &selectionLabel })
    {
        addAndMakeVisible(*l);
        l->setFont(juce::FontOptions(13.0f));
        l->setMinimumHorizontalScale(0.7f);
    }
    statsLabel.setColour(juce::Label::textColourId, theme::fg);
    hintLabel.setColour(juce::Label::textColourId, theme::muted);
    hintLabel.setFont(juce::FontOptions(12.0f));
    hintLabel.setJustificationType(juce::Justification::centredRight);
    hintLabel.setText("Double-click: add/delete  |  Drag: move  |  Edge: resize  |  Alt: no snap  |  Ctrl+wheel: zoom",
                      juce::dontSendNotification);
    selectionLabel.setColour(juce::Label::textColourId, theme::accent);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, theme::accent);
    progressBar.setColour(juce::ProgressBar::backgroundColourId, theme::line);
    progressBar.setPercentageDisplay(false);
    addChildComponent(progressBar);

    addAndMakeVisible(roll);
    roll.onSelectionChanged = [this] { selectionLabel.setText(roll.selectionSummary(), juce::dontSendNotification); };

    controls.setLookAndFeel(&lnf);
    controlsView.setViewedComponent(&controls, false);
    controlsView.setScrollBarsShown(true, false);
    controlsView.setScrollBarThickness(8);
    addAndMakeVisible(controlsView);

    proc.addChangeListener(this);
    setResizable(true, true);
    setResizeLimits(860, 520, 2560, 1600);
    setSize(1180, 720);
    startTimerHz(30);
    refresh();
    roll.notesReplaced();
}

MelodyLiftEditor::~MelodyLiftEditor()
{
    proc.removeChangeListener(this);
    controls.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void MelodyLiftEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);
    // brand
    g.setColour(theme::accent);
    g.fillRect(14, 21, 14, 3);
    g.fillRect(14, 27, 9, 3);
    g.setColour(theme::fg);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("MelodyLift", 34, 0, 120, kTopH, juce::Justification::centredLeft);

    auto rollFrame = roll.getBounds().expanded(1);
    g.setColour(theme::line);
    g.drawRect(rollFrame);
    g.drawRect(controlsView.getBounds().expanded(1));

    if (dropHover)
    {
        g.setColour(theme::accent);
        g.drawRect(roll.getBounds(), 3);
    }
}

void MelodyLiftEditor::resized()
{
    auto r = getLocalBounds().reduced(kGap, 0);
    auto top = r.removeFromTop(kTopH).reduced(0, 9);
    top.removeFromLeft(140);
    captureBtn.setBounds(top.removeFromLeft(118));
    top.removeFromLeft(6);
    loadBtn.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(6);
    clearBtn.setBounds(top.removeFromLeft(62));
    top.removeFromLeft(12);
    saveBtn.setBounds(top.removeFromRight(100));
    top.removeFromRight(8);
    dragChip.setBounds(top.removeFromRight(96));
    top.removeFromRight(14);
    fitBtn.setBounds(top.removeFromRight(46));
    top.removeFromRight(4);
    redoBtn.setBounds(top.removeFromRight(54));
    top.removeFromRight(4);
    undoBtn.setBounds(top.removeFromRight(54));
    top.removeFromRight(12);
    auto statusArea = top;
    progressBar.setBounds(statusArea.removeFromBottom(5).withTrimmedRight(statusArea.getWidth() / 3));
    statusLabel.setBounds(statusArea);

    auto bottom = r.removeFromBottom(kBottomH);
    statsLabel.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2));
    selectionLabel.setBounds(bottom.removeFromLeft(bottom.getWidth() / 3));
    hintLabel.setBounds(bottom);

    auto panel = r.removeFromRight(kPanelW);
    r.removeFromRight(kGap);
    controlsView.setBounds(panel.reduced(1));
    controls.setSize(controlsView.getMaximumVisibleWidth(), 100);
    controls.resized();
    controls.setSize(controlsView.getMaximumVisibleWidth(), std::max(controls.preferredHeight(), controlsView.getHeight()));
    roll.setBounds(r.reduced(1));
}

void MelodyLiftEditor::showNotice(const juce::String& text)
{
    noticeText = text;
    noticeUntil = juce::Time::getMillisecondCounter() + 5000;
    refresh();
}

void MelodyLiftEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto n = proc.takeNotice();
    if (n.isNotEmpty()) showNotice(n);
    controls.refreshEnablement();
    roll.notesReplaced();
    refresh();
}

void MelodyLiftEditor::refresh()
{
    using S = MelodyLiftProcessor::Status;
    const auto st = proc.getStatus();
    const bool showNotice = noticeText.isNotEmpty() && juce::Time::getMillisecondCounter() < noticeUntil;
    statusLabel.setText(showNotice ? noticeText : proc.getStatusText(), juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, st == S::Capturing || st == S::Armed ? theme::danger : theme::fg);
    progressBar.setVisible(st == S::Analysing);
    progressValue = proc.getProgress();

    const bool armed = proc.isArmed();
    captureBtn.setButtonText(armed ? "Stop capture" : "Capture");
    captureBtn.setToggleState(armed && (st == S::Capturing ? (blink / 8) % 2 == 0 : true), juce::dontSendNotification);
    captureBtn.setEnabled(st != S::Analysing);
    loadBtn.setEnabled(!armed && st != S::Analysing);
    undoBtn.setEnabled(proc.canUndo());
    redoBtn.setEnabled(proc.canRedo());
    saveBtn.setEnabled(!proc.getNotes().empty());

    const auto& notes = proc.getNotes();
    juce::String stats;
    if (proc.hasAnalysis())
    {
        stats << (int) notes.size() << (notes.size() == 1 ? " note" : " notes");
        if (!notes.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& x : notes) { lo = std::min(lo, x.pitch); hi = std::max(hi, x.pitch); }
            stats << "  |  " << ml::noteName(lo) << "-" << ml::noteName(hi);
        }
        const auto k = proc.currentKey();
        static const char* scaleNames[] = { "major", "minor", "harmonic minor", "dorian", "mixolydian", "major pent.", "minor pent." };
        stats << "  |  key " << ml::pitchClassName(k.tonic) << " " << scaleNames[juce::jlimit(0, 6, k.scale)]
              << (k.isAuto ? " (detected)" : "");
        const double cents = proc.getTranscription().prep.tuning * 100;
        stats << "  |  A4 " << juce::String(440.0 * std::pow(2.0, cents / 1200.0), 1) << " Hz";
    }
    statsLabel.setText(stats, juce::dontSendNotification);
    selectionLabel.setText(roll.selectionSummary(), juce::dontSendNotification);
    dragChip.repaint();
}

void MelodyLiftEditor::timerCallback()
{
    ++blink;
    roll.playheadTick();
    const auto st = proc.getStatus();
    if (st != MelodyLiftProcessor::Status::Idle || (noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil))
    {
        if (noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil) noticeUntil = 0, noticeText.clear();
        refresh();
    }
}

bool MelodyLiftEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (isAudioFile(f)) return true;
    return false;
}

void MelodyLiftEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    for (const auto& f : files)
        if (isAudioFile(f)) { proc.loadAudioFile(juce::File(f)); return; }
}
