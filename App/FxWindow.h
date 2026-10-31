/*
    FxWindow.h
    The effect positions of the Studio: four inserts on each track (after its instrument) and the master chain
    (before the limiter). Each position takes a VST3 effect of Leo's list (hidden plugins left out),
    with Edit (the plugin's own window) and Bypass. JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Host/Engine.h"

namespace ksp::ui
{

class FxPanel : public juce::Component
{
public:
    std::function<void (int, const juce::PluginDescription*)> onChoose;   // position, the plugin (nullptr = take it out)
    std::function<void (int)> onEdit;
    std::function<void (int, bool)> onBypass;

    FxPanel()
    {
        for (int p = 0; p < host::Engine::kFxPositions; ++p)
        {
            auto& r = rows[static_cast<std::size_t> (p)];
            for (juce::Component* c : { static_cast<juce::Component*> (&r.title), static_cast<juce::Component*> (&r.box),
                                        static_cast<juce::Component*> (&r.edit), static_cast<juce::Component*> (&r.bypass) })
                addAndMakeVisible (*c);
            r.title.setText (positionName (p), juce::dontSendNotification);
            r.title.setFont (juce::FontOptions (12.0f));
            r.edit.setButtonText ("Edit");
            r.bypass.setButtonText ("Byp");
            r.box.onChange = [this, p] { chosen (p); };
            r.edit.onClick = [this, p] { if (onEdit) onEdit (p); };
            r.bypass.onClick = [this, p] { if (onBypass) onBypass (p, rows[static_cast<std::size_t> (p)].bypass.getToggleState()); };
        }
        for (auto* h : { &headTracks, &headTracks2, &headSends })
            addAndMakeVisible (*h);
        headTracks.setText ("Track inserts: T1 and T2", juce::dontSendNotification);
        headTracks2.setText ("Track inserts: T3 and T4", juce::dontSendNotification);
        headSends.setText ("Master chain (before the limiter)", juce::dontSendNotification);
        setSize (1100, 360);
    }

    /** The effects the boxes offer. */
    void setChoices (std::vector<juce::PluginDescription> effects)
    {
        choices = std::move (effects);
        std::sort (choices.begin(), choices.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        for (int p = 0; p < host::Engine::kFxPositions; ++p)
            rebuild (p);
    }

    /** What position `p` holds now: the loaded plugin (null = none), a missing one's name, and its bypass state. */
    void show (int p, const juce::PluginDescription* loaded, const juce::String& missing, bool bypass)
    {
        auto& r = rows[static_cast<std::size_t> (p)];
        shown[static_cast<std::size_t> (p)] = { loaded != nullptr ? *loaded : juce::PluginDescription(), loaded != nullptr, missing };
        r.bypass.setToggleState (bypass, juce::dontSendNotification);
        r.edit.setEnabled (loaded != nullptr);
        r.bypass.setEnabled (loaded != nullptr);
        rebuild (p);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        const int colW = (area.getWidth() - 2 * 14) / 3;
        auto col1 = area.removeFromLeft (colW);
        area.removeFromLeft (14);
        auto col2 = area.removeFromLeft (colW);
        area.removeFromLeft (14);
        auto col3 = area;
        headTracks.setBounds (col1.removeFromTop (20));
        headTracks2.setBounds (col2.removeFromTop (20));
        headSends.setBounds (col3.removeFromTop (20));
        const auto place = [] (juce::Rectangle<int>& col, Row& r) {
            auto row = col.removeFromTop (28);
            r.title.setBounds (row.removeFromLeft (80));
            r.bypass.setBounds (row.removeFromRight (48));
            r.edit.setBounds (row.removeFromRight (44));
            r.box.setBounds (row.reduced (2, 0));
            col.removeFromTop (3);
        };
        for (int p = 0; p < 8; ++p)
            place (col1, rows[static_cast<std::size_t> (p)]);
        for (int p = 8; p < 16; ++p)
            place (col2, rows[static_cast<std::size_t> (p)]);
        for (int p = 16; p < host::Engine::kFxPositions; ++p)
            place (col3, rows[static_cast<std::size_t> (p)]);
    }

private:
    struct Row
    {
        juce::Label title;
        juce::ComboBox box;
        juce::TextButton edit;
        juce::ToggleButton bypass;
    };

    struct Shown
    {
        juce::PluginDescription desc;
        bool loaded = false;
        juce::String missing;
    };

    static juce::String positionName (int p)
    {
        if (p < host::Engine::kMasterFxBase)
            return "T" + juce::String (p / host::Engine::kInserts + 1) + " insert " + juce::String (p % host::Engine::kInserts + 1);
        return "Master " + juce::String (p - host::Engine::kMasterFxBase + 1);
    }

    void rebuild (int p)
    {
        auto& r = rows[static_cast<std::size_t> (p)];
        const auto& sh = shown[static_cast<std::size_t> (p)];
        r.box.clear (juce::dontSendNotification);
        r.box.addItem ("(none)", 1);
        int selected = 1;
        for (std::size_t k = 0; k < choices.size(); ++k)
        {
            r.box.addItem (choices[k].name + "  (" + choices[k].manufacturerName + ")", static_cast<int> (k) + 2);
            if (sh.loaded && sh.desc.createIdentifierString() == choices[k].createIdentifierString())
                selected = static_cast<int> (k) + 2;
        }
        if (sh.loaded && selected == 1)
        {
            r.box.addItem (sh.desc.name + " (hidden)", 1000);
            selected = 1000;
        }
        if (! sh.loaded && sh.missing.isNotEmpty())
        {
            r.box.addItem ("Missing: " + sh.missing, 1001);
            selected = 1001;
        }
        r.box.setSelectedId (selected, juce::dontSendNotification);
    }

    void chosen (int p)
    {
        const int id = rows[static_cast<std::size_t> (p)].box.getSelectedId();
        if (id == 1000 || id == 1001)
            return;
        if (! onChoose)
            return;
        if (id == 1)
            onChoose (p, nullptr);
        else if (id >= 2 && static_cast<std::size_t> (id - 2) < choices.size())
            onChoose (p, &choices[static_cast<std::size_t> (id - 2)]);
    }

    std::array<Row, host::Engine::kFxPositions> rows;
    std::array<Shown, host::Engine::kFxPositions> shown;
    std::vector<juce::PluginDescription> choices;
    juce::Label headTracks, headTracks2, headSends;
};

class FxWindow : public juce::DocumentWindow
{
public:
    FxWindow (std::function<void()> onClose)
        : DocumentWindow ("Effects", ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        panel = new FxPanel();
        setContentOwned (panel, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    FxPanel& content() { return *panel; }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    FxPanel* panel = nullptr;
    std::function<void()> closed;
};

} // namespace ksp::ui
