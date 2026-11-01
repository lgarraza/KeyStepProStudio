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
    std::function<void (const juce::String& style, const juce::String& mood, unsigned seed, bool chains, bool slow, bool musical, int key, int scale)> onGenerate;
    std::function<void()> onUndo;

    GeneratorPanel()
    {
        for (juce::Component* c : { static_cast<juce::Component*> (&styleBox), static_cast<juce::Component*> (&styleLabel),
                                    static_cast<juce::Component*> (&moodBox), static_cast<juce::Component*> (&moodLabel),
                                    static_cast<juce::Component*> (&moodInfo), static_cast<juce::Component*> (&seedLabel),
                                    static_cast<juce::Component*> (&seedBox), static_cast<juce::Component*> (&diceButton),
                                    static_cast<juce::Component*> (&chainsToggle), static_cast<juce::Component*> (&slowToggle),
                                    static_cast<juce::Component*> (&musicalToggle), static_cast<juce::Component*> (&seedHelp), static_cast<juce::Component*> (&keyBox),
                                    static_cast<juce::Component*> (&keyLabel), static_cast<juce::Component*> (&scaleBox), static_cast<juce::Component*> (&scaleLabel),
                                    static_cast<juce::Component*> (&generateButton), static_cast<juce::Component*> (&undoButton),
                                    static_cast<juce::Component*> (&note) })
            addAndMakeVisible (*c);
        styleLabel.setText ("Style", juce::dontSendNotification);
        families.add ("ambient");
        styleBox.addItem ("Ambient", 1);
        for (const auto& st : ksp::gen::styles())
            if (! families.contains (juce::String (st.family)))
            {
                families.add (st.family);
                styleBox.addItem (st.familyLabel, families.size());
            }
        styleBox.setSelectedId (1, juce::dontSendNotification);
        styleBox.onChange = [this] { fillMoods (1); showMoodInfo(); };
        moodLabel.setText ("Mood", juce::dontSendNotification);
        seedLabel.setText ("Seed", juce::dontSendNotification);
        keyLabel.setText ("Key", juce::dontSendNotification);
        scaleLabel.setText ("Scale", juce::dontSendNotification);
        keyBox.addItem ("Auto (the style's)", 1);
        for (int pc = 0; pc < 12; ++pc)
            keyBox.addItem (ksp::pitchClassName (pc), pc + 2);
        scaleBox.addItem ("Auto (the style's)", 1);
        for (int sc = 1; sc <= 5; ++sc)
            scaleBox.addItem (ksp::scaleName (sc), sc + 1);
        keyBox.onChange = [this] { showMoodInfo(); };
        scaleBox.onChange = [this] { showMoodInfo(); };
        keyBox.setSelectedId (1, juce::dontSendNotification);
        scaleBox.setSelectedId (1, juce::dontSendNotification);
        keyBox.setTooltip ("Auto uses the key of the style and mood (shown below); a key here puts all four tracks in it.");
        scaleBox.setTooltip ("Auto uses the scale of the style and mood; a scale here is used by all four tracks.");
        fillMoods (2);
        moodBox.onChange = [this] { showMoodInfo(); };
        seedBox.setInputRestrictions (9, "0123456789");
        seedBox.setText ("1", false);
        diceButton.setButtonText ("New seed");
        diceButton.onClick = [this] { seedBox.setText (juce::String (juce::Random::getSystemRandom().nextInt (1000000) + 1), false); };
        chainsToggle.setButtonText ("Chain the variations in every scene (the patterns advance by themselves)");
        slowToggle.setButtonText ("Slow time divisions (drone, pad and bells 1/4, melody 1/8)");
        musicalToggle.setButtonText ("Musical arrangement: 8 scenes with an energy curve (intro, drop, breakdown, peak, outro)");
        musicalToggle.setToggleState (true, juce::dontSendNotification);
        musicalToggle.onClick = [this] { showMoodInfo(); };
        chainsToggle.setToggleState (true, juce::dontSendNotification);
        slowToggle.setToggleState (true, juce::dontSendNotification);
        generateButton.setButtonText ("Generate into this project");
        undoButton.setButtonText ("Undo");
        undoButton.setEnabled (false);
        generateButton.onClick = [this] {
            if (onGenerate)
                onGenerate (styleName(), moodName(), static_cast<unsigned> (juce::jmax (1, seedBox.getText().getIntValue())), chainsToggle.getToggleState(),
                            slowToggle.getToggleState(), musicalToggle.getToggleState(), keyBox.getSelectedId() - 2,
                            scaleBox.getSelectedId() - 1);
        };
        undoButton.onClick = [this] { if (onUndo) onUndo(); };
        seedHelp.setText ("Seed: the number the generator \"rolls the dice\" with. The same style and seed always give the same music, so note "
                          "the number of a take you like. Another seed is another take of the same idea (same key, tempo and style, different notes).",
                          juce::dontSendNotification);
        seedHelp.setFont (juce::FontOptions (11.0f));
        seedHelp.setColour (juce::Label::textColourId, juce::Colours::grey);
        seedBox.setTooltip ("The same style and seed always give the same music; another seed is another take.");
        diceButton.setTooltip ("A random seed: a new take of the same style.");
        moodInfo.setFont (juce::FontOptions (12.0f));
        note.setFont (juce::FontOptions (11.0f));
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        showMoodInfo();
        setSize (560, 528);
    }

    void setState (const juce::String& style, const juce::String& mood, unsigned seed, bool chains, bool slow, bool musical, int key, int scale, bool canUndo)
    {
        if (const auto* st = ksp::gen::findStyle (style.toStdString()))
        {
            styleBox.setSelectedId (families.indexOf (juce::String (st->family)) + 1, juce::dontSendNotification);
            const auto list = familyStyles();
            fillMoods (static_cast<int> (std::find (list.begin(), list.end(), st) - list.begin()) + 1);
        }
        else
        {
            styleBox.setSelectedId (1, juce::dontSendNotification);
            fillMoods (mood == "ethereal" ? 2 : mood == "dark" ? 3 : mood == "meditative" ? 4 : 1);
        }
        seedBox.setText (juce::String (seed), false);
        chainsToggle.setToggleState (chains, juce::dontSendNotification);
        slowToggle.setToggleState (slow, juce::dontSendNotification);
        musicalToggle.setToggleState (musical, juce::dontSendNotification);
        keyBox.setSelectedId (juce::jlimit (-1, 11, key) + 2, juce::dontSendNotification);
        scaleBox.setSelectedId (juce::jlimit (0, 5, scale) + 1, juce::dontSendNotification);
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
        row = r.removeFromTop (28);
        keyLabel.setBounds (row.removeFromLeft (50));
        keyBox.setBounds (row.removeFromLeft (180));
        row.removeFromLeft (20);
        scaleLabel.setBounds (row.removeFromLeft (40));
        scaleBox.setBounds (row.removeFromLeft (176));
        r.removeFromTop (4);
        seedHelp.setBounds (r.removeFromTop (44));
        r.removeFromTop (2);
        moodInfo.setBounds (r.removeFromTop (66));
        r.removeFromTop (10);
        chainsToggle.setBounds (r.removeFromTop (26));
        slowToggle.setBounds (r.removeFromTop (26));
        musicalToggle.setBounds (r.removeFromTop (26));
        r.removeFromTop (12);
        auto buttons = r.removeFromTop (32);
        generateButton.setBounds (buttons.removeFromLeft (230));
        buttons.removeFromLeft (8);
        undoButton.setBounds (buttons.removeFromLeft (90));
        r.removeFromTop (12);
        note.setBounds (r);
    }

private:
    /** The presets of the family chosen in the Style box (none for Ambient, whose moods are Ambient.h's). */
    std::vector<const ksp::gen::Style*> familyStyles() const
    {
        std::vector<const ksp::gen::Style*> out;
        const auto family = families[styleBox.getSelectedId() - 1];
        for (const auto& st : ksp::gen::styles())
            if (juce::String (st.family) == family)
                out.push_back (&st);
        return out;
    }

    void fillMoods (int select)
    {
        moodBox.clear (juce::dontSendNotification);
        const auto list = familyStyles();
        if (list.empty())
            for (auto* m : { "Classic", "Ethereal", "Dark", "Meditative" })
                moodBox.addItem (m, moodBox.getNumItems() + 1);
        for (const auto* st : list)
            moodBox.addItem (st->mood, moodBox.getNumItems() + 1);
        moodBox.setSelectedId (juce::jlimit (1, moodBox.getNumItems(), select), juce::dontSendNotification);
    }

    const ksp::gen::Style* style() const
    {
        const auto list = familyStyles();
        const int i = moodBox.getSelectedId() - 1;
        return i >= 0 && i < static_cast<int> (list.size()) ? list[static_cast<std::size_t> (i)] : nullptr;
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
        slowToggle.setEnabled (st == nullptr);
        musicalToggle.setEnabled (st != nullptr);
        chainsToggle.setEnabled (st == nullptr || ! musicalToggle.getToggleState());
        const juce::String tracks = st != nullptr ? juce::String ("Track 1 drums, track 2 ") + ksp::gen::roleLabel (st->tracks[0].spec.role)
                                                        + ", track 3 " + ksp::gen::roleLabel (st->tracks[1].spec.role)
                                                        + ", track 4 " + ksp::gen::roleLabel (st->tracks[2].spec.role) + "."
                                                  : juce::String ("Track 1 drone, track 2 pad chords, track 3 lead melody, track 4 bells.");
        // the key and scale the music will have: the choice, else the style's or the mood's own
        const auto own = st != nullptr ? ksp::gen::styleParams (*st) : ksp::gen::moodParams (moodName().toStdString());
        const int keyPick = keyBox.getSelectedId() - 2, scalePick = scaleBox.getSelectedId() - 1;
        const bool chosen = keyPick >= 0 || scalePick >= 1;
        const juce::String keyScale = juce::String (ksp::pitchClassName (keyPick >= 0 ? keyPick : own.root)) + " "
                                      + juce::String (ksp::scaleName (scalePick >= 1 ? scalePick : own.scale)).toLowerCase();
        juce::String text = st != nullptr ? keyScale + juce::String (st->info).fromFirstOccurrenceOf (",", true, false) + " The project gets that tempo."
                                          : juce::String (info[juce::jlimit (0, 3, moodBox.getSelectedId() - 1)]);
        if (chosen)
            text += st != nullptr ? " (Your key and scale; the chords keep the style's degrees.)"
                                  : " It will be played in " + keyScale + " (your choice; the chords keep the mood's degrees).";
        moodInfo.setText (text + "\n" + tracks, juce::dontSendNotification);
        const bool musical = st != nullptr && musicalToggle.getToggleState();
        note.setText (musical
                          ? "Puts track 1 in drum mode and replaces its drum patterns 1-5 and 16, patterns 1-12 and 16 of tracks 2-4 (energy levels by "
                            "chord; 16 is empty) and scenes 1-8: intro, groove, drop, drop variation, breakdown, build, peak, outro. Each scene chains four "
                            "patterns per track. The KeyStep does not change scenes by itself: you play them live. Drums use the default drum map "
                            "and reach the track 1 instrument on the drum channel (10). Nothing reaches the KeyStep until you load it (Write to slot)."
                      : st != nullptr
                          ? "Puts track 1 in drum mode and replaces its drum patterns 1-4, patterns 1-4 of tracks 2-4 (bass, chords, lead; their time "
                            "division and Mono/Poly too) and scenes 1-4. Drums use the default drum map (kick 36, snare 38, clap 39, hats 42 and 46) and "
                            "reach the track 1 instrument on the drum channel (10). Nothing reaches the KeyStep until you load it (Write to slot)."
                          : "Replaces patterns 1-4 of the four tracks and scenes 1-8 of the project shown (and the playback settings of those patterns). It does "
                            "not touch drums, the arpeggiator or other patterns. Nothing reaches the KeyStep until you load it (Write to slot).",
                      juce::dontSendNotification);
    }

    juce::StringArray families;   // "ambient" then the families of Styles.h, in the Style box's order
    juce::ComboBox styleBox, moodBox, keyBox, scaleBox;
    juce::TextEditor seedBox;
    juce::Label styleLabel, moodLabel, seedLabel, keyLabel, scaleLabel, seedHelp, moodInfo, note;
    juce::ToggleButton chainsToggle, slowToggle, musicalToggle;
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
