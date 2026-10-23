#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <functional>
#include <map>

/** Drag source that hands a freshly written .mid file to the host (FL Studio, Ableton...). */
class DragMidiChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragMidiChip(std::function<juce::File()> makeFile, std::function<bool()> hasContent, juce::String label)
        : makeFile(std::move(makeFile)), hasContent(std::move(hasContent)), label(std::move(label))
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        setTooltip("Drag onto your DAW's playlist or a channel's piano roll");
    }
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void setLabel(const juce::String& l) { label = l; repaint(); }

private:
    std::function<juce::File()> makeFile;
    std::function<bool()> hasContent;
    juce::String label;
    bool dragging = false;
};

/** Scrollable settings rack bound to the processor parameters. */
class ControlsPanel : public juce::Component
{
public:
    enum class Kind { Vocal, Generator };
    ControlsPanel(MelodyLiftProcessor& p, Kind kind);
    void paint(juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const { return contentHeight; }
    void refreshEnablement();

private:
    struct Row
    {
        juce::String title;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        bool header = false;
        bool fullWidth = false;
        juce::String help;
    };
    void header(const juce::String& title);
    void slider(const juce::String& id, const juce::String& label, const juce::String& help = {});
    void combo(const juce::String& id, const juce::String& label);
    void toggle(const juce::String& id, const juce::String& label);
    void button(const juce::String& label, std::function<void()> onClick);

    MelodyLiftProcessor& proc;
    std::vector<Row> rows;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::pair<int, juce::String>> headerPos;
    int contentHeight = 0;
};

/** Small toggle drawn as a speaker (mute) or padlock (lock) icon. */
class IconToggle : public juce::Button
{
public:
    enum class Icon { Mute, Lock };
    explicit IconToggle(Icon i) : juce::Button({}), icon(i) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    juce::Colour onColour = juce::Colours::orange;

private:
    Icon icon;
};

/** One generator layer: select it for editing, mute, lock, re-roll, drag out. */
class LayerCard : public juce::Component
{
public:
    LayerCard(MelodyLiftProcessor& p, int layer);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh();

private:
    MelodyLiftProcessor& proc;
    int layer;
    IconToggle mute { IconToggle::Icon::Mute }, lock { IconToggle::Icon::Lock };
    juce::TextButton dice { "New" };
    DragMidiChip drag;
};

class MelodyLiftEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MelodyLiftEditor(MelodyLiftProcessor&);
    ~MelodyLiftEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void showNotice(const juce::String& text);
    void setTab(int tab);
    bool genTab() const { return tab == 0; }

    MelodyLiftProcessor& proc;
    theme::LookAndFeel lnf;
    int tab = 0;

    juce::TextButton tabGen { "Generate" }, tabVocal { "Vocal to MIDI" };
    // generator actions
    juce::TextButton generateBtn { "Generate" }, hookBtn { "New hook" }, variationBtn { "Variation" },
        chordsBtn { "New chords" }, previewBtn { "Preview" };
    // vocal actions
    juce::TextButton captureBtn { "Capture" }, loadBtn { "Load audio" }, clearBtn { "Clear" }, harmonizeBtn { "Chords for vocal" };
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, fitBtn { "Fit" }, saveBtn { "Save MIDI" };
    DragMidiChip dragChip;
    juce::Label statusLabel, hintLabel, selectionLabel;
    double progressValue = 0;
    juce::ProgressBar progressBar { progressValue };
    PianoRoll roll;
    ControlsPanel vocalControls, genControls;
    juce::Viewport vocalView, genView;
    std::array<std::unique_ptr<LayerCard>, ml::gen::NumLayers> cards;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    juce::String noticeText;
    uint32_t noticeUntil = 0;
    bool dropHover = false;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodyLiftEditor)
};
