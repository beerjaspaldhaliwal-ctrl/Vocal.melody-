#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <set>

/**
 * Piano-roll view and editor for the detected notes, drawn on the song
 * timeline (bars from the host tempo) with the sung pitch curve behind them.
 */
class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll(MelodyLiftProcessor& p);

    enum class Mode { Vocal, Generator };
    void setMode(Mode m);
    Mode getMode() const { return mode; }
    static juce::Colour layerColour(int layer);

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify(const juce::MouseEvent&, float scale) override;
    bool keyPressed(const juce::KeyPress&) override;

    void fitToNotes();
    void zoomBy(float factor);
    void deleteSelection();
    void selectAll();
    /** Called by the editor's timer; follows the host playhead. */
    void playheadTick();
    void notesReplaced();

    std::function<void()> onSelectionChanged;
    juce::String selectionSummary() const;

private:
    static constexpr int kKeyW = 54;
    int rulerH() const { return mode == Mode::Generator ? 40 : 22; }
    const std::vector<ml::Note>& notesRef() const;
    std::vector<ml::Note>& editRef();
    void beginEdit();
    void endEdit();
    double playheadSeconds() const;

    double timeToX(double t) const { return kKeyW + t * pps - sx; }
    double xToTime(double x) const { return (x - kKeyW + sx) / pps; }
    double pitchToY(double p) const { return rulerH() + (127 - p) * rowH - sy; }
    int yToPitch(double y) const { return 127 - (int) std::floor((y - rulerH() + sy) / rowH); }
    double gridSec() const;
    double snap(double t, const juce::ModifierKeys& m) const;
    double contentEnd() const;
    void clampView();

    struct Hit { int index = -1; bool edge = false; };
    Hit hitTest(juce::Point<float> p) const;

    MelodyLiftProcessor& proc;
    double pps = 90, rowH = 14, sx = 0, sy = 0;
    std::set<int> selected;

    enum class Drag { None, Move, Resize, Pan };
    struct Orig { int id; double start, end; int pitch; };
    Drag drag = Drag::None;
    std::vector<Orig> orig;
    int anchorId = -1;
    bool moved = false;
    juce::Point<float> down;
    double panSx = 0, panSy = 0;
    double lastPlayhead = -1;
    int fittedFor = -1;
    Mode mode = Mode::Vocal;
};
