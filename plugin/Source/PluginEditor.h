#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <map>

/** Drag source that hands a freshly written .mid file to the host (FL Studio, Ableton...). */
class DragMidiChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragMidiChip(MelodyLiftProcessor& p) : proc(p)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        setTooltip("Drag onto a track in your DAW's playlist or piano roll");
    }
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    MelodyLiftProcessor& proc;
    bool dragging = false;
};

/** Scrollable settings rack bound to the processor parameters. */
class ControlsPanel : public juce::Component
{
public:
    explicit ControlsPanel(MelodyLiftProcessor& p);
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

    MelodyLiftProcessor& proc;
    std::vector<Row> rows;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::pair<int, juce::String>> headerPos;
    int contentHeight = 0;
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

    MelodyLiftProcessor& proc;
    theme::LookAndFeel lnf;

    juce::TextButton captureBtn { "Capture" }, loadBtn { "Load audio" }, clearBtn { "Clear" },
        undoBtn { "Undo" }, redoBtn { "Redo" }, fitBtn { "Fit" }, saveBtn { "Save MIDI" };
    DragMidiChip dragChip;
    juce::Label statusLabel, statsLabel, hintLabel, selectionLabel;
    double progressValue = 0;
    juce::ProgressBar progressBar { progressValue };
    PianoRoll roll;
    ControlsPanel controls;
    juce::Viewport controlsView;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    juce::String noticeText;
    uint32_t noticeUntil = 0;
    bool dropHover = false;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodyLiftEditor)
};
