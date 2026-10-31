/*
    GeneratorWindow.h
    The generator inside the Studio: choose a style (ambient with its moods, Source/Gen/Ambient.h, or a Source/Gen/Styles.h preset: techno, house,
    drum & bass) and a seed and it writes patterns 1 to 4 of the four tracks and the scenes into the project that is open, ready to hear through the
    instruments and to load into the KeyStep. The styles put track 1 in drum mode. Undo brings the project back as it was. JUCE, message thread.
*/

#pragma once

#include "KspStyle.h"
#include "Gen/Styles.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ksp::ui
{

class GeneratorPanel : public juce::Component
{
public:
    std::function<void (const juce::String& style, const juce::String& mood, unsigned seed, bool chains, bool slow)> onGenerate;
    std::function<void()> onUndo;

    GeneratorPanel()
    {
        for (juce::Component* c : { static_cast<juce::Component*> (&styleBox), static_cast<juce::Component*> (&styleLabel),
                                    static_cast<juce::Component*> (&moodBox), static_cast<juce::Component*> (&moodLabel),
                                    static_cast<juce::Component*> (&moodInfo), static_cast<juce::Component*> (&seedLabel),
                                    static_cast<juce::Component*> (&seedBox), static_cast<juce::Component*> (&diceButton),
                                    static_cast<juce::Component*> (&chainsToggle), static_cast<juce::Component*> (&slowToggle),
                                    static_cast<juce::Component*> (&generateButton), static_cast<juce::Component*> (&undoButton),
                                    static_cast<juce::Component*> (&note) })
            addAndMakeVisible (*c);
        styleLabel.setText ("Style", juce::dontSendNotification);
        styleBox.addItem ("Ambient", 1);
        for (std::size_t i = 0; i < ksp::gen::styles().size(); ++i)
            styleBox.addItem (ksp::gen::styles()[i].label, static_cast<int> (i) + 2);
        styleBox.setSelectedId (1, juce::dontSendNotification);
        styleBox.onChange = [this] { showMoodInfo(); };
        moodLabel.setText ("Mood", juce::dontSendNotification);
        seedLabel.setText ("Seed", juce::dontSendNotification);
        moodBox.addItem ("Classic", 1);
        moodBox.addItem ("Ethereal", 2);
        moodBox.addItem ("Dark", 3);
        moodBox.addItem ("Meditative", 4);
        moodBox.setSelectedId (2, juce::dontSendNotification);
        moodBox.onChange = [this] { showMoodInfo(); };
        seedBox.setInputRestrictions (9, "0123456789");
        seedBox.setText ("1", false);
        diceButton.setButtonText ("New seed");
        diceButton.onClick = [this] { seedBox.setText (juce::String (juce::Random::getSystemRandom().nextInt (1000000) + 1), false); };
        chainsToggle.setButtonText ("Chain the variations in every scene (the patterns advance by themselves)");
        slowToggle.setButtonText ("Slow time divisions (drone, pad and bells 1/4, melody 1/8)");
        chainsToggle.setToggleState (true, juce::dontSendNotification);
        slowToggle.setToggleState (true, juce::dontSendNotification);
        generateButton.setButtonText ("Generate into this project");
        undoButton.setButtonText ("Undo");
        undoButton.setEnabled (false);
        generateButton.onClick = [this] {
            if (onGenerate)
                onGenerate (styleName(), moodName(), static_cast<unsigned> (juce::jmax (1, seedBox.getText().getIntValue())), chainsToggle.getToggleState(),
                            slowToggle.getToggleState());
        };
        undoButton.onClick = [this] { if (onUndo) onUndo(); };
        moodInfo.setFont (juce::FontOptions (12.0f));
        note.setFont (juce::FontOptions (11.0f));
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        showMoodInfo();
        setSize (560, 380);
    }

    void setState (const juce::String& style, const juce::String& mood, unsigned seed, bool chains, bool slow, bool canUndo)
    {
        int styleId = 1;
        for (std::size_t i = 0; i < ksp::gen::styles().size(); ++i)
            if (style == juce::String (ksp::gen::styles()[i].name))
                styleId = static_cast<int> (i) + 2;
        styleBox.setSelectedId (styleId, juce::dontSendNotification);
        const int id = mood == "ethereal" ? 2 : mood == "dark" ? 3 : mood == "meditative" ? 4 : mood == "classic" ? 1 : moodBox.getSelectedId();
        moodBox.setSelectedId (id, juce::dontSendNotification);
        seedBox.setText (juce::String (seed), false);
        chainsToggle.setToggleState (chains, juce::dontSendNotification);
        slowToggle.setToggleState (slow, juce::dontSendNotification);
        undoButton.setEnabled (canUndo);
        showMoodInfo();
    }

    void setCanUndo (bool can) { undoButton.setEnabled (can); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        auto row = r.removeFromTop (28);
        styleLabel.setBounds (row.removeFromLeft (50));
        styleBox.setBounds (row.removeFromLeft (180));
        r.removeFromTop (8);
        row = r.removeFromTop (28);
        moodLabel.setBounds (row.removeFromLeft (50));
        moodBox.setBounds (row.removeFromLeft (180));
        row.removeFromLeft (20);
        seedLabel.setBounds (row.removeFromLeft (40));
        seedBox.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (6);
        diceButton.setBounds (row.removeFromLeft (90));
        r.removeFromTop (6);
        moodInfo.setBounds (r.removeFromTop (36));
        r.removeFromTop (10);
        chainsToggle.setBounds (r.removeFromTop (26));
        slowToggle.setBounds (r.removeFromTop (26));
        r.removeFromTop (12);
        auto buttons = r.removeFromTop (32);
        generateButton.setBounds (buttons.removeFromLeft (230));
        buttons.removeFromLeft (8);
        undoButton.setBounds (buttons.removeFromLeft (90));
        r.removeFromTop (12);
        note.setBounds (r);
    }

private:
    const ksp::gen::Style* style() const
    {
        const int i = styleBox.getSelectedId() - 2;
        return i >= 0 && i < static_cast<int> (ksp::gen::styles().size()) ? &ksp::gen::styles()[static_cast<std::size_t> (i)] : nullptr;
    }

    juce::String styleName() const { return style() != nullptr ? juce::String (style()->name) : juce::String ("ambient"); }

    juce::String moodName() const
    {
        switch (moodBox.getSelectedId())
        {
            case 2: return "ethereal";
            case 3: return "dark";
            case 4: return "meditative";
            default: return "classic";
        }
    }

    void showMoodInfo()
    {
        static const char* info[] { "D Dorian with simple triads (the first version).", "C major: Cmaj7sus4, G7sus4, Am9, Fmaj9.",
                                    "D minor: Dm9, Bb maj7, Gm7, Asus4 with the seventh.", "G mixolydian: open sus2 and add9 chords." };
        const auto* st = style();
        moodBox.setEnabled (st == nullptr);
        slowToggle.setEnabled (st == nullptr);
        moodInfo.setText (st != nullptr ? juce::String (st->info) + " The project gets that tempo."
                                        : juce::String (info[juce::jlimit (0, 3, moodBox.getSelectedId() - 1)]),
                          juce::dontSendNotification);
        note.setText (st != nullptr
                          ? "Puts track 1 in drum mode and replaces its drum patterns 1-4, patterns 1-4 of tracks 2-4 (bass, chords, lead; their time "
                            "division and Mono/Poly too) and scenes 1-4. Drums use the default drum map (kick 36, snare 38, clap 39, hats 42 and 46) and "
                            "reach the track 1 instrument on the drum channel (10). Nothing reaches the KeyStep until you load it (Write to slot)."
                          : "Replaces patterns 1-4 of the four tracks and scenes 1-8 of the project shown (and the playback settings of those patterns). It does "
                            "not touch drums, the arpeggiator or other patterns. Nothing reaches the KeyStep until you load it (Write to slot).",
                      juce::dontSendNotification);
    }

    juce::ComboBox styleBox, moodBox;
    juce::TextEditor seedBox;
    juce::Label styleLabel, moodLabel, seedLabel, moodInfo, note;
    juce::ToggleButton chainsToggle, slowToggle;
    juce::TextButton diceButton, generateButton, undoButton;
};

class GeneratorWindow : public juce::DocumentWindow
{
public:
    explicit GeneratorWindow (std::function<void()> onClose)
        : DocumentWindow ("Generate", ksp::ui::palette::chassis, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        panel = new GeneratorPanel();
        setContentOwned (panel, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    GeneratorPanel& content() { return *panel; }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    GeneratorPanel* panel = nullptr;
    std::function<void()> closed;
};

} // namespace ksp::ui
