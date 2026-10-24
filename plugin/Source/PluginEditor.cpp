#include "PluginEditor.h"

namespace
{
constexpr int kTopH = 52, kBottomH = 30, kPanelW = 280, kGap = 8, kCardsH = 46;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;aif;aiff;flac;ogg;mp3");
}

void styleTab(juce::TextButton& b, bool on)
{
    b.setColour(juce::TextButton::buttonColourId, on ? theme::raised.brighter(0.15f) : theme::bg);
    b.setColour(juce::TextButton::textColourOffId, on ? theme::fg : theme::muted);
}
} // namespace

// ------------------------------------------------------------ drag chip

void DragMidiChip::paint(juce::Graphics& g)
{
    const bool on = hasContent();
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    juce::Path p;
    p.addRoundedRectangle(r, 5.0f);
    const float dash[] = { 4.0f, 3.0f };
    juce::Path dashed;
    juce::PathStrokeType(1.2f).createDashedStroke(dashed, p, dash, 2);
    g.setColour(on ? theme::accent : theme::muted.withAlpha(0.5f));
    g.fillPath(dashed);
    g.setFont(juce::FontOptions(juce::jmin(13.0f, getHeight() * 0.45f), juce::Font::bold));
    g.drawText(label, getLocalBounds(), juce::Justification::centred);
}

void DragMidiChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4 || !hasContent()) return;
    const auto f = makeFile();
    if (!f.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this,
                                                               [this] { dragging = false; });
}

// -------------------------------------------------------- controls panel

ControlsPanel::ControlsPanel(MelodyLiftProcessor& p, Kind kind) : proc(p)
{
    if (kind == Kind::Generator)
    {
        header("Style");
        combo(ids::gStyle, "Genre");
        combo(ids::gSection, "Song section");
        combo(ids::gKey, "Key");
        combo(ids::gScale, "Scale / raga");
        combo(ids::gBars, "Length");
        button("Use my vocal's key", [this] { proc.useVocalKey(); });
        header("Hook");
        slider(ids::gRepeat, "Catchiness", "How strongly the hook repeats its motif");
        slider(ids::gIdiom, "Genre phrases", "Start hooks from the genre's phrase shapes");
        slider(ids::gDensity, "Density", "Fewer, longer notes  <->  busy flows");
        slider(ids::gSyncop, "Syncopation", "On the beat  <->  off-beat bounce");
        slider(ids::gComplex, "Variation", "How much repeats change");
        slider(ids::gOrnament, "Ornaments", "Runs, murki, gamak, grace notes");
        combo(ids::gRange, "Range");
        combo(ids::gOctave, "Register");
        header("Vocal feel");
        slider(ids::gFeel, "Vocal feel", "Scoops, meend slides, vibrato, pocket");
        slider(ids::gHumanize, "Humanize");
        slider(ids::gSwing, "Swing");
        combo(ids::gSound, "Melody sound");
        toggle(ids::gExpr, "Export vocal expression (pitch bends)");
        header("Playback");
        if (!kInstrumentBuild) toggle(ids::gPlay, "Play pattern with the song");
        slider(ids::gVolume, "Preview volume");
        combo(ids::midiOut, "MIDI out");
    }
    else
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
        combo(ids::midiOut, "MIDI out");
    }
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
    if (id == ids::gStyle)
    {
        // genres grouped by scene; item IDs are style index + 1
        for (int g = 0; g < ml::gen::kNumStyleGroups; ++g)
        {
            c->addSectionHeading(ml::gen::styleGroupName(g));
            for (int st = 0; st < ml::gen::NumStyles; ++st)
                if (ml::gen::styleGroup(st) == g) c->addItem(ml::gen::styleName(st), st + 1);
        }
        auto* box = c.get();
        auto att = std::make_unique<juce::ParameterAttachment>(*proc.apvts.getParameter(id), [box](float v) {
            box->setSelectedId(juce::roundToInt(v) + 1, juce::dontSendNotification);
        });
        auto* a = att.get();
        box->onChange = [box, a] { if (box->getSelectedId() > 0) a->setValueAsCompleteGesture((float) (box->getSelectedId() - 1)); };
        att->sendInitialUpdate();
        customAtt.push_back(std::move(att));
    }
    else
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(id)))
            c->addItemList(choice->choices, 1);
        comboAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, id, *c));
    }
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

void ControlsPanel::button(const juce::String& label, std::function<void()> onClick)
{
    Row r;
    auto b = std::make_unique<juce::TextButton>(label);
    b->onClick = std::move(onClick);
    addAndMakeVisible(*b);
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
            r.control->setBounds(10, y, w + 8, 26);
            y += 32;
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

// ------------------------------------------------------------ layer card

void IconToggle::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? onColour.withAlpha(0.25f) : (over || down ? theme::raised.brighter(0.08f) : theme::raised));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(on ? onColour : (over ? theme::muted : theme::line));
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    const auto c = r.getCentre();
    g.setColour(on ? onColour : theme::muted);
    juce::Path p;
    if (icon == Icon::Mute)
    {
        // speaker
        p.addRectangle(c.x - 6, c.y - 2.5f, 3.5f, 5.0f);
        p.addTriangle(c.x - 2.5f, c.y - 2.5f, c.x + 2.0f, c.y - 6.0f, c.x + 2.0f, c.y + 6.0f);
        p.addTriangle(c.x - 2.5f, c.y - 2.5f, c.x - 2.5f, c.y + 2.5f, c.x + 2.0f, c.y + 6.0f);
        g.fillPath(p);
        if (on)
        {
            g.drawLine(c.x + 3.5f, c.y - 3, c.x + 8.5f, c.y + 3, 1.6f);
            g.drawLine(c.x + 3.5f, c.y + 3, c.x + 8.5f, c.y - 3, 1.6f);
        }
        else
        {
            juce::Path wave;
            wave.addCentredArc(c.x + 2.5f, c.y, 4.5f, 4.5f, 0, 0.6f, 2.55f, true);
            g.strokePath(wave, juce::PathStrokeType(1.3f));
        }
    }
    else
    {
        // padlock: closed when locked, open shackle otherwise
        g.fillRoundedRectangle(juce::Rectangle<float>(c.x - 5.5f, c.y - 1.0f, 11.0f, 8.0f), 1.5f);
        juce::Path shackle;
        const float lift = on ? 0.0f : 3.0f;
        shackle.addCentredArc(c.x, c.y - 2.0f - lift, 3.5f, 4.0f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        shackle.lineTo(c.x + 3.5f, c.y - 1.0f - (on ? 0 : lift - 2));
        shackle.startNewSubPath(c.x - 3.5f, c.y - 2.0f - lift);
        shackle.lineTo(c.x - 3.5f, c.y - 1.0f);
        g.strokePath(shackle, juce::PathStrokeType(1.6f));
    }
}

LayerCard::LayerCard(MelodyLiftProcessor& p, int l)
    : proc(p), layer(l),
      drag([this] { return proc.writePatternMidiToTempFile(layer); }, [this] { return !proc.genLayer(layer).empty(); }, "Drag")
{
    for (auto* b : std::initializer_list<juce::Component*> { &mute, &lock, &dice }) addAndMakeVisible(*b);
    addAndMakeVisible(drag);
    mute.onColour = theme::muted;
    lock.onColour = PianoRoll::layerColour(l);
    mute.setTooltip("Mute this layer in the preview and MIDI out");
    lock.setTooltip("Keep this layer when you press Generate");
    dice.setTooltip("Write a new version of this layer");
    mute.onClick = [this] { proc.setLayerMuted(layer, mute.getToggleState()); };
    lock.onClick = [this] { proc.setLayerLocked(layer, lock.getToggleState()); };
    dice.onClick = [this] { proc.rerollLayer(layer); proc.setActiveLayer(layer); };
    refresh();
}

void LayerCard::refresh()
{
    mute.setToggleState(proc.isLayerMuted(layer), juce::dontSendNotification);
    lock.setToggleState(proc.isLayerLocked(layer), juce::dontSendNotification);
    repaint();
}

void LayerCard::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool active = proc.getActiveLayer() == layer;
    const auto col = PianoRoll::layerColour(layer);
    g.setColour(active ? theme::raised.brighter(0.08f) : theme::raised);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(active ? col : theme::line);
    g.drawRoundedRectangle(r, 6.0f, active ? 1.6f : 1.0f);
    g.setColour(proc.isLayerMuted(layer) ? col.withAlpha(0.3f) : col);
    g.fillRoundedRectangle(juce::Rectangle<float>(8, r.getCentreY() - 6, 12, 12), 3.0f);
    g.setColour(active ? theme::fg : theme::muted);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(ml::gen::layerName(layer), juce::Rectangle<int>(26, 0, std::max(10, mute.getX() - 30), getHeight()),
               juce::Justification::centredLeft, true);
}

void LayerCard::resized()
{
    auto r = getLocalBounds().reduced(6, 8);
    drag.setBounds(r.removeFromRight(48));
    r.removeFromRight(4);
    dice.setBounds(r.removeFromRight(44));
    r.removeFromRight(4);
    lock.setBounds(r.removeFromRight(28));
    r.removeFromRight(4);
    mute.setBounds(r.removeFromRight(28));
}

void LayerCard::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasClicked()) proc.setActiveLayer(layer);
}

// ----------------------------------------------------------------- editor

MelodyLiftEditor::MelodyLiftEditor(MelodyLiftProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      dragChip([this] { return genTab() ? proc.writePatternMidiToTempFile(-1) : proc.writeMidiToTempFile(); },
               [this] { return genTab() ? proc.hasPattern() : !proc.getNotes().empty(); }, "Drag MIDI"),
      roll(p), vocalControls(p, ControlsPanel::Kind::Vocal), genControls(p, ControlsPanel::Kind::Generator)
{
    setLookAndFeel(&lnf);

    for (auto* b : { &tabGen, &tabVocal, &generateBtn, &hookBtn, &variationBtn, &chordsBtn, &previewBtn, &captureBtn, &loadBtn,
                     &clearBtn, &harmonizeBtn, &undoBtn, &redoBtn, &fitBtn, &saveBtn })
        addAndMakeVisible(*b);
    for (auto* b : { &saveBtn, &generateBtn })
    {
        b->getProperties().set("primary", true);
        b->setColour(juce::TextButton::textColourOffId, theme::accentFg);
    }
    previewBtn.setClickingTogglesState(true);
    previewBtn.setColour(juce::TextButton::buttonOnColourId, theme::accent.darker(0.4f));

    tabGen.onClick = [this] { setTab(0); };
    tabVocal.onClick = [this] { setTab(1); };
    generateBtn.setTooltip("New chords, hook, bass and counter (locked layers stay)");
    hookBtn.setTooltip("New hook over the same chords");
    variationBtn.setTooltip("Same rhythm, new notes: a variation of the hook");
    chordsBtn.setTooltip("New chord progression; the hook adapts to it");
    previewBtn.setTooltip("Loop the pattern with the built-in sounds");
    harmonizeBtn.setTooltip("Write chords, bass and counter that fit your vocal");
    captureBtn.setTooltip("Arm, then press play in your DAW. Capture stops when playback stops.");
    loadBtn.setTooltip("Open a vocal file (WAV, AIFF, FLAC, OGG, MP3). It is placed at the start of the song.");
    clearBtn.setTooltip("Remove the captured audio and all notes");
    fitBtn.setTooltip("Zoom to fit");

    generateBtn.onClick = [this] { proc.generate(MelodyLiftProcessor::GenAction::All); };
    hookBtn.onClick = [this] { proc.generate(MelodyLiftProcessor::GenAction::Hook); proc.setActiveLayer(ml::gen::Melody); };
    variationBtn.onClick = [this] { proc.generate(MelodyLiftProcessor::GenAction::Variation); proc.setActiveLayer(ml::gen::Melody); };
    chordsBtn.onClick = [this] { proc.generate(MelodyLiftProcessor::GenAction::Chords); };
    previewBtn.onClick = [this] { proc.setPreviewPlaying(previewBtn.getToggleState()); };
    harmonizeBtn.onClick = [this] { if (proc.chordsForVocal()) setTab(0); };

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
    undoBtn.onClick = [this] { if (genTab()) proc.genUndo(); else proc.undo(); };
    redoBtn.onClick = [this] { if (genTab()) proc.genRedo(); else proc.redo(); };
    fitBtn.onClick = [this] { roll.fitToNotes(); };
    saveBtn.onClick = [this] {
        const bool gen = genTab();
        if (gen ? !proc.hasPattern() : proc.getNotes().empty()) return;
        const auto name = gen ? proc.writePatternMidiToTempFile(-1).getFileName() : proc.exportBaseName() + ".mid";
        chooser = std::make_unique<juce::FileChooser>("Save MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, gen](const juce::FileChooser& fc) {
                                 auto f = fc.getResult();
                                 if (f == juce::File()) return;
                                 if (!f.hasFileExtension("mid")) f = f.withFileExtension("mid");
                                 const bool ok = gen ? proc.writePatternMidiFile(f, -1) : proc.writeMidiFile(f);
                                 showNotice(ok ? "Saved " + f.getFileName() : "Could not write " + f.getFullPathName());
                             });
    };

    addAndMakeVisible(dragChip);
    for (auto* l : { &statusLabel, &hintLabel, &selectionLabel })
    {
        addAndMakeVisible(*l);
        l->setFont(juce::FontOptions(13.0f));
        l->setMinimumHorizontalScale(0.7f);
    }
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

    for (auto pr : { std::make_pair(&vocalControls, &vocalView), std::make_pair(&genControls, &genView) })
    {
        pr.first->setLookAndFeel(&lnf);
        pr.second->setViewedComponent(pr.first, false);
        pr.second->setScrollBarsShown(true, false);
        pr.second->setScrollBarThickness(8);
        addChildComponent(*pr.second);
    }
    for (int l = 0; l < ml::gen::NumLayers; ++l)
    {
        cards[(size_t) l] = std::make_unique<LayerCard>(proc, l);
        addChildComponent(*cards[(size_t) l]);
    }
    if (kInstrumentBuild) captureBtn.setTooltip("Capture needs MelodyLift on a mixer track (the effect version)");

    proc.addChangeListener(this);
    setResizable(true, true);
    setResizeLimits(960, 580, 2560, 1600);
    setSize(1280, 780);
    setTab(proc.getUiTab());
    startTimerHz(30);
}

MelodyLiftEditor::~MelodyLiftEditor()
{
    proc.removeChangeListener(this);
    vocalControls.setLookAndFeel(nullptr);
    genControls.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void MelodyLiftEditor::setTab(int t)
{
    tab = t == 1 ? 1 : 0;
    proc.setUiTab(tab);
    styleTab(tabGen, genTab());
    styleTab(tabVocal, !genTab());
    for (auto* b : { &generateBtn, &hookBtn, &variationBtn, &chordsBtn, &previewBtn }) b->setVisible(genTab());
    for (auto* b : { &loadBtn, &clearBtn, &harmonizeBtn }) b->setVisible(!genTab());
    captureBtn.setVisible(!genTab() && !kInstrumentBuild);
    genView.setVisible(genTab());
    vocalView.setVisible(!genTab());
    for (auto& c : cards) c->setVisible(genTab());
    hintLabel.setVisible(!genTab());
    dragChip.setLabel(genTab() ? "Drag all" : "Drag MIDI");
    roll.setMode(genTab() ? PianoRoll::Mode::Generator : PianoRoll::Mode::Vocal);
    if (!genTab() && proc.isPreviewPlaying()) proc.setPreviewPlaying(false);
    resized();
    refresh();
    roll.fitToNotes();
}

void MelodyLiftEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);
    g.setColour(theme::accent);
    g.fillRect(14, 21, 14, 3);
    g.fillRect(14, 27, 9, 3);
    g.setColour(theme::fg);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("MelodyLift", 34, 0, 120, kTopH, juce::Justification::centredLeft);

    g.setColour(theme::line);
    g.drawRect(roll.getBounds().expanded(1));
    g.drawRect((genTab() ? genView : vocalView).getBounds().expanded(1));
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
    tabGen.setBounds(top.removeFromLeft(92));
    tabVocal.setBounds(top.removeFromLeft(112));
    top.removeFromLeft(16);

    saveBtn.setBounds(top.removeFromRight(96));
    top.removeFromRight(6);
    dragChip.setBounds(top.removeFromRight(90));
    top.removeFromRight(12);
    fitBtn.setBounds(top.removeFromRight(42));
    top.removeFromRight(4);
    redoBtn.setBounds(top.removeFromRight(52));
    top.removeFromRight(4);
    undoBtn.setBounds(top.removeFromRight(52));
    top.removeFromRight(12);

    if (genTab())
    {
        generateBtn.setBounds(top.removeFromLeft(104));
        top.removeFromLeft(6);
        for (auto* b : { &hookBtn, &variationBtn, &chordsBtn })
        {
            b->setBounds(top.removeFromLeft(88));
            top.removeFromLeft(4);
        }
        top.removeFromLeft(4);
        previewBtn.setBounds(top.removeFromLeft(80));
    }
    else
    {
        if (!kInstrumentBuild)
        {
            captureBtn.setBounds(top.removeFromLeft(112));
            top.removeFromLeft(6);
        }
        loadBtn.setBounds(top.removeFromLeft(96));
        top.removeFromLeft(6);
        clearBtn.setBounds(top.removeFromLeft(58));
        top.removeFromLeft(6);
        harmonizeBtn.setBounds(top.removeFromLeft(130));
    }

    auto bottom = r.removeFromBottom(kBottomH);
    progressBar.setBounds(bottom.withHeight(4).withWidth(bottom.getWidth() / 3).withY(bottom.getBottom() - 6));
    statusLabel.setBounds(bottom.removeFromLeft(bottom.getWidth() * 55 / 100));
    selectionLabel.setBounds(bottom.removeFromLeft(genTab() ? bottom.getWidth() : bottom.getWidth() / 3));
    hintLabel.setBounds(bottom);

    auto panel = r.removeFromRight(kPanelW);
    r.removeFromRight(kGap);
    for (auto pr : { std::make_pair(&vocalControls, &vocalView), std::make_pair(&genControls, &genView) })
    {
        pr.second->setBounds(panel.reduced(1));
        pr.first->setSize(pr.second->getMaximumVisibleWidth(), 100);
        pr.first->resized();
        pr.first->setSize(pr.second->getMaximumVisibleWidth(), std::max(pr.first->preferredHeight(), pr.second->getHeight()));
    }
    if (genTab())
    {
        auto strip = r.removeFromBottom(kCardsH);
        r.removeFromBottom(6);
        const int w = (strip.getWidth() - 3 * 6) / 4;
        for (int l = 0; l < ml::gen::NumLayers; ++l)
        {
            cards[(size_t) l]->setBounds(strip.removeFromLeft(w));
            strip.removeFromLeft(6);
        }
    }
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
    vocalControls.refreshEnablement();
    roll.notesReplaced();
    for (auto& c : cards) c->refresh();
    refresh();
}

void MelodyLiftEditor::refresh()
{
    using S = MelodyLiftProcessor::Status;
    const auto st = proc.getStatus();
    const bool notice = noticeText.isNotEmpty() && juce::Time::getMillisecondCounter() < noticeUntil;
    juce::String status = genTab() ? proc.patternSummary() : proc.getStatusText();
    if (st == S::Analysing || st == S::Capturing || st == S::Armed) status = proc.getStatusText();
    statusLabel.setText(notice ? noticeText : status, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, st == S::Capturing || st == S::Armed ? theme::danger : theme::fg);
    progressBar.setVisible(st == S::Analysing);
    progressValue = proc.getProgress();

    const bool armed = proc.isArmed();
    captureBtn.setButtonText(armed ? "Stop capture" : "Capture");
    captureBtn.setToggleState(armed && (st == S::Capturing ? (blink / 8) % 2 == 0 : true), juce::dontSendNotification);
    captureBtn.setEnabled(st != S::Analysing);
    loadBtn.setEnabled(!armed && st != S::Analysing);
    harmonizeBtn.setEnabled(!proc.getNotes().empty() && st != S::Analysing);
    undoBtn.setEnabled(genTab() ? proc.canGenUndo() : proc.canUndo());
    redoBtn.setEnabled(genTab() ? proc.canGenRedo() : proc.canRedo());
    saveBtn.setEnabled(genTab() ? proc.hasPattern() : !proc.getNotes().empty());
    previewBtn.setToggleState(proc.isPreviewPlaying(), juce::dontSendNotification);
    previewBtn.setButtonText(proc.isPreviewPlaying() ? "Stop" : "Preview");

    if (!genTab() && proc.hasAnalysis() && st == S::Idle && !notice)
    {
        const auto& notes = proc.getNotes();
        juce::String stats = proc.getStatusText();
        stats << "  |  " << (int) notes.size() << (notes.size() == 1 ? " note" : " notes");
        if (!notes.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& x : notes) { lo = std::min(lo, x.pitch); hi = std::max(hi, x.pitch); }
            stats << "  |  " << ml::noteName(lo) << "-" << ml::noteName(hi);
        }
        const auto k = proc.currentKey();
        static const char* scaleNames[] = { "major", "minor", "harmonic minor", "dorian", "mixolydian", "major pent.",
                                            "minor pent.", "phrygian", "phrygian dom." };
        stats << "  |  key " << ml::pitchClassName(k.tonic) << " " << scaleNames[juce::jlimit(0, 8, k.scale)]
              << (k.isAuto ? " (detected)" : "");
        statusLabel.setText(stats, juce::dontSendNotification);
    }
    selectionLabel.setText(roll.selectionSummary(), juce::dontSendNotification);
    dragChip.repaint();
}

void MelodyLiftEditor::timerCallback()
{
    ++blink;
    roll.playheadTick();
    const auto st = proc.getStatus();
    const bool expired = noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil;
    if (st != MelodyLiftProcessor::Status::Idle || expired)
    {
        if (expired) { noticeUntil = 0; noticeText.clear(); }
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
        if (isAudioFile(f))
        {
            setTab(1);
            proc.loadAudioFile(juce::File(f));
            return;
        }
}
