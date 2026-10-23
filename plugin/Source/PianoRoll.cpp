#include "PianoRoll.h"
#include "Theme.h"

#include <cmath>

namespace
{
bool isBlack(int p)
{
    const int pc = ((p % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

PianoRoll::PianoRoll(MelodyLiftProcessor& p) : proc(p)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
}

double PianoRoll::gridSec() const
{
    return ml::gridSeconds(proc.getHostBpm(), (int) proc.apvts.getRawParameterValue(ids::grid)->load());
}

double PianoRoll::snap(double t, const juce::ModifierKeys& m) const
{
    const double g = gridSec();
    if (g <= 0 || m.isAltDown()) return t;
    return std::round(t / g) * g;
}

double PianoRoll::contentEnd() const
{
    double e = 8;
    for (const auto& n : proc.getNotes()) e = std::max(e, n.end);
    if (!proc.getTrack().empty()) e = std::max(e, proc.getTrack().size() * proc.getTrack().hopSec());
    return e;
}

void PianoRoll::clampView()
{
    pps = juce::jlimit(4.0, 1600.0, pps);
    rowH = juce::jlimit(6.0, 40.0, rowH);
    const double visH = getHeight() - kRulerH;
    sy = juce::jlimit(0.0, std::max(0.0, 128 * rowH - visH), sy);
    const double visW = getWidth() - kKeyW;
    sx = juce::jlimit(0.0, std::max(0.0, contentEnd() * pps + 120 - visW), sx);
}

void PianoRoll::fitToNotes()
{
    const auto& notes = proc.getNotes();
    const double visW = std::max(100, getWidth() - kKeyW);
    const double visH = std::max(100, getHeight() - kRulerH);
    if (notes.empty())
    {
        sy = (127 - 66) * rowH - visH / 2;
        clampView();
        repaint();
        return;
    }
    double t0 = 1e9, t1 = 0;
    int lo = 127, hi = 0;
    for (const auto& n : notes)
    {
        t0 = std::min(t0, n.start);
        t1 = std::max(t1, n.end);
        lo = std::min(lo, n.pitch);
        hi = std::max(hi, n.pitch);
    }
    const double span = std::max(1.0, t1 - t0);
    pps = span <= 60 ? (visW - 40) / span : 60;
    sx = std::max(0.0, t0 * pps - 20);
    rowH = juce::jlimit(8.0, 22.0, visH / (hi - lo + 8));
    sy = (127 - (lo + hi) / 2.0) * rowH - visH / 2;
    clampView();
    repaint();
}

void PianoRoll::notesReplaced()
{
    std::set<int> keep;
    for (const auto& n : proc.getNotes())
        if (selected.count(n.id)) keep.insert(n.id);
    selected.swap(keep);
    if (fittedFor != proc.getAnalysisCount() && getWidth() > kKeyW + 100 && !proc.getNotes().empty())
    {
        fittedFor = proc.getAnalysisCount();
        fitToNotes();
    }
    repaint();
}

void PianoRoll::resized()
{
    clampView();
    notesReplaced();
}

void PianoRoll::zoomBy(float factor)
{
    const double cx = kKeyW + (getWidth() - kKeyW) / 2.0;
    const double t = xToTime(cx);
    pps *= factor;
    clampView();
    sx = t * pps - (cx - kKeyW);
    clampView();
    repaint();
}

void PianoRoll::playheadTick()
{
    const double ph = proc.getPlayheadSeconds();
    if (std::abs(ph - lastPlayhead) < 1e-4) return;
    lastPlayhead = ph;
    if (proc.isHostPlaying() && drag == Drag::None)
    {
        const double x = timeToX(ph);
        if (x > getWidth() - 50 || x < kKeyW)
        {
            sx = ph * pps - (getWidth() - kKeyW) * 0.15;
            clampView();
        }
    }
    repaint();
}

// ---------------------------------------------------------------- painting

void PianoRoll::paint(juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll(theme::rollBg);
    const auto& notes = proc.getNotes();

    const int pTop = juce::jlimit(0, 127, yToPitch(kRulerH));
    const int pBot = juce::jlimit(0, 127, yToPitch(H));
    const double t0 = xToTime(kKeyW), t1 = xToTime(W);
    const double beat = 60.0 / std::max(20.0, proc.getHostBpm()) * (4.0 / std::max(1, proc.getTimeSigDen()));
    const int beatsPerBar = std::max(1, proc.getTimeSigNum());

    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(kKeyW, kRulerH, (int) W - kKeyW, (int) H - kRulerH);

        for (int p = pTop; p >= pBot; --p)
        {
            const float y = (float) pitchToY(p);
            if (isBlack(p))
            {
                g.setColour(theme::rollBlack);
                g.fillRect(juce::Rectangle<float>((float) kKeyW, y, W - kKeyW, (float) rowH));
            }
            g.setColour(p % 12 == 0 ? theme::rollC : theme::rollLine);
            g.fillRect(juce::Rectangle<float>((float) kKeyW, std::round(y + (float) rowH) - 0.5f, W - kKeyW, 1.0f));
        }

        // grid, beats and bars
        const double gs = gridSec();
        if (gs > 0 && gs * pps >= 7)
        {
            g.setColour(theme::rollLine);
            for (long k = (long) std::floor(t0 / gs); k * gs <= t1; ++k)
                g.fillRect(juce::Rectangle<float>((float) std::round(timeToX(k * gs)) - 0.5f, (float) kRulerH, 1.0f, H));
        }
        const bool showBeats = beat * pps >= 5;
        for (long k = std::max(0L, (long) std::floor(t0 / beat)); k * beat <= t1; ++k)
        {
            const bool bar = k % beatsPerBar == 0;
            if (!bar && !showBeats) continue;
            g.setColour(bar ? theme::rollBar : theme::rollC);
            g.fillRect(juce::Rectangle<float>((float) std::round(timeToX(k * beat)) - 0.5f, (float) kRulerH, 1.0f, H));
        }

        // sung pitch curve
        const auto& tr = proc.getTrack();
        const auto& prep = proc.getTranscription().prep;
        if (!tr.empty() && prep.midi.size() == tr.size())
        {
            const double hop = tr.hopSec();
            const int transpose = (int) proc.apvts.getRawParameterValue(ids::transpose)->load();
            const long i0 = std::max(0L, (long) std::floor(t0 / hop) - 1);
            const long i1 = std::min((long) tr.size() - 1, (long) std::ceil(t1 / hop) + 1);
            juce::Path path;
            bool pen = false;
            for (long i = i0; i <= i1; ++i)
            {
                const float m = prep.midi[(size_t) i];
                if (std::isnan(m)) { pen = false; continue; }
                const float x = (float) timeToX(i * hop);
                const float y = (float) (pitchToY(m - prep.tuning + transpose) + rowH / 2);
                if (pen) path.lineTo(x, y); else path.startNewSubPath(x, y);
                pen = true;
            }
            g.setColour(theme::curve.withAlpha(0.85f));
            g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
        }

        // notes
        g.setFont(juce::FontOptions((float) std::min(11.0, rowH - 3)));
        for (const auto& n : notes)
        {
            if (n.end < t0 || n.start > t1) continue;
            const float x0 = (float) timeToX(n.start), x1 = (float) timeToX(n.end);
            const float y = (float) pitchToY(n.pitch);
            if (y > H || y + rowH < kRulerH) continue;
            const bool sel = selected.count(n.id) > 0;
            const juce::Rectangle<float> r(x0, y + 1, std::max(2.0f, x1 - x0 - 1), (float) rowH - 2);
            g.setColour(theme::accent.withAlpha(sel ? 1.0f : 0.8f + 0.2f * (float) n.vel / 127.0f));
            g.fillRoundedRectangle(r, (float) std::min(3.0, rowH / 4));
            if (sel)
            {
                g.setColour(theme::fg);
                g.drawRoundedRectangle(r, (float) std::min(3.0, rowH / 4), 2.0f);
            }
            if (r.getWidth() > 30 && rowH >= 11)
            {
                g.setColour(theme::noteText);
                g.drawText(ml::noteName(n.pitch), r.reduced(4, 0), juce::Justification::centredLeft, false);
            }
        }
    }

    // ruler
    g.setColour(theme::panel);
    g.fillRect(juce::Rectangle<float>((float) kKeyW, 0, W - kKeyW, (float) kRulerH));
    {
        const double barPx = beat * beatsPerBar * pps;
        int every = 1;
        while (barPx * every < 44) every *= 2;
        g.setFont(juce::FontOptions(11.0f));
        for (long k = std::max(0L, (long) std::floor(t0 / (beat * beatsPerBar))); k * beat * beatsPerBar <= t1; ++k)
        {
            const float x = (float) timeToX(k * beat * beatsPerBar);
            if (x < kKeyW) continue;
            g.setColour(theme::muted);
            if (k % every == 0)
            {
                g.fillRect(juce::Rectangle<float>(std::round(x) - 0.5f, kRulerH - 9.0f, 1.0f, 9.0f));
                g.drawText(juce::String(k + 1), juce::Rectangle<float>(x + 4, 0, 40, kRulerH - 2.0f), juce::Justification::centredLeft);
            }
            else
                g.fillRect(juce::Rectangle<float>(std::round(x) - 0.5f, kRulerH - 4.0f, 1.0f, 4.0f));
        }
    }
    g.setColour(theme::line);
    g.fillRect(0.0f, kRulerH - 1.0f, W, 1.0f);

    // playhead
    const float ph = (float) timeToX(proc.getPlayheadSeconds());
    if (ph >= kKeyW && ph <= W)
    {
        g.setColour(theme::playhead);
        g.fillRect(juce::Rectangle<float>(std::round(ph) - 0.5f, 0, 1.5f, H));
        juce::Path tri;
        tri.addTriangle(ph - 5, 0, ph + 5, 0, ph, 7);
        g.fillPath(tri);
    }

    // keyboard
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(0, kRulerH, kKeyW, (int) H - kRulerH);
        g.setColour(theme::keyWhite);
        g.fillRect(0.0f, (float) kRulerH, (float) kKeyW, H);
        g.setFont(juce::FontOptions((float) std::min(10.0, rowH - 2)));
        for (int p = pTop; p >= pBot; --p)
        {
            const float y = (float) pitchToY(p);
            if (isBlack(p))
            {
                g.setColour(theme::keyBlack);
                g.fillRect(juce::Rectangle<float>(0, y, kKeyW * 0.62f, (float) rowH));
            }
            else if (p % 12 == 0 || p % 12 == 5)
            {
                g.setColour(theme::rollC);
                g.fillRect(juce::Rectangle<float>(0, std::round(y + (float) rowH) - 0.5f, (float) kKeyW, 1.0f));
            }
            if (p % 12 == 0 && rowH >= 7)
            {
                g.setColour(theme::keyText);
                g.drawText(ml::noteName(p), juce::Rectangle<float>(0, y, kKeyW - 5.0f, (float) rowH), juce::Justification::centredRight);
            }
        }
    }
    g.setColour(theme::line);
    g.fillRect((float) kKeyW - 1, (float) kRulerH, 1.0f, H);
    g.setColour(theme::panel);
    g.fillRect(0.0f, 0.0f, kKeyW - 1.0f, kRulerH - 1.0f);
    g.setColour(theme::muted);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(juce::String(proc.getHostBpm(), 1) + " bpm", juce::Rectangle<float>(4, 0, kKeyW - 6.0f, kRulerH - 1.0f),
               juce::Justification::centredLeft);

    if (notes.empty())
    {
        g.setColour(theme::muted);
        g.setFont(juce::FontOptions(15.0f));
        auto msg = proc.getStatus() == MelodyLiftProcessor::Status::Idle && !proc.hasAnalysis()
                       ? "Press Capture, then play the song in your DAW.\nOr drop a vocal file here."
                       : "";
        g.drawFittedText(msg, getLocalBounds().withTrimmedLeft(kKeyW).withTrimmedTop(kRulerH), juce::Justification::centred, 3);
    }
}

// ------------------------------------------------------------- interaction

PianoRoll::Hit PianoRoll::hitTest(juce::Point<float> p) const
{
    Hit h;
    if (p.x < kKeyW || p.y < kRulerH) return h;
    const int pitch = yToPitch(p.y);
    const double t = xToTime(p.x);
    const double slack = 3 / pps;
    const auto& notes = proc.getNotes();
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(size_t) i];
        if (n.pitch == pitch && t >= n.start - slack && t <= n.end + slack)
        {
            h.index = i;
            const double xe = timeToX(n.end);
            h.edge = xe - p.x < 7 && xe - timeToX(n.start) > 10;
            return h;
        }
    }
    return h;
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    const auto h = hitTest(e.position);
    setMouseCursor(h.index < 0 ? juce::MouseCursor::NormalCursor
                   : h.edge   ? juce::MouseCursor::LeftRightResizeCursor
                              : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    down = e.position;
    moved = false;
    const auto h = hitTest(e.position);
    auto& notes = proc.editNotes();
    if (h.index >= 0)
    {
        const auto& n = notes[(size_t) h.index];
        if (e.mods.isShiftDown())
        {
            if (selected.count(n.id)) selected.erase(n.id); else selected.insert(n.id);
        }
        else if (!selected.count(n.id))
        {
            selected.clear();
            selected.insert(n.id);
        }
        anchorId = n.id;
        orig.clear();
        for (const auto& m : notes)
            if (selected.count(m.id)) orig.push_back({ m.id, m.start, m.end, m.pitch });
        drag = h.edge ? Drag::Resize : Drag::Move;
    }
    else
    {
        if (!e.mods.isShiftDown()) selected.clear();
        drag = Drag::Pan;
        panSx = sx;
        panSy = sy;
    }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    const float dx = e.position.x - down.x, dy = e.position.y - down.y;
    if (drag == Drag::Pan)
    {
        sx = panSx - dx;
        sy = panSy - dy;
        clampView();
        repaint();
        return;
    }
    if (drag == Drag::None || orig.empty()) return;
    if (!moved && std::abs(dx) < 3 && std::abs(dy) < 3) return;
    if (!moved) { proc.beginEdit(); moved = true; }

    auto& notes = proc.editNotes();
    auto find = [&](int id) -> ml::Note* {
        for (auto& n : notes) if (n.id == id) return &n;
        return nullptr;
    };
    const Orig* a = nullptr;
    for (const auto& o : orig) if (o.id == anchorId) a = &o;
    if (a == nullptr) return;
    const double dt = dx / pps;

    if (drag == Drag::Move)
    {
        const int dp = (int) std::lround(-dy / rowH);
        double minStart = 1e9;
        for (const auto& o : orig) minStart = std::min(minStart, o.start);
        const double shift = std::max(snap(a->start + dt, e.mods) - a->start, -minStart);
        for (const auto& o : orig)
            if (auto* n = find(o.id))
            {
                n->start = o.start + shift;
                n->end = o.end + shift;
                n->pitch = juce::jlimit(0, 127, o.pitch + dp);
            }
    }
    else
    {
        const double g = gridSec();
        const double minLen = std::min(0.03, g > 0 ? g : 0.03);
        const double newEnd = std::max(a->start + minLen, snap(a->end + dt, e.mods));
        const double delta = newEnd - a->end;
        for (const auto& o : orig)
            if (auto* n = find(o.id)) n->end = std::max(n->start + minLen, o.end + delta);
    }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent&)
{
    if (moved) proc.endEdit();
    drag = Drag::None;
    moved = false;
    orig.clear();
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.position.x < kKeyW || e.position.y < kRulerH) return;
    const auto h = hitTest(e.position);
    proc.beginEdit();
    auto& notes = proc.editNotes();
    if (h.index >= 0)
    {
        selected.erase(notes[(size_t) h.index].id);
        notes.erase(notes.begin() + h.index);
    }
    else
    {
        const double g = gridSec();
        const double t = std::max(0.0, g > 0 && !e.mods.isAltDown() ? std::floor(xToTime(e.position.x) / g) * g
                                                                       : xToTime(e.position.x));
        ml::Note n;
        n.id = ml::newNoteId();
        n.pitch = n.detPitch = juce::jlimit(0, 127, yToPitch(e.position.y));
        n.pitchRaw = n.pitch;
        n.start = n.srcStart = t;
        n.end = t + std::max(g > 0 ? g : 0.25, 0.1);
        n.vel = 96;
        notes.push_back(n);
        selected.clear();
        selected.insert(n.id);
    }
    proc.endEdit();
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double dy = -w.deltaY * 120, dx = -w.deltaX * 120;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double t = xToTime(e.position.x);
        pps *= std::exp(w.deltaY * 0.8);
        clampView();
        sx = t * pps - (e.position.x - kKeyW);
    }
    else if (e.mods.isAltDown())
    {
        const double visH = getHeight() - kRulerH;
        const double centre = 127 - (sy + visH / 2) / rowH;
        rowH *= std::exp(w.deltaY * 0.6);
        clampView();
        sy = (127 - centre) * rowH - visH / 2;
    }
    else if (e.mods.isShiftDown())
        sx += dy != 0 ? dy : dx;
    else
    {
        sx += dx;
        sy += dy;
    }
    clampView();
    repaint();
}

void PianoRoll::mouseMagnify(const juce::MouseEvent& e, float scale)
{
    const double t = xToTime(e.position.x);
    pps *= scale;
    clampView();
    sx = t * pps - (e.position.x - kKeyW);
    clampView();
    repaint();
}

void PianoRoll::deleteSelection()
{
    if (selected.empty()) return;
    proc.beginEdit();
    auto& notes = proc.editNotes();
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const ml::Note& n) { return selected.count(n.id) > 0; }),
                notes.end());
    selected.clear();
    proc.endEdit();
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PianoRoll::selectAll()
{
    for (const auto& n : proc.getNotes()) selected.insert(n.id);
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

bool PianoRoll::keyPressed(const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) proc.redo(); else proc.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { proc.redo(); return true; }
    if (mods.isCommandDown() && (code == 'A' || code == 'a')) { selectAll(); return true; }
    if (code == juce::KeyPress::escapeKey) { selected.clear(); repaint(); if (onSelectionChanged) onSelectionChanged(); return true; }
    if (selected.empty()) return false;
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }

    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    if (!vertical && !horizontal) return false;
    proc.beginEdit();
    auto& notes = proc.editNotes();
    if (vertical)
    {
        const int d = (code == juce::KeyPress::upKey ? 1 : -1) * (mods.isShiftDown() ? 12 : 1);
        for (auto& n : notes) if (selected.count(n.id)) n.pitch = juce::jlimit(0, 127, n.pitch + d);
    }
    else
    {
        const double g = gridSec();
        double step = (g > 0 ? g : 0.01) * (code == juce::KeyPress::rightKey ? 1 : -1);
        double minStart = 1e9;
        for (const auto& n : notes) if (selected.count(n.id)) minStart = std::min(minStart, n.start);
        step = std::max(step, -minStart);
        for (auto& n : notes) if (selected.count(n.id)) { n.start += step; n.end += step; }
    }
    proc.endEdit();
    if (onSelectionChanged) onSelectionChanged();
    repaint();
    return true;
}

juce::String PianoRoll::selectionSummary() const
{
    std::vector<const ml::Note*> sel;
    for (const auto& n : proc.getNotes()) if (selected.count(n.id)) sel.push_back(&n);
    if (sel.empty()) return {};
    if (sel.size() > 1) return juce::String((int) sel.size()) + " notes selected";
    const auto& n = *sel[0];
    const double beat = 60.0 / std::max(20.0, proc.getHostBpm());
    const double beats = n.start / beat;
    const int bpb = std::max(1, proc.getTimeSigNum());
    const juce::String pos = juce::String((int) (beats / bpb) + 1) + "." + juce::String((int) std::fmod(beats, bpb) + 1)
                             + "." + juce::String((int) (std::fmod(beats, 1.0) * 4) + 1);
    return juce::String(ml::noteName(n.pitch)) + "  bar " + pos + "  " + juce::String(juce::roundToInt((n.end - n.start) * 1000))
           + " ms  vel " + juce::String(n.vel);
}
