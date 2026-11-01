// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Skin.h"
#include "PanelGuide.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{

namespace
{
SkinColour sheet { 0xff141517, Skin::Area::Drawer }, edge { 0xff34373e, Skin::Area::Drawer }, dim { 0xff8d939c, Skin::Area::Drawer }, amber { 0xffffc23d, Skin::Area::Drawer }, face { 0xff26282d, Skin::Area::Drawer };
juce::Colour heading() { return Palette::pinstripe.brighter (0.4f); } // follows the skin

// Three columns separated by "===". "# " starts a heading ("#! " an amber one), "- " a bullet, "| a | b" a table
// row, "[menu]" draws the sample right-click menu; other lines are paragraphs. **text** is bold.
const char* const panelGuide = R"(# KNOBS
- **Drag** up or down to turn a knob. Hold **Shift** for fine adjustment.
- The **mouse wheel** or trackpad turns knobs too (**Shift** for fine steps).
- **Double-click** puts a knob back to the value stored in the loaded program.
- While you turn, the display shows the new value and, in brackets, the stored one, so you can see how far you've moved.
- **Right-click** opens the modulation menu (see the next column). Right-click never changes a value.
# BUTTONS AND LEDS
Buttons switch on and off with a click and light red when on. Where the TEO-5 uses one button to step through choices (LFO shape, LFO 1/2, envelope destinations, arpeggiator octave and repeat), the LEDs next to it show the choice.
# THE DISPLAY
- Shows the program name, tempo, clock divide, arpeggiator and effect, like the synth. **EXT BPM** means the TEO-5 follows the DAW's clock.
- The **star** is the program's favourite mark in your library (dim if it isn't in the library).
- **Double-click** the display to rename the program.
# CHOOSING PROGRAMS
- **1–16** choose a program in the current bank. **BANK**, then a number, chooses the bank (10–16 show as A–G). **FACTORY** switches between User and Factory banks.
- **BANK ‹ ›** and **PATCH ‹ ›** under the display step through banks and programs, as do the arrow keys when no drawer is open.
- **REVERT** (under MOD) drops all your edits and goes back to the program as loaded. **COMPARE** switches between your edit and the program as loaded. **WRITE** opens the library to save the sound: click a slot in a TEO-5 User bank or MY BANKS (its program plays from your keys so you can hear what you'd replace), then Confirm. The OLED shows **EDITED** in red while the sound differs from the stored program.
===
#! ASSIGNING MODULATION (RIGHT-CLICK)
Right-click any control the TEO-5 can modulate: CUTOFF, RESONANCE, STATE, VOLUME, OVERDRIVE, VINTAGE, X-MOD and the LFO, effect, reverb, oscillator and envelope knobs. The **OSC 1, SUB, OSC 2 and NOISE** buttons stand for their levels.
[menu]
- **Modulate <control> from ▸** lists every source (LFOs, envelopes, velocity, mod wheel, pressure…).
- Pick one and it goes into the **first free mod slot**. The MOD MATRIX drawer opens with that slot outlined in amber.
- The new slot's **amount starts at zero**, so the sound doesn't change until you drag the amount up or down.
- **Already modulated by** lists the slots that already route to this control; choose one to jump to it.
- On a source's own controls (LFO, ENV 1, ENV 2, OSC 2, NOISE) there's also **Send <source> to ▸**, with every destination grouped by section.
- If all 16 slots are in use, the menu says so.
# THE MOD MATRIX DRAWER
- Open it with the **MOD** button or the MOD MATRIX tab. It shows all 16 slots: source, amount, destination.
- Drag an amount to set it (**Shift** for fine); double-click for the stored amount.
- Click a slot's **LED** to switch it off and on again. Right-click an amount for Clear amount, Clear slot or Restore slot.
- **Undo / Redo** at the top right step through your mod changes.
===
# BUTTONS WORTH KNOWING
- **MOD** opens the mod matrix. **GLOBAL** and **PROGRAM** open the synth's settings drawers. Each lights while its drawer is open.
- **TAP TEMPO**: tap in time at least twice; a pause of two seconds starts a new count.
- **RECORD** (SEQ) opens the sequencer with step entry (REC) on; **PLAY** runs the TEO-5's step sequencer.
- **LIBRARY** opens your program library (lit while open).
- **PANIC** stops hung notes. The program, sequencer and arpeggiator are left alone.
- **INIT** loads the TEO-5's Basic Program into the edit buffer, a clean start for a new sound.
# DRAWERS
The tabs on the sides open drawers: **LIBRARY**, **MOD MATRIX** and **SEQUENCER** on the left; **GLOBAL**, **PROGRAM**, **MIDI** and **SKINS** (the editor's colours) on the right. One drawer opens on each side at a time. The sequencer has its own ? guide.
# SHORTCUTS
| Fine adjustment | Shift + drag or wheel
| Back to stored value | Double-click a knob
| Add modulation | Right-click a control
| Next / previous program | → / ← (no drawer open)
| Next / previous bank | ↑ / ↓ (no drawer open)
| Rename the program | Double-click the display)";

// A line with **bold** parts.
juce::AttributedString rich (const juce::String& text, float size = 14.0f)
{
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    as.setLineSpacing (0.5f);
    auto rest = text;
    for (bool bold = false; rest.isNotEmpty(); bold = ! bold)
    {
        const auto part = rest.upToFirstOccurrenceOf ("**", false, false);
        rest = rest.fromFirstOccurrenceOf ("**", false, false);
        if (part.isNotEmpty())
            as.append (part, juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)),
                       bold ? juce::Colours::white : Palette::silkscreen);
    }
    return as;
}
} // namespace

// One column: the guide's blocks laid out for the column's width.
class PanelGuide::Column : public juce::Component
{
public:
    explicit Column (const juce::String& text) { lines.addLines (text.trim()); }

    void layoutFor (int width)
    {
        blocks.clear();
        float y = 2.0f;
        const float w = (float) width;
        for (auto& line : lines)
        {
            Block b;
            if (line.startsWith ("#"))
            {
                b.kind = Block::Heading;
                b.amber = line.startsWith ("#!");
                b.text = line.fromFirstOccurrenceOf (" ", false, false);
                y += blocks.empty() ? 0.0f : 7.0f;
                b.area = { 0.0f, y, w, 17.0f };
            }
            else if (line == "[menu]")
            {
                b.kind = Block::Menu;
                b.area = { 0.0f, y + 3.0f, w, 4 * 19.0f + 9.0f };
                y += 6.0f;
            }
            else if (line.startsWith ("|"))
            {
                b.kind = Block::Row;
                b.text = line.substring (1).upToFirstOccurrenceOf ("|", false, false).trim();
                b.value = line.substring (1).fromFirstOccurrenceOf ("|", false, false).trim();
                b.layout.createLayout (rich (b.value, 13.0f), w * 0.5f);
                b.area = { 0.0f, y, w, juce::jmax (19.0f, b.layout.getHeight() + 4.0f) };
            }
            else
            {
                const bool bullet = line.startsWith ("- ");
                b.kind = bullet ? Block::Bullet : Block::Paragraph;
                const float indent = bullet ? 13.0f : 0.0f;
                b.layout.createLayout (rich (bullet ? line.substring (2) : line), w - indent);
                b.area = { indent, y, w - indent, b.layout.getHeight() + (bullet ? 3.0f : 4.0f) };
            }
            y = b.area.getBottom();
            blocks.push_back (std::move (b));
        }
        setSize (width, juce::roundToInt (y + 2.0f));
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& b : blocks)
        {
            switch (b.kind)
            {
                case Block::Heading:
                    g.setColour (b.amber ? amber : heading());
                    g.setFont (TEO5LookAndFeel::panelFont (11.5f));
                    g.drawText (b.text, b.area, juce::Justification::bottomLeft, true);
                    break;
                case Block::Bullet:
                    g.setColour (dim);
                    g.fillEllipse (b.area.getX() - 10.0f, b.area.getY() + 7.5f, 4.0f, 4.0f);
                    b.layout.draw (g, b.area);
                    break;
                case Block::Paragraph:
                    b.layout.draw (g, b.area);
                    break;
                case Block::Row:
                    g.setColour (juce::Colours::white);
                    g.setFont (juce::Font (juce::FontOptions (13.0f)));
                    g.drawText (b.text, b.area.withWidth (b.area.getWidth() * 0.5f).withHeight (17.0f),
                                juce::Justification::centredLeft, true);
                    b.layout.draw (g, b.area.withTrimmedLeft (b.area.getWidth() * 0.5f).withTrimmedTop (1.5f));
                    g.setColour (skinned (0xff23252a, Skin::Area::Drawer));
                    g.fillRect (b.area.withTop (b.area.getBottom() - 1.0f));
                    break;
                case Block::Menu:
                    paintMenu (g, b.area);
                    break;
            }
        }
    }

private:
    struct Block
    {
        enum Kind { Heading, Paragraph, Bullet, Row, Menu } kind = Paragraph;
        bool amber = false;
        juce::String text, value;
        juce::TextLayout layout;
        juce::Rectangle<float> area;
    };

    // A drawing of the right-click menu on CUTOFF, as the editor shows it.
    static void paintMenu (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (skinned (0xff1b1d21, Skin::Area::Drawer));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        auto rows = r.reduced (1.0f, 4.0f);
        auto row = [&rows] { return rows.removeFromTop (19.0f); };
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.setColour (dim);
        g.drawText ("Cutoff", row().withTrimmedLeft (9.0f), juce::Justification::centredLeft);
        auto item = row();
        g.setColour (Palette::pinstripe);
        g.fillRect (item);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText ("Modulate Cutoff from", item.withTrimmedLeft (9.0f), juce::Justification::centredLeft);
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb8"), item.withTrimmedRight (9.0f), juce::Justification::centredRight);
        g.setColour (edge);
        g.fillRect (rows.removeFromTop (1.0f).reduced (6.0f, 0.0f));
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("Already modulated by", row().withTrimmedLeft (9.0f), juce::Justification::centredLeft);
        g.setColour (Palette::silkscreen);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText ("Slot 3: Env 1 (+40)", row().withTrimmedLeft (9.0f), juce::Justification::centredLeft);
    }

    juce::StringArray lines;
    std::vector<Block> blocks;
};

PanelGuide::PanelGuide()
{
    setWantsKeyboardFocus (true);
    for (auto& text : juce::StringArray::fromTokens (juce::String::fromUTF8 (panelGuide).replace ("\n===\n", "\x01"), "\x01", {}))
    {
        auto* col = columns.add (new Column (text));
        auto* vp = viewports.add (new juce::Viewport());
        vp->setViewedComponent (col, false);
        vp->setScrollBarsShown (true, false);
        vp->setScrollBarThickness (8);
        vp->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, skinned (0xff4a4f59, Skin::Area::Drawer));
        addAndMakeVisible (vp);
    }
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

PanelGuide::~PanelGuide() = default;

void PanelGuide::scrollToTop()
{
    for (auto* vp : viewports)
        vp->setViewPosition (0, 0);
}

bool PanelGuide::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

void PanelGuide::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (sheet);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Palette::pinstripe.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    // Header: title and what the panel does; footer: where the full guide is.
    g.setColour (Palette::silkscreen);
    g.setFont (TEO5LookAndFeel::panelFont (13.0f));
    g.drawText ("THE FRONT PANEL", 34, 0, 200, 36, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText ("Every change goes to the TEO-5's edit buffer straight away. Nothing is stored in its memory until you press WRITE.",
                214, 0, getWidth() - 330, 36, juce::Justification::centredLeft);
    g.drawText ("The full user's guide (PDF) comes with the download.      Close: the ? button, CLOSE or Esc.",
                14, getHeight() - 28, getWidth() - 28, 28, juce::Justification::centredLeft);

    g.setColour (edge);
    g.fillRect (0, 36, getWidth(), 1);
    g.fillRect (0, getHeight() - 29, getWidth(), 1);
    for (int i = 1; i < viewports.size(); ++i)
        g.fillRect (viewports[i]->getX() - 1, 37, 1, getHeight() - 66);
}

void PanelGuide::resized()
{
    close.setBounds (getWidth() - 84, 7, 70, 22);
    auto area = getLocalBounds().withTrimmedTop (37).withTrimmedBottom (29);
    const int n = viewports.size(), colW = area.getWidth() / juce::jmax (1, n);
    for (int i = 0; i < n; ++i)
    {
        auto c = (i == n - 1 ? area : area.removeFromLeft (colW)).reduced (0, 6);
        if (i > 0)
            c.removeFromLeft (1);
        viewports[i]->setBounds (c.withTrimmedLeft (16).withTrimmedRight (6));
        columns[i]->layoutFor (viewports[i]->getWidth() - 12);
    }
}

void PanelGuide::HelpButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setGradientFill (juce::ColourGradient (on ? Palette::pinstripe : skinned (0xff33363c, Skin::Area::Drawer), r.getCentreX() - 3, r.getY() + 4,
                                             on ? Palette::pinstripe.darker (0.3f) : skinned (0xff1d1f23, Skin::Area::Drawer), r.getCentreX(), r.getBottom(), true));
    g.fillEllipse (r);
    g.setColour (on ? heading() : juce::Colour (highlighted || down ? 0xff8a919c : 0xff5d636d));
    g.drawEllipse (r, 1.0f);
    g.setColour (on ? juce::Colours::white : Palette::silkscreen);
    g.setFont (TEO5LookAndFeel::panelFont (r.getHeight() * 0.5f));
    g.drawText ("?", r.translated (0.5f, 0.5f), juce::Justification::centred);
}

} // namespace teo5
