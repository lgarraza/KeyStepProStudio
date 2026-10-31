// Look of the KeyStep Pro: white chassis, light-grey bevelled buttons, the four track colours and a black top panel.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace ksp::ui
{

namespace palette
{
inline const juce::Colour chassis   { 0xfff3f4f5 };   // the white body
inline const juce::Colour panelDark { 0xff121214 };   // the black strip with the display
inline const juce::Colour button    { 0xffc9ced3 };   // grey button
inline const juce::Colour buttonHi  { 0xffd9dde1 };
inline const juce::Colour buttonLo  { 0xffaab0b6 };
inline const juce::Colour edge      { 0xff8d959c };
inline const juce::Colour ink       { 0xff23272b };   // button labels
inline const juce::Colour inkSoft   { 0xff5d666e };
inline const juce::Colour blue      { 0xff2b8fc4 };   // the blue secondary labels of the panel
inline const juce::Colour rec       { 0xffd2282f };
inline const juce::Colour gridA     { 0xfffafafb };
inline const juce::Colour gridB     { 0xffe9ecee };
inline const juce::Colour gridLine  { 0xffc3c9ce };
inline const juce::Colour lit       { 0xff3df2a0 };   // a lit button (the green of Restart / the lit step keys)
inline const juce::Colour segOn     { 0xfff4f6f8 };   // the white digits of the track displays
inline const juce::Colour step      { 0xffdadee2 };   // the 1..16 step buttons
inline const juce::Colour magic     { 0xffe8b320 };   // the gold sparkle of the Generate wand
inline const std::array<juce::Colour, 4> track { juce::Colour (0xff12a67f), juce::Colour (0xfff25a1f),
                                                  juce::Colour (0xfff7c40d), juce::Colour (0xffe3202e) };
inline juce::Colour trackColour (int t) { return track[static_cast<std::size_t> (juce::jlimit (0, 3, t))]; }
}

/** The soft halo of a lit key: a few expanding, fading outlines. */
inline void drawGlow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius = 3.0f)
{
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.07f * (7 - i) / 3.0f));
        g.drawRoundedRectangle (r.expanded (i * 1.1f), radius + i, 1.6f);
    }
}

//==============================================================================
/** Seven-segment text on black, like the KeyStep's displays: digits, '-', '.', and the unlit segments ghosted. */
class SevenSegDisplay : public juce::Component
{
public:
    explicit SevenSegDisplay (int numDigits = 2) : digits (numDigits) {}

    void setText (const juce::String& t, juce::Colour c = palette::segOn)
    {
        if (t == text && c == colour)
            return;
        text = t;
        colour = c;
        repaint();
    }

    void setCaption (const juce::String& c) { caption = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (palette::panelDark);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff3a3d44));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        auto area = r.reduced (6.0f, 4.0f);
        if (caption.isNotEmpty())
        {
            g.setColour (palette::blue);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (caption, area.removeFromLeft (30.0f), juce::Justification::centredLeft);
        }
        // the string is laid out right-aligned into `digits` cells ('.' belongs to the digit before it)
        juce::StringArray cells;
        for (auto ch : text)
        {
            if (ch == '.' && cells.size() > 0) cells.set (cells.size() - 1, cells[cells.size() - 1] + ".");
            else cells.add (juce::String::charToString (ch));
        }
        while (cells.size() < digits) cells.insert (0, " ");
        while (cells.size() > digits) cells.remove (0);
        const float cw = area.getWidth() / (float) digits;
        for (int i = 0; i < digits; ++i)
            drawDigit (g, area.withX (area.getX() + cw * (float) i).withWidth (cw).reduced (cw * 0.12f, 0), cells[i]);
    }

private:
    int digits;
    juce::String text, caption;
    juce::Colour colour = palette::segOn;

    // segments a b c d e f g
    static unsigned maskFor (juce::juce_wchar ch)
    {
        static const unsigned num[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        if (ch >= '0' && ch <= '9') return num[ch - '0'];
        return ch == '-' ? 0x40u : 0u;
    }

    void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& cell) const
    {
        const unsigned on = maskFor (cell.isEmpty() ? ' ' : cell[0]);
        const float t = juce::jmax (2.0f, r.getHeight() * 0.11f), w = r.getWidth() - 4.0f, h = r.getHeight() * 0.5f;
        const float x = r.getX() + 2.0f, y = r.getY();
        const juce::Rectangle<float> seg[7] = {
            { x + t, y, w - 2 * t, t },                         // a
            { x + w - t, y + t, t, h - t - t * 0.5f },          // b
            { x + w - t, y + h + t * 0.5f, t, h - t - t * 0.5f },   // c
            { x + t, y + 2 * h - t, w - 2 * t, t },             // d
            { x, y + h + t * 0.5f, t, h - t - t * 0.5f },       // e
            { x, y + t, t, h - t - t * 0.5f },                  // f
            { x + t, y + h - t * 0.5f, w - 2 * t, t } };        // g
        for (int i = 0; i < 7; ++i)
        {
            const bool lit = (on >> i) & 1u;
            if (lit)
            {
                g.setColour (colour.withAlpha (0.18f));
                g.fillRoundedRectangle (seg[i].expanded (1.5f), 2.0f);
            }
            g.setColour (lit ? colour : colour.withAlpha (0.07f));
            g.fillRoundedRectangle (seg[i], 1.0f);
        }
        if (cell.endsWithChar ('.'))
        {
            g.setColour (colour);
            g.fillEllipse (x + w + 0.5f, y + 2 * h - t, t, t);
        }
    }
};

//==============================================================================
class KspLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KspLookAndFeel()
    {
        using namespace palette;
        setColour (juce::ResizableWindow::backgroundColourId, chassis);
        setColour (juce::DocumentWindow::textColourId, ink);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::TextButton::buttonColourId, button);
        setColour (juce::TextButton::buttonOnColourId, buttonLo);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, ink);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, ink);
        setColour (juce::ToggleButton::tickDisabledColourId, inkSoft);
        setColour (juce::ComboBox::backgroundColourId, button);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::arrowColourId, ink);
        setColour (juce::PopupMenu::backgroundColourId, chassis);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, blue);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::thumbColourId, ink);
        setColour (juce::Slider::trackColourId, blue);
        setColour (juce::Slider::backgroundColourId, buttonLo);
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::focusedOutlineColourId, blue);
        setColour (juce::ListBox::backgroundColourId, gridA);
        setColour (juce::ListBox::textColourId, ink);
        setColour (juce::ListBox::outlineColourId, edge);
        setColour (juce::TableHeaderComponent::backgroundColourId, button);
        setColour (juce::TableHeaderComponent::textColourId, ink);
        setColour (juce::TableHeaderComponent::outlineColourId, edge);
        setColour (juce::ScrollBar::thumbColourId, buttonLo);
        setColour (juce::AlertWindow::backgroundColourId, chassis);
        setColour (juce::AlertWindow::textColourId, ink);
        setColour (juce::AlertWindow::outlineColourId, edge);
        setColour (juce::TooltipWindow::backgroundColourId, panelDark);
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
    }

    // a hardware-style key: light bevel, darker when pressed / on; a custom buttonColourId (the red Rec) tints it
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bg;
        const bool lit = bg == palette::rec;   // the Rec key while recording
        if (lit)                        drawGlow (g, r, palette::rec);
        if (down || b.getToggleState()) c = c.darker (0.18f);
        else if (over)                  c = c.brighter (0.10f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.12f), 0, r.getY(), c.darker (0.06f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (palette::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    // a toggle (Mute, checkboxes in the windows) is a key that lights up with a glow when on
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) override
    {
        const bool on = b.getToggleState();
        const auto r = b.getLocalBounds().toFloat().reduced (3.0f);
        // the colour it lights up in: green unless the button sets its own (Mute red, Solo blue)
        const auto litColour = b.isColourSpecified (juce::ToggleButton::tickColourId) ? b.findColour (juce::ToggleButton::tickColourId) : palette::lit;
        if (on)
            drawGlow (g, r, litColour);
        const auto c = on ? litColour : (down ? palette::buttonLo : over ? palette::buttonHi : palette::button);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.12f), 0, r.getY(), c.darker (0.06f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? litColour.darker (0.4f) : palette::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (on && litColour != palette::lit ? juce::Colours::white : palette::ink);
        g.setFont (juce::FontOptions (juce::jmin (13.0f, r.getHeight() * 0.6f), juce::Font::bold));
        g.drawText (b.getButtonText(), r, juce::Justification::centred);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (14.0f, h * 0.55f), juce::Font::bold); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (palette::buttonHi, 0, 0, palette::button, 0, (float) h, false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (palette::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        juce::Path p;
        const float cx = (float) w - 14.0f, cy = h * 0.5f;
        p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.fillPath (p);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
            return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        const float cy = y + h * 0.5f;
        g.setColour (palette::buttonLo);
        g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        g.setColour (palette::blue);
        g.fillRoundedRectangle ((float) x, cy - 2.0f, pos - x, 4.0f, 2.0f);
        g.setColour (palette::ink);   // a round black knob cap, like the encoders
        g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (pos - 1.5f, cy - 5.5f, 3.0f, 3.0f);
    }
};

//==============================================================================
/** Four coloured panels like the KeyStep's track blocks (green, orange, yellow, red), each with a grey "Track N" key. */
/** A text button with a magic wand drawn left of its label (the Generate button: it should catch the eye). */
class WandButton : public juce::TextButton
{
public:
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto& lf = getLookAndFeel();
        lf.drawButtonBackground (g, *this, findColour (buttonColourId), over, down);
        auto r = getLocalBounds().reduced (8, 4);
        const auto icon = r.removeFromLeft (r.getHeight()).toFloat().reduced (1.0f);
        r.removeFromLeft (4);
        // the stick, from bottom left towards the star, with a light tip
        const juce::Point<float> a (icon.getX() + icon.getWidth() * 0.12f, icon.getBottom() - icon.getHeight() * 0.10f),
                                 b (icon.getX() + icon.getWidth() * 0.58f, icon.getY() + icon.getHeight() * 0.44f);
        g.setColour (palette::ink);
        g.drawLine ({ a, b }, 3.0f);
        g.setColour (palette::chassis);
        g.drawLine ({ b.translated (-2.6f, 2.6f), b }, 2.0f);
        // the sparkle: a four-point star at the tip and two small ones around it
        auto star = [&g] (juce::Point<float> c, float rad) {
            juce::Path p;
            for (int i = 0; i < 8; ++i)
            {
                const float ang = juce::MathConstants<float>::pi * 0.25f * static_cast<float> (i);
                const float len = i % 2 == 0 ? rad : rad * 0.32f;
                const juce::Point<float> pt (c.x + len * std::sin (ang), c.y - len * std::cos (ang));
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            p.closeSubPath();
            g.fillPath (p);
        };
        g.setColour (palette::magic);
        const float s = icon.getHeight();
        star ({ icon.getX() + s * 0.74f, icon.getY() + s * 0.28f }, s * 0.30f);
        star ({ icon.getX() + s * 0.30f, icon.getY() + s * 0.20f }, s * 0.13f);
        star ({ icon.getRight() - s * 0.05f, icon.getY() + s * 0.66f }, s * 0.11f);
        g.setColour (findColour (textColourOffId));
        g.setFont (lf.getTextButtonFont (*this, getHeight()));
        g.drawText (getButtonText(), r, juce::Justification::centredLeft);
    }
};

class TrackTabs : public juce::Component
{
public:
    std::function<void (int)> onSelect;   // 1..4

    int getSelected() const { return selected; }

    void setSelected (int t, bool notify)
    {
        t = juce::jlimit (1, 4, t);
        if (t == selected && ! notify)
            return;
        selected = t;
        repaint();
        if (notify && onSelect)
            onSelect (t);
    }

    void paint (juce::Graphics& g) override
    {
        for (int t = 1; t <= 4; ++t)
        {
            auto area = tabBounds (t);
            const bool on = t == selected;
            const auto c = palette::trackColour (t - 1);
            g.setColour (c.withMultipliedSaturation (on ? 1.0f : 0.85f).interpolatedWith (palette::chassis, on ? 0.0f : 0.18f));
            g.fillRect (area);
            if (on)
            {
                g.setColour (palette::panelDark);   // the selected track: a black bar on top, like the lit display above it
                g.fillRect (area.removeFromTop (4));
            }
            const auto key = tabBounds (t).reduced (10, 6).withTrimmedTop (4).toFloat();
            if (on)
                drawGlow (g, key, palette::segOn);   // the selected track's key is lit
            g.setColour (on ? juce::Colours::white : palette::button);
            g.fillRoundedRectangle (key, 3.0f);
            g.setColour (on ? palette::ink : palette::edge);
            g.drawRoundedRectangle (key, 3.0f, on ? 1.5f : 1.0f);
            g.setColour (on ? palette::ink : palette::inkSoft);
            g.setFont (juce::FontOptions (13.0f, on ? juce::Font::bold : juce::Font::plain));
            g.drawText ("Track " + juce::String (t), key, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int t = 1; t <= 4; ++t)
            if (tabBounds (t).contains (e.getPosition()))
                setSelected (t, true);
    }

private:
    juce::Rectangle<int> tabBounds (int t) const
    {
        const int w = getWidth() / 4;
        return { (t - 1) * w, 0, w, getHeight() };
    }

    int selected = 1;
};

} // namespace ksp::ui
