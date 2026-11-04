/*
    PluginTable.h
    The window with the list of VST3 plugins found: name, maker and type, a filter box, and a checkbox per plugin to hide it from
    the lists the tracks offer (it is kept, not deleted; "Show hidden" brings it back into view). JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "Host/PluginList.h"

namespace ksp::ui
{

class PluginTable : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit PluginTable (std::shared_ptr<ksp::host::PluginList> l) : list (std::move (l))
    {
        addAndMakeVisible (table);
        for (auto* c : { static_cast<juce::Component*> (&filterBox), static_cast<juce::Component*> (&typeBox),
                         static_cast<juce::Component*> (&showHiddenToggle), static_cast<juce::Component*> (&countLabel) })
            addAndMakeVisible (*c);

        table.setModel (this);
        table.setRowHeight (24);
        auto& h = table.getHeader();
        h.addColumn ("Hide", 1, 46, 46, 46, juce::TableHeaderComponent::notResizable);
        h.addColumn ("Name", 2, 280, 120, 600);
        h.addColumn ("Maker", 3, 170, 80, 400);
        h.addColumn ("Type", 4, 90, 60, 140);
        h.setStretchToFitActive (true);

        filterBox.setTextToShowWhenEmpty ("Filter by name or maker", juce::Colours::grey);
        filterBox.onTextChange = [this] { refresh(); };
        typeBox.addItem ("All", 1);
        typeBox.addItem ("Instruments", 2);
        typeBox.addItem ("Effects", 3);
        typeBox.setSelectedId (1, juce::dontSendNotification);
        typeBox.onChange = [this] { refresh(); };
        showHiddenToggle.setButtonText ("Show hidden");
        showHiddenToggle.setToggleState (false, juce::dontSendNotification);
        showHiddenToggle.onClick = [this] { refresh(); };

        setSize (720, 520);
        refresh();
    }

    /** Re-reads the list (after a scan) and applies the filters. */
    void refresh()
    {
        rows.clear();
        const auto text = filterBox.getText().trim();
        const int type = typeBox.getSelectedId();
        for (const auto& d : list->all())
        {
            if (d.manufacturerName == "KeyStepPro" || (type == 2 && ! d.isInstrument) || (type == 3 && d.isInstrument))
                continue;
            if (! showHiddenToggle.getToggleState() && list->isHidden (d))
                continue;
            if (text.isNotEmpty() && ! d.name.containsIgnoreCase (text) && ! d.manufacturerName.containsIgnoreCase (text))
                continue;
            rows.push_back (d);
        }
        std::sort (rows.begin(), rows.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        table.updateContent();
        table.repaint();
        countLabel.setText (juce::String (rows.size()) + " shown, " + juce::String (list->numHidden()) + " hidden in total",
                            juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (28);
        filterBox.setBounds (top.removeFromLeft (260));
        top.removeFromLeft (6);
        typeBox.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (10);
        showHiddenToggle.setBounds (top.removeFromLeft (120));
        r.removeFromTop (6);
        countLabel.setBounds (r.removeFromBottom (22));
        table.setBounds (r);
    }

private:
    class HideToggle : public juce::ToggleButton
    {
    public:
        std::function<void (bool)> onChange;
        void clicked() override { if (onChange) onChange (getToggleState()); }
    };

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? ksp::ui::palette::blue.withAlpha (0.35f) : (row % 2 ? ksp::ui::palette::gridB : ksp::ui::palette::gridA));
    }

    void paintCell (juce::Graphics& g, int row, int column, int w, int h, bool) override
    {
        if (row < 0 || row >= static_cast<int> (rows.size()) || column == 1)
            return;
        const auto& d = rows[static_cast<std::size_t> (row)];
        g.setColour (list->isHidden (d) ? juce::Colours::grey : ksp::ui::palette::ink);
        g.setFont (14.0f);
        const juce::String text = column == 2 ? d.name : column == 3 ? d.manufacturerName : juce::String (d.isInstrument ? "Instrument" : "Effect");
        g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForCell (int row, int column, bool, juce::Component* existing) override
    {
        if (column != 1 || row < 0 || row >= static_cast<int> (rows.size()))
        {
            delete existing;
            return nullptr;
        }
        auto* t = dynamic_cast<HideToggle*> (existing);
        if (t == nullptr)
        {
            delete existing;
            t = new HideToggle();
        }
        const auto d = rows[static_cast<std::size_t> (row)];
        t->setToggleState (list->isHidden (d), juce::dontSendNotification);
        juce::Component::SafePointer<PluginTable> self (this);
        t->onChange = [self, d] (bool hide) {
            if (self == nullptr)
                return;
            self->list->setHidden (d, hide);
            juce::MessageManager::callAsync ([self] { if (self != nullptr) self->refresh(); });
        };
        return t;
    }

    int getNumRows() override { return static_cast<int> (rows.size()); }

    std::shared_ptr<ksp::host::PluginList> list;
    std::vector<juce::PluginDescription> rows;
    juce::TableListBox table;
    juce::TextEditor filterBox;
    juce::ComboBox typeBox;
    juce::ToggleButton showHiddenToggle;
    juce::Label countLabel;
};

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (std::shared_ptr<ksp::host::PluginList> list, std::function<void()> onClose)
        : DocumentWindow ("VST3 plugins", ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (table = new PluginTable (std::move (list)), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void refresh() { table->refresh(); }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    PluginTable* table = nullptr;
    std::function<void()> closed;
};

} // namespace ksp::ui
