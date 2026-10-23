#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Same palette as the web app's dark theme.
namespace theme
{
inline const juce::Colour bg { 0xff0f1317 }, panel { 0xff151b21 }, raised { 0xff1c242c }, line { 0xff27313b },
    fg { 0xffe4e9ee }, muted { 0xff8a97a4 }, accent { 0xfff2a33a }, accentFg { 0xff1b1206 }, curve { 0xff5fd0e4 },
    danger { 0xffef5b5b }, rollBg { 0xff12171c }, rollBlack { 0xff0d1115 }, rollLine { 0xff1d252d },
    rollC { 0xff2b3641 }, rollBar { 0xff3a4652 }, keyWhite { 0xffd9dee3 }, keyBlack { 0xff20272e },
    keyText { 0xff56616c }, noteText { 0xff241605 }, playhead { 0xffff6a4d };

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        auto scheme = juce::LookAndFeel_V4::getDarkColourScheme();
        scheme.setUIColour(ColourScheme::windowBackground, bg);
        scheme.setUIColour(ColourScheme::widgetBackground, raised);
        scheme.setUIColour(ColourScheme::menuBackground, raised);
        scheme.setUIColour(ColourScheme::outline, line);
        scheme.setUIColour(ColourScheme::defaultText, fg);
        scheme.setUIColour(ColourScheme::defaultFill, accent);
        scheme.setUIColour(ColourScheme::highlightedText, accentFg);
        scheme.setUIColour(ColourScheme::highlightedFill, accent);
        scheme.setUIColour(ColourScheme::menuText, fg);
        setColourScheme(scheme);

        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent.withAlpha(0.85f));
        setColour(juce::Slider::backgroundColourId, line);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxTextColourId, accent);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.3f));
        setColour(juce::TextButton::buttonColourId, raised);
        setColour(juce::TextButton::buttonOnColourId, danger);
        setColour(juce::TextButton::textColourOffId, fg);
        setColour(juce::TextButton::textColourOnId, fg);
        setColour(juce::ComboBox::backgroundColourId, raised);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::arrowColourId, muted);
        setColour(juce::ToggleButton::tickColourId, accent);
        setColour(juce::ToggleButton::tickDisabledColourId, muted);
        setColour(juce::Label::textColourId, fg);
        setColour(juce::ScrollBar::thumbColourId, line);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour(juce::PopupMenu::highlightedTextColourId, accentFg);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = base;
        if (b.getProperties()["primary"]) c = accent;
        if (down) c = c.brighter(0.08f);
        else if (over) c = c.brighter(0.04f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(b.getProperties()["primary"] ? accent : (over ? muted : line));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override
    {
        return juce::Font(juce::FontOptions(juce::jmin(14.0f, h * 0.5f), juce::Font::bold));
    }
};
} // namespace theme
