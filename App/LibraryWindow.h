/*
    LibraryWindow.h
    The window with the Studio library: one row per project (name, last change, what the KeyStep project holds, the instruments),
    Open (also a double click), Duplicate and Delete. The library is the truth of the app (the KeyStep Pro's memory is only a
    volatile working copy), so this works without a KeyStep connected. JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "Studio/Library.h"

namespace ksp::ui
{

class LibraryPanel : public juce::Component, private juce::TableListBoxModel
{
public:
    std::function<void (const std::string&)> onOpen;
    std::function<void (const std::string&)> onDeleted;

    explicit LibraryPanel (std::shared_ptr<ksp::studio::Library> l) : library (std::move (l))
    {
        for (auto* c : { static_cast<juce::Component*> (&table), static_cast<juce::Component*> (&openButton),
                         static_cast<juce::Component*> (&duplicateButton), static_cast<juce::Component*> (&deleteButton),
                         static_cast<juce::Component*> (&countLabel) })
            addAndMakeVisible (*c);

        table.setModel (this);
        table.setRowHeight (24);
        auto& h = table.getHeader();
        h.addColumn ("Name", 1, 240, 100, 600);
        h.addColumn ("Changed", 2, 140, 80, 220);
        h.addColumn ("KeyStep project", 3, 270, 100, 500);
        h.addColumn ("Instruments", 4, 200, 80, 500);
        h.setStretchToFitActive (true);

        openButton.setButtonText ("Open");
        duplicateButton.setButtonText ("Duplicate");
        deleteButton.setButtonText ("Delete");
        openButton.onClick = [this] { openSelected(); };
        duplicateButton.onClick = [this] { duplicateSelected(); };
        deleteButton.onClick = [this] { deleteSelected(); };

        setSize (860, 480);
        refresh();
    }

    void refresh()
    {
        rows = library->list();
        table.updateContent();
        table.repaint();
        countLabel.setText (juce::String (rows.size()) + " project(s) in " + juce::String (library->path().string()),
                            juce::dontSendNotification);
        updateButtons();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto bottom = r.removeFromBottom (30);
        openButton.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (6);
        duplicateButton.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (6);
        deleteButton.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (14);
        countLabel.setBounds (bottom);
        r.removeFromBottom (6);
        table.setBounds (r);
    }

private:
    void updateButtons()
    {
        const bool any = table.getSelectedRow() >= 0 && table.getSelectedRow() < static_cast<int> (rows.size());
        for (auto* b : { &openButton, &duplicateButton, &deleteButton })
            b->setEnabled (any);
    }

    const ksp::studio::Entry* selected() const
    {
        const int r = table.getSelectedRow();
        return r >= 0 && r < static_cast<int> (rows.size()) ? &rows[static_cast<std::size_t> (r)] : nullptr;
    }

    void openSelected()
    {
        if (const auto* e = selected(); e != nullptr && onOpen)
            onOpen (e->id);
    }

    void duplicateSelected()
    {
        if (const auto* e = selected())
        {
            std::string error;
            library->duplicate (e->id, e->name + " (copy)", error);
            refresh();
        }
    }

    void deleteSelected()
    {
        const auto* e = selected();
        if (e == nullptr)
            return;
        const auto id = e->id;
        juce::Component::SafePointer<LibraryPanel> self (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete project",
            "Delete \"" + juce::String (e->name) + "\" from the library? This cannot be undone.", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create ([self, id] (int ok) {
                if (ok == 0 || self == nullptr)
                    return;
                self->library->remove (id);
                if (self->onDeleted)
                    self->onDeleted (id);
                self->refresh();
            }));
    }

    int getNumRows() override { return static_cast<int> (rows.size()); }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? ksp::ui::palette::blue.withAlpha (0.35f) : (row % 2 ? ksp::ui::palette::gridB : ksp::ui::palette::gridA));
    }

    void paintCell (juce::Graphics& g, int row, int column, int w, int h, bool) override
    {
        if (row < 0 || row >= static_cast<int> (rows.size()))
            return;
        const auto& e = rows[static_cast<std::size_t> (row)];
        juce::String text;
        if (column == 1)
            text = e.name;
        else if (column == 2)
            text = juce::String (e.modified).replaceCharacter ('T', ' ').upToFirstOccurrenceOf ("Z", false, false);
        else if (column == 3)
            text = e.summary;
        else
        {
            for (const auto& n : e.instruments)
                text += (text.isEmpty() ? "" : ", ") + juce::String (n);
        }
        g.setColour (ksp::ui::palette::ink);
        g.setFont (14.0f);
        g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override { updateButtons(); }
    void cellDoubleClicked (int row, int, const juce::MouseEvent&) override
    {
        table.selectRow (row);
        openSelected();
    }

    std::shared_ptr<ksp::studio::Library> library;
    std::vector<ksp::studio::Entry> rows;
    juce::TableListBox table;
    juce::TextButton openButton, duplicateButton, deleteButton;
    juce::Label countLabel;
};

class LibraryWindow : public juce::DocumentWindow
{
public:
    LibraryWindow (std::shared_ptr<ksp::studio::Library> library, std::function<void (const std::string&)> open,
                   std::function<void (const std::string&)> deleted, std::function<void()> onClose)
        : DocumentWindow ("Library", ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        panel = new LibraryPanel (std::move (library));
        panel->onOpen = std::move (open);
        panel->onDeleted = std::move (deleted);
        setContentOwned (panel, true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void refresh() { panel->refresh(); }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    LibraryPanel* panel = nullptr;
    std::function<void()> closed;
};

} // namespace ksp::ui
