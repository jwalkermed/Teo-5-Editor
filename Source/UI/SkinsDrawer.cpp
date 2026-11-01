// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "SkinsDrawer.h"
#include "TEO5LookAndFeel.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace teo5
{
namespace
{
constexpr int headerH = 40, rowH = 82, rowGap = 6, thumbW = 190, thumbH = 62, sectionH = 22, actionsH = 20, buttonsH = 34, footH = 50;

void styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, skinned (0xff232427));
    b.setColour (juce::TextButton::textColourOffId, Palette::silkscreen);
}
} // namespace

// ---- The list: BUILT IN, then MY SKINS (with Edit / Duplicate / Export / Delete), then New and Import -------------

class SkinsDrawer::List : public juce::Component
{
public:
    explicit List (SkinsDrawer& o) : owner (o)
    {
        for (auto* b : { &newButton, &importButton })
        {
            styleButton (*b);
            addAndMakeVisible (b);
        }
        newButton.onClick = [this] { owner.startNew(); };
        importButton.onClick = [this] { owner.importSkin(); };
        newButton.setTooltip ("Make your own skin, starting from the one in use");
        importButton.setTooltip ("Add a skin from a .teoskin file");
    }

    std::vector<juce::Image> thumbnails;

    // Rows: section titles, skins (custom ones have a line of actions under them), the buttons, the note.
    int rowTop (int index) const
    {
        int y = sectionH; // BUILT IN
        for (int i = 0; i < index; ++i)
        {
            y += rowH + rowGap + (Skin::isCustom (i) ? actionsH : 0);
            if (i == Skin::builtInCount() - 1)
                y += sectionH; // MY SKINS
        }
        return y;
    }
    juce::Rectangle<int> rowBounds (int index) const { return { 0, rowTop (index), getWidth(), rowH }; }
    int listBottom() const
    {
        const int n = Skin::count();
        return n > Skin::builtInCount() ? rowTop (n - 1) + rowH + rowGap + actionsH : rowTop (n - 1) + rowH + rowGap + sectionH + 18;
    }
    int preferredHeight() const { return listBottom() + buttonsH + footH; }

    int rowAt (juce::Point<int> p) const
    {
        for (int i = 0; i < Skin::count(); ++i)
            if (rowBounds (i).contains (p))
                return i;
        return -1;
    }

    // The actions under a custom skin: Edit, Duplicate, Export, Delete.
    static constexpr const char* actions[] = { "Edit", "Duplicate", "Export", "Delete" };
    // From the left edge, under the picture, each as wide as its word (all four fit in the narrowest drawer).
    juce::Rectangle<int> actionBounds (int index, int action) const
    {
        static const int widths[] = { 30, 64, 46, 46 };
        int x = 6;
        for (int a = 0; a < action; ++a)
            x += widths[a] + 14;
        return { x, rowBounds (index).getBottom() + 1, widths[action], actionsH - 2 };
    }

    void resized() override
    {
        const int y = listBottom() + 4;
        newButton.setBounds (0, y, 170, 24);
        importButton.setBounds (getWidth() - 90, y, 90, 24);
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = juce::Colour (Palette::pinstripe);
        auto section = [&] (const char* text, int y)
        {
            g.setColour (Palette::silkscreen.withAlpha (0.6f));
            g.setFont (TEO5LookAndFeel::panelFont (8.5f));
            g.drawText (text, juce::Rectangle<int> (2, y, getWidth(), sectionH), juce::Justification::centredLeft);
        };
        section ("BUILT IN", 0);
        section ("MY SKINS", rowTop (Skin::builtInCount() - 1) + rowH + rowGap);
        if (Skin::count() == Skin::builtInCount())
        {
            g.setColour (Palette::silkscreen.withAlpha (0.55f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("None yet: make one with New custom skin.", juce::Rectangle<int> (2, rowTop (Skin::count() - 1) + rowH + rowGap + sectionH, getWidth(), 18),
                        juce::Justification::centredLeft);
        }

        for (int i = 0; i < Skin::count(); ++i)
        {
            auto r = rowBounds (i);
            const bool on = i == Skin::current();
            if (on || i == hover)
            {
                g.setColour (on ? accent : accent.withAlpha (0.45f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.75f), 5.0f, on ? 2.0f : 1.0f);
            }
            auto inner = r.reduced (6);
            auto pic = inner.removeFromLeft (thumbW).withSizeKeepingCentre (thumbW, thumbH);
            if (juce::isPositiveAndBelow (i, (int) thumbnails.size()) && thumbnails[(size_t) i].isValid())
                g.drawImage (thumbnails[(size_t) i], pic.toFloat(), juce::RectanglePlacement::stretchToFit);
            g.setColour (juce::Colours::black);
            g.drawRect (pic, 1);
            inner.removeFromLeft (10);

            const auto info = Skin::info (i);
            auto name = inner.removeFromTop (inner.getHeight() / 2).withTrimmedTop (4);
            g.setColour (Palette::silkscreen);
            g.setFont (TEO5LookAndFeel::panelFont (10.5f));
            g.drawText (info.name.toUpperCase(), name, juce::Justification::bottomLeft, true);
            if (on)
            {
                g.setColour (accent);
                g.setFont (TEO5LookAndFeel::panelFont (8.0f));
                g.drawText ("IN USE", name, juce::Justification::bottomRight, false);
            }
            g.setColour (Palette::silkscreen.withAlpha (0.75f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (info.description, inner.withTrimmedTop (2), juce::Justification::topLeft, 3, 1.0f);

            if (Skin::isCustom (i))
                for (int a = 0; a < 4; ++a)
                {
                    const bool hot = hoverAction == std::make_pair (i, a);
                    g.setColour (hot ? accent : Palette::silkscreen.withAlpha (0.7f));
                    g.setFont (juce::FontOptions (11.5f).withStyle (hot ? "Bold" : "Regular"));
                    g.drawText (actions[a], actionBounds (i, a), juce::Justification::centredLeft, false);
                }
        }

        auto foot = juce::Rectangle<int> (0, listBottom() + buttonsH + 4, getWidth(), footH - 4);
        g.setColour (accent.withAlpha (0.35f));
        g.fillRect (foot.removeFromTop (1));
        g.setColour (Palette::silkscreen.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Your choice is saved for every TEO-5 Editor on this Mac, in every project.", foot.withTrimmedTop (6),
                          juce::Justification::topLeft, 2, 1.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! e.mouseWasClicked())
            return;
        for (int i = Skin::builtInCount(); i < Skin::count(); ++i)
            for (int a = 0; a < 4; ++a)
                if (actionBounds (i, a).contains (e.getPosition()))
                {
                    switch (a)
                    {
                        case 0: owner.startEdit (i); break;
                        case 1: owner.duplicate (i); break;
                        case 2: owner.exportSkin (i); break;
                        default: owner.confirmDelete (i); break;
                    }
                    return;
                }
        if (const int i = rowAt (e.getPosition()); i >= 0)
            Skin::select (i); // the editor re-colours itself; the pictures stay as they are
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        std::pair<int, int> action { -1, -1 };
        for (int i = Skin::builtInCount(); i < Skin::count(); ++i)
            for (int a = 0; a < 4; ++a)
                if (actionBounds (i, a).contains (e.getPosition()))
                    action = { i, a };
        const int i = action.first >= 0 ? -1 : rowAt (e.getPosition());
        setMouseCursor (i >= 0 || action.first >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (i != hover || action != hoverAction)
        {
            hover = i;
            hoverAction = action;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        hoverAction = { -1, -1 };
        repaint();
    }

private:
    SkinsDrawer& owner;
    juce::TextButton newButton { "+ New custom skin" }, importButton { juce::String::fromUTF8 ("Import\xe2\x80\xa6") };
    int hover = -1;
    std::pair<int, int> hoverAction { -1, -1 };
};

// ---- The skin editor ---------------------------------------------------------------------------------------------

class SkinsDrawer::Editor : public juce::Component, private juce::ChangeListener
{
public:
    explicit Editor (SkinsDrawer& o) : owner (o)
    {
        name.setTextToShowWhenEmpty ("Skin name", Palette::silkscreen.withAlpha (0.4f));
        name.setInputRestrictions (24);
        name.onTextChange = [this] { working.name = name.getText(); };
        addAndMakeVisible (name);

        selector.addChangeListener (this);
        addAndMakeVisible (selector);

        hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
        hex.onReturnKey = hex.onFocusLost = [this]
        {
            const auto t = hex.getText().trim().removeCharacters ("#");
            if (t.length() == 6)
                setElement (juce::Colour::fromString ("ff" + t));
        };
        addAndMakeVisible (hex);

        for (auto* b : { &darkButton, &lightButton, &fixButton, &cancelButton, &saveButton })
        {
            styleButton (*b);
            addAndMakeVisible (b);
        }
        darkButton.setClickingTogglesState (false);
        darkButton.onClick = [this] { working.lightDrawers = false; changed(); };
        lightButton.onClick = [this] { working.lightDrawers = true; changed(); };
        fixButton.onClick = [this]
        {
            // Black or white lettering, whichever reads better on the panel.
            working.lettering = Skin::contrast (juce::Colour (0xfff2f2f2), working.panel) >= Skin::contrast (juce::Colour (0xff15171a), working.panel)
                                  ? juce::Colour (0xfff2f2f2) : juce::Colour (0xff15171a);
            changed();
            showElement();
        };
        cancelButton.onClick = [this] { owner.cancelEdit(); };
        saveButton.onClick = [this] { save(); };
        saveButton.setColour (juce::TextButton::buttonColourId, Palette::pinstripe.withAlpha (0.45f));
    }

    ~Editor() override { selector.removeChangeListener (this); }

    // index: the custom skin being edited (shown live); isNew: it was made for this edit (Cancel removes it).
    void open (int skinIndex, int previousSkin, bool isNew)
    {
        index = skinIndex;
        previous = previousSkin;
        fresh = isNew;
        original = working = Skin::coloursOf (index);
        name.setText (working.name, false);
        element = 0;
        showElement();
        changed (false);
    }

    int editingIndex() const { return index; }

    void cancel()
    {
        if (index < 0)
            return;
        const int i = std::exchange (index, -1);
        if (fresh)
        {
            Skin::select (previous, true);
            Skin::reapply (true);
            Skin::removeCustom (i);
        }
        else
        {
            Skin::updateCustom (i, original, true);
            Skin::select (previous, true);
            Skin::reapply (true);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto section = [&] (const char* text, juce::Rectangle<int> r)
        {
            g.setColour (Palette::silkscreen.withAlpha (0.6f));
            g.setFont (TEO5LookAndFeel::panelFont (8.5f));
            g.drawText (text, r, juce::Justification::centredLeft);
        };
        section ("NAME", { 0, 0, getWidth(), 18 });
        section ("COLOURS", { 0, 50, getWidth(), 18 });

        const auto accent = juce::Colour (Palette::pinstripe);
        for (int i = 0; i < numElements; ++i)
        {
            auto r = elementBounds (i);
            if (i == element)
            {
                g.setColour (accent.withAlpha (0.12f));
                g.fillRoundedRectangle (r.toFloat(), 4.0f);
                g.setColour (accent);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.2f);
            }
            auto inner = r.reduced (6, 2);
            const auto c = colourOf (i);
            auto code = inner.removeFromRight (64);
            auto sw = inner.removeFromRight (34).withSizeKeepingCentre (34, 18);
            g.setColour (c);
            g.fillRoundedRectangle (sw.toFloat(), 3.0f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (sw.toFloat(), 3.0f, 1.0f);
            g.setColour (Palette::silkscreen.withAlpha (0.8f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("#" + c.toDisplayString (false), code, juce::Justification::centredRight);
            g.setColour (Palette::silkscreen);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (elementNames[i], inner.removeFromTop (inner.getHeight() / 2 + 2), juce::Justification::bottomLeft);
            g.setColour (Palette::silkscreen.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (elementHints[i], inner, juce::Justification::topLeft);
        }
        g.setColour (Palette::silkscreen);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Drawers", juce::Rectangle<int> (6, drawerToggleY(), 100, 24), juce::Justification::centredLeft);

        // Presets under the colour picker.
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            auto r = presetBounds (i).toFloat();
            g.setColour (juce::Colour (presets[(size_t) i]));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (r, 2.5f, 1.0f);
        }

        const double ratio = Skin::contrast (working.lettering, working.panel);
        if (ratio < 3.0)
        {
            auto w = warningBounds().toFloat();
            g.setColour (juce::Colour (0xfff0b44a));
            g.fillRoundedRectangle (w, 5.0f);
            g.setColour (juce::Colour (0xff1b1406));
            g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (juce::String::fromUTF8 ("\xe2\x9a\xa0 Lettering is hard to read on this panel (contrast ")
                                  + juce::String (ratio, 1) + ":1).",
                              w.toNearestInt().reduced (8, 2).withTrimmedRight (96), juce::Justification::centredLeft, 2, 1.0f);
        }
    }

    void resized() override
    {
        name.setBounds (0, 20, getWidth(), 24);
        const int y = drawerToggleY();
        lightButton.setBounds (getWidth() - 64, y, 64, 24);
        darkButton.setBounds (getWidth() - 132, y, 64, 24);
        selector.setBounds (0, selectorTop(), getWidth(), selectorH);
        hex.setBounds (0, selectorTop() + selectorH + 6, 86, 24);
        const auto w = warningBounds();
        fixButton.setBounds (w.getRight() - 96, w.getY() + 6, 88, 22);
        const int by = w.getBottom() + 8;
        saveButton.setBounds (getWidth() - 80, by, 80, 26);
        cancelButton.setBounds (getWidth() - 166, by, 80, 26);
    }

    int preferredHeight() const { return warningBounds().getBottom() + 8 + 26 + 8; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < numElements; ++i)
            if (elementBounds (i).contains (e.getPosition()))
            {
                element = i;
                showElement();
                repaint();
                return;
            }
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presetBounds (i).contains (e.getPosition()))
                setElement (juce::Colour (presets[(size_t) i]));
    }

private:
    static constexpr int numElements = 5;
    static constexpr const char* elementNames[] = { "Panel", "Lettering", "Pinstripes", "LEDs", "Drawers" };
    static constexpr const char* elementHints[] = { "the face of the front panel", "labels, names, drawer text",
                                                    "lines, selections, tabs", "lights, lit buttons, display",
                                                    "their background shade" };
    const std::vector<juce::uint32> presets { 0xffffffff, 0xffe6e6e6, 0xff9aa0a8, 0xff2c2d31, 0xff111214, 0xffff4a1c, 0xffffb347,
                                              0xfff2d14a, 0xff33e070, 0xff3ec6db, 0xff2f7fd0, 0xff7b5cff, 0xffff3fb0, 0xff8a2c3a,
                                              0xffc9894f };

    // Fixed layout (the height is needed before the picker is laid out).
    static constexpr int selectorH = 150;
    juce::Rectangle<int> elementBounds (int i) const { return { 0, 70 + i * 36, getWidth(), 34 }; }
    int drawerToggleY() const { return elementBounds (numElements - 1).getBottom() + 6; }
    int selectorTop() const { return drawerToggleY() + 32; }
    juce::Rectangle<int> presetBounds (int i) const { return { 94 + i * 16, selectorTop() + selectorH + 11, 13, 13 }; }
    juce::Rectangle<int> warningBounds() const { return { 0, selectorTop() + selectorH + 38, getWidth(), 36 }; }

    juce::Colour colourOf (int i) const
    {
        switch (i)
        {
            case 0: return working.panel;
            case 1: return working.lettering;
            case 2: return working.pinstripes;
            case 3: return working.leds;
            default: return working.drawers;
        }
    }

    void setElement (juce::Colour c)
    {
        switch (element)
        {
            case 0: working.panel = c; break;
            case 1: working.lettering = c; break;
            case 2: working.pinstripes = c; break;
            case 3: working.leds = c; break;
            default: working.drawers = c; break;
        }
        showElement();
        changed();
    }

    // The picker and hex box show the chosen element's colour.
    void showElement()
    {
        selector.setCurrentColour (colourOf (element), juce::dontSendNotification);
        hex.setText ("#" + colourOf (element).toDisplayString (false), false);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Dragging in the picker: the whole editor follows as you go.
        const auto c = selector.getCurrentColour();
        switch (element)
        {
            case 0: working.panel = c; break;
            case 1: working.lettering = c; break;
            case 2: working.pinstripes = c; break;
            case 3: working.leds = c; break;
            default: working.drawers = c; break;
        }
        hex.setText ("#" + c.toDisplayString (false), false);
        changed();
    }

    void changed (bool apply = true)
    {
        darkButton.setToggleState (! working.lightDrawers, juce::dontSendNotification);
        lightButton.setToggleState (working.lightDrawers, juce::dontSendNotification);
        darkButton.setColour (juce::TextButton::buttonOnColourId, Palette::pinstripe);
        lightButton.setColour (juce::TextButton::buttonOnColourId, Palette::pinstripe);
        fixButton.setVisible (Skin::contrast (working.lettering, working.panel) < 3.0);
        if (apply && index >= 0)
            Skin::updateCustom (index, working, false); // live, saved only with Save
        repaint();
    }

    void save()
    {
        if (index < 0)
            return;
        working.name = name.getText().trim().isEmpty() ? juce::String ("My Skin") : name.getText().trim();
        Skin::updateCustom (index, working, true);
        Skin::rememberChoice();
        index = -1;
        owner.cancelEdit(); // back to the list (nothing to undo now)
    }

    SkinsDrawer& owner;
    juce::TextEditor name, hex;
    juce::ColourSelector selector { juce::ColourSelector::showColourspace, 0, 4 };
    juce::TextButton darkButton { "Dark" }, lightButton { "Light" }, fixButton { "Fix contrast" }, cancelButton { "Cancel" }, saveButton { "Save" };
    Skin::Custom working, original;
    int index = -1, previous = 0, element = 0;
    bool fresh = false;
};

// ---- Drawer ----------------------------------------------------------------------------------------------------

SkinsDrawer::SkinsDrawer() : list (std::make_unique<List> (*this)), editor (std::make_unique<Editor> (*this))
{
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false); // vertical only, shown when the list doesn't fit
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

SkinsDrawer::~SkinsDrawer() = default;

bool SkinsDrawer::isEditing() const { return editor->editingIndex() >= 0; }

void SkinsDrawer::refreshThumbnails()
{
    if (! renderPanel)
        return;
    list->thumbnails.clear();
    for (int i = 0; i < Skin::count(); ++i)
        list->thumbnails.push_back (renderPanel (i));
    resized();
    list->repaint();
}

void SkinsDrawer::visibilityChanged()
{
    // Pictures of the panel in each skin, made when the drawer opens (they match the panel as it is now).
    if (isVisible())
        refreshThumbnails();
    else if (isEditing())
        cancelEdit(); // closing the drawer mid-edit puts things back
}

void SkinsDrawer::startNew()
{
    const int previous = Skin::current();
    auto c = Skin::coloursOf (previous);
    c.name = "My Skin " + juce::String (Skin::count() - Skin::builtInCount() + 1);
    const int index = Skin::addCustom (c, false);
    Skin::select (index, true);
    Skin::reapply (true);
    editor->open (index, previous, true);
    resized();
    repaint();
}

void SkinsDrawer::startEdit (int index)
{
    if (! Skin::isCustom (index))
        return;
    const int previous = Skin::current() == index ? index : Skin::current();
    Skin::select (index, true);
    Skin::reapply (true);
    editor->open (index, previous, false);
    resized();
    repaint();
}

void SkinsDrawer::cancelEdit()
{
    editor->cancel();
    refreshThumbnails();
    resized();
    repaint();
    showSkin (Skin::current());
}

void SkinsDrawer::showSkin (int index)
{
    if (isEditing() || ! juce::isPositiveAndBelow (index, Skin::count()))
        return;
    const auto r = list->rowBounds (index).withBottom (list->rowBounds (index).getBottom() + actionsH);
    const int top = viewport.getViewPositionY(), h = viewport.getViewHeight();
    if (r.getY() < top || r.getBottom() > top + h)
        viewport.setViewPosition (0, juce::jmax (0, r.getBottom() - h + 8));
}

void SkinsDrawer::duplicate (int index)
{
    auto c = Skin::coloursOf (index);
    c.name = c.name + " copy";
    const int added = Skin::addCustom (c);
    refreshThumbnails();
    showSkin (added);
}

void SkinsDrawer::confirmDelete (int index)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete skin",
                                        "Delete \"" + Skin::info (index).name + "\"? This can't be undone (export it first to keep a copy).",
                                        "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SkinsDrawer> (this), index] (int r)
                                        {
                                            if (r == 1 && safe != nullptr)
                                            {
                                                Skin::removeCustom (index);
                                                safe->refreshThumbnails();
                                            }
                                        }));
}

void SkinsDrawer::exportSkin (int index)
{
    chooser = std::make_unique<juce::FileChooser> ("Export skin", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                      .getChildFile (juce::File::createLegalFileName (Skin::info (index).name) + ".teoskin"),
                                                   "*.teoskin");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [index] (const juce::FileChooser& fc)
                          {
                              if (auto f = fc.getResult(); f != juce::File())
                                  Skin::exportCustom (index, f.withFileExtension ("teoskin"));
                          });
}

void SkinsDrawer::importSkin()
{
    chooser = std::make_unique<juce::FileChooser> ("Import skin", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.teoskin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SkinsDrawer> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;
                              if (auto c = Skin::readCustom (fc.getResult()))
                              {
                                  Skin::select (Skin::addCustom (*c));
                                  safe->refreshThumbnails();
                                  safe->showSkin (Skin::current());
                              }
                          });
}

void SkinsDrawer::paint (juce::Graphics& g)
{
    g.fillAll (skinned (0xf0181a1c));
    g.setColour (Palette::pinstripe);
    g.fillRect (0, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);

    const bool editing = isEditing();
    g.setColour (Palette::silkscreen);
    g.setFont (TEO5LookAndFeel::panelFont (14.0f));
    g.drawText (editing ? "SKIN EDITOR" : "SKINS", juce::Rectangle<int> (16, 8, 220, 24), juce::Justification::centredLeft);
    g.setColour (skinned (0xff8d939c));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (editing ? "Changes show live" : "Click to use", juce::Rectangle<int> (getWidth() - 150, 8, 136, 24),
                juce::Justification::centredRight);
}

void SkinsDrawer::resized()
{
    auto area = getLocalBounds().withTrimmedTop (headerH + 4).reduced (14, 0).withTrimmedBottom (8);
    viewport.setBounds (area);
    const bool editing = isEditing();
    juce::Component* content = editing ? static_cast<juce::Component*> (editor.get()) : list.get();
    if (viewport.getViewedComponent() != content)
        viewport.setViewedComponent (content, false);
    const int h = editing ? editor->preferredHeight() : list->preferredHeight();
    content->setSize (area.getWidth() - (h > area.getHeight() ? 12 : 0), h);
}

} // namespace teo5
