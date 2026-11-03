/*
    DrumGrid.h
    The grid for track 1 in drum mode (Source/Model/DrumPattern.h): 24 lanes x 64 steps, lane 1 (the kick) at the bottom.
    Click a cell to add or remove a hit and drag to paint more of the same; the mouse wheel on a hit changes its velocity
    (shown as its shade); click a lane name to hear it; click or drag in the ruler to set the last step. Lane names are the
    General MIDI sounds of the KeyStep's default drum map (lane n plays note 36 + n). JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include "Model/DrumPattern.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ksp::ui
{

class DrumGrid : public juce::Component
{
public:
    static constexpr int kCellW = 18, kCellH = 14, kNameW = 96, kRulerH = 18, kLanes = ksp::model::kDrumLanes;

    std::function<void()> onEdited;
    std::function<void (int note, int velocity)> onHit;   // a hit was added or a lane name clicked: it is heard
    std::function<void (int)> onLastStep;                  // the ruler was clicked or dragged: the new length, 1..64
    std::function<void (const juce::String&)> onMessage;   // e.g. the 192 hits limit

    DrumGrid() { setSize (kNameW + kCellW * ksp::model::kSteps, kRulerH + kCellH * kLanes); }

    void setPattern (ksp::model::DrumPattern* p)
    {
        pattern = p;
        repaint();
    }

    static int laneNote (int lane) { return 36 + lane; }

    static const char* laneName (int lane)
    {
        static const char* names[kLanes] { "Kick", "Side stick", "Snare", "Clap", "Snare 2", "Low tom", "Closed hat", "Low tom 2",
                                           "Pedal hat", "Mid tom", "Open hat", "Mid tom 2", "High tom", "Crash", "High tom 2", "Ride",
                                           "China", "Ride bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap", "Ride 2" };
        return names[juce::jlimit (0, kLanes - 1, lane)];
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (palette::gridA);
        if (pattern == nullptr)
            return;
        const int len = juce::jlimit (1, ksp::model::kSteps, pattern->length);
        const int endX = kNameW + len * kCellW;
        const auto colour = palette::trackColour (0);

        for (int lane = 0; lane < kLanes; ++lane)
        {
            const int y = rowY (lane);
            g.setColour (lane % 2 ? palette::gridB : palette::gridA);
            g.fillRect (kNameW, y, getWidth() - kNameW, kCellH);
            g.setColour (lane == hoverLane ? palette::buttonHi : juce::Colours::white);
            g.fillRect (0, y, kNameW - 2, kCellH - 1);
            g.setColour (palette::gridLine);
            g.drawRect (0, y, kNameW - 2, kCellH, 1);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (laneName (lane), 4, y, kNameW - 34, kCellH, juce::Justification::centredLeft);
            g.setColour (palette::inkSoft);
            g.drawText (ksp::noteName (laneNote (lane)), kNameW - 32, y, 28, kCellH, juce::Justification::centredRight);
        }

        for (int st = 0; st <= ksp::model::kSteps; ++st)
        {
            g.setColour (st % 16 == 0 ? juce::Colour (0xff7d868e) : st % 4 == 0 ? juce::Colour (0xffa5adb4) : palette::gridLine);
            g.fillRect (kNameW + st * kCellW, kRulerH, 1, getHeight() - kRulerH);
        }

        // hits: the shade is the velocity; after the last step they are kept but dimmed (the KeyStep does not play them)
        for (const auto& h : pattern->hits)
        {
            const float a = 0.35f + 0.65f * static_cast<float> (h.velocity) / 127.0f;
            g.setColour (colour.withAlpha (h.step < len ? a : a * 0.35f));
            const auto cell = cellBox (h.lane, h.step).toFloat().reduced (1.5f);
            g.fillRoundedRectangle (cell, 2.0f);
            g.setColour (palette::panelDark.withAlpha (h.step < len ? 0.8f : 0.3f));
            g.drawRoundedRectangle (cell, 2.0f, 1.0f);
        }

        g.setColour (palette::chassis.withAlpha (0.6f));
        g.fillRect (endX, kRulerH, getWidth() - endX, getHeight() - kRulerH);
        g.setColour (palette::panelDark);
        g.fillRect (endX - 1, kRulerH, 3, getHeight() - kRulerH);

        // the ruler: step numbers and the END flag, as in the note grid
        g.setColour (palette::panelDark);
        g.fillRect (0, 0, getWidth(), kRulerH);
        g.setColour (juce::Colour (0xff3a3d44));
        g.fillRect (kNameW, 0, len * kCellW, kRulerH);
        g.setFont (9.0f);
        for (int st = 0; st < ksp::model::kSteps; st += 4)
        {
            g.setColour (st < len ? juce::Colours::white : juce::Colours::grey);
            g.drawText (juce::String (st + 1), kNameW + st * kCellW + 2, 0, kCellW * 4, kRulerH, juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::grey);
        g.drawText ("drums  last step", 2, 0, kNameW - 2, kRulerH, juce::Justification::centredLeft);
        const int flagW = 36;
        g.setColour (colour);
        g.fillRect (endX - flagW, 1, flagW, kRulerH - 2);
        g.setColour (palette::panelDark);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("END " + juce::String (len), endX - flagW, 1, flagW, kRulerH - 2, juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int lane = e.y >= kRulerH ? laneAt (e.y) : -1;
        const bool overName = e.x < kNameW && lane >= 0;
        if ((overName ? lane : -1) != hoverLane)
        {
            hoverLane = overName ? lane : -1;
            repaint();
        }
        setMouseCursor (e.y < kRulerH && e.x >= kNameW ? juce::MouseCursor::LeftRightResizeCursor
                        : overName || e.x >= kNameW ? juce::MouseCursor::PointingHandCursor
                                                    : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverLane = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        brush = Paint::none;
        if (pattern == nullptr)
            return;
        if (e.y < kRulerH)
        {
            setLength (e.x);
            brush = Paint::ruler;
            return;
        }
        const int lane = laneAt (e.y);
        if (e.x < kNameW)
        {
            if (onHit)
                onHit (laneNote (lane), 100);
            return;
        }
        const int step = stepAt (e.x);
        brush = find (lane, step) != nullptr ? Paint::erase : Paint::add;
        apply (lane, step);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (brush == Paint::ruler)
            setLength (e.x);
        else if (brush != Paint::none && e.x >= kNameW && e.y >= kRulerH && e.y < getHeight())
            apply (laneAt (e.y), stepAt (e.x));
    }

    void mouseUp (const juce::MouseEvent&) override { brush = Paint::none; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto* h = pattern != nullptr && e.x >= kNameW && e.y >= kRulerH ? find (laneAt (e.y), stepAt (e.x)) : nullptr;
        if (h == nullptr)
        {
            Component::mouseWheelMove (e, w);   // the viewport scrolls
            return;
        }
        h->velocity = juce::jlimit (1, 127, h->velocity + (w.deltaY > 0 ? 8 : -8));
        if (onMessage)
            onMessage (juce::String (laneName (h->lane)) + ", step " + juce::String (h->step + 1) + ": velocity " + juce::String (h->velocity));
        edited();
    }

private:
    enum class Paint { none, add, erase, ruler };

    int rowY (int lane) const { return kRulerH + (kLanes - 1 - lane) * kCellH; }
    int laneAt (int y) const { return juce::jlimit (0, kLanes - 1, kLanes - 1 - (y - kRulerH) / kCellH); }
    int stepAt (int x) const { return juce::jlimit (0, ksp::model::kSteps - 1, (x - kNameW) / kCellW); }
    juce::Rectangle<int> cellBox (int lane, int step) const { return { kNameW + step * kCellW, rowY (lane), kCellW, kCellH }; }

    ksp::model::Hit* find (int lane, int step)
    {
        for (auto& h : pattern->hits)
            if (h.lane == lane && h.step == step)
                return &h;
        return nullptr;
    }

    void apply (int lane, int step)
    {
        auto& hits = pattern->hits;
        if (brush == Paint::erase)
        {
            const auto it = std::find_if (hits.begin(), hits.end(), [&] (const auto& h) { return h.lane == lane && h.step == step; });
            if (it == hits.end())
                return;
            hits.erase (it);
            edited();
            return;
        }
        if (find (lane, step) != nullptr)
            return;
        if (static_cast<int> (hits.size()) >= ksp::model::kSlots)
        {
            if (onMessage)
                onMessage ("The KeyStep Pro holds 192 drum hits per pattern; remove some first.");
            return;
        }
        ksp::model::Hit h;
        h.lane = lane;
        h.step = step;
        hits.push_back (h);
        if (onHit)
            onHit (laneNote (lane), h.velocity);
        edited();
    }

    void setLength (int x)
    {
        const int n = juce::jlimit (1, ksp::model::kSteps, (x - kNameW + kCellW / 2) / kCellW);
        if (n != pattern->length && onLastStep)
            onLastStep (n);
    }

    void edited()
    {
        repaint();
        if (onEdited)
            onEdited();
    }

    ksp::model::DrumPattern* pattern = nullptr;
    Paint brush = Paint::none;
    int hoverLane = -1;
};

} // namespace ksp::ui
