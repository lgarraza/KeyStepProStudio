/*
    MidiMonitor.h
    The MIDI monitor: what the KeyStep Pro sends while its controls are used. Two views of the same stream:
      - "Controls seen": one row for each distinct control (a CC of a channel, the notes of a channel, the pitch bend...), with its last value,
        its range and how many times it spoke. It answers "what does this knob / strip / button send?".
      - the log of the last messages, newest at the bottom, with the time they arrived.
    The clock is not logged (24 a quarter note would drown everything): it is counted and its tempo shown. JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <tuple>

namespace ksp::ui
{

class MidiMonitorPanel : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    std::function<void (bool)> onActive;   // the monitor shows / hides: the app starts / stops feeding it

    MidiMonitorPanel()
    {
        for (juce::Component* c : { static_cast<juce::Component*> (&table), static_cast<juce::Component*> (&log),
                                    static_cast<juce::Component*> (&pauseToggle), static_cast<juce::Component*> (&clearButton),
                                    static_cast<juce::Component*> (&copyButton), static_cast<juce::Component*> (&summary),
                                    static_cast<juce::Component*> (&hintLabel) })
            addAndMakeVisible (*c);
        table.setModel (this);
        table.setRowHeight (22);
        auto& h = table.getHeader();
        h.addColumn ("Message", 1, 190, 80, 400);
        h.addColumn ("Ch", 2, 40, 30, 60);
        h.addColumn ("Number", 3, 190, 60, 360);
        h.addColumn ("Last", 4, 54, 40, 90);
        h.addColumn ("Min - max", 5, 80, 60, 120);
        h.addColumn ("Count", 6, 60, 40, 100);
        h.setStretchToFitActive (true);
        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setScrollbarsShown (true);
        log.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        log.setColour (juce::TextEditor::backgroundColourId, ksp::ui::palette::gridA);
        pauseToggle.setButtonText ("Pause");
        clearButton.setButtonText ("Clear");
        copyButton.setButtonText ("Copy all");
        clearButton.onClick = [this] { clear(); };
        copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (report()); };
        summary.setFont (juce::FontOptions (12.0f));
        hintLabel.setText ("Move a knob, press a pad, use the strips and the buttons of the KeyStep: each one that sends MIDI appears above.",
                           juce::dontSendNotification);
        hintLabel.setFont (juce::FontOptions (12.0f));
        hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
        opened = juce::Time::getMillisecondCounterHiRes();
        setSize (860, 560);
        startTimerHz (8);
        if (onActive)
            onActive (true);
    }

    ~MidiMonitorPanel() override
    {
        if (onActive)
            onActive (false);
    }

    void activate()
    {
        if (onActive)
            onActive (true);
    }

    /** One message of the KeyStep (message thread). */
    void add (const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty() || pauseToggle.getToggleState())
            return;
        const juce::MidiMessage m (bytes.data(), static_cast<int> (bytes.size()));
        const auto status = bytes[0];
        if (status == 0xF8)
        {
            ++clocks;
            return;
        }
        if (status == 0xFE)
            return;   // active sensing

        Key k;
        juce::String what, number;
        int value = 0;
        if (status >= 0xF8 || status == 0xFA || status == 0xFB || status == 0xFC)
        {
            k = { 9, 0, status };
            what = status == 0xFA ? "Start" : status == 0xFB ? "Continue" : status == 0xFC ? "Stop" : "Real-time 0x" + juce::String::toHexString (status);
        }
        else if (m.isNoteOnOrOff())
        {
            k = { 0, m.getChannel(), 0 };
            what = "Notes";
            value = m.getNoteNumber();
            number = "";
            noteRange (k, value);
        }
        else if (m.isController())
        {
            k = { 1, m.getChannel(), m.getControllerNumber() };
            what = "Control change";
            number = juce::String (m.getControllerNumber()) + "  " + juce::MidiMessage::getControllerName (m.getControllerNumber());
            value = m.getControllerValue();
        }
        else if (m.isPitchWheel())
        {
            k = { 2, m.getChannel(), 0 };
            what = "Pitch bend";
            value = m.getPitchWheelValue();
        }
        else if (m.isChannelPressure())
        {
            k = { 3, m.getChannel(), 0 };
            what = "Channel pressure";
            value = m.getChannelPressureValue();
        }
        else if (m.isAftertouch())
        {
            k = { 4, m.getChannel(), m.getNoteNumber() };
            what = "Key pressure";
            number = juce::MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3);
            value = m.getAfterTouchValue();
        }
        else if (m.isProgramChange())
        {
            k = { 5, m.getChannel(), 0 };
            what = "Program change";
            value = m.getProgramChangeNumber();
        }
        else
        {
            k = { 8, 0, status };
            what = "Other 0x" + juce::String::toHexString (status);
            value = bytes.size() > 1 ? bytes[1] : 0;
        }

        auto it = index.find (k);
        if (it == index.end())
        {
            rows.push_back ({ k, what, number, value, value, value, 0 });
            it = index.emplace (k, rows.size() - 1).first;
        }
        auto& r = rows[it->second];
        r.last = value;
        r.lo = std::min (r.lo, value);
        r.hi = std::max (r.hi, value);
        ++r.count;
        if (k.type == 0)
            r.number = juce::String (juce::MidiMessage::getMidiNoteName (noteLo[k.channel], true, true, 3)) + " to "
                       + juce::String (juce::MidiMessage::getMidiNoteName (noteHi[k.channel], true, true, 3));
        dirty = true;

        log.moveCaretToEnd();
        log.insertTextAtCaret (describe (m, what, number, value) + "\n");
        if (++logLines > 500)
            trim();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (26);
        pauseToggle.setBounds (top.removeFromLeft (80));
        clearButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (6);
        copyButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (14);
        summary.setBounds (top);
        r.removeFromTop (4);
        hintLabel.setBounds (r.removeFromTop (18));
        r.removeFromTop (4);
        table.setBounds (r.removeFromTop (r.getHeight() * 11 / 20));
        r.removeFromTop (6);
        log.setBounds (r);
    }

private:
    struct Key
    {
        int type = 0, channel = 0, number = 0;
        bool operator< (const Key& o) const noexcept
        {
            return std::tie (type, channel, number) < std::tie (o.type, o.channel, o.number);
        }
    };

    struct Row
    {
        Key key;
        juce::String what, number;
        int last = 0, lo = 0, hi = 0;
        long count = 0;
    };

    void noteRange (const Key& k, int note)
    {
        if (noteHi.count (k.channel) == 0)
        {
            noteLo[k.channel] = note;
            noteHi[k.channel] = note;
        }
        noteLo[k.channel] = std::min (noteLo[k.channel], note);
        noteHi[k.channel] = std::max (noteHi[k.channel], note);
    }

    juce::String describe (const juce::MidiMessage& m, const juce::String& what, const juce::String& number, int value) const
    {
        const double t = (juce::Time::getMillisecondCounterHiRes() - opened) / 1000.0;
        juce::String body;
        if (m.isNoteOn())
            body = "note on  " + juce::String (juce::MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3)) + "  velocity " + juce::String (m.getVelocity());
        else if (m.isNoteOff())
            body = "note off " + juce::String (juce::MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3));
        else if (m.isController())
            body = "CC " + number + " = " + juce::String (value);
        else
            body = what + (number.isNotEmpty() ? " " + number : juce::String()) + " = " + juce::String (value);
        const juce::String ch = m.getChannel() > 0 ? "ch " + juce::String (m.getChannel()).paddedLeft (' ', 2) : juce::String ("     ");
        return juce::String (t, 3).paddedLeft (' ', 9) + "  " + ch + "  " + body;
    }

    void trim()
    {
        auto lines = juce::StringArray::fromLines (log.getText());
        lines.removeRange (0, lines.size() - 300);
        log.setText (lines.joinIntoString ("\n") + "\n");
        log.moveCaretToEnd();
        logLines = 300;
    }

    void clear()
    {
        rows.clear();
        index.clear();
        noteLo.clear();
        noteHi.clear();
        log.clear();
        logLines = 0;
        clocks = 0;
        dirty = true;
        opened = juce::Time::getMillisecondCounterHiRes();
    }

    juce::String report() const
    {
        juce::String out = "KeyStep Pro MIDI monitor\nControls seen:\n";
        for (const auto& r : rows)
            out += "  " + r.what + (r.key.channel > 0 ? "  ch " + juce::String (r.key.channel) : juce::String()) + "  " + r.number + "  last " + juce::String (r.last)
                   + "  range " + juce::String (r.lo) + "-" + juce::String (r.hi) + "  x" + juce::String (r.count) + "\n";
        out += "Clock ticks seen: " + juce::String (clocks) + "\n\nLog:\n" + log.getText();
        return out;
    }

    void timerCallback() override
    {
        if (dirty)
        {
            dirty = false;
            table.updateContent();
            table.repaint();
        }
        summary.setText (juce::String (rows.size()) + " distinct control(s), " + juce::String (clocks) + " clock ticks", juce::dontSendNotification);
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
        const auto& r = rows[static_cast<std::size_t> (row)];
        juce::String text;
        switch (column)
        {
            case 1: text = r.what; break;
            case 2: text = r.key.channel > 0 ? juce::String (r.key.channel) : juce::String ("-"); break;
            case 3: text = r.number; break;
            case 4: text = r.key.type == 0 || r.key.type == 9 ? juce::String ("-") : juce::String (r.last); break;
            case 5: text = r.key.type == 0 || r.key.type == 9 ? juce::String ("-") : juce::String (r.lo) + " - " + juce::String (r.hi); break;
            default: text = juce::String (r.count); break;
        }
        g.setColour (ksp::ui::palette::ink);
        g.setFont (13.0f);
        g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    std::vector<Row> rows;
    std::map<Key, std::size_t> index;
    std::map<int, int> noteLo, noteHi;
    juce::TableListBox table;
    juce::TextEditor log;
    juce::ToggleButton pauseToggle;
    juce::TextButton clearButton, copyButton;
    juce::Label summary, hintLabel;
    double opened = 0.0;
    long clocks = 0;
    int logLines = 0;
    bool dirty = false;
};

class MidiMonitorWindow : public juce::DocumentWindow
{
public:
    MidiMonitorWindow (std::function<void (bool)> active, std::function<void()> onClose)
        : DocumentWindow ("MIDI monitor", ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        panel = new MidiMonitorPanel();
        panel->onActive = std::move (active);
        setContentOwned (panel, true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        panel->activate();
    }

    ~MidiMonitorWindow() override
    {
        if (panel != nullptr && panel->onActive)
            panel->onActive (false);
    }

    MidiMonitorPanel& content() { return *panel; }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    MidiMonitorPanel* panel = nullptr;
    std::function<void()> closed;
};

} // namespace ksp::ui
