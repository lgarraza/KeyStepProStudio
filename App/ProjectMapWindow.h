/*
    ProjectMapWindow.h
    "Project Map": the open project as one picture. Three views of the same ProjectMap (Source/Model/ProjectMap.h): the scene matrix (scenes x tracks,
    a thumbnail of the pattern each track plays), the chain timeline of one scene (patterns to scale in steps), and the pattern bank (16 per track and
    the scenes that use each). Hover or click a cell for the detail on the right. Read only. JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include "../Source/Model/ProjectMap.h"
#include "../Source/Ksp/Protocol.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <optional>
#include <functional>
#include <vector>

namespace ksp::ui
{

class ProjectMapPanel : public juce::Component
{
public:
    ProjectMapPanel()
    {
        const char* names[] { "Scene matrix", "Chain timeline", "Pattern bank" };
        for (int v = 0; v < 3; ++v)
        {
            auto& b = tabs[static_cast<std::size_t> (v)];
            b.setButtonText (names[v]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (1);
            b.onClick = [this, v] { view = v; hover = {}; repaint(); };
            addAndMakeVisible (b);
        }
        tabs[0].setToggleState (true, juce::dontSendNotification);
        undoButton.setButtonText ("Undo edit");
        undoButton.setEnabled (false);
        undoButton.onClick = [this] { if (onUndo) onUndo(); };
        addAndMakeVisible (undoButton);
        setSize (1120, 640);
    }

    /** Edits: `onEditScene` writes a scene (scene 1..16); `onClearScene` resets one; `onUndo` takes back the last map edit. */
    std::function<void (int, const ksp::model::Scene&)> onEditScene;
    std::function<void (int)> onClearScene;
    std::function<void()> onUndo;

    void setCanUndo (bool can) { undoButton.setEnabled (can); }

    void setProject (const ksp::model::ProjectMap& m, const juce::String& name)
    {
        map = m;
        title = name;
        hover = {};
        int first = 1;
        for (int s = 1; s <= ksp::model::kScenes; ++s)
            if (map.scenes[static_cast<std::size_t> (s - 1)].valid)
            {
                first = s;
                break;
            }
        scene = first;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        auto top = r.removeFromTop (28);
        top.removeFromLeft (4);
        for (auto& b : tabs)
        {
            b.setBounds (top.removeFromLeft (130));
            top.removeFromLeft (6);
        }
        undoButton.setBounds (top.removeFromRight (100));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace palette;
        g.fillAll (chassis);
        hits.clear();

        auto r = getLocalBounds().reduced (12, 10);
        auto top = r.removeFromTop (28);
        g.setColour (inkSoft);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Structure of the KeyStep Pro project:  " + title + "   (tracks, patterns, scenes, chains)", top.withTrimmedLeft (3 * 136 + 8).withTrimmedRight (108),
                    juce::Justification::centredLeft);
        r.removeFromTop (8);
        paintSummary (g, r.removeFromTop (48));
        r.removeFromTop (12);

        auto detail = r.removeFromRight (270);
        r.removeFromRight (12);
        if (view == 0)
            paintMatrix (g, r);
        else if (view == 1)
            paintChains (g, r);
        else
            paintBank (g, r);
        paintDetail (g, detail);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool onScene = false;
        for (const auto& h : hits)
            if (h.first.contains (e.getPosition()))
                onScene = h.second.sceneButton;
        setMouseCursor (onScene ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        pick (e.getPosition(), false);
    }
    void mouseDown (const juce::MouseEvent& e) override { pick (e.getPosition(), true); }
    void mouseExit (const juce::MouseEvent&) override {}

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (view != 0)
            return;
        for (const auto& h : hits)
            if (h.first.contains (e.getPosition()))
            {
                if (h.second.sceneButton && e.mods.isPopupMenu())
                    sceneMenu (h.second.scene);
                else if (! h.second.sceneButton && h.second.scene > 0 && h.second.track >= 0)
                    cellMenu (h.second.scene, h.second.track);
                return;
            }
    }

private:
    struct Hit
    {
        int scene = 0;        // 1..16, 0 = none
        int track = -1;       // 0..3
        int pat = 0;          // 1..16
        int index = -1;       // position inside a chain
        bool sceneButton = false;
        bool operator== (const Hit& o) const { return scene == o.scene && track == o.track && pat == o.pat && index == o.index && sceneButton == o.sceneButton; }
    };

    void pick (juce::Point<int> pos, bool click)
    {
        for (const auto& h : hits)
            if (h.first.contains (pos))
            {
                if (h.second.sceneButton)
                {
                    if (click)   // selects the scene (empty ones too: the timeline says it is not saved)
                    {
                        scene = h.second.scene;
                        hover = {};
                        repaint();
                    }
                    return;
                }
                if (! (h.second == hover))
                {
                    hover = h.second;
                    repaint();
                }
                return;
            }
    }

    /** Pick the pattern a track plays in a scene (drops that cell's chain; the scene becomes a saved scene). */
    void cellMenu (int s, int t)
    {
        if (! onEditScene)
            return;
        juce::PopupMenu m;
        m.addSectionHeader ("Scene " + juce::String (s) + ", track " + juce::String (t + 1) + ": play pattern");
        const auto& sc = map.scenes[static_cast<std::size_t> (s - 1)];
        for (int k = 1; k <= ksp::model::kPatterns; ++k)
        {
            const auto* p = patternAt (t, k);
            const bool cur = sc.valid && sc.chain[static_cast<std::size_t> (t)].empty() && sc.pattern[static_cast<std::size_t> (t)] == k;
            m.addItem (k, "Pattern " + juce::String (k) + (p == nullptr ? "  (empty)" : "  (" + juce::String (p->length) + " steps)"), true, cur);
        }
        if (sc.valid && ! sc.chain[static_cast<std::size_t> (t)].empty())
        {
            m.addSeparator();
            m.addItem (100, "Remove the chain (keep pattern " + juce::String (sc.pattern[static_cast<std::size_t> (t)]) + ")");
        }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, s, t, sc] (int r) {
            if (r == 0)
                return;
            auto n = sc;
            if (r <= 99)
                n.pattern[static_cast<std::size_t> (t)] = r;
            n.chain[static_cast<std::size_t> (t)].clear();
            onEditScene (s, n);
        });
    }

    void sceneMenu (int s)
    {
        if (! onEditScene)
            return;
        const auto sc = map.scenes[static_cast<std::size_t> (s - 1)];
        juce::PopupMenu m;
        m.addSectionHeader ("Scene " + juce::String (s));
        m.addItem (1, "Copy", sc.valid);
        m.addItem (2, "Paste", clipboard.has_value());
        m.addItem (3, "Clear", sc.valid);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, s, sc] (int r) {
            if (r == 1)
                clipboard = sc;
            else if (r == 2 && clipboard.has_value())
                onEditScene (s, *clipboard);
            else if (r == 3 && onClearScene)
                onClearScene (s);
        });
    }

    const ksp::model::Pattern* patternAt (int track, int pat) const
    {
        if (track < 0 || track > 3 || pat < 1 || pat > ksp::model::kPatterns)
            return nullptr;
        const auto& p = map.patterns[static_cast<std::size_t> (track)][static_cast<std::size_t> (pat - 1)];
        return p.valid ? &p : nullptr;
    }

    static juce::Colour textOn (juce::Colour c) { return c.getPerceivedBrightness() > 0.6f ? palette::ink : juce::Colours::white; }

    // ---- drawing helpers
    static void drawThumb (juce::Graphics& g, const ksp::model::Pattern& p, juce::Rectangle<int> area, juce::Colour c, bool grid)
    {
        using namespace ksp::model;
        g.setColour (palette::gridA);
        g.fillRect (area);
        g.setColour (c.withAlpha (0.13f));
        g.fillRect (area);
        const auto st = noteStats (p);
        const int lo = st.count > 0 ? st.lo : 60, hi = st.count > 0 ? st.hi : 60;
        const int rows = juce::jmax (hi - lo + 1, 6);
        const auto a = area.toFloat().reduced (2.0f);
        const float sw = a.getWidth() / static_cast<float> (p.length), rh = a.getHeight() / static_cast<float> (rows);
        if (grid)
        {
            g.setColour (palette::gridB);
            for (int i = 0; i < p.length; i += 4)
                g.fillRect (a.getX() + sw * static_cast<float> (i), a.getY(), juce::jmax (1.0f, sw * (i % 16 == 0 ? 0.5f : 0.2f)), a.getHeight());
        }
        g.setColour (c.darker (0.25f));
        for (int i = 0; i < p.length && i < kSteps; ++i)
        {
            const auto& s = p.steps[static_cast<std::size_t> (i)];
            if (! s.on)
                continue;
            for (const auto& n : s.notes)
                g.fillRect (a.getX() + sw * static_cast<float> (i), a.getBottom() - rh * static_cast<float> (n.pitch - lo + 1), juce::jmax (sw - 0.5f, 1.0f),
                            juce::jmax (rh - 0.5f, 1.0f));
        }
    }

    static void drawEmpty (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (palette::gridB);
        g.fillRect (area);
        g.setColour (palette::gridLine);
        g.saveState();
        g.reduceClipRegion (area);
        for (int x = area.getX() - area.getHeight(); x < area.getRight(); x += 8)
            g.drawLine (static_cast<float> (x), static_cast<float> (area.getBottom()), static_cast<float> (x + area.getHeight()), static_cast<float> (area.getY()), 1.0f);
        g.restoreState();
        g.setColour (palette::gridLine);
        g.drawRect (area);
    }

    static void badge (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour bg, juce::Colour fg)
    {
        g.setColour (bg);
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        g.setColour (fg);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (text, r, juce::Justification::centred);
    }

    void trackHeader (juce::Graphics& g, juce::Rectangle<int> r, int t)
    {
        const auto c = palette::trackColour (t);
        g.setColour (c);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (textOn (c));
        g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
        g.drawText ("T" + juce::String (t + 1), r, juce::Justification::centred);
    }

    void sceneHeader (juce::Graphics& g, juce::Rectangle<int> r, int s, bool selected)
    {
        const bool saved = map.scenes[static_cast<std::size_t> (s - 1)].valid;
        g.setColour (palette::panelDark.withAlpha (saved ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        if (selected)
        {
            g.setColour (palette::lit);
            g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f, 2.0f);
        }
        g.setColour (palette::segOn);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
        g.drawText (juce::String (s).paddedLeft ('0', 2), r.withTrimmedBottom (r.getHeight() / 3), juce::Justification::centred);
        g.setColour (saved ? palette::lit : palette::inkSoft);
        g.setFont (juce::FontOptions (8.5f));
        g.drawText (saved ? "SAVED" : "EMPTY", r.withTrimmedTop (r.getHeight() * 2 / 3), juce::Justification::centred);
    }

    void outline (juce::Graphics& g, juce::Rectangle<int> r, bool on)
    {
        g.setColour (on ? palette::ink : palette::gridLine);
        g.drawRect (r, on ? 2 : 1);
    }

    // ---- the quick-scan strip: how full the project is, then one card per track
    void paintSummary (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        const int gap = 8, cardW = (r.getWidth() - gap * 4) / 5;
        auto card = [&] (juce::Rectangle<int> a, juce::Colour stripe) {
            g.setColour (palette::gridA);
            g.fillRoundedRectangle (a.toFloat(), 4.0f);
            g.setColour (palette::edge);
            g.drawRoundedRectangle (a.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (stripe);
            g.fillRect (a.getX() + 1, a.getY() + 4, 5, a.getHeight() - 8);
        };

        auto a = r.removeFromLeft (cardW);
        r.removeFromLeft (gap);
        card (a, palette::lit);
        g.setColour (palette::ink);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
        g.drawText (juce::String (map.savedScenes) + "/16", a.withTrimmedLeft (14).removeFromLeft (64), juce::Justification::centredLeft);
        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("scenes saved\n" + juce::String (map.usedPatterns) + " of 64 patterns recorded", a.withTrimmedLeft (80).reduced (0, 4),
                          juce::Justification::centredLeft, 2);

        for (int t = 0; t < 4; ++t)
        {
            auto b = r.removeFromLeft (cardW);
            r.removeFromLeft (gap);
            const auto c = palette::trackColour (t);
            card (b, c);
            int recorded = 0, chains = 0;
            std::vector<int> lens;
            for (int k = 0; k < kPatterns; ++k)
            {
                const auto& pt = map.patterns[static_cast<std::size_t> (t)][static_cast<std::size_t> (k)];
                if (! pt.valid)
                    continue;
                ++recorded;
                if (std::find (lens.begin(), lens.end(), pt.length) == lens.end())
                    lens.push_back (pt.length);
            }
            for (const auto& sc : map.scenes)
                if (sc.valid && ! sc.chain[static_cast<std::size_t> (t)].empty())
                    ++chains;
            std::sort (lens.begin(), lens.end());
            juce::String ls;
            for (std::size_t i = 0; i < lens.size() && i < 4; ++i)
                ls << (i ? "/" : "") << lens[i];
            auto inner = b.withTrimmedLeft (14).reduced (0, 5);
            g.setColour (palette::ink);
            g.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
            g.drawText ("T" + juce::String (t + 1) + "  " + juce::String (recorded) + " patterns", inner.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (palette::inkSoft);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (chains) + (chains == 1 ? " chain" : " chains") + (ls.isEmpty() ? juce::String() : "  |  steps " + ls), inner,
                        juce::Justification::centredLeft);
        }
    }

    // ---- view 1: scene matrix
    void paintMatrix (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        const int labelW = 36, headH = 36, gap = 3;
        const int cw = (r.getWidth() - labelW - gap * kScenes) / kScenes;
        const int ch = juce::jmax (40, juce::jmin (96, (r.getHeight() - headH - 40 - gap * 4) / 4));
        {   // the selected scene (click its number) keeps a column band
            g.setColour (palette::lit.withAlpha (0.24f));
            g.fillRect (r.getX() + labelW + (scene - 1) * (cw + gap) - 2, r.getY(), cw + 4, headH + 4 * (ch + gap));
        }
        if (hover.scene > 0)   // the scene's column and the track's row stand out
        {
            g.setColour (palette::lit.withAlpha (0.16f));
            g.fillRect (r.getX() + labelW + (hover.scene - 1) * (cw + gap) - 2, r.getY(), cw + 4, headH + 4 * (ch + gap));
        }
        g.setColour (palette::blue);
        g.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        g.drawText ("SCENE", r.getX(), r.getY(), labelW - 4, headH - 4, juce::Justification::centred);
        for (int s = 1; s <= kScenes; ++s)
        {
            const int x = r.getX() + labelW + (s - 1) * (cw + gap);
            const juce::Rectangle<int> head { x, r.getY(), cw, headH - 4 };
            sceneHeader (g, head, s, s == hover.scene || s == scene);
            hits.push_back ({ head, { s, -1, 0, -1, true } });
        }
        for (int t = 0; t < 4; ++t)
        {
            const int y = r.getY() + headH + t * (ch + gap);
            if (hover.track == t && hover.scene > 0)
            {
                g.setColour (palette::trackColour (t).withAlpha (0.22f));
                g.fillRect (r.getX(), y - 2, r.getWidth(), ch + 4);
            }
            trackHeader (g, { r.getX(), y, labelW - 4, ch }, t);
            for (int s = 1; s <= kScenes; ++s)
            {
                const juce::Rectangle<int> cell { r.getX() + labelW + (s - 1) * (cw + gap), y, cw, ch };
                const auto& sc = map.scenes[static_cast<std::size_t> (s - 1)];
                const int pn = sc.pattern[static_cast<std::size_t> (t)];
                const auto* p = sc.valid ? patternAt (t, pn) : nullptr;
                hits.push_back ({ cell, { s, t, p != nullptr ? pn : 0, -1, false } });
                const bool on = hover.scene == s && hover.track == t;
                if (p == nullptr)
                {
                    drawEmpty (g, cell);
                    outline (g, cell, on);
                    continue;
                }
                drawThumb (g, *p, cell, palette::trackColour (t), false);
                const auto& c = sc.chain[static_cast<std::size_t> (t)];
                if (! c.empty())
                {
                    // the chain, in play order, as a strip along the bottom: one segment per pattern, wide as its length
                    const auto order = playOrder (map, s, t);
                    const int total = juce::jmax (1, loopSteps (map, s, t));
                    float x = static_cast<float> (cell.getX());
                    const int stripH = 9;
                    g.setColour (palette::panelDark);
                    g.fillRect (cell.getX(), cell.getBottom() - stripH, cell.getWidth(), stripH);
                    for (std::size_t i = 0; i < order.size(); ++i)
                    {
                        const auto* cp = patternAt (t, order[i]);
                        const float w = static_cast<float> (cell.getWidth()) * static_cast<float> (cp != nullptr ? cp->length : 16) / static_cast<float> (total);
                        g.setColour (palette::trackColour (t).withMultipliedBrightness (i % 2 ? 0.7f : 1.0f));
                        g.fillRect (x + 1.0f, static_cast<float> (cell.getBottom() - stripH + 1), juce::jmax (1.0f, w - 2.0f), static_cast<float> (stripH - 2));
                        x += w;
                    }
                    badge (g, { cell.getRight() - 31, cell.getBottom() - stripH - 14, 29, 13 }, juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (static_cast<int> (c.size())), palette::lit,
                           palette::panelDark);
                }
                outline (g, cell, on);
                badge (g, { cell.getX() + 2, cell.getY() + 2, 18, 13 }, juce::String (pn), palette::panelDark, palette::segOn);
            }
        }
        auto note = r.withTop (r.getY() + headH + 4 * (ch + gap) + 6);
        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Each cell is the pattern that track plays in that scene, drawn as its own piano roll. The number is the pattern, x n means the track plays "
                          "a chain of n patterns; the strip at the bottom of the cell shows that chain in order, to scale. Hatched: nothing to play. Click a cell to pick its pattern; click a scene number to select that scene (the Chain timeline shows it), right-click it to copy, paste or clear it. Drums are not shown yet.",
                          note, juce::Justification::topLeft, 3);
    }

    // ---- view 2: chain timeline of one scene
    void paintChains (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        auto pickRow = r.removeFromTop (30);
        g.setColour (palette::blue);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText ("SCENE", pickRow.removeFromLeft (46), juce::Justification::centredLeft);
        const int bw = juce::jmin (40, (pickRow.getWidth() - 3 * kScenes) / kScenes);
        for (int s = 1; s <= kScenes; ++s)
        {
            const juce::Rectangle<int> b { pickRow.getX() + (s - 1) * (bw + 3), pickRow.getY(), bw, 28 };
            sceneHeader (g, b, s, s == scene);
            hits.push_back ({ b, { s, -1, 0, -1, true } });
        }
        r.removeFromTop (10);

        int maxLoop = 0;
        for (int t = 0; t < 4; ++t)
            maxLoop = juce::jmax (maxLoop, loopSteps (map, scene, t));
        if (maxLoop == 0)
        {
            g.setColour (palette::inkSoft);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("This scene is not saved. Pick one with SAVED under its number.", r.removeFromTop (40), juce::Justification::centredLeft);
            return;
        }

        const int labelW = 36, laneH = 72, gap = 10;
        const int trackW = r.getWidth() - labelW;
        const float perStep = static_cast<float> (trackW) / static_cast<float> (maxLoop);

        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (10.0f));
        for (int m = 0; m < maxLoop; m += 64)
        {
            const int x = r.getX() + labelW + static_cast<int> (static_cast<float> (m) * perStep);
            g.drawVerticalLine (x, static_cast<float> (r.getY() + 12), static_cast<float> (r.getY() + 16));
            g.drawText (juce::String (m), x + 3, r.getY(), 40, 14, juce::Justification::centredLeft);
        }
        r.removeFromTop (18);

        for (int t = 0; t < 4; ++t)
        {
            const int y = r.getY() + t * (laneH + gap);
            trackHeader (g, { r.getX(), y, labelW - 4, laneH }, t);
            const auto order = playOrder (map, scene, t);
            float x = static_cast<float> (r.getX() + labelW);
            for (std::size_t i = 0; i < order.size(); ++i)
            {
                const auto* p = patternAt (t, order[i]);
                const int len = p != nullptr ? p->length : 16;
                const int w = juce::jmax (8, static_cast<int> (static_cast<float> (len) * perStep) - 2);
                const juce::Rectangle<int> blk { static_cast<int> (x), y, w, laneH };
                x += static_cast<float> (len) * perStep;
                hits.push_back ({ blk, { scene, t, order[i], static_cast<int> (i), false } });
                if (p == nullptr)
                    drawEmpty (g, blk);
                else
                    drawThumb (g, *p, blk, palette::trackColour (t), false);
                outline (g, blk, hover.scene == scene && hover.track == t && hover.index == static_cast<int> (i));
                badge (g, { blk.getX() + 2, blk.getY() + 2, 18, 13 }, juce::String (order[i]), palette::panelDark, palette::segOn);
                g.setColour (palette::inkSoft);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (len), blk.reduced (3, 1), juce::Justification::bottomRight);
            }
        }
        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Blocks are to scale, in steps. A chain plays its patterns in order and loops; a repeat is the same pattern listed twice. Tracks with "
                          "different lengths drift against each other.",
                          r.withTop (r.getY() + 4 * (laneH + gap) + 2), juce::Justification::topLeft, 3);
    }

    // ---- view 3: pattern bank
    void paintBank (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        const int labelW = 36, headH = 22, gap = 3, usedH = 15;
        const int cw = (r.getWidth() - labelW - gap * kPatterns) / kPatterns;
        const int ch = juce::jmin (84, (r.getHeight() - headH - 30 - 4 * (gap + usedH)) / 4);
        for (int k = 1; k <= kPatterns; ++k)
        {
            const int x = r.getX() + labelW + (k - 1) * (cw + gap);
            g.setColour (palette::panelDark);
            g.fillRoundedRectangle (juce::Rectangle<int> (x, r.getY(), cw, headH - 4).toFloat(), 3.0f);
            g.setColour (palette::segOn);
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
            g.drawText (juce::String (k).paddedLeft ('0', 2), x, r.getY(), cw, headH - 4, juce::Justification::centred);
        }
        for (int t = 0; t < 4; ++t)
        {
            const int y = r.getY() + headH + t * (ch + usedH + gap);
            trackHeader (g, { r.getX(), y, labelW - 4, ch }, t);
            for (int k = 1; k <= kPatterns; ++k)
            {
                const juce::Rectangle<int> cell { r.getX() + labelW + (k - 1) * (cw + gap), y, cw, ch };
                const auto* p = patternAt (t, k);
                hits.push_back ({ cell, { 0, t, k, -1, false } });
                if (p == nullptr)
                    drawEmpty (g, cell);
                else
                    drawThumb (g, *p, cell, palette::trackColour (t), false);
                outline (g, cell, hover.scene == 0 && hover.track == t && hover.pat == k);
                if (p != nullptr)
                {
                    const auto used = scenesUsing (map, t, k);
                    g.setColour (palette::inkSoft);
                    g.setFont (juce::FontOptions (9.5f));
                    juce::String u = "S";
                    for (std::size_t i = 0; i < used.size(); ++i)
                        u << (i ? "," : "") << used[i];
                    g.drawText (used.empty() ? juce::String ("unused") : u, cell.getX() - 2, cell.getBottom() + 1, cw + 4, usedH - 2, juce::Justification::centred);
                }
            }
        }
        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Under each pattern: the scenes that play it, alone or inside a chain (S1,3 = scenes 1 and 3). Hatched: that pattern was never recorded.",
                          r.withTop (r.getY() + headH + 4 * (ch + usedH + gap) + 4), juce::Justification::topLeft, 3);
    }

    // ---- detail panel
    void paintDetail (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        g.setColour (palette::gridA);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (palette::edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        r.reduce (10, 10);
        g.setColour (palette::ink);
        g.setFont (juce::FontOptions (12.5f));

        if (hover.track < 0)
        {
            if (view == 0 && scene >= 1 && scene <= kScenes)
            {
                paintSceneSummary (g, r);
                return;
            }
            g.setColour (palette::inkSoft);
            g.drawFittedText ("Hover or click a cell to see the pattern, its length, notes and chain.", r, juce::Justification::topLeft, 4);
            return;
        }
        const auto* p = patternAt (hover.track, hover.pat);
        auto head = r.removeFromTop (22);
        const auto tc = palette::trackColour (hover.track);
        badge (g, head.removeFromLeft (86), "TRACK " + juce::String (hover.track + 1), tc, textOn (tc));
        head.removeFromLeft (6);
        if (hover.scene > 0)
        {
            badge (g, head.removeFromLeft (70), "SCENE " + juce::String (hover.scene), palette::panelDark, palette::segOn);
            head.removeFromLeft (6);
        }
        if (hover.pat > 0)
            badge (g, head.removeFromLeft (60), "PAT " + juce::String (hover.pat), palette::panelDark, palette::segOn);
        r.removeFromTop (10);
        if (p == nullptr)
        {
            g.setColour (palette::inkSoft);
            g.drawFittedText (hover.scene > 0 ? "Nothing to play on this track in this scene." : "This pattern was never recorded.", r, juce::Justification::topLeft, 3);
            return;
        }
        auto roll = r.removeFromTop (110);
        drawThumb (g, *p, roll, tc, true);
        g.setColour (palette::gridLine);
        g.drawRect (roll);
        r.removeFromTop (10);

        const auto st = noteStats (*p);
        auto line = [&] (const juce::String& k, const juce::String& v) {
            auto row = r.removeFromTop (19);
            g.setColour (palette::inkSoft);
            g.drawText (k, row.removeFromLeft (84), juce::Justification::centredLeft);
            g.setColour (palette::ink);
            g.drawText (v, row, juce::Justification::centredLeft);
        };
        line ("Length", juce::String (p->length) + " steps (" + juce::String (p->length / 16.0, 2) + " bars)");
        line ("Notes", juce::String (st.count));
        if (st.count > 0)
            line ("Range", juce::String (ksp::noteName (st.lo)) + " to " + ksp::noteName (st.hi));
        line ("Root, scale", juce::String (ksp::pitchClassName (p->root)) + ", " + ksp::scaleName (p->scale));
        const auto used = scenesUsing (map, hover.track, hover.pat);
        juce::String u;
        for (std::size_t i = 0; i < used.size(); ++i)
            u << (i ? ", " : "") << used[i];
        line ("Used in", used.empty() ? juce::String ("no scene") : "scenes " + u);

        if (hover.scene > 0)
        {
            const auto& c = map.scenes[static_cast<std::size_t> (hover.scene - 1)].chain[static_cast<std::size_t> (hover.track)];
            if (! c.empty())
            {
                r.removeFromTop (8);
                g.setColour (palette::blue);
                g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
                g.drawText ("CHAIN IN SCENE " + juce::String (hover.scene), r.removeFromTop (16), juce::Justification::centredLeft);
                auto row = r.removeFromTop (20);
                for (std::size_t i = 0; i < c.size() && row.getWidth() > 24; ++i)
                {
                    const bool cur = hover.index < 0 ? c[i] == hover.pat : static_cast<int> (i) == hover.index;
                    badge (g, row.removeFromLeft (22), juce::String (c[i]), cur ? palette::lit : palette::panelDark, cur ? palette::panelDark : palette::segOn);
                    row.removeFromLeft (3);
                }
            }
        }
    }

    /** The selected scene as a whole: what each track plays and for how many bars. */
    void paintSceneSummary (juce::Graphics& g, juce::Rectangle<int> r)
    {
        using namespace ksp::model;
        const auto& sc = map.scenes[static_cast<std::size_t> (scene - 1)];
        auto head = r.removeFromTop (22);
        badge (g, head.removeFromLeft (86), "SCENE " + juce::String (scene), palette::panelDark, palette::segOn);
        r.removeFromTop (10);
        if (! sc.valid)
        {
            g.setColour (palette::inkSoft);
            g.drawFittedText ("This scene is empty. Click a cell to pick a pattern and save it as a scene.", r, juce::Justification::topLeft, 3);
            return;
        }
        for (int t = 0; t < 4; ++t)
        {
            const auto tc = palette::trackColour (t);
            auto row = r.removeFromTop (24);
            badge (g, row.removeFromLeft (30), "T" + juce::String (t + 1), tc, textOn (tc));
            row.removeFromLeft (6);
            const auto& c = sc.chain[static_cast<std::size_t> (t)];
            if (c.empty())
            {
                g.setColour (palette::inkSoft);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (sc.pattern[static_cast<std::size_t> (t)] > 0 ? "pattern " + juce::String (sc.pattern[static_cast<std::size_t> (t)]) : juce::String ("nothing"),
                            row, juce::Justification::centredLeft);
            }
            else
                for (std::size_t i = 0; i < c.size() && row.getWidth() > 24; ++i)
                {
                    badge (g, row.removeFromLeft (22), juce::String (c[i]), palette::panelDark, palette::segOn);
                    row.removeFromLeft (3);
                }
            r.removeFromTop (6);
        }
        r.removeFromTop (6);
        int steps = 0;
        for (int t = 0; t < 4; ++t)
            steps = juce::jmax (steps, loopSteps (map, scene, t));
        g.setColour (palette::inkSoft);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Plays its chains for " + juce::String (steps / 16.0, 1) + " bars before it repeats. Hover a cell for its pattern; right-click the number to copy, paste or clear.",
                          r, juce::Justification::topLeft, 4);
    }

    std::array<juce::TextButton, 3> tabs;
    juce::TextButton undoButton;
    std::optional<ksp::model::Scene> clipboard;
    ksp::model::ProjectMap map;
    juce::String title { "Project" };
    int view = 0, scene = 1;
    Hit hover;
    std::vector<std::pair<juce::Rectangle<int>, Hit>> hits;
};

class ProjectMapWindow : public juce::DocumentWindow
{
public:
    ProjectMapWindow (std::function<void()> onClose, std::function<void()> onActivated)
        : DocumentWindow ("Project Map: structure of the KeyStep Pro project", ksp::ui::palette::chassis, DocumentWindow::closeButton | DocumentWindow::minimiseButton), closed (std::move (onClose)), activated (std::move (onActivated))
    {
        setUsingNativeTitleBar (true);
        panel = new ProjectMapPanel();
        setContentOwned (panel, true);
        setResizable (true, false);
        setResizeLimits (900, 560, 2400, 1200);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ProjectMapPanel& content() { return *panel; }
    void closeButtonPressed() override { if (closed) closed(); }
    void activeWindowStatusChanged() override
    {
        DocumentWindow::activeWindowStatusChanged();
        if (isActiveWindow() && activated)
            activated();   // pick up the edits made since the window was last in front
    }

private:
    ProjectMapPanel* panel = nullptr;
    std::function<void()> closed, activated;
};

}
