/*
    KeyStepPro Studio: phase 2, pattern editor.
    Open a backup file (or read a slot from the KeyStep), pick track and pattern, click notes on the grid or record over
    them (Rec: notes played on the KeyStep while its sequencer runs are added to the pattern shown), and write
    the project to a test slot (10..16) with a backup of the slot first and a read-back after. Slots 1..9 and the
    global settings are not offered by this window.
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include "Host/PluginList.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "Host/Engine.h"
#include "KspStyle.h"
#include "FxWindow.h"
#include "LibraryWindow.h"
#include "GeneratorWindow.h"
#include "ProjectMapWindow.h"
#include "Gen/Ambient.h"
#include "MidiMonitor.h"
#include "PluginTable.h"
#include "Studio/Library.h"
#include "Ksp/Device.h"
#include "Model/Overdub.h"
#include "Model/Pattern.h"
#include "Model/NoteMove.h"

namespace
{

// The KeyStep's memory is a volatile working copy of the library: all 16 slots can be loaded (with a backup and a warning first)
constexpr int kFirstWritableSlot = 1, kLastWritableSlot = 16;
constexpr int kRows = 24, kCellW = 18, kCellH = 14, kKeyW = 44, kRulerH = 18;

/** Is the pitch class in the pattern's scale (0 chromatic .. 6 blues)? Other scales (user, unused) mark nothing. */
bool inScale (int pitch, int root, int scale)
{
    static const int masks[] { 0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x9AD, 0x4E9 };   // bit i = semitone i above the root
    if (scale < 1 || scale > 6)
        return false;
    return (masks[scale] >> (((pitch - root) % 12 + 12) % 12) & 1) != 0;
}

bool isBlackKey (int pitch) { const int pc = pitch % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

// a note's time shift (0..99, 49 = on the step) as % of a step, and its randomness (the chance it plays) in %
juce::String shiftText (int v) { const int pct = v - ksp::model::kShiftCentre; return (pct > 0 ? "+" : "") + juce::String (pct) + "%"; }
juce::String randomText (int v) { return juce::String (v) + "%"; }

//==============================================================================
/** Steps across, two octaves of pitches down; click a cell to add or remove a note. */
/** The hover card of the grid: everything the KeyStep keeps about a note and its step, drawn next to the mouse. */
class NoteCard : public juce::Component
{
public:
    struct Info
    {
        bool hasNote = false;
        int pitch = 60, velocity = 0, gate = 0, shift = 0, random = 0;
        int step = 0, length = 16, passes = 15, divisionBits = 2, track = 1, pattern = 1;
        bool stepOn = false, beyondLast = false;
        std::vector<int> chord;   // every pitch of the step, lowest first
    };

    NoteCard() { setInterceptsMouseClicks (false, false); }

    static constexpr int kArrowW = 10;

    /** `arrowOnLeft`: the card is to the right of its step and the arrow is on its left edge, `arrowAtY` rows down from the card's top. */
    void show (const Info& i, bool arrowOnLeft, int arrowAtY)
    {
        info = i;
        leftArrow = arrowOnLeft;
        arrowY = arrowAtY;
        setSize (240 + kArrowW, heightFor (i));
    }

    static int heightFor (const Info& i) { return 14 + 50 + 34 + 2 * 22 + 22 + 22 + (i.chord.size() > 1 ? 22 : 0) + (i.beyondLast ? 20 : 0) + 8; }

    void paint (juce::Graphics& g) override
    {
        const auto panel = juce::Colour (0xf2171a20), border = juce::Colour (0xff3a4660), dim = juce::Colour (0xff9aa3b5);
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const float ay = static_cast<float> (juce::jlimit (14, getHeight() - 14, arrowY));
        juce::Path tip;
        if (leftArrow)
        {
            bounds.removeFromLeft (static_cast<float> (kArrowW));
            tip.addTriangle (0.5f, ay, bounds.getX() + 0.5f, ay - 8.0f, bounds.getX() + 0.5f, ay + 8.0f);
        }
        else
        {
            bounds.removeFromRight (static_cast<float> (kArrowW));
            tip.addTriangle (static_cast<float> (getWidth()) - 0.5f, ay, bounds.getRight() - 0.5f, ay - 8.0f, bounds.getRight() - 0.5f, ay + 8.0f);
        }
        g.setColour (panel);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.fillPath (tip);
        g.setColour (border);
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        g.strokePath (tip, juce::PathStrokeType (1.0f));
        g.setColour (panel);   // hides the border line where the arrow joins the card
        g.fillRect (leftArrow ? bounds.getX() - 0.5f : bounds.getRight() - 1.5f, ay - 7.0f, 2.0f, 14.0f);

        int y = 10;
        const int bodyLeft = leftArrow ? kArrowW : 0;
        const int x = 12 + bodyLeft, w = getWidth() - 24 - kArrowW;

        // the note: a large name, its MIDI number and frequency; who plays it
        g.setColour (juce::Colour (0xff7bd88f));
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (info.hasNote ? juce::String (ksp::noteName (info.pitch)) : juce::String ("-"), x, y, 90, 30, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        if (info.hasNote)
        {
            const double hz = 440.0 * std::pow (2.0, (info.pitch - 69) / 12.0);
            g.drawText ("MIDI " + juce::String (info.pitch), x + 92, y + 2, w - 92, 16, juce::Justification::centredLeft);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (hz, hz < 100.0 ? 2 : 1) + " Hz", x + 92, y + 17, w - 92, 14, juce::Justification::centredLeft);
        }
        else
        {
            g.drawText ("empty step", x + 92, y + 2, w - 92, 16, juce::Justification::centredLeft);
        }
        y += 34;
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Track " + juce::String (info.track) + "  |  pattern " + juce::String (info.pattern) + "  |  MIDI channel " + juce::String (info.track),
                    x, y, w, 14, juce::Justification::centredLeft);
        y += 20;

        // the step: its number, its page of 16 and its beat
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("STEP " + juce::String (info.step + 1) + " of " + juce::String (info.length), x, y, 110, 16, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        juce::String when = "page " + juce::String (info.step / 16 + 1);
        if (info.divisionBits <= 2)
        {
            const int perBeat = info.divisionBits == 0 ? 1 : info.divisionBits == 1 ? 2 : 4;
            when += "  |  beat " + juce::String (info.step / perBeat + 1) + (perBeat > 1 ? "." + juce::String (info.step % perBeat + 1) : juce::String());
        }
        g.drawText (when, x + 110, y, w - 110, 16, juce::Justification::centredRight);
        y += 20;
        if (info.beyondLast)
        {
            g.setColour (juce::Colour (0xffffb347));
            g.drawText ("after the last step: the KeyStep does not play it", x, y, w, 16, juce::Justification::centredLeft);
            y += 20;
        }

        if (info.hasNote)
        {
            bar (g, x, y, w, "Velocity", info.velocity, juce::Colour (0xffe0a458));
            y += 22;
            bar (g, x, y, w, "Gate", info.gate, juce::Colour (0xff6fa8dc));
            y += 22;
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Time shift", x, y, 70, 16, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white);
            g.drawText (shiftText (info.shift), x + 70, y, 44, 16, juce::Justification::centredLeft);
            g.setColour (dim);
            g.drawText ("Random", x + 118, y, 56, 16, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white);
            g.drawText (randomText (info.random), x + 176, y, 40, 16, juce::Justification::centredLeft);
            y += 22;
        }

        // the passes in which the step plays (the KeyStep's 4-bit pass mask) and whether it is on
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (info.stepOn ? "Plays on pass" : "Step is off", x, y, 90, 16, juce::Justification::centredLeft);
        for (int k = 0; k < 4; ++k)
        {
            const bool on = (info.passes & (1 << k)) != 0;
            const auto r = juce::Rectangle<float> (static_cast<float> (x + 96 + k * 26), static_cast<float> (y), 22.0f, 16.0f);
            g.setColour (on ? juce::Colour (0xff7bd88f) : juce::Colour (0xff2b2f38));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (on ? juce::Colours::black : dim);
            g.drawText (juce::String (k + 1), r.toNearestInt(), juce::Justification::centred);
        }
        y += 22;

        if (info.chord.size() > 1)
        {
            g.setColour (dim);
            g.drawText ("Notes in the step", x, y, 100, 16, juce::Justification::centredLeft);
            juce::String names;
            for (int p : info.chord)
                names += (names.isEmpty() ? "" : "  ") + juce::String (ksp::noteName (p));
            g.setColour (juce::Colours::white);
            g.drawText (names, x + 104, y, w - 104, 16, juce::Justification::centredLeft);
        }
    }

private:
    static void bar (juce::Graphics& g, int x, int y, int w, const juce::String& label, int value, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff9aa3b5));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, x, y, 58, 16, juce::Justification::centredLeft);
        const auto track = juce::Rectangle<float> (static_cast<float> (x + 60), static_cast<float> (y + 5), static_cast<float> (w - 60 - 38), 6.0f);
        g.setColour (juce::Colour (0xff2b2f38));
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (c);
        g.fillRoundedRectangle (track.withWidth (std::max (3.0f, track.getWidth() * static_cast<float> (value) / 127.0f)), 3.0f);
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (value), x + w - 34, y, 34, 16, juce::Justification::centredRight);
    }

    Info info;
    bool leftArrow = true;
    int arrowY = 20;
};

/** A knob like the KeyStep's: a dark body with a pointer, a ring of LEDs lit red up to the value (or from `centre` to it),
    its name below (the value while dragging). Values are 0..maxValue as stored; `format` turns one into the text shown.
    Drag up / down or use the wheel. */
class KspKnob : public juce::Component
{
public:
    std::function<void (int)> onChange;   // by the user only

    explicit KspKnob (juce::String n, int maxV = 127, int centreV = 0, std::function<juce::String (int)> fmt = {})
        : name (std::move (n)), maxValue (maxV), centre (centreV), format (std::move (fmt)) {}

    void setValue (int v, bool enabledNow)
    {
        v = juce::jlimit (0, maxValue, v);
        if (v != value || enabledNow != active)
        {
            value = v;
            active = enabledNow;
            repaint();
        }
    }

    int getValue() const { return value; }

    void paint (juce::Graphics& g) override
    {
        const float labelH = 16.0f;
        auto area = getLocalBounds().toFloat();
        auto label = area.removeFromBottom (labelH);
        const float d = std::min (area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float ringR = d * 0.5f - 3.0f, bodyR = ringR * 0.66f;
        const int leds = 11;
        const float a0 = juce::degreesToRadians (-135.0f), sweep = juce::degreesToRadians (270.0f);
        const int lit = juce::roundToInt (value / static_cast<float> (maxValue) * (leds - 1));
        const int from = juce::roundToInt (centre / static_cast<float> (maxValue) * (leds - 1));
        for (int i = 0; i < leds; ++i)
        {
            const float a = a0 + sweep * i / (leds - 1);
            const juce::Point<float> p (c.x + std::sin (a) * ringR, c.y - std::cos (a) * ringR);
            const bool on = active && (centre > 0 ? i >= std::min (from, lit) && i <= std::max (from, lit)
                                                  : i <= lit && (value > 0 || i == 0));
            if (on)
            {
                g.setColour (juce::Colour (0xffff2a2a).withAlpha (0.25f));
                g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            }
            g.setColour (on ? juce::Colour (0xffff3b3b) : juce::Colour (0xffb9bfc5));
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d55), c.x - bodyR * 0.4f, c.y - bodyR * 0.5f,
                                                 juce::Colour (0xff0e0f12), c.x + bodyR * 0.5f, c.y + bodyR * 0.7f, true));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
        g.setColour (juce::Colour (0xff08090b));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.5f);
        const float a = a0 + sweep * value / static_cast<float> (maxValue);
        g.setColour (active ? juce::Colours::white : juce::Colour (0xff80848c));
        g.drawLine (c.x + std::sin (a) * bodyR * 0.35f, c.y - std::cos (a) * bodyR * 0.35f,
                    c.x + std::sin (a) * bodyR * 0.9f, c.y - std::cos (a) * bodyR * 0.9f, 2.5f);
        g.setColour (ksp::ui::palette::ink);
        g.setFont (11.0f);
        g.drawText (dragging ? (format ? format (value) : juce::String (value)) : name, label.toNearestInt(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = active;
        dragStartValue = value;
        dragStartY = e.y;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (active)
            change (dragStartValue + (e.mods.isShiftDown() ? (dragStartY - e.y) / 3 : (dragStartY - e.y) * 2));   // shift = fine
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (active)
            change (value + (w.deltaY > 0 ? 1 : -1));
    }

private:
    juce::String name;
    int maxValue = 127, centre = 0;
    std::function<juce::String (int)> format;
    int value = 0, dragStartValue = 0, dragStartY = 0;
    bool active = false, dragging = false;

    void change (int v)
    {
        v = juce::jlimit (0, maxValue, v);
        if (v == value)
            return;
        value = v;
        repaint();
        if (onChange)
            onChange (v);
    }
};

class PatternGrid : public juce::Component
{
public:
    std::function<void()> onEdited;
    std::function<void (int, int)> onNoteAdded;   // a click added a note (pitch, velocity): it is heard
    std::function<void()> onSelected;       // the selected note changed (or went away): the knobs follow it
    std::function<void (int)> onLastStep;   // the ruler was clicked or dragged: the new length (last step), 1..64

    void setTrack (int t) { trackIndex = t; repaint(); }
    /** Which track and pattern the grid shows (for the hover card). */
    void setContext (int track, int patternNumber)
    {
        if (track != trackNumber || patternNumber != patternNo)
        {
            selStep = selPitch = -1;
            sel.clear();
            history.clear();
        }
        trackNumber = track;
        patternNo = patternNumber;
    }

    PatternGrid()
    {
        setWantsKeyboardFocus (true);
        addChildComponent (card);
        card.setAlwaysOnTop (true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setEndHover (onEndHandle (e.x, e.y));
        updateCard (e);
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        setEndHover (false);
        hideCard();
    }

    void setPattern (ksp::model::Pattern* p, int lowest)
    {
        pattern = p;
        lowPitch = lowest;
        const bool had = selStep >= 0;
        if (selectedNote() == nullptr)
            selStep = selPitch = -1;
        pruneSelection();
        if (had && selStep < 0 && onSelected)
            onSelected();
        // all 64 steps are always shown: the ones after the last step are dimmed, the last one is marked
        setSize (kKeyW + kCellW * ksp::model::kSteps, kRulerH + kCellH * kRows);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ksp::ui::palette::gridA);
        if (pattern == nullptr)
            return;
        const int len = juce::jlimit (1, ksp::model::kSteps, pattern->length);
        const int endX = kKeyW + len * kCellW;
        const auto accent = ksp::ui::palette::panelDark;

        for (int r = 0; r < kRows; ++r)
        {
            const int pitch = lowPitch + kRows - 1 - r;
            const int y = kRulerH + r * kCellH;
            g.setColour (isBlackKey (pitch) ? ksp::ui::palette::gridB : ksp::ui::palette::gridA);
            g.fillRect (kKeyW, y, getWidth() - kKeyW, kCellH);
            if (isBlackKey (pitch))
            {
                g.setColour (ksp::ui::palette::panelDark);
                g.fillRect (0, y, kKeyW - 2, kCellH - 1);
            }
            else
            {
                g.setColour (juce::Colours::white);
                g.fillRect (0, y, kKeyW - 2, kCellH - 1);
                g.setColour (ksp::ui::palette::gridLine);
                g.drawRect (0, y, kKeyW - 2, kCellH, 1);
            }
            // rows inside the pattern's scale get a green strip on the key and a light tint on the row
            if (inScale (pitch, pattern->root, pattern->scale))
            {
                g.setColour (juce::Colour (0xff3ea05a).withAlpha (0.14f));
                g.fillRect (kKeyW, y, getWidth() - kKeyW, kCellH);
                g.setColour (juce::Colour (0xff3ea05a));
                g.fillRect (kKeyW - 5, y + 1, 3, kCellH - 2);
            }
            // every key is named; the Cs are bold
            g.setColour (isBlackKey (pitch) ? juce::Colours::white : juce::Colours::black);
            g.setFont (juce::FontOptions (10.0f, pitch % 12 == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (ksp::noteName (pitch), 3, y, kKeyW - 9, kCellH, juce::Justification::centredLeft);
        }

        // the last step: a tint on its column
        g.setColour (palette_track().withAlpha (0.12f));
        g.fillRect (endX - kCellW, kRulerH, kCellW, getHeight() - kRulerH);

        // grid lines: every 4 steps a beat, every 16 a bar of the KeyStep's step pages
        for (int st = 0; st <= ksp::model::kSteps; ++st)
        {
            g.setColour (st % 16 == 0 ? juce::Colour (0xff7d868e) : st % 4 == 0 ? juce::Colour (0xffa5adb4) : ksp::ui::palette::gridLine);
            g.fillRect (kKeyW + st * kCellW, kRulerH, 1, getHeight() - kRulerH);
        }

        // notes (after the last step they are kept but dimmed: the KeyStep does not play them)
        for (int st = 0; st < ksp::model::kSteps; ++st)
        {
            g.setColour (st < len ? palette_track() : palette_track().withAlpha (0.35f));
            for (const auto& n : pattern->steps[static_cast<std::size_t> (st)].notes)
            {
                const int r = lowPitch + kRows - 1 - n.pitch;
                if (r >= 0 && r < kRows)
                    g.fillRoundedRectangle (static_cast<float> (kKeyW + st * kCellW + 2), static_cast<float> (kRulerH + r * kCellH + 1),
                                            kCellW - 3.0f, kCellH - 2.0f, 2.0f);
                g.setColour (ksp::ui::palette::panelDark);
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText (ksp::noteName (n.pitch), kKeyW + st * kCellW, kRulerH + r * kCellH, kCellW, kCellH, juce::Justification::centred, false);
                g.setColour (st < len ? palette_track() : palette_track().withAlpha (0.35f));
            }
        }

        auto outline = [&] (int step, int pitch, float thickness)
        {
            const int r = lowPitch + kRows - 1 - pitch;
            if (r >= 0 && r < kRows)
                g.drawRoundedRectangle (static_cast<float> (kKeyW + step * kCellW) + 1.0f, static_cast<float> (kRulerH + r * kCellH) + 0.5f,
                                        kCellW - 1.0f, kCellH - 1.0f, 2.0f, thickness);
        };
        g.setColour (juce::Colour (0xffff3b3b).withAlpha (0.7f));
        for (const auto& r : sel)
            outline (r.step, r.pitch, 1.5f);
        if (selectedNote() != nullptr)
        {
            g.setColour (juce::Colour (0xffff3b3b));
            outline (selStep, selPitch, 2.0f);
        }
        if (! marquee.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (marquee);
            g.setColour (juce::Colours::white);
            g.drawRect (marquee, 1);
        }
        if (hoverStep >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (kKeyW + hoverStep * kCellW, kRulerH, kCellW, getHeight() - kRulerH);
            const int r = lowPitch + kRows - 1 - hoverPitch;
            if (r >= 0 && r < kRows)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (static_cast<float> (kKeyW + hoverStep * kCellW) + 1.0f, static_cast<float> (kRulerH + r * kCellH) + 0.5f,
                                        kCellW - 1.0f, kCellH - 1.0f, 2.0f, 1.5f);
            }
        }

        // dim everything after the last step, then the marker
        g.setColour (ksp::ui::palette::chassis.withAlpha (0.6f));
        g.fillRect (endX, kRulerH, getWidth() - endX, getHeight() - kRulerH);
        const bool grabbed = endHover || drag == Drag::end;
        g.setColour (grabbed ? palette_track() : accent);
        g.fillRect (endX - (grabbed ? 2 : 1), kRulerH, grabbed ? 5 : 3, getHeight() - kRulerH);

        // the ruler: step numbers, the used part brighter, and the "last step" flag
        g.setColour (ksp::ui::palette::panelDark);
        g.fillRect (0, 0, getWidth(), kRulerH);
        g.setColour (juce::Colour (0xff3a3d44));
        g.fillRect (kKeyW, 0, len * kCellW, kRulerH);
        g.setFont (9.0f);
        for (int st = 0; st < ksp::model::kSteps; st += 4)
        {
            g.setColour (st < len ? juce::Colours::white : juce::Colours::grey);
            g.drawText (juce::String (st + 1), kKeyW + st * kCellW + 2, 0, kCellW * 4, kRulerH, juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::grey);
        g.drawText ("last step", 2, 0, kKeyW - 2, kRulerH, juce::Justification::centredLeft);
        const int flagW = 36;
        g.setColour (grabbed ? palette_track().brighter (0.4f) : palette_track());
        g.fillRect (endX - flagW, 1, flagW, kRulerH - 2);
        if (grabbed)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (endX - flagW, 1, flagW, kRulerH - 2, 2);
        }
        g.setColour (ksp::ui::palette::panelDark);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("END " + juce::String (len), endX - flagW, 1, flagW, kRulerH - 2, juce::Justification::centred);

        // the grip: always visible on the END line, three bars between two arrows (drag it left or right)
        const auto grip = gripBox();
        g.setColour (grabbed ? palette_track().brighter (0.4f) : palette_track());
        g.fillRoundedRectangle (grip.toFloat(), 4.0f);
        g.setColour (grabbed ? juce::Colours::white : ksp::ui::palette::panelDark);
        g.drawRoundedRectangle (grip.toFloat().reduced (0.5f), 4.0f, 1.5f);
        for (int i = -1; i <= 1; ++i)
            g.fillRect (grip.getCentreX() + i * 4 - 1, grip.getY() + 8, 2, grip.getHeight() - 16);
        const float cy = static_cast<float> (grip.getCentreY());
        juce::Path tri;
        tri.addTriangle (static_cast<float> (grip.getX() - 2), cy, static_cast<float> (grip.getX() - 8), cy - 5.0f, static_cast<float> (grip.getX() - 8), cy + 5.0f);
        tri.addTriangle (static_cast<float> (grip.getRight() + 2), cy, static_cast<float> (grip.getRight() + 8), cy - 5.0f, static_cast<float> (grip.getRight() + 8), cy + 5.0f);
        g.setColour (grabbed ? juce::Colours::white : ksp::ui::palette::panelDark);
        g.fillPath (tri);
    }

    /** The grip box, centred on the END line half way down the grid. */
    juce::Rectangle<int> gripBox() const
    {
        const int endX = kKeyW + juce::jlimit (1, ksp::model::kSteps, pattern->length) * kCellW;
        return juce::Rectangle<int> (22, 38).withCentre ({ endX, kRulerH + kCellH * kRows / 2 });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (pattern == nullptr || e.x < kKeyW)
            return;
        grabKeyboardFocus();
        drag = Drag::none;
        if (onEndHandle (e.x, e.y))   // grab the END flag or its line: drag to change the length
        {
            drag = Drag::end;
            endGrab = kKeyW + pattern->length * kCellW - e.x;
            repaint();
            return;
        }
        if (e.y < kRulerH)
        {
            drag = Drag::ruler;
            setLastStep (e);
            return;
        }
        const int s = (e.x - kKeyW) / kCellW, r = (e.y - kRulerH) / kCellH;
        if (s < 0 || s >= ksp::model::kSteps || r < 0 || r >= kRows)
            return;
        const int pitch = lowPitch + kRows - 1 - r;
        auto& step = pattern->steps[static_cast<std::size_t> (s)];
        auto it = std::find_if (step.notes.begin(), step.notes.end(), [=] (const auto& n) { return n.pitch == pitch; });
        const ksp::model::NoteRef ref { s, pitch };
        if (! e.mods.isPopupMenu())
        {
            const bool toggle = e.mods.isShiftDown() || e.mods.isCommandDown();
            if (it != step.notes.end())   // on a note: select it (Shift / Ctrl adds or removes), and drag moves the selection
            {
                if (toggle)
                {
                    const auto at = std::find (sel.begin(), sel.end(), ref);
                    if (at != sel.end())
                        sel.erase (at);
                    else
                        sel.push_back (ref);
                    selStep = sel.empty() ? -1 : sel.back().step;
                    selPitch = sel.empty() ? -1 : sel.back().pitch;
                    if (onSelected)
                        onSelected();
                }
                else
                {
                    if (std::find (sel.begin(), sel.end(), ref) == sel.end())
                        select (s, pitch);
                    else
                    {
                        selStep = s;
                        selPitch = pitch;
                        if (onSelected)
                            onSelected();
                    }
                    drag = Drag::notes;
                    dragBase = *pattern;
                    dragSel = sel;
                    dragFrom = ref;
                    dragCopy = e.mods.isAltDown();
                    dragMoved = false;
                    lastDP = 0;
                }
            }
            else if (e.mods.isShiftDown())   // Shift on an empty cell: a rectangle selects the notes it touches
            {
                drag = Drag::marquee;
                marqueeFrom = e.getPosition();
                marquee = {};
                sel.clear();
                selStep = selPitch = -1;
                if (onSelected)
                    onSelected();
            }
            else   // an empty cell: add the note
            {
                if (static_cast<int> (step.notes.size()) >= ksp::model::kMaxStepNotes)
                    return;
                const auto before = *pattern;
                ksp::model::Note n = pattern->defaults;
                n.pitch = pitch;
                if (n.gate == 0) n.gate = 64;
                if (n.velocity == 0) n.velocity = 100;
                step.notes.push_back (n);
                step.on = true;
                pushHistory (before);
                if (onEdited)
                    onEdited();
                if (onNoteAdded)
                    onNoteAdded (n.pitch, n.velocity);
                select (s, pitch);
            }
        }
        else if (it != step.notes.end())   // right click: delete the note
            deleteNotes ({ ref });
        repaint();
        updateCard (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pattern == nullptr)
            return;
        if (drag == Drag::end)   // the marker follows the mouse, snapped to the nearest step line
        {
            const int n = juce::jlimit (1, ksp::model::kSteps, juce::roundToInt ((e.x + endGrab - kKeyW) / static_cast<float> (kCellW)));
            if (n != pattern->length && onLastStep)
                onLastStep (n);
        }
        else if (drag == Drag::ruler)
            setLastStep (e);
        else if (drag == Drag::notes)
            dragNotes (e);
        else if (drag == Drag::marquee)
        {
            marquee = juce::Rectangle<int> (marqueeFrom, e.getPosition()).getIntersection (juce::Rectangle<int> (kKeyW, kRulerH, getWidth() - kKeyW, kCellH * kRows));
            sel.clear();
            for (int st = 0; st < ksp::model::kSteps; ++st)
                for (const auto& n : pattern->steps[static_cast<std::size_t> (st)].notes)
                {
                    const int r = lowPitch + kRows - 1 - n.pitch;
                    if (r >= 0 && r < kRows
                        && marquee.intersects (juce::Rectangle<int> (kKeyW + st * kCellW, kRulerH + r * kCellH, kCellW, kCellH)))
                        sel.push_back ({ st, n.pitch });
                }
            selStep = sel.empty() ? -1 : sel.back().step;
            selPitch = sel.empty() ? -1 : sel.back().pitch;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (drag == Drag::notes && dragMoved)
        {
            pushHistory (dragBase);
            if (onEdited)
                onEdited();
        }
        else if (drag == Drag::marquee)
        {
            marquee = {};
            if (onSelected)
                onSelected();
        }
        drag = Drag::none;
        setEndHover (onEndHandle (e.x, e.y));
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        {
            undo();
            return true;
        }
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! sel.empty())
        {
            deleteNotes (sel);
            return true;
        }
        return false;
    }

    /** Undoes the last grid edit (add, delete, drag), as long as the pattern is still the one it left. */
    void undo()
    {
        if (pattern == nullptr || history.empty())
            return;
        if (! ksp::model::sameSteps (*pattern, history.back().after))
        {
            history.clear();   // the pattern was changed from elsewhere (Undo take, Generate, length ...): not ours to restore
            return;
        }
        pattern->steps = history.back().before.steps;
        history.pop_back();
        pruneSelection();
        if (onEdited)
            onEdited();
        if (onSelected)
            onSelected();
        repaint();
    }

    /** The selected note (null when none, or it is gone). */
    ksp::model::Note* selectedNote()
    {
        if (pattern == nullptr || selStep < 0)
            return nullptr;
        for (auto& n : pattern->steps[static_cast<std::size_t> (selStep)].notes)
            if (n.pitch == selPitch)
                return &n;
        return nullptr;
    }

    /** The knobs changed a note value (field 0 pitch, 1 gate, 2 velocity, 3 shift, 4 random). */
    void setSelectedValue (int field, int v)
    {
        auto* n = selectedNote();
        if (n == nullptr)
            return;
        if (field == 0)
        {
            for (const auto& o : pattern->steps[static_cast<std::size_t> (selStep)].notes)
                if (&o != n && o.pitch == v)
                    return;   // another note of the chord is already there
            n->pitch = v;
            selPitch = v;
        }
        else
            (field == 1 ? n->gate : field == 2 ? n->velocity : field == 3 ? n->shift : n->random) = v;
        repaint();
        if (onEdited)
            onEdited();
    }

    void select (int step, int pitch)
    {
        selStep = step;
        selPitch = pitch;
        sel.clear();
        if (step >= 0)
            sel.push_back ({ step, pitch });
        repaint();
        if (onSelected)
            onSelected();
    }

private:
    ksp::model::Pattern* pattern = nullptr;
    int lowPitch = 48, trackIndex = 0;
    juce::Colour palette_track() const { return ksp::ui::palette::trackColour (trackIndex); }
    enum class Drag { none, ruler, notes, marquee, end };
    Drag drag = Drag::none;
    std::vector<ksp::model::NoteRef> sel;   // every selected note; the last clicked one is also selStep / selPitch (the knobs)
    struct Edit { ksp::model::Pattern before, after; };
    std::vector<Edit> history;              // grid edits for Undo (Ctrl+Z)
    ksp::model::Pattern dragBase;           // the pattern when a note drag began; each move is applied to it again
    std::vector<ksp::model::NoteRef> dragSel;
    ksp::model::NoteRef dragFrom;
    bool dragCopy = false, dragMoved = false;
    int lastDP = 0;
    juce::Point<int> marqueeFrom;
    juce::Rectangle<int> marquee;
    bool endHover = false;
    int endGrab = 0;   // pixels from the mouse to the marker line when it was grabbed
    int trackNumber = 1, patternNo = 1;
    NoteCard card;
    int selStep = -1, selPitch = -1;       // the selected note (right click), edited by the knobs
    int hoverStep = -1, hoverPitch = -1;   // the step and note the card is about: highlighted in the grid

    /** `before` is the pattern before an edit that is done: it goes on the Undo list (if it changed anything). */
    void pushHistory (const ksp::model::Pattern& before)
    {
        if (ksp::model::sameSteps (before, *pattern))
            return;
        history.push_back ({ before, *pattern });
        if (history.size() > 30)
            history.erase (history.begin());
    }

    void pruneSelection()
    {
        if (pattern == nullptr)
        {
            sel.clear();
            return;
        }
        sel.erase (std::remove_if (sel.begin(), sel.end(), [this] (const ksp::model::NoteRef& r)
                   {
                       const auto& notes = pattern->steps[static_cast<std::size_t> (r.step)].notes;
                       return std::none_of (notes.begin(), notes.end(), [&] (const auto& n) { return n.pitch == r.pitch; });
                   }), sel.end());
    }

    void deleteNotes (const std::vector<ksp::model::NoteRef>& refs)
    {
        const auto before = *pattern;
        for (const auto& r : refs)
        {
            auto& st = pattern->steps[static_cast<std::size_t> (r.step)];
            st.notes.erase (std::remove_if (st.notes.begin(), st.notes.end(), [&] (const auto& n) { return n.pitch == r.pitch; }), st.notes.end());
            st.on = ! st.notes.empty();
        }
        pushHistory (before);
        sel.erase (std::remove_if (sel.begin(), sel.end(), [&] (const auto& r) { return std::find (refs.begin(), refs.end(), r) != refs.end(); }), sel.end());
        if (selectedNote() == nullptr)
            selStep = selPitch = -1;
        if (onEdited)
            onEdited();
        if (onSelected)
            onSelected();
        repaint();
    }

    /** Moves the selection by the cells the mouse has travelled from where it grabbed the note (Alt: copies). */
    void dragNotes (const juce::MouseEvent& e)
    {
        if (! dragMoved && e.getDistanceFromDragStart() < 4)
            return;
        hideCard();
        const int cs = static_cast<int> (std::floor ((e.x - kKeyW) / static_cast<float> (kCellW)));
        const int cr = static_cast<int> (std::floor ((e.y - kRulerH) / static_cast<float> (kCellH)));
        const int pitch = juce::jlimit (lowPitch, lowPitch + kRows - 1, lowPitch + kRows - 1 - cr);   // never to a row that is not shown
        const int ds = juce::jlimit (0, ksp::model::kSteps - 1, cs) - dragFrom.step, dp = pitch - dragFrom.pitch;

        auto next = dragBase;
        std::vector<ksp::model::NoteRef> moved = dragSel;
        if (ds != 0 || dp != 0 || dragCopy)
        {
            if (! ksp::model::moveNotes (next, dragSel, ds, dp, dragCopy, &moved))
                return;   // it does not fit there: stays where it last fitted
        }
        if (ksp::model::sameSteps (next, *pattern) && moved == sel)
            return;
        pattern->steps = next.steps;
        sel = moved;
        dragMoved = ! ksp::model::sameSteps (*pattern, dragBase);
        const auto prim = std::find (dragSel.begin(), dragSel.end(), ksp::model::NoteRef { selStep, selPitch });
        if (prim != dragSel.end() && moved.size() == dragSel.size())
        {
            const auto m = moved[static_cast<std::size_t> (prim - dragSel.begin())];
            selStep = m.step;
            selPitch = m.pitch;
        }
        if (dp != lastDP)
        {
            lastDP = dp;
            if (auto* n = selectedNote(); n != nullptr && onNoteAdded)
                onNoteAdded (n->pitch, n->velocity);   // heard as it changes pitch
        }
        repaint();
    }

    /** The END flag (in the ruler) and the marker line (grid) are the handle to drag. */
    bool onEndHandle (int x, int y) const
    {
        if (pattern == nullptr)
            return false;
        const int endX = kKeyW + juce::jlimit (1, ksp::model::kSteps, pattern->length) * kCellW;
        return y < kRulerH ? (x >= endX - 36 && x <= endX + 3)
                           : std::abs (x - endX) <= 3 || gripBox().expanded (9, 0).contains (x, y);
    }

    void setEndHover (bool h)
    {
        if (h == endHover)
            return;
        endHover = h;
        setMouseCursor (h ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void hideCard()
    {
        card.setVisible (false);
        if (hoverStep >= 0)
        {
            hoverStep = -1;
            repaint();
        }
    }

    /** The card for the step under the mouse (a note under it, or else the step's own summary); hidden over the ruler and empty steps. */
    void updateCard (const juce::MouseEvent& e)
    {
        const int st = (e.x - kKeyW) / kCellW;
        const bool inGrid = pattern != nullptr && e.x >= kKeyW && st >= 0 && st < ksp::model::kSteps && e.y >= kRulerH;
        if (! inGrid)
        {
            hideCard();
            return;
        }
        const auto& step = pattern->steps[static_cast<std::size_t> (st)];
        if (step.notes.empty())
        {
            hideCard();
            return;
        }
        NoteCard::Info info;
        info.step = st;
        info.length = pattern->length;
        info.beyondLast = st >= pattern->length;
        info.passes = step.passes;
        info.stepOn = step.on;
        info.divisionBits = ksp::model::divisionBits (*pattern);
        info.track = trackNumber;
        info.pattern = patternNo;
        for (const auto& n : step.notes)
            info.chord.push_back (n.pitch);
        std::sort (info.chord.begin(), info.chord.end());

        const ksp::model::Note* shown = nullptr;
        if (e.y < kRulerH + kCellH * kRows)
        {
            const int pitch = lowPitch + kRows - 1 - (e.y - kRulerH) / kCellH;
            for (const auto& n : step.notes)
                if (n.pitch == pitch)
                    shown = &n;
        }
        if (shown == nullptr)   // over an empty row of the step: the highest note speaks for the step
            shown = &*std::max_element (step.notes.begin(), step.notes.end(), [] (const auto& a, const auto& b) { return a.pitch < b.pitch; });
        info.hasNote = true;
        info.pitch = shown->pitch;
        info.velocity = shown->velocity;
        info.gate = shown->gate;
        info.shift = shown->shift;
        info.random = shown->random;

        // the card sits beside the step with its arrow on the note: to the right of the step, or to its left near the end of the grid
        const int stepLeft = kKeyW + st * kCellW, stepRight = stepLeft + kCellW;
        const int row = lowPitch + kRows - 1 - shown->pitch;
        const int anchorY = row >= 0 && row < kRows ? kRulerH + row * kCellH + kCellH / 2 : e.y;
        const bool onRight = stepRight + 6 + 240 + NoteCard::kArrowW <= getWidth() - 4;
        card.show (info, onRight, 0);
        int y = juce::jlimit (kRulerH, juce::jmax (kRulerH, getHeight() - card.getHeight() - 4), anchorY - 28);
        card.show (info, onRight, anchorY - y);
        card.setTopLeftPosition (onRight ? stepRight + 2 : stepLeft - card.getWidth() - 2, y);
        card.setVisible (true);
        if (hoverStep != st || hoverPitch != shown->pitch)
        {
            hoverStep = st;
            hoverPitch = shown->pitch;
            repaint();
        }
    }

    void setLastStep (const juce::MouseEvent& e)
    {
        const int n = juce::jlimit (1, ksp::model::kSteps, (e.x - kKeyW) / kCellW + 1);
        if (n != pattern->length && onLastStep)
            onLastStep (n);
    }

};

//==============================================================================
/** Covers the whole window while the KeyStep is being read or written: swallows every click and key, shows what is happening and a progress bar. */
class BusyOverlay : public juce::Component
{
public:
    BusyOverlay() : bar (progress)
    {
        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (bar);
        addAndMakeVisible (text);
        text.setJustificationType (juce::Justification::centred);
        text.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        text.setColour (juce::Label::textColourId, juce::Colours::black);
        setVisible (false);
    }

    void begin (const juce::String& what)
    {
        progress = -1.0;   // indeterminate until the first step arrives
        text.setText (what, juce::dontSendNotification);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }
    void update (const juce::String& what, double x) { progress = juce::jlimit (0.0, 1.0, x); text.setText (what, juce::dontSendNotification); }
    void end() { setVisible (false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.45f));
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (panel().toFloat(), 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (panel().toFloat(), 8.0f, 1.5f);
    }
    void resized() override
    {
        auto r = panel().reduced (20, 18);
        text.setBounds (r.removeFromTop (28));
        r.removeFromTop (10);
        bar.setBounds (r.removeFromTop (22));
    }
    bool keyPressed (const juce::KeyPress&) override { return true; }

private:
    juce::Rectangle<int> panel() const { return getLocalBounds().withSizeKeepingCentre (420, 110); }
    double progress = -1.0;
    juce::ProgressBar bar;
    juce::Label text;
};

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent()
    {
        for (auto* b : { &openButton, &readButton, &syncButton, &octDown, &octUp, &copyButton, &recButton, &undoTakeButton, &toolsButton, &libraryButton, &saveButton, &saveAsButton })
            addAndMakeVisible (*b);
        for (auto* c : { &projectBox, &patternBox, &lengthBox, &rootBox, &scaleBox, &copyTrackBox, &copyPatBox, &recChannelBox, &stepBox })
            addAndMakeVisible (*c);
        for (auto* l : { &status, &octLabel, &sectionLabel })
            addAndMakeVisible (*l);
        addAndMakeVisible (viewport);
        addChildComponent (busyOverlay);
        addAndMakeVisible (trackTabs);
        addAndMakeVisible (lenDisplay);
        addAndMakeVisible (bpmDisplay);
        lenDisplay.setCaption ("STEPS");
        bpmDisplay.setCaption ("BPM");
        bpmDisplay.setText ("---");
        viewport.setViewedComponent (&grid, false);
        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible (knobs[static_cast<std::size_t> (i)]);
            knobs[static_cast<std::size_t> (i)].onChange = [this, i] (int v) { grid.setSelectedValue (i, v); syncKnobs(); };
        }
        grid.onSelected = [this] { syncKnobs(); };

        openButton.setButtonText ("Open backup...");
        readButton.setButtonText ("Read slot");
        octDown.setButtonText ("-");
        octUp.setButtonText ("+");
        copyButton.setButtonText ("Copy to");
        toolsButton.setButtonText ("Tools");
        libraryButton.setButtonText ("Library...");
        saveButton.setButtonText ("Save");
        saveAsButton.setButtonText ("Save as...");
        recButton.setButtonText ("Rec");
        undoTakeButton.setButtonText ("Undo take");
        sectionLabel.setText ("Project / Track / Pattern / Length / Root / Scale", juce::dontSendNotification);
        sectionLabel.setFont (juce::FontOptions (11.0f));

        for (int p = 1; p <= 16; ++p) projectBox.addItem ("Project " + juce::String (p), p);
        for (int t = 1; t <= 4; ++t) trackBox.addItem ("Track " + juce::String (t), t);
        for (int n = 1; n <= 64; ++n) lengthBox.addItem (juce::String (n) + " steps", n);
        for (int pc = 0; pc < 12; ++pc) rootBox.addItem (ksp::pitchClassName (pc), pc + 1);
        for (int sc : { 0, 1, 2, 3, 4, 5, 6, 8, 9 }) scaleBox.addItem (ksp::scaleName (sc), sc + 1);
        for (int t = 1; t <= 4; ++t) copyTrackBox.addItem ("Track " + juce::String (t), t);
        for (int n = 1; n <= 16; ++n) copyPatBox.addItem ("Pattern " + juce::String (n), n);
        for (int c = 1; c <= 16; ++c) recChannelBox.addItem ("Ch " + juce::String (c), c);
        for (auto [name, clocks] : { std::pair { "1/4", 24 }, { "1/8", 12 }, { "1/8T", 8 }, { "1/16", 6 }, { "1/16T", 4 }, { "1/32", 3 } })
            stepBox.addItem (name, clocks);
        recChannelBox.setSelectedId (1, juce::dontSendNotification);
        stepBox.setSelectedId (6, juce::dontSendNotification);
        copyTrackBox.setSelectedId (1, juce::dontSendNotification);
        copyPatBox.setSelectedId (2, juce::dontSendNotification);
        projectBox.setSelectedId (1, juce::dontSendNotification);
        trackBox.setSelectedId (1, juce::dontSendNotification);
        fillPatternBox();
        setBusy (false);
        status.setText ("Open a backup file or read a project from the KeyStep.", juce::dontSendNotification);

        openButton.onClick = [this] { openBackup(); };
        readButton.onClick = [this] { readFromDevice (0); };   // read-only: always reachable, even with unsent edits
        syncButton.onClick = [this] { if (haveProject && fwReadOnly.isEmpty()) confirmWrite(); else readFromDevice(); };
        copyButton.onClick = [this] { copyPattern(); };
        toolsButton.onClick = [this] { showToolsMenu(); };
        libraryButton.onClick = [this] { showLibrary(); };
        saveButton.onClick = [this] { saveToLibrary (false); };
        saveAsButton.onClick = [this] { saveToLibrary (true); };
        recButton.onClick = [this] { if (overdub) stopRecording(); else startRecording(); };
        undoTakeButton.onClick = [this] { undoTake(); };
        octDown.onClick = [this] { lowPitch = std::max (0, lowPitch - 12); showPattern(); };
        octUp.onClick = [this] { lowPitch = std::min (96, lowPitch + 12); showPattern(); };
        projectBox.onChange = [this] { selectProject(); };
        trackTabs.onSelect = [this] (int t) { trackBox.setSelectedId (t, juce::sendNotification); };
        trackBox.onChange = [this] { trackTabs.setSelected (trackBox.getSelectedId(), false); grid.setTrack (track() - 1); refreshCards(); recChannelBox.setSelectedId (track(), juce::dontSendNotification); loadPattern(); };
        patternBox.onChange = [this] { loadPattern(); };
        lengthBox.onChange = [this] { pattern.length = lengthBox.getSelectedId(); markEdited(); showPattern(); };
        rootBox.onChange = [this] { pattern.root = rootBox.getSelectedId() - 1; markEdited(); };
        scaleBox.onChange = [this] { pattern.scale = scaleBox.getSelectedId() - 1; markEdited(); };
        grid.onEdited = [this] { markEdited(); };
        grid.onNoteAdded = [this] (int pitch, int velocity) { auditionNote (pitch, velocity); };
        grid.onLastStep = [this] (int n) { lengthBox.setSelectedId (n, juce::sendNotification); };
        buildInstrumentRow();
        setSize (1220, 756);
        buildRecordRow();
        setSize (1220, 748);
        startTimerHz (10);
    }

    ~MainComponent() override
    {
        stopTimer();
        stopScan->store (true);
        if (device != nullptr)
            device->setListener (nullptr);
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            closeEditor (t);
        mapWindow.reset();
        generatorWindow.reset();
        monitorWindow.reset();
        pluginWindow.reset();
        libraryWindow.reset();
        for (int p = 0; p < ksp::host::Engine::kFxPositions; ++p)
            fxEditorWindow[static_cast<std::size_t> (p)].reset();
        fxWindow.reset();
    }

    /** The track cards: a body in the full track colour with a border; the selected track is bright, the others dimmed. */
    void paint (juce::Graphics& g) override
    {
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            const auto c = ksp::ui::palette::trackColour (t);
            const bool sel = t == track() - 1;
            const auto card = cardRect[i].toFloat();
            if (sel)
                ksp::ui::drawGlow (g, card, c);
            g.setColour (c.interpolatedWith (ksp::ui::palette::chassis, sel ? 0.0f : 0.30f));   // full track colour
            g.fillRoundedRectangle (card, 8.0f);
            g.setColour (sel ? juce::Colours::white : c.darker (0.25f));
            g.drawRoundedRectangle (card.reduced (1.5f), 8.0f, sel ? 3.0f : 2.0f);
            const auto dot = instLabel[i].getBounds().toFloat();   // the number sits on it, so they share the box
            g.setColour (ksp::ui::palette::ink.withAlpha (sel ? 1.0f : 0.7f));
            g.fillEllipse (dot);
        }
    }

    /** The selected card full, the rest dimmed (the dB readouts follow the sliders). */
    void refreshCards()
    {
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            const float a = t == track() - 1 ? 1.0f : 0.75f;
            for (juce::Component* c : { static_cast<juce::Component*> (&instLabel[i]), static_cast<juce::Component*> (&instBox[i]),
                                        static_cast<juce::Component*> (&editButton[i]), static_cast<juce::Component*> (&muteToggle[i]), static_cast<juce::Component*> (&soloToggle[i]),
                                        static_cast<juce::Component*> (&volSlider[i]), static_cast<juce::Component*> (&dbLabel[i]) })
                c->setAlpha (a);
            volSlider[i].setColour (juce::Slider::trackColourId, ksp::ui::palette::ink);
            const double v = volSlider[i].getValue();
            dbLabel[i].setText (v <= 0.001 ? juce::String ("-inf") : juce::String (20.0 * std::log10 (v), 1) + " dB", juce::dontSendNotification);
        }
        repaint();
    }

    int cardAt (const juce::MouseEvent& e) const
    {
        const auto pos = e.getEventRelativeTo (this).getPosition();
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            if (cardRect[static_cast<std::size_t> (t)].contains (pos))
                return t + 1;
        return 0;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int t = cardAt (e); t != 0 && t != track())
            trackBox.setSelectedId (t, juce::sendNotification);
    }

    // a hand over the cards of the tracks that are not selected: clicking one selects it
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this)
        {
            const int t = cardAt (e);
            setMouseCursor (t != 0 && t != track() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
    }

    void resized() override
    {
        busyOverlay.setBounds (getLocalBounds());
        auto r = getLocalBounds().reduced (10);
        auto top = r.removeFromTop (28);
        libraryButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (4);
        saveButton.setBounds (top.removeFromLeft (60));
        top.removeFromLeft (4);
        saveAsButton.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (10);
        openButton.setBounds (top.removeFromLeft (130));
        top.removeFromLeft (6);
        projectBox.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (4);
        readButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (4);
        syncButton.setBounds (top.removeFromLeft (70));
        r.removeFromTop (6);
        auto cardsRow = r.removeFromTop (cardHeight);
        trackTabs.setVisible (false);   // the cards select the track now
        toolsButton.setBounds (top.removeFromRight (80));
        top.removeFromLeft (6);
        generateButton.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (6);
        mapButton.setBounds (top.removeFromLeft (110));
        r.removeFromTop (6);
        auto row = r.removeFromTop (28);
        for (auto* c : { &patternBox, &lengthBox, &rootBox, &scaleBox })
        {
            c->setBounds (row.removeFromLeft (c == &scaleBox ? 150 : 100));
            row.removeFromLeft (6);
        }
        octDown.setBounds (row.removeFromLeft (28));
        octLabel.setBounds (row.removeFromLeft (110));
        octUp.setBounds (row.removeFromLeft (28));
        row.removeFromLeft (16);
        copyButton.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (4);
        copyTrackBox.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (4);
        copyPatBox.setBounds (row.removeFromLeft (100));
        bpmDisplay.setBounds (row.removeFromRight (130));
        row.removeFromRight (8);
        lenDisplay.setBounds (row.removeFromRight (100));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (30);
        recButton.setBounds (bottom.removeFromLeft (60));
        bottom.removeFromLeft (4);
        recChannelBox.setBounds (bottom.removeFromLeft (76));
        bottom.removeFromLeft (4);
        stepBox.setBounds (bottom.removeFromLeft (76));
        bottom.removeFromLeft (4);
        undoTakeButton.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (10);
        status.setBounds (bottom);
        r.removeFromBottom (6);
        // four cards in a row, one per track, in the track's colour; the audio readout at the right
        auto instArea = cardsRow;
        const int w = instArea.getWidth() / 4;
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            cardRect[i] = instArea.removeFromLeft (w).withTrimmedRight (8);
            auto cell = cardRect[i].reduced (8, 6);
            auto line1 = cell.removeFromTop (26);
            cell.removeFromTop (4);
            instLabel[i].setBounds (line1.removeFromLeft (26));
            line1.removeFromLeft (6);
            instBox[i].setBounds (line1);
            soloToggle[i].setBounds (cell.removeFromRight (36));
            cell.removeFromRight (4);
            muteToggle[i].setBounds (cell.removeFromRight (36));
            cell.removeFromRight (4);
            editButton[i].setBounds (cell.removeFromRight (46));
            cell.removeFromRight (4);
            dbLabel[i].setBounds (cell.removeFromRight (54));
            volSlider[i].setBounds (cell);
        }
        r.removeFromBottom (6);
        auto recRow = r.removeFromBottom (28);
        transportLabel.setBounds (recRow.removeFromLeft (330));
        meterLabel.setBounds (recRow.removeFromLeft (260));
        recStatusLabel.setBounds (recRow);
        r.removeFromBottom (6);
        auto knobRow = r.removeFromBottom (86);
        r.removeFromBottom (4);
        for (auto& k : knobs)
        {
            k.setBounds (knobRow.removeFromLeft (84));
            knobRow.removeFromLeft (6);
        }
        viewport.setBounds (r);
    }

private:
    // ---- model
    ksp::ProjectData project;
    bool haveProject = false, edited = false, glowOn = false;
    bool unsaved = false, unwritten = false;   // changes not in the library / not on the KeyStep yet: asked about when closing
    ksp::model::Pattern pattern;
    int lowPitch = 48;
    std::unique_ptr<ksp::Device> device;
    std::optional<ksp::BackupData> backup;
    std::atomic<bool> busy { false };
    std::shared_ptr<ksp::host::PluginList> plugins = std::make_shared<ksp::host::PluginList>();
    std::shared_ptr<std::atomic<bool>> stopScan = std::make_shared<std::atomic<bool>> (false);
    std::atomic<bool> scanning { false };
    std::unique_ptr<ksp::ui::PluginWindow> pluginWindow;

    // ---- instruments: one hosted VST3 per track of the KeyStep, fed by the KeyStep's MIDI
    ksp::host::Engine engine;
    std::vector<juce::PluginDescription> instChoices;   // what the boxes offer: ids 2.. are indexes + 2
    std::array<juce::Label, 4> instLabel;
    std::array<juce::ComboBox, 4> instBox;
    std::array<juce::TextButton, 4> editButton;
    std::array<juce::ToggleButton, 4> muteToggle, soloToggle;
    std::array<juce::Slider, 4> volSlider;
    std::array<juce::Label, 4> dbLabel;
    std::array<juce::Rectangle<int>, 4> cardRect;
    static constexpr int cardHeight = 78;
    std::array<juce::String, 4> missingName;             // a plugin of the project that is not on this machine: kept untouched
    juce::TextButton toolsButton, generateButton, mapButton;
    std::unique_ptr<ksp::ui::GeneratorWindow> generatorWindow;
    std::unique_ptr<ksp::ui::ProjectMapWindow> mapWindow;
    std::optional<ksp::ProjectData> beforeGenerate;   // the project as it was before the last generation, for Undo
    std::unique_ptr<ksp::ui::MidiMonitorWindow> monitorWindow;
    std::shared_ptr<std::atomic<bool>> monitorOn = std::make_shared<std::atomic<bool>> (false);
    int recMode = 1, recTail = 3;   // WAV recording (Tools menu): 1 mix, 2 stems, 3 both; the tail is an index + 1 into the tails
    bool recFollow = true;          // start at the KeyStep's Start and end after its Stop plus the tail
    juce::Label transportLabel, recStatusLabel;
    std::unique_ptr<ksp::ui::FxWindow> fxWindow;
    std::array<std::unique_ptr<juce::DocumentWindow>, ksp::host::Engine::kFxPositions> fxEditorWindow;
    std::array<std::optional<ksp::studio::PluginSetup>, ksp::host::Engine::kFxPositions> missingFx;   // plugins of the project not on this machine
    std::array<bool, ksp::host::Engine::kFxPositions> fxBypass {};
    juce::Label meterLabel;
    std::shared_ptr<std::atomic<bool>> recordingOn = std::make_shared<std::atomic<bool>> (false);
    std::array<std::unique_ptr<juce::DocumentWindow>, 4> editorWindow;
    std::shared_ptr<ksp::studio::Library> library = std::make_shared<ksp::studio::Library> (
        std::filesystem::path (ksp::host::dataFolder().getChildFile ("Library").getFullPathName().toStdString()));
    std::unique_ptr<ksp::ui::LibraryWindow> libraryWindow;
    ksp::studio::StudioProject current;   // the library project being edited (its plugin setup and name); current.ksp is not kept up to date
    bool haveStudio = false;              // false: what is shown came from a backup or the KeyStep and is not in the library yet
    std::unique_ptr<juce::AlertWindow> nameDialog, closeDialog;
    std::optional<ksp::model::Overdub> overdub;   // while Rec is on
    ksp::model::Pattern takeBefore;              // the pattern before the last take, for Undo take
    bool haveTake = false;

    juce::TextButton libraryButton, saveButton, saveAsButton, copyButton, openButton, readButton, syncButton, octDown, octUp, recButton, undoTakeButton;
    ksp::ui::TrackTabs trackTabs;
    ksp::ui::SevenSegDisplay lenDisplay { 2 }, bpmDisplay { 5 };
    BusyOverlay busyOverlay;
    juce::String fwReadOnly;   // firmware version of a KeyStep that is connected but not tested: empty when writing is allowed
    juce::ComboBox projectBox, trackBox, patternBox, lengthBox, rootBox, scaleBox, copyTrackBox, copyPatBox, recChannelBox, stepBox;
    juce::Label status, octLabel, sectionLabel;
    juce::Viewport viewport;
    PatternGrid grid;
    std::array<KspKnob, 5> knobs { KspKnob ("Pitch"), KspKnob ("Gate"), KspKnob ("Velocity"),
                                   KspKnob ("Time Shift", ksp::model::kShiftMax, ksp::model::kShiftCentre, shiftText),
                                   KspKnob ("Randomness", ksp::model::kRandomMax, 0, randomText) };

    void syncKnobs()
    {
        const auto* n = grid.selectedNote();
        const int v[5] { n ? n->pitch : 0, n ? n->gate : 0, n ? n->velocity : 0, n ? n->shift : 0, n ? n->random : 0 };
        for (int i = 0; i < 5; ++i)
            knobs[static_cast<std::size_t> (i)].setValue (v[i], n != nullptr);
    }
    std::unique_ptr<juce::FileChooser> chooser;

    int track() const { return trackBox.getSelectedId(); }
    int patNo() const { return std::max (1, patternBox.getSelectedId()); }

    void setBusy (bool b)
    {
        busy = b;
        for (juce::Component* c : { static_cast<juce::Component*> (&openButton), static_cast<juce::Component*> (&readButton) })
            c->setEnabled (! b);
        copyButton.setEnabled (haveProject);
        recButton.setEnabled (! b && haveProject);
        undoTakeButton.setEnabled (haveTake && ! overdub);
    }

    void say (const juce::String& s) { status.setText (s, juce::dontSendNotification); }

    void noteChanged() { unsaved = unwritten = true; }

    void markEdited()
    {
        edited = true;
        noteChanged();
        say ("Edited (not written yet).");
    }

    void fillPatternBox()
    {
        const int keep = std::max (1, patternBox.getSelectedId());
        patternBox.clear (juce::dontSendNotification);
        for (int p = 1; p <= 16; ++p)
        {
            bool valid = false;
            if (haveProject)
                valid = ksp::model::readPattern (project, track(), p).valid;
            patternBox.addItem ("Pattern " + juce::String (p) + (valid ? "  *" : ""), p);
        }
        patternBox.setSelectedId (keep, juce::dontSendNotification);
    }

    /** Puts the edited pattern back into the project before switching away from it. */
    void commitPattern()
    {
        if (haveProject && edited)
        {
            const auto err = ksp::model::writePattern (project, loadedTrack, loadedPattern, pattern);
            if (! err.empty())
                say (juce::String (err));
        }
    }

    void loadPattern()
    {
        stopRecording();
        haveTake = false;
        undoTakeButton.setEnabled (false);
        commitPattern();
        refreshMap();
        loadedTrack = track();
        loadedPattern = patNo();
        fillPatternBox();
        if (! haveProject)
            return;
        pattern = ksp::model::readPattern (project, loadedTrack, loadedPattern);
        lengthBox.setSelectedId (pattern.length, juce::dontSendNotification);
        rootBox.setSelectedId (pattern.root % 12 + 1, juce::dontSendNotification);
        scaleBox.setSelectedId (pattern.scale + 1, juce::dontSendNotification);
        int lowest = 128;
        for (const auto& st : pattern.steps)
            for (const auto& n : st.notes)
                lowest = std::min (lowest, n.pitch);
        if (lowest < 128)
            lowPitch = std::min (96, lowest / 12 * 12);
        showPattern();
        say (pattern.valid ? "Pattern loaded." : "Empty pattern (default).");
        if (pattern.unreadable > 0)
            say ("Warning: " + juce::String (pattern.unreadable) + " note slot(s) could not be read from the backup.");
    }

    /** Copies the pattern shown (with its unsaved edits) over another pattern of the project; written to a slot later. */
    void copyPattern()
    {
        const int t = copyTrackBox.getSelectedId(), n = copyPatBox.getSelectedId();
        if (! haveProject || (t == loadedTrack && n == loadedPattern))
            return;
        const auto err = ksp::model::writePattern (project, t, n, pattern);
        if (! err.empty())
        {
            say (juce::String (err));
            return;
        }
        fillPatternBox();
        say ("Copied to track " + juce::String (t) + ", pattern " + juce::String (n) + " (not written to the KeyStep yet).");
        edited = true;
        noteChanged();
    }

    /** Moves every pattern of the current track one octave (12 semitones) in the project; it reaches the KeyStep with Write to slot. */
    void transposeCurrentTrack (int semitones)
    {
        if (! haveProject)
            return;
        commitPattern();
        edited = false;
        const int moved = ksp::model::transposeTrack (project, track(), semitones);
        if (moved < 0)
        {
            say ("Track " + juce::String (track()) + ": a note would leave the 0-127 range, nothing moved.");
            return;
        }
        noteChanged();
        loadPattern();
        say ("Track " + juce::String (track()) + ": " + juce::String (moved) + " pattern(s) moved " + (semitones > 0 ? "up" : "down")
             + " an octave (not written to the KeyStep yet).");
    }

    /** Scans the VST3 folders (one file at a time in KspScanner.exe) and keeps the list: instruments for the tracks, effects for
        their chains. Takes a few minutes the first time. */
    void scanPlugins()
    {
        if (scanning.exchange (true))
            return;
        say ("Scanning plugins...");
        std::thread ([this, list = plugins, stop = stopScan, safe = juce::Component::SafePointer<MainComponent> (this)] {
            const int n = list->scan ([this, safe, stop] (const juce::String& file) {
                post ([file] (MainComponent& m) { m.say ("Scanning " + juce::File (file).getFileName()); });
                return ! stop->load();
            });
            const auto inst = static_cast<int> (list->instruments().size()), fx = static_cast<int> (list->effects().size());
            juce::MessageManager::callAsync ([safe, n, inst, fx] {
                if (safe == nullptr)
                    return;
                safe->scanning = false;
                if (safe->pluginWindow != nullptr)
                    safe->pluginWindow->refresh();
                safe->refreshInstrumentChoices();
                safe->say (n < 0 ? "KspScanner.exe not found next to the app: plugins are not scanned in the app itself."
                                 : "Plugins: " + juce::String (inst) + " instruments, " + juce::String (fx) + " effects.");
            });
        }).detach();
    }

    /** The list of plugins found, with a checkbox per plugin to hide it from what the tracks offer. */
    void showPlugins()
    {
        if (pluginWindow != nullptr)
        {
            pluginWindow->toFront (true);
            return;
        }
        pluginWindow = std::make_unique<ksp::ui::PluginWindow> (plugins, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            juce::MessageManager::callAsync ([safe] {
                if (safe == nullptr)
                    return;
                safe->pluginWindow.reset();
                safe->refreshInstrumentChoices();   // plugins may have been hidden or shown
            });
        });
    }

    // ---- library: the truth of the app. The KeyStep is only a volatile working copy of it.
    void showLibrary()
    {
        if (libraryWindow != nullptr)
        {
            libraryWindow->refresh();
            libraryWindow->toFront (true);
            return;
        }
        juce::Component::SafePointer<MainComponent> self (this);
        libraryWindow = std::make_unique<ksp::ui::LibraryWindow> (
            library,
            [self] (const std::string& id) { if (self != nullptr) self->openFromLibrary (id); },
            [self] (const std::string& id) { if (self != nullptr && self->haveStudio && self->current.id == id) self->haveStudio = false; },
            [self] { juce::MessageManager::callAsync ([self] { if (self != nullptr) self->libraryWindow.reset(); }); });
    }

    /** Opens a library project (no KeyStep needed). */
    void openFromLibrary (const std::string& id)
    {
        std::string error;
        auto p = library->load (id, error);
        if (! p)
        {
            say ("Cannot open the project: " + juce::String (error));
            return;
        }
        current = std::move (*p);
        backup.reset();
        setProject (current.ksp, "Library: " + juce::String (current.name));
        haveStudio = true;   // after setProject, which clears it
        restoreTracks();
    }

    /** Opens the most recently changed library project, if any (at start, so the app is not empty without a KeyStep). */
public:
    void openMostRecent()
    {
        const auto list = library->list();
        if (! list.empty())
            openFromLibrary (list.front().id);
    }

    /** Asks before closing: Save / Discard / Cancel for unsaved changes, then a notice if the changes never reached the KeyStep.
        `proceed` runs only when the app may close. */
    void confirmClose (std::function<void()> proceed)
    {
        commitPattern();
        edited = false;
        if (closeDialog != nullptr)
            return;
        if (! haveProject)
        {
            proceed();
            return;
        }
        const auto askUnwritten = [this, proceed] {
            if (! unwritten)
            {
                proceed();
                return;
            }
            closeDialog = std::make_unique<juce::AlertWindow> ("Not on the KeyStep yet",
                "The changes to this project have not been written to the KeyStep. Close anyway?", juce::MessageBoxIconType::WarningIcon, this);
            closeDialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            closeDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            closeDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, proceed] (int ok) {
                closeDialog.reset();
                if (ok == 1)
                    proceed();
            }), false);
        };
        if (! unsaved)
        {
            askUnwritten();
            return;
        }
        closeDialog = std::make_unique<juce::AlertWindow> ("Unsaved changes",
            "Save the changes to the library before closing?", juce::MessageBoxIconType::QuestionIcon, this);
        closeDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        closeDialog->addButton ("Discard", 2);
        closeDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        closeDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, proceed, askUnwritten] (int r) {
            closeDialog.reset();
            if (r == 1)
                saveToLibrary (false, [askUnwritten] (bool saved) { if (saved) askUnwritten(); });
            else if (r == 2)
                proceed();
        }), false);
    }

private:
    /** Saves the project shown into the library; asks for a name the first time, or always with `asNew` (Save as: a new library project). */
    void saveToLibrary (bool asNew, std::function<void (bool)> done = {})
    {
        if (! haveProject)
        {
            if (done) done (false);
            return;
        }
        commitPattern();
        edited = false;
        if (haveStudio && ! asNew && ! current.name.empty())
        {
            const bool saved = finishSave();
            if (done) done (saved);
            return;
        }
        nameDialog = std::make_unique<juce::AlertWindow> ("Save to the library", "Name of the project:", juce::MessageBoxIconType::QuestionIcon, this);
        nameDialog->addTextEditor ("name", haveStudio ? juce::String (current.name) : "Project " + juce::String (project.project), "Name");
        nameDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, asNew, done] (int ok) {
            const auto name = nameDialog != nullptr ? nameDialog->getTextEditorContents ("name").trim() : juce::String();
            nameDialog.reset();
            if (ok == 0 || name.isEmpty())
            {
                if (done) done (false);
                return;
            }
            if (! haveStudio)
                current = ksp::studio::fromKsp (project, name.toStdString());
            else if (asNew)
            {
                current.id.clear();   // the library gives it a new id and creation date
                current.created.clear();
            }
            current.name = name.toStdString();
            const bool saved = finishSave();
            if (done) done (saved);
        }), false);
    }

    bool finishSave()
    {
        current.ksp = project;
        current.ksp.project = 1;
        captureTracks();
        const auto error = library->save (current);
        haveStudio = error.empty();
        if (error.empty())
            unsaved = false;
        say (error.empty() ? "Saved to the library: " + juce::String (current.name) : "Not saved: " + juce::String (error));
        if (libraryWindow != nullptr)
            libraryWindow->refresh();
        return error.empty();
    }

    // ---- instruments
    void buildInstrumentRow()
    {
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            for (juce::Component* c : { static_cast<juce::Component*> (&instLabel[i]), static_cast<juce::Component*> (&instBox[i]),
                                        static_cast<juce::Component*> (&editButton[i]), static_cast<juce::Component*> (&muteToggle[i]), static_cast<juce::Component*> (&soloToggle[i]),
                                        static_cast<juce::Component*> (&volSlider[i]), static_cast<juce::Component*> (&dbLabel[i]) })
                addAndMakeVisible (*c);
            instLabel[i].setText (juce::String (t + 1), juce::dontSendNotification);
            instLabel[i].setJustificationType (juce::Justification::centred);
            instLabel[i].setColour (juce::Label::textColourId, juce::Colours::white);
            dbLabel[i].setColour (juce::Label::textColourId, ksp::ui::palette::ink);
            muteToggle[i].setColour (juce::ToggleButton::tickColourId, ksp::ui::palette::rec);
            soloToggle[i].setColour (juce::ToggleButton::tickColourId, ksp::ui::palette::blue);
            instLabel[i].setFont (juce::FontOptions (15.0f, juce::Font::bold));
            instLabel[i].setInterceptsMouseClicks (false, false);
            dbLabel[i].setJustificationType (juce::Justification::centredRight);
            dbLabel[i].setFont (juce::FontOptions (12.0f));
            dbLabel[i].setInterceptsMouseClicks (false, false);
            editButton[i].setButtonText ("Edit");
            muteToggle[i].setButtonText ("M");
            soloToggle[i].setButtonText ("S");
            volSlider[i].setSliderStyle (juce::Slider::LinearHorizontal);
            volSlider[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volSlider[i].setRange (0.0, 1.0, 0.01);
            volSlider[i].setValue (0.8, juce::dontSendNotification);
            instBox[i].onChange = [this, t] { instrumentChosen (t); };
            editButton[i].onClick = [this, t] { openEditor (t); };
            soloToggle[i].onClick = [this, t] { engine.setSolo (t, soloToggle[static_cast<std::size_t> (t)].getToggleState()); };
            muteToggle[i].onClick = [this, t] { engine.setMute (t, muteToggle[static_cast<std::size_t> (t)].getToggleState()); };
            volSlider[i].onValueChange = [this, t] { engine.setVolume (t, static_cast<float> (volSlider[static_cast<std::size_t> (t)].getValue())); refreshCards(); };
            // a click anywhere on a card, its controls included, selects that track (mouseDown below)
            for (juce::Component* c : { static_cast<juce::Component*> (&instBox[i]), static_cast<juce::Component*> (&editButton[i]),
                                        static_cast<juce::Component*> (&muteToggle[i]), static_cast<juce::Component*> (&soloToggle[i]),
                                        static_cast<juce::Component*> (&volSlider[i]) })
                c->addMouseListener (this, true);
        }
        addAndMakeVisible (meterLabel);
        meterLabel.setText ("Audio off", juce::dontSendNotification);
        meterLabel.setFont (juce::FontOptions (12.0f));
        refreshInstrumentChoices();
        refreshCards();
    }

    /** The instruments the boxes offer (the scanned ones that are not hidden), and what each track has loaded. */
    void refreshInstrumentChoices()
    {
        instChoices = plugins->instruments();
        std::sort (instChoices.begin(), instChoices.end(),
                   [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            rebuildInstBox (t);
    }

    void rebuildInstBox (int t)
    {
        auto& box = instBox[static_cast<std::size_t> (t)];
        box.clear (juce::dontSendNotification);
        box.addItem ("(no instrument)", 1);
        int selected = 1;
        const auto* loaded = engine.instrument (t);
        for (std::size_t k = 0; k < instChoices.size(); ++k)
        {
            box.addItem (instChoices[k].name + "  (" + instChoices[k].manufacturerName + ")", static_cast<int> (k) + 2);
            if (loaded != nullptr && loaded->description().createIdentifierString() == instChoices[k].createIdentifierString())
                selected = static_cast<int> (k) + 2;
        }
        if (loaded != nullptr && selected == 1)
        {
            box.addItem (loaded->description().name + " (hidden)", 1000);
            selected = 1000;
        }
        if (loaded == nullptr && missingName[static_cast<std::size_t> (t)].isNotEmpty())
        {
            box.addItem ("Missing: " + missingName[static_cast<std::size_t> (t)], 1001);
            selected = 1001;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    }

    void instrumentChosen (int t)
    {
        const int id = instBox[static_cast<std::size_t> (t)].getSelectedId();
        if (id == 1000 || id == 1001)
            return;   // what the track already has
        if (id == 1)
        {
            closeEditor (t);
            engine.setInstrument (t, nullptr);
            missingName[static_cast<std::size_t> (t)].clear();
            say ("Track " + juce::String (t + 1) + ": no instrument.");
            return;
        }
        if (id >= 2 && static_cast<std::size_t> (id - 2) < instChoices.size())
            loadInstrument (t, instChoices[static_cast<std::size_t> (id - 2)], {});
        rebuildInstBox (t);
    }

    /** Loads a plugin on a track (blocking, on the message thread: a few seconds for the heavy ones). */
    bool loadInstrument (int t, const juce::PluginDescription& d, const juce::String& state)
    {
        closeEditor (t);
        if (const auto audioError = engine.start(); audioError.isNotEmpty())
            say ("No audio device: " + audioError);
        say ("Loading " + d.name + "...");
        juce::MouseCursor::showWaitCursor();
        juce::String error;
        auto inst = ksp::host::TrackInstrument::create (d, engine.sampleRate(), engine.blockSize(), error);
        juce::MouseCursor::hideWaitCursor();
        if (inst == nullptr)
        {
            say ("Cannot load " + d.name + ": " + error);
            return false;
        }
        if (! inst->setStateBase64 (state))
            say ("The saved state of " + d.name + " could not be restored; it starts with its own default.");
        engine.setInstrument (t, std::move (inst));
        missingName[static_cast<std::size_t> (t)].clear();
        say ("Track " + juce::String (t + 1) + ": " + d.name + " (MIDI channel " + juce::String (t + 1) + ")");
        if (device == nullptr && ! busy)
            work ([this] { connect(); });   // the KeyStep's notes reach the instruments once its MIDI port is open
        return true;
    }

    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (const juce::String& name, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
            : DocumentWindow (name, ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (editor, true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { if (closed) closed(); }
        std::function<void()> closed;
    };

    void closeEditor (int t) { editorWindow[static_cast<std::size_t> (t)].reset(); }

    void openEditor (int t)
    {
        auto* inst = engine.instrument (t);
        if (inst == nullptr)
            return;
        auto& slot = editorWindow[static_cast<std::size_t> (t)];
        if (slot != nullptr)
        {
            slot->toFront (true);
            return;
        }
        if (! inst->instance().hasEditor())
        {
            say (inst->description().name + " has no editor window.");
            return;
        }
        auto* editor = inst->instance().createEditorIfNeeded();
        if (editor == nullptr)
            return;
        juce::Component::SafePointer<MainComponent> self (this);
        slot = std::make_unique<EditorWindow> ("Track " + juce::String (t + 1) + ": " + inst->description().name, editor, [self, t] {
            juce::MessageManager::callAsync ([self, t] { if (self != nullptr) self->closeEditor (t); });
        });
    }

    // ---- the KeyStep's transport and WAV recording
    static juce::File recordingsFolder() { return ksp::host::dataFolder().getChildFile ("Recordings"); }

    static juce::String formatTime (double seconds)
    {
        const int total = static_cast<int> (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void buildRecordRow()
    {
        for (juce::Component* c : { static_cast<juce::Component*> (&transportLabel), static_cast<juce::Component*> (&recStatusLabel) })
            addAndMakeVisible (*c);
        addAndMakeVisible (generateButton);
        addAndMakeVisible (mapButton);
        mapButton.setButtonText ("Project Map");
        mapButton.onClick = [this] { showMap(); };
        generateButton.setButtonText ("Generate...");
        generateButton.onClick = [this] { showGenerator(); };
        transportLabel.setText ("STOP", juce::dontSendNotification);
        transportLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        recStatusLabel.setFont (juce::FontOptions (12.0f));
    }

    /** The Tools menu: the extras that are not the KeyStep's own (plugins, audio, WAV recording, the MIDI monitor). */
    void showToolsMenu()
    {
        enum { scan = 1, plugins, monitor, down, up, recWav, folder, audio, fx, test, follow, mode0 = 10, tail0 = 20 };
        const bool recording = engine.recorder().armed();
        juce::PopupMenu menu, modes, tails;
        const juce::StringArray modeNames { "Mix", "Stems", "Mix + stems" };
        for (int k = 0; k < 3; ++k)
            modes.addItem (mode0 + k + 1, modeNames[k], true, recMode == k + 1);
        static const int tailSeconds[] { 0, 2, 5, 10, 20, 30 };
        for (int k = 0; k < 6; ++k)
            tails.addItem (tail0 + k + 1, juce::String (tailSeconds[k]) + " s", true, recTail == k + 1);
        menu.addItem (scan, "Scan plugins", ! scanning.load());
        menu.addItem (plugins, "Plugins...");
        menu.addItem (monitor, "MIDI monitor");
        menu.addSeparator();
        menu.addItem (down, "Track -8va", haveProject);
        menu.addItem (up, "Track +8va", haveProject);
        menu.addSeparator();
        menu.addItem (recWav, recording ? "Stop rec WAV" : "Rec WAV");
        menu.addSubMenu ("Rec WAV mode", modes);
        menu.addSubMenu ("Rec WAV tail", tails);
        menu.addItem (follow, "Follow the KeyStep", true, recFollow);
        menu.addItem (folder, "Recordings folder");
        menu.addSeparator();
        menu.addItem (audio, "Audio...");
        menu.addItem (fx, "FX...");
        menu.addItem (test, "Test note");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolsButton),
                            [safe = juce::Component::SafePointer<MainComponent> (this)] (int id) {
            if (safe == nullptr || id == 0)
                return;
            auto& m = *safe;
            if (id > tail0)         m.recTail = id - tail0;
            else if (id > mode0)    m.recMode = id - mode0;
            else if (id == scan)    m.scanPlugins();
            else if (id == plugins) m.showPlugins();
            else if (id == monitor) m.showMonitor();
            else if (id == down)    m.transposeCurrentTrack (-12);
            else if (id == up)      m.transposeCurrentTrack (12);
            else if (id == recWav)  { if (m.engine.recorder().armed()) m.stopWavRecording ("Recording stopped."); else m.startWavRecording(); }
            else if (id == follow)  m.recFollow = ! m.recFollow;
            else if (id == folder)  { recordingsFolder().createDirectory(); recordingsFolder().revealToUser(); }
            else if (id == audio)   m.showAudioSettings();
            else if (id == fx)      m.showFx();
            else if (id == test)    m.testNotes();
        });
    }

    /** Arms the recording: with "Follow the KeyStep" it starts at the KeyStep's Start and ends after its Stop plus the tail. */
    void startWavRecording()
    {
        if (const auto audioError = engine.start(); audioError.isNotEmpty())
        {
            say ("No audio device: " + audioError);
            return;
        }
        ksp::host::Recorder::Options o;
        const int mode = recMode;
        o.mix = mode == 1 || mode == 3;
        o.stems = mode == 2 || mode == 3;
        o.syncToKeyStep = recFollow;
        static const double tails[] { 0.0, 2.0, 5.0, 10.0, 20.0, 30.0 };
        o.tailSeconds = tails[juce::jlimit (0, 5, recTail - 1)];
        auto name = juce::File::createLegalFileName (haveStudio && ! current.name.empty() ? juce::String (current.name) : juce::String ("Studio"));
        name += "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        juce::String error;
        if (! engine.recorder().start (recordingsFolder(), name, engine.sampleRate(), o, error))
        {
            say (error);
            return;
        }
        say (o.syncToKeyStep ? "Armed: it records when the KeyStep plays (Start) and a tail after it stops." : "Recording...");
    }

    void stopWavRecording (const juce::String& why)
    {
        const double seconds = engine.recorder().secondsRecorded();
        const auto files = engine.recorder().stop();
        recStatusLabel.setText ({}, juce::dontSendNotification);
        say (why + " " + juce::String (files.size()) + " file(s), " + formatTime (seconds) + ", in " + recordingsFolder().getFullPathName());
    }

    void updateTransportUi()
    {
        const auto& tr = engine.transport();
        const auto pos = mix::Transport::positionOf (tr.ticks());
        transportLabel.setText ((tr.playing() ? "PLAY   " : "STOP   ") + juce::String (pos.bar) + "." + juce::String (pos.beat) + "."
                                    + juce::String (pos.sixteenth) + "    " + formatTime (mix::Transport::secondsOf (tr.ticks(), tr.bpm()))
                                    + "    " + juce::String (tr.bpm(), 1) + " BPM",
                                juce::dontSendNotification);
        auto& rec = engine.recorder();
        if (rec.finished())
        {
            stopWavRecording ("The KeyStep stopped and the tail was recorded.");
            return;
        }
        if (rec.armed())
            recStatusLabel.setText (rec.recording() ? "REC  " + formatTime (rec.secondsRecorded()) : juce::String ("armed: waiting for the KeyStep to play"),
                                    juce::dontSendNotification);
    }

    /** The MIDI monitor: what the KeyStep sends while its controls are used. Needs the KeyStep's MIDI port open. */
    void showMonitor()
    {
        if (monitorWindow != nullptr)
        {
            monitorWindow->toFront (true);
            return;
        }
        juce::Component::SafePointer<MainComponent> self (this);
        monitorWindow = std::make_unique<ksp::ui::MidiMonitorWindow> (
            [flag = monitorOn] (bool on) { flag->store (on); },
            [self] { juce::MessageManager::callAsync ([self] { if (self != nullptr) self->monitorWindow.reset(); }); });
        if (device == nullptr && ! busy)
            work ([this] { connect(); });
        else if (device == nullptr)
            say ("The KeyStep is busy; open the monitor again in a moment.");
    }

    // ---- Project Map: the open project's scenes, chains and patterns as one picture (read only)
    void refreshMap()
    {
        if (mapWindow == nullptr)
            return;
        const juce::String name = ! haveProject ? juce::String ("No project")
                                  : haveStudio  ? juce::String (current.name) + "  (KeyStep Pro project " + juce::String (project.project) + ")"
                                                : "KeyStep Pro project " + juce::String (project.project) + " (not in the library)";
        mapWindow->content().setProject (haveProject ? ksp::model::buildMap (project) : ksp::model::ProjectMap {}, name);
    }

    void showMap()
    {
        if (mapWindow != nullptr)
        {
            commitPattern();
            refreshMap();
            mapWindow->toFront (true);
            return;
        }
        juce::Component::SafePointer<MainComponent> self (this);
        mapWindow = std::make_unique<ksp::ui::ProjectMapWindow> (
            [self] { juce::MessageManager::callAsync ([self] { if (self != nullptr) self->mapWindow.reset(); }); },
            [self] { if (self != nullptr) { self->commitPattern(); self->refreshMap(); } });
        commitPattern();
        refreshMap();
    }

    // ---- the ambient generator: patterns 1-4 of the four tracks and the scenes, into the project that is open
    void showGenerator()
    {
        if (generatorWindow != nullptr)
        {
            generatorWindow->toFront (true);
            return;
        }
        juce::Component::SafePointer<MainComponent> self (this);
        generatorWindow = std::make_unique<ksp::ui::GeneratorWindow> (
            [self] { juce::MessageManager::callAsync ([self] { if (self != nullptr) self->generatorWindow.reset(); }); });
        auto& panel = generatorWindow->content();
        panel.onGenerate = [self] (const juce::String& mood, unsigned seed, bool chains, bool slow) {
            if (self != nullptr)
                self->generateInto (mood, seed, chains, slow);
        };
        panel.onUndo = [self] { if (self != nullptr) self->undoGenerate(); };
        if (haveStudio)   // the settings the project was generated with
            panel.setState (juce::String (current.generator.mood), current.generator.seed, current.generator.chains, current.generator.slow,
                            beforeGenerate.has_value());
        else
            panel.setCanUndo (beforeGenerate.has_value());
    }

    void generateInto (const juce::String& mood, unsigned seed, bool chains, bool slow)
    {
        if (! haveProject)
        {
            say ("Open a project, a backup or read one from the KeyStep first: the generator writes into it.");
            return;
        }
        stopRecording();
        commitPattern();
        edited = false;
        const auto before = project;
        auto params = ksp::gen::moodParams (mood.toStdString(), seed);
        params.chains = chains;
        params.slow = slow;
        std::string error;
        try
        {
            error = ksp::gen::apply (project, params);
            if (error.empty())
                error = ksp::gen::applyScenes (project, params);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (! error.empty())
        {
            project = before;
            say ("The generator could not write into this project: " + juce::String (error));
            return;
        }
        beforeGenerate = before;
        noteChanged();
        current.generator = ksp::studio::Generator { mood.toStdString(), seed, chains, slow };
        loadPattern();
        if (generatorWindow != nullptr)
            generatorWindow->content().setCanUndo (true);
        say ("Generated " + mood + ", seed " + juce::String (seed) + ": patterns 1-4 and scenes 1-8 (not saved, not on the KeyStep yet).");
    }

    void undoGenerate()
    {
        if (! beforeGenerate.has_value())
            return;
        project = *beforeGenerate;
        beforeGenerate.reset();
        edited = false;
        noteChanged();
        loadPattern();
        if (generatorWindow != nullptr)
            generatorWindow->content().setCanUndo (false);
        say ("Generation undone: the project is as it was.");
    }

    void showAudioSettings()
    {
        if (const auto e = engine.start(); e.isNotEmpty())
        {
            say ("No audio device: " + e);
            return;
        }
        auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 0, 0, 2, false, false, true, false);
        selector->setSize (520, 380);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector);
        o.dialogTitle = "Audio output";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = ksp::ui::palette::chassis;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    /** A note on every track that has an instrument, on that track's channel, so the sound can be heard without the KeyStep playing. */
    void testNotes()
    {
        juce::Component::SafePointer<MainComponent> self (this);
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            if (engine.instrument (t) != nullptr)
            {
                engine.midiFromUi (juce::MidiMessage::noteOn (t + 1, 60, 0.8f));
                juce::Timer::callAfterDelay (900, [self, t] { if (self != nullptr) self->engine.midiFromUi (juce::MidiMessage::noteOff (t + 1, 60)); });
            }
    }

    /** A short note on the selected track's instrument, so a step added in the grid is heard. */
    void auditionNote (int pitch, int velocity)
    {
        const int t = track() - 1;
        if (engine.instrument (t) == nullptr)
            return;
        const int ch = engine.channel (t);
        engine.midiFromUi (juce::MidiMessage::noteOn (ch, pitch, static_cast<juce::uint8> (juce::jlimit (1, 127, velocity))));
        juce::Component::SafePointer<MainComponent> self (this);
        juce::Timer::callAfterDelay (300, [self, ch, pitch] { if (self != nullptr) self->engine.midiFromUi (juce::MidiMessage::noteOff (ch, pitch)); });
    }

    /** Slow glow on the sync button while the project has edits not yet written to the KeyStep (rides the existing 10 Hz timer). */
    void updateSyncUi()
    {
        const bool send = haveProject && fwReadOnly.isEmpty();   // untested firmware: read only (the status line says so)
        readButton.setButtonText ("Read");
        syncButton.setButtonText ("Write");
        syncButton.setEnabled (! busy && send);
    }

    void updatePendingGlow()
    {
        updateSyncUi();
        if (! unwritten && ! glowOn)
            return;
        float k = 0.0f;
        if (unwritten)
            k = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (juce::Time::getMillisecondCounter() % 2400) / 2400.0f);
        glowOn = unwritten;
        const auto c = ksp::ui::palette::button.interpolatedWith (ksp::ui::palette::lit, 0.55f * k);
        syncButton.setColour (juce::TextButton::buttonColourId, c);
        syncButton.repaint();
    }

    void timerCallback() override
    {
        updatePendingGlow();
        lenDisplay.setText (juce::String (pattern.length), ksp::ui::palette::trackColour (track() - 1).brighter (0.5f));
        bpmDisplay.setText (engine.isRunning() ? juce::String (engine.bpm(), 1) : juce::String ("---"));
        updateTransportUi();
        if (! engine.isRunning())
            return;
        const float peak = engine.takePeak(), reduction = engine.takeReductionDb();
        const auto db = peak > 0.00001f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB" : juce::String ("silence");
        const auto text = "Out " + db + ", limiter " + juce::String (reduction, 1) + " dB, " + juce::String (engine.bpm(), 1) + " BPM";
        meterLabel.setText (text, juce::dontSendNotification);
    }

    /** The KeyStep's MIDI goes to the instruments (lock-free) and, while Rec is on, to the overdub on the message thread. */
    void installMidiListener()
    {
        device->setListener ([eng = &engine, rec = recordingOn, mon = monitorOn, safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::MidiMessage& m) {
            eng->midiFromDevice (m);
            if (mon->load())
            {
                const auto* raw = m.getRawData();
                juce::MessageManager::callAsync ([safe, bytes = std::vector<std::uint8_t> (raw, raw + m.getRawDataSize())] {
                    if (safe != nullptr && safe->monitorWindow != nullptr)
                        safe->monitorWindow->content().add (bytes);
                });
            }
            if (rec->load())
            {
                const auto* raw = m.getRawData();
                juce::MessageManager::callAsync ([safe, bytes = std::vector<std::uint8_t> (raw, raw + m.getRawDataSize())] {
                    if (safe != nullptr)
                        safe->recordMidi (bytes);
                });
            }
        });
    }

    /** What the tracks hold now (instrument, its state, volume, mute) into the project being saved. A plugin that is missing on
        this machine keeps what the file had. */
    void captureTracks()
    {
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            auto& ts = current.tracks[i];
            if (const auto* inst = engine.instrument (t))
                ts.instrument = ksp::studio::PluginSetup { inst->description().name.toStdString(), inst->descriptionXml().toStdString(),
                                                           inst->stateBase64().toStdString(), false };
            else if (missingName[i].isEmpty())
                ts.instrument.reset();
            ts.volume = volSlider[i].getValue();
            ts.mute = muteToggle[i].getToggleState();
            ts.solo = soloToggle[i].getToggleState();
        }
        captureFx();
    }

    /** Loads the instruments of the project that was just opened (a plugin that cannot be loaded stays as "Missing"). */
    void restoreTracks()
    {
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            const auto& ts = current.tracks[i];
            closeEditor (t);
            volSlider[i].setValue (ts.volume, juce::dontSendNotification);
            refreshCards();
            muteToggle[i].setToggleState (ts.mute, juce::dontSendNotification);
            engine.setVolume (t, static_cast<float> (ts.volume));
            engine.setMute (t, ts.mute);
            soloToggle[i].setToggleState (ts.solo, juce::dontSendNotification);
            engine.setSolo (t, ts.solo);
            missingName[i].clear();
            if (! ts.instrument)
            {
                engine.setInstrument (t, nullptr);
            }
            else
            {
                juce::PluginDescription d;
                const auto xml = juce::XmlDocument::parse (juce::String (ts.instrument->description));
                if (xml == nullptr || ! d.loadFromXml (*xml) || ! loadInstrument (t, d, juce::String (ts.instrument->state)))
                {
                    engine.setInstrument (t, nullptr);
                    missingName[i] = juce::String (ts.instrument->name);
                }
            }
            rebuildInstBox (t);
        }
        restoreFx();
    }

    // ---- effects: the inserts of the tracks and the master chain (VST3 effects)
    static bool isBlank (const ksp::studio::PluginSetup& p) { return p.name.empty() && p.description.empty(); }

    std::optional<ksp::studio::PluginSetup> fxSetupAt (int pos)
    {
        const auto i = static_cast<std::size_t> (pos);
        if (auto* fx = engine.effect (pos))
            return ksp::studio::PluginSetup { fx->description().name.toStdString(), fx->descriptionXml().toStdString(),
                                              fx->stateBase64().toStdString(), fxBypass[i] };
        return missingFx[i];   // a missing plugin keeps what the file had
    }

    void captureFx()
    {
        auto chain = [this] (int first, int count) {
            std::vector<ksp::studio::PluginSetup> v;
            for (int k = 0; k < count; ++k)
                v.push_back (fxSetupAt (first + k).value_or (ksp::studio::PluginSetup {}));
            while (! v.empty() && isBlank (v.back()))
                v.pop_back();
            return v;
        };
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            current.tracks[static_cast<std::size_t> (t)].effects = chain (t * ksp::host::Engine::kInserts, ksp::host::Engine::kInserts);
        current.masterFx = chain (ksp::host::Engine::kMasterFxBase, ksp::host::Engine::kMasterFx);
    }

    void restoreFx()
    {
        auto apply = [this] (int pos, const ksp::studio::PluginSetup* ps) {
            const auto i = static_cast<std::size_t> (pos);
            missingFx[i].reset();
            fxBypass[i] = false;
            fxEditorWindow[i].reset();
            if (ps == nullptr || isBlank (*ps))
            {
                engine.setEffect (pos, nullptr);
                return;
            }
            juce::PluginDescription d;
            const auto xml = juce::XmlDocument::parse (juce::String (ps->description));
            if (xml != nullptr && d.loadFromXml (*xml) && loadEffect (pos, d, juce::String (ps->state)))
            {
                fxBypass[i] = ps->bypass;
                engine.setEffectBypass (pos, ps->bypass);
            }
            else
            {
                engine.setEffect (pos, nullptr);
                missingFx[i] = *ps;
            }
        };
        for (int t = 0; t < ksp::host::Engine::kTracks; ++t)
            for (int k = 0; k < ksp::host::Engine::kInserts; ++k)
            {
                const auto& v = current.tracks[static_cast<std::size_t> (t)].effects;
                apply (t * ksp::host::Engine::kInserts + k, static_cast<std::size_t> (k) < v.size() ? &v[static_cast<std::size_t> (k)] : nullptr);
            }
        for (int k = 0; k < ksp::host::Engine::kMasterFx; ++k)
            apply (ksp::host::Engine::kMasterFxBase + k,
                   static_cast<std::size_t> (k) < current.masterFx.size() ? &current.masterFx[static_cast<std::size_t> (k)] : nullptr);
        refreshFxRows();
    }

    /** Loads a VST3 effect at a position (blocking, on the message thread). */
    bool loadEffect (int pos, const juce::PluginDescription& d, const juce::String& state)
    {
        if (const auto audioError = engine.start(); audioError.isNotEmpty())
            say ("No audio device: " + audioError);
        say ("Loading " + d.name + "...");
        juce::MouseCursor::showWaitCursor();
        juce::String error;
        auto fx = ksp::host::TrackEffect::create (d, engine.sampleRate(), engine.blockSize(), error);
        juce::MouseCursor::hideWaitCursor();
        if (fx == nullptr)
        {
            say ("Cannot load " + d.name + ": " + error);
            return false;
        }
        if (! fx->setStateBase64 (state))
            say ("The saved state of " + d.name + " could not be restored; it starts with its own default.");
        fxEditorWindow[static_cast<std::size_t> (pos)].reset();
        engine.setEffect (pos, std::move (fx));
        missingFx[static_cast<std::size_t> (pos)].reset();
        say (d.name + " loaded.");
        return true;
    }

    void showFx()
    {
        if (fxWindow != nullptr)
        {
            fxWindow->content().setChoices (plugins->effects());
            refreshFxRows();
            fxWindow->toFront (true);
            return;
        }
        juce::Component::SafePointer<MainComponent> self (this);
        fxWindow = std::make_unique<ksp::ui::FxWindow> ([self] {
            juce::MessageManager::callAsync ([self] { if (self != nullptr) self->fxWindow.reset(); });
        });
        auto& panel = fxWindow->content();
        panel.onChoose = [self] (int pos, const juce::PluginDescription* d) {
            if (self == nullptr)
                return;
            if (d == nullptr)
            {
                self->fxEditorWindow[static_cast<std::size_t> (pos)].reset();
                self->engine.setEffect (pos, nullptr);
                self->missingFx[static_cast<std::size_t> (pos)].reset();
            }
            else
            {
                self->fxBypass[static_cast<std::size_t> (pos)] = false;
                self->engine.setEffectBypass (pos, false);
                self->loadEffect (pos, *d, {});
            }
            self->refreshFxRows();
        };
        panel.onEdit = [self] (int pos) { if (self != nullptr) self->openFxEditor (pos); };
        panel.onBypass = [self] (int pos, bool b) {
            if (self == nullptr)
                return;
            self->fxBypass[static_cast<std::size_t> (pos)] = b;
            self->engine.setEffectBypass (pos, b);
        };
        panel.setChoices (plugins->effects());
        refreshFxRows();
    }

    void refreshFxRows()
    {
        if (fxWindow == nullptr)
            return;
        for (int p = 0; p < ksp::host::Engine::kFxPositions; ++p)
        {
            const auto i = static_cast<std::size_t> (p);
            const auto* fx = engine.effect (p);
            fxWindow->content().show (p, fx != nullptr ? &fx->description() : nullptr,
                                      missingFx[i] ? juce::String (missingFx[i]->name) : juce::String(), fxBypass[i]);
        }
    }

    void openFxEditor (int pos)
    {
        auto* fx = engine.effect (pos);
        if (fx == nullptr)
            return;
        auto& slot = fxEditorWindow[static_cast<std::size_t> (pos)];
        if (slot != nullptr)
        {
            slot->toFront (true);
            return;
        }
        if (! fx->instance().hasEditor())
        {
            say (fx->description().name + " has no editor window.");
            return;
        }
        auto* editor = fx->instance().createEditorIfNeeded();
        if (editor == nullptr)
            return;
        juce::Component::SafePointer<MainComponent> self (this);
        slot = std::make_unique<EditorWindow> (fx->description().name, editor, [self, pos] {
            juce::MessageManager::callAsync ([self, pos] { if (self != nullptr) self->fxEditorWindow[static_cast<std::size_t> (pos)].reset(); });
        });
    }

    void showPattern()
    {
        octLabel.setText (juce::String (ksp::noteName (lowPitch)) + " - " + juce::String (ksp::noteName (lowPitch + kRows - 1)),
                          juce::dontSendNotification);
        grid.setContext (track(), patNo());
        grid.setPattern (haveProject ? &pattern : nullptr, lowPitch);
        syncKnobs();
    }

    void setProject (ksp::ProjectData p, const juce::String& what)
    {
        stopRecording();
        beforeGenerate.reset();   // Undo belongs to the project it was made on
        if (generatorWindow != nullptr)
            generatorWindow->content().setCanUndo (false);
        project = std::move (p);
        haveProject = true;
        haveStudio = false;   // a project from a backup or the KeyStep is new to the library: Save asks for a name
        edited = false;
        unsaved = unwritten = false;
        loadedTrack = track();
        loadedPattern = patNo();
        projectBox.setSelectedId (project.project, juce::dontSendNotification);
        pattern = {};
        loadPattern();
        setBusy (false);
        say (what + ": " + juce::String (ksp::summary (project)));
    }

    void selectProject()
    {
        if (backup && backup->projects.size() > 0)
            for (const auto& p : backup->projects)
                if (p.project == projectBox.getSelectedId())
                    setProject (p, "Project " + juce::String (p.project) + " from the file");
    }

    // ---- file
    void openBackup()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a backup file", ksp::backupsFolder(), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { openFile (fc.getResult()); });
    }

public:
    void openFile (const juce::File& f)
    {
        if (! f.existsAsFile())
            return;
        auto r = ksp::fromJson (f.loadFileAsString().toStdString());
        if (! r.backup || r.backup->projects.empty())
        {
            say ("Cannot open " + f.getFileName() + ": " + juce::String (r.error));
            return;
        }
        backup = std::move (r.backup);
        int pick = backup->projects[0].project;
        for (const auto& p : backup->projects)
            if (ksp::summary (p).find ("T1 0, T2 0, T3 0, T4 0") != 0) { pick = p.project; break; }
        projectBox.setSelectedId (pick, juce::dontSendNotification);
        selectProject();
    }

private:
    // ---- device (worker threads; results come back on the message thread)
    void work (std::function<void()> job)
    {
        setBusy (true);
        busyOverlay.begin ("Talking to the KeyStep...");
        std::thread ([this, job = std::move (job), safe = juce::Component::SafePointer<MainComponent> (this)] {
            job();
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->setBusy (false); safe->busyOverlay.end(); } });
        }).detach();
    }

    void post (std::function<void (MainComponent&)> fn)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), fn = std::move (fn)] {
            if (safe != nullptr)
                fn (*safe);
        });
    }

    bool connect()
    {
        if (device != nullptr)
            return true;
        juce::String error;
        device = ksp::Device::open ("KeyStep Pro", error);
        if (device == nullptr)
            post ([error] (MainComponent& m) { m.say (error); });
        else
        {
            installMidiListener();
            const bool ro = ! device->canWrite();
            const auto fw = juce::String (device->identity().versionString());
            post ([ro, fw] (MainComponent& m) {
                m.fwReadOnly = ro ? fw : juce::String();
                m.say (ro ? "KeyStep Pro firmware " + fw + " is not tested: read only until it is (tested: 2.5.20)."
                          : juce::String ("KeyStep Pro connected: its notes play the instruments of the tracks."));
            });
        }
        return device != nullptr;
    }

    ksp::Progress progressTo (const juce::String& what)
    {
        return [this, what] (double x) {
            post ([what, x] (MainComponent& m) { m.busyOverlay.update (what, x); });   // the bar shows the %; the status line stays quiet meanwhile
            return true;
        };
    }

    /** slot 0 = the one picked in the Project box; otherwise that slot (the sync button re-reads the project's origin slot). */
    void readFromDevice (int slot = 0)
    {
        const int p = slot > 0 ? slot : projectBox.getSelectedId();
        stopRecording();
        work ([this, p] {
            if (! connect())
                return;
            auto r = device->readSlot (p, progressTo ("Reading project " + juce::String (p)));
            post ([r, p] (MainComponent& m) {
                if (r.project) { m.backup.reset(); m.setProject (*r.project, "Project " + juce::String (p) + " from the KeyStep"); }
                else m.say (juce::String (r.error));
            });
        });
    }

    void confirmWrite()
    {
        const int slot = juce::jlimit (kFirstWritableSlot, kLastWritableSlot, project.project);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Send to slot " + juce::String (slot),
            "This replaces project " + juce::String (slot) + " on the KeyStep Pro with the project shown here. "
            + "To keep both, use Save as... and the project stays in the library. "
            "The KeyStep only holds a working copy of your library: what that slot has now is saved to your Backups folder first, and it is read back after writing.",
            "Write", "Cancel", this, juce::ModalCallbackFunction::create ([this, slot] (int ok) { if (ok) writeToSlot (slot); }));
    }

    void writeToSlot (int slot)
    {
        stopRecording();
        commitPattern();
        edited = false;
        auto written = project;
        written.project = slot;
        work ([this, slot, written = std::move (written)]() mutable {
            if (! connect())
                return;
            juce::File pre;
            auto r = device->backupSlot (slot, progressTo ("Backing up slot " + juce::String (slot)), pre);
            if (r.failed())
            {
                post ([r] (MainComponent& m) { m.say ("Nothing was written: " + r.getErrorMessage()); });
                return;
            }
            // only what differs from the slot as just read (and the states): much less than the whole project
            const auto plan = ksp::writePlan (written, &device->slotBefore());
            post ([] (MainComponent& m) { m.say ("Writing..."); });
            r = device->writeSlot (plan, slot, [this] (double x) {
                post ([x] (MainComponent& m) { m.say ("Writing " + juce::String (static_cast<int> (x * 100)) + " %"); });
            });
            if (r.failed())
            {
                post ([r] (MainComponent& m) { m.say (r.getErrorMessage()); });
                return;
            }
            auto after = device->readSlot (slot, progressTo ("Reading back"));
            juce::String msg;
            if (! after.project)
                msg = "Written, but the read-back failed: " + juce::String (after.error);
            else if (auto d = ksp::compareReadBack (written, *after.project); ! d.empty())
                msg = juce::String (d.size()) + " value(s) differ after writing slot " + juce::String (slot) + "; the old slot is in " + pre.getFullPathName();
            else
                msg = "Slot " + juce::String (slot) + " written and verified.";
            post ([msg, ok = after.project && msg.startsWith ("Slot")] (MainComponent& m) { m.say (msg); if (ok) m.unwritten = false; });
        });
    }

    // ---- overdub (MIDI from the KeyStep; handled on the message thread in arrival order)
    void startRecording()
    {
        if (! haveProject || busy || ! connect())
            return;
        takeBefore = pattern;
        overdub.emplace (stepBox.getSelectedId(), recChannelBox.getSelectedId() - 1);
        recordingOn->store (true);
        recButton.setButtonText ("Stop rec");
        recButton.setColour (juce::TextButton::buttonColourId, ksp::ui::palette::rec);
        recButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        undoTakeButton.setEnabled (false);
        say ("Recording on channel " + juce::String (recChannelBox.getSelectedId()) + ": press Play on the KeyStep and play over the pattern.");
    }

    void recordMidi (const std::vector<std::uint8_t>& m)
    {
        if (! overdub || m.empty())
            return;
        const bool wasRunning = overdub->isRunning();
        if (overdub->handle (pattern, m.data(), m.size()))
        {
            edited = true;
            noteChanged();
            grid.repaint();
        }
        else if (overdub->isRunning() == wasRunning)
            return;
        say (juce::String (overdub->isRunning() ? "Recording: " : "Recording (KeyStep stopped): ")
             + juce::String (overdub->added) + " note(s) added"
             + (overdub->dropped > 0 ? ", " + juce::String (overdub->dropped) + " did not fit" : juce::String()));
    }

    void stopRecording()
    {
        if (! overdub)
            return;
        recordingOn->store (false);
        const int added = overdub->added, dropped = overdub->dropped;
        overdub.reset();
        haveTake = added > 0;
        recButton.setButtonText ("Rec");
        recButton.removeColour (juce::TextButton::buttonColourId);
        recButton.removeColour (juce::TextButton::textColourOffId);
        undoTakeButton.setEnabled (haveTake);
        say ("Take: " + juce::String (added) + " note(s) added" + (dropped > 0 ? ", " + juce::String (dropped) + " did not fit" : juce::String())
             + (added > 0 ? " (not written yet)." : "."));
    }

    void undoTake()
    {
        if (! haveTake || overdub)
            return;
        pattern = takeBefore;
        haveTake = false;
        undoTakeButton.setEnabled (false);
        showPattern();
        say ("Last take removed.");
    }

    int loadedTrack = 1, loadedPattern = 1;
};

//==============================================================================
class StudioApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "KeyStepPro Studio"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        window = std::make_unique<Window> (getApplicationName());
        const auto path = commandLine.unquoted().trim();
        if (path.isNotEmpty())
            window->main().openFile (juce::File::getCurrentWorkingDirectory().getChildFile (path));
        else
            window->main().openMostRecent();
    }
    void systemRequestedQuit() override
    {
        if (window == nullptr)
            quit();
        else
            window->main().confirmClose ([] { juce::JUCEApplication::quit(); });
    }
    void shutdown() override { window.reset(); juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (const juce::String& name)
            : DocumentWindow (name, ksp::ui::palette::chassis, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content = new MainComponent(), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent& main() { return *content; }
        MainComponent* content = nullptr;
    };
    ksp::ui::KspLookAndFeel look;   // declared before the window: outlives it
    std::unique_ptr<Window> window;
};

} // namespace

START_JUCE_APPLICATION (StudioApp)
