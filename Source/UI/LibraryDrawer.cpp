// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "LibraryDrawer.h"
#include "TEO5LookAndFeel.h"
#include "../Library/SyxIO.h"

namespace teo5
{

namespace
{
constexpr int rowHeight = 22;
const juce::String dragDescription = "teo5-programs";
const juce::String bankDragDescription = "teo5-bank";

// Per-program icons after the favourite star: export .syx, rename, delete. Each is iconWidth wide.
enum class RowIcon { None, Star, Export, Rename, Delete };
constexpr int starWidth = 22, iconWidth = 20, iconsRight = starWidth + 3 * iconWidth;
const juce::Colour saveAmber (0xffe8a33a); // the slot chosen to save into

RowIcon iconAt (int x)
{
    if (x < starWidth) return RowIcon::Star;
    if (x >= iconsRight) return RowIcon::None;
    const RowIcon icons[] = { RowIcon::Export, RowIcon::Rename, RowIcon::Delete };
    return icons[(x - starWidth) / iconWidth];
}

void drawRowIcon (juce::Graphics& g, RowIcon icon, juce::Rectangle<float> box, juce::Colour c)
{
    const auto ctr = box.getCentre();
    juce::Path p;
    g.setColour (c);
    switch (icon)
    {
        case RowIcon::Export: // down arrow: to a file
        {
            p.startNewSubPath (ctr.x, ctr.y - 5.0f);
            p.lineTo (ctr.x, ctr.y + 4.0f);
            p.startNewSubPath (ctr.x - 4.0f, ctr.y + 0.5f);
            p.lineTo (ctr.x, ctr.y + 4.5f);
            p.lineTo (ctr.x + 4.0f, ctr.y + 0.5f);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillRect (ctr.x - 4.5f, ctr.y + 5.5f, 9.0f, 1.2f); // the line it goes to
            break;
        }
        case RowIcon::Rename: // pencil
        {
            p.addRectangle (-1.6f, -6.0f, 3.2f, 9.0f);
            p.addTriangle (-1.6f, 3.4f, 1.6f, 3.4f, 0.0f, 6.0f);
            p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f).translated (ctr));
            g.strokePath (p, juce::PathStrokeType (1.1f));
            break;
        }
        case RowIcon::Delete: // trash can
        {
            p.addRectangle (ctr.x - 3.5f, ctr.y - 2.5f, 7.0f, 8.0f);
            p.startNewSubPath (ctr.x - 5.0f, ctr.y - 4.0f);
            p.lineTo (ctr.x + 5.0f, ctr.y - 4.0f);
            p.startNewSubPath (ctr.x - 1.5f, ctr.y - 4.0f);
            p.lineTo (ctr.x - 1.5f, ctr.y - 5.5f);
            p.lineTo (ctr.x + 1.5f, ctr.y - 5.5f);
            p.lineTo (ctr.x + 1.5f, ctr.y - 4.0f);
            p.startNewSubPath (ctr.x - 1.2f, ctr.y - 0.5f);
            p.lineTo (ctr.x - 1.2f, ctr.y + 3.5f);
            p.startNewSubPath (ctr.x + 1.2f, ctr.y - 0.5f);
            p.lineTo (ctr.x + 1.2f, ctr.y + 3.5f);
            g.strokePath (p, juce::PathStrokeType (1.1f));
            break;
        }
        case RowIcon::None:
        case RowIcon::Star:
            break;
    }
}
}

// ---- List view ----------------------------------------------------------------------------------

class LibraryDrawer::ListView : public juce::Component, public juce::DragAndDropTarget, public juce::TooltipClient
{
public:
    ListView (LibraryDrawer& o, int paneIndex) : owner (o), pane (paneIndex) { setWantsKeyboardFocus (true); }

    int rowAt (int y) const { return y / rowHeight; }

    void updateSize (int width) { setSize (width, juce::jmax (1, (int) owner.rows (pane).size()) * rowHeight); }

    void paint (juce::Graphics& g) override
    {
        const auto& rows = owner.rows (pane);
        if (rows.empty())
        {
            g.setColour (dropRow >= 0 ? Palette::ledOn : Palette::silkscreen.withAlpha (0.3f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 4.0f, 1.0f);
            g.setFont (TEO5LookAndFeel::panelFont (9.0f));
            g.drawFittedText (pane == 0 ? "Fetch banks from the TEO-5 (Fetch button)"
                                        : "Drag programs here to start a bank,\nor drop .syx files",
                              getLocalBounds().reduced (12), juce::Justification::centred, 3);
            return;
        }
        auto clip = g.getClipBounds();
        const int first = juce::jmax (0, rowAt (clip.getY())), last = juce::jmin ((int) rows.size() - 1, rowAt (clip.getBottom()));
        auto& model = owner.getModel();

        for (int i = first; i <= last; ++i)
        {
            auto r = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
            const auto& row = rows[(size_t) i];
            if (! juce::isPositiveAndBelow (row.bank, (int) model.banks().size()))
                continue;
            const auto& bank = model.banks()[(size_t) row.bank];

            if (row.isBank)
            {
                g.setColour (juce::Colour (0xff101113));
                g.fillRect (r);
                g.setColour (Palette::pinstripe);
                g.fillRect (r.removeFromBottom (1));

                juce::Path tri;
                const float cx = 10.0f, cy = (float) i * rowHeight + rowHeight * 0.5f;
                if (owner.isCollapsed (row.bank))
                    tri.addTriangle (cx - 3, cy - 4, cx - 3, cy + 4, cx + 3, cy);
                else
                    tri.addTriangle (cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 3);
                g.setColour (Palette::silkscreen.withAlpha (0.8f));
                g.fillPath (tri);

                g.setFont (TEO5LookAndFeel::panelFont (11.5f));
                g.setColour (Palette::silkscreen);
                auto text = r.withTrimmedLeft (20).withTrimmedRight (6);
                g.drawText (bank.name, text, juce::Justification::centredLeft, true);
                g.setFont (TEO5LookAndFeel::panelFont (8.5f));
                const int unsent = (int) std::count_if (bank.programs.begin(), bank.programs.end(),
                                                        [] (const LibraryProgram& p) { return p.unsent; });
                if (unsent > 0)
                {
                    g.setColour (Palette::ledOn);
                    g.drawText (juce::String (unsent) + " NOT SENT", text, juce::Justification::centredRight, false);
                }
                else
                {
                    g.setColour (Palette::silkscreen.withAlpha (0.45f));
                    const int used = (int) std::count_if (bank.programs.begin(), bank.programs.end(),
                                                          [] (const LibraryProgram& p) { return ! p.isEmpty(); });
                    g.drawText (juce::String (used), text, juce::Justification::centredRight, false);
                }
            }
            else
            {
                const ProgramRef ref { row.bank, row.index };
                const auto* p = model.program (ref);
                if (p == nullptr && ! row.isNew)
                    continue;

                g.setColour (i % 2 ? juce::Colour (0xff1c1d20) : juce::Colour (0xff202124));
                g.fillRect (r);
                const bool target = owner.isSaveTarget (row);
                const bool blocked = owner.isSaving() && model.isFactoryBank (row.bank); // can't save into a factory bank
                if (owner.isSelected (ref))
                {
                    g.setColour (Palette::pinstripe.withAlpha (0.45f));
                    g.fillRect (r);
                }
                if (target)
                {
                    g.setColour (saveAmber.withAlpha (0.16f));
                    g.fillRect (r);
                    g.setColour (saveAmber);
                    g.drawRect (r.reduced (1, 0), 2);
                }

                if (p == nullptr || p->isEmpty())
                {
                    // An empty slot: its number and "Empty" (while saving: a place to save into).
                    auto text = r.withTrimmedLeft (iconsRight + 16).withTrimmedRight (6);
                    if (target)
                    {
                        g.setFont (TEO5LookAndFeel::panelFont (7.5f));
                        g.setColour (saveAmber);
                        g.drawText ("SAVE HERE", text.removeFromRight (64), juce::Justification::centredRight, false);
                    }
                    else if (p != nullptr && p->unsent)
                    {
                        // Deleted from a TEO-5 bank: the synth still has the old program until the bank is sent.
                        g.setColour (Palette::ledOn);
                        g.fillRect (r.getX(), r.getY() + 2, 2, r.getHeight() - 4);
                        g.setFont (TEO5LookAndFeel::panelFont (7.5f));
                        g.drawText ("NOT SENT", text.removeFromRight (56), juce::Justification::centredRight, false);
                    }
                    g.setFont (TEO5LookAndFeel::panelFont (9.0f));
                    g.setColour (Palette::silkscreen.withAlpha (0.3f));
                    g.drawText (juce::String (row.index + 1).paddedLeft ('0', 2), text.removeFromLeft (24), juce::Justification::centredLeft);
                    g.setFont (juce::FontOptions (13.0f).withStyle ("Italic"));
                    if (owner.isSaving() && ! blocked)
                        g.setColour (Palette::silkscreen.withAlpha (0.6f));
                    g.drawText (owner.isSaving() && ! blocked ? "Empty: save here" : "Empty", text, juce::Justification::centredLeft, true);
                    continue;
                }

                // Favourite star.
                g.setColour (p->favorite ? juce::Colour (0xffffc040) : Palette::silkscreen.withAlpha (0.22f));
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (juce::String::fromUTF8 (p->favorite ? "\xe2\x98\x85" : "\xe2\x98\x86"),
                            r.withWidth (22), juce::Justification::centred);

                // Export / rename / delete icons; rename and delete aren't offered in factory banks.
                const bool factoryBank = model.isFactoryBank (row.bank);
                const bool rowHovered = hoverRow == i;
                for (auto icon : { RowIcon::Export, RowIcon::Rename, RowIcon::Delete })
                {
                    if (factoryBank && (icon == RowIcon::Rename || icon == RowIcon::Delete))
                        continue;
                    const int x = starWidth + iconWidth * ((int) icon - (int) RowIcon::Export);
                    const auto box = juce::Rectangle<float> ((float) x, (float) r.getY(), (float) iconWidth, (float) rowHeight);
                    const bool hot = rowHovered && iconAt (hoverX) == icon;
                    drawRowIcon (g, icon, box, hot ? (icon == RowIcon::Delete ? Palette::ledOn : Palette::silkscreen)
                                                   : Palette::silkscreen.withAlpha (rowHovered ? 0.55f : 0.28f));
                }

                // Loaded program LED.
                if (owner.loadedProgram() == ref)
                    TEO5LookAndFeel::drawLed (g, { (float) iconsRight + 7.0f, (float) r.getCentreY() }, 3.0f, true);

                auto text = r.withTrimmedLeft (iconsRight + 16).withTrimmedRight (6);
                g.setFont (TEO5LookAndFeel::panelFont (9.0f));
                g.setColour (Palette::silkscreen.withAlpha (0.5f));
                g.drawText (juce::String (row.index + 1).paddedLeft ('0', 2), text.removeFromLeft (24), juce::Justification::centredLeft);

                if (p->tags.size() > 0)
                {
                    g.setFont (juce::FontOptions (12.0f));
                    g.setColour (Palette::silkscreen.withAlpha (0.65f));
                    auto tags = p->tags.joinIntoString (", ");
                    const int tw = juce::jmin (text.getWidth() / 3, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tags) + 6);
                    g.drawText (tags, text.removeFromRight (tw), juce::Justification::centredRight, true);
                }
                if (target)
                {
                    g.setFont (TEO5LookAndFeel::panelFont (7.5f));
                    g.setColour (saveAmber);
                    g.drawText ("SAVE HERE", text.removeFromRight (64), juce::Justification::centredRight, false);
                }
                else if (p->unsent)
                {
                    // Changed here, not yet on the synth: amber edge + tag until the bank is sent.
                    g.setColour (Palette::ledOn);
                    g.fillRect (r.getX(), r.getY() + 2, 2, r.getHeight() - 4);
                    g.setFont (TEO5LookAndFeel::panelFont (7.5f));
                    g.drawText ("NOT SENT", text.removeFromRight (56), juce::Justification::centredRight, false);
                }
                g.setFont (TEO5LookAndFeel::oledFont (14.0f));
                g.setColour (Palette::silkscreen);
                g.drawText (p->name, text, juce::Justification::centredLeft, true);
                if (blocked)
                {
                    g.setColour (juce::Colour (0xff1c1d20).withAlpha (0.65f)); // dimmed: can't save here
                    g.fillRect (r);
                }
            }
        }

        if (bankDrag && dropRow >= 0 && dropRow < (int) rows.size())
        {
            // Moving a bank: a line where it will land (above the bank under the cursor, or below it).
            g.setColour (Palette::ledOn);
            g.fillRect (0, bankDropY() - 1, getWidth(), 3);
        }
        else if (dropRow >= 0 && dropRow < (int) rows.size())
        {
            g.setColour (Palette::ledOn);
            const auto& target = rows[(size_t) dropRow];
            if (owner.isHardwareBank (target.bank))
            {
                // Replacing: outline the slots the dropped programs will land in.
                const int first = target.isBank ? dropRow + 1 : dropRow;
                const int count = juce::jmax (1, owner.dragCount());
                int last = first;
                while (last + 1 < (int) rows.size() && last + 1 < first + count && ! rows[(size_t) last + 1].isBank)
                    ++last;
                g.drawRect (juce::Rectangle<int> (1, first * rowHeight, getWidth() - 2, (last - first + 1) * rowHeight), 2);
            }
            else
            {
                const int y = (dropRow + (dropBelow ? 1 : 0)) * rowHeight;
                g.fillRect (0, y - 1, getWidth(), 2);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragStarted = false;
        const int row = rowAt (e.y);
        if (! juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()))
            return;
        const auto& r = owner.rows (pane)[(size_t) row];

        if (owner.isSaving() && ! r.isBank)
        {
            owner.rowClicked (pane, row, e); // chooses where to save
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (owner.isSaving())
                return;
            if (! r.isBank && ! owner.isSelected ({ r.bank, r.index }))
                owner.rowClicked (pane, row, e);
            owner.rowMenu (pane, row);
            return;
        }
        if (r.isBank && e.x < 22)
        {
            owner.toggleCollapsed (r.bank);
            return;
        }
        const bool emptySlot = r.isNew || (! r.isBank && owner.getModel().program ({ r.bank, r.index }) != nullptr
                                           && owner.getModel().program ({ r.bank, r.index })->isEmpty());
        if (! r.isBank && e.x < iconsRight && ! emptySlot)
        {
            const auto icon = iconAt (e.x);
            if (icon == RowIcon::Star)
                owner.toggleFavourite (pane, row);
            else
                owner.rowIconClicked (pane, row, (int) icon - (int) RowIcon::Export);
            return;
        }
        // Keep a multi-selection intact when starting a drag from it.
        if (! (! r.isBank && owner.isSelected ({ r.bank, r.index }) && ! e.mods.isAnyModifierKeyDown()))
            owner.rowClicked (pane, row, e);
        pendingClickRow = row;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragStarted && pendingClickRow >= 0 && ! e.mods.isAnyModifierKeyDown() && ! e.mods.isPopupMenu())
        {
            owner.rowClicked (pane, pendingClickRow, e); // plain click on an already-selected row: select just it
            // A plain click on a program loads it into the edit buffer (Shift/Cmd-click only select).
            const auto& rowList = owner.rows (pane);
            if (juce::isPositiveAndBelow (pendingClickRow, (int) rowList.size()) && ! rowList[(size_t) pendingClickRow].isBank
                && e.getMouseDownX() >= 22)
                owner.rowDoubleClicked (pane, pendingClickRow);
        }
        pendingClickRow = -1;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragStarted && e.getDistanceFromDragStart() > 5 && pendingClickRow >= 0 && ! owner.isSaving())
        {
            dragStarted = true;
            if (owner.rows (pane)[(size_t) pendingClickRow].isBank)
                owner.beginBankDrag (pane, pendingClickRow);
            else
                owner.beginRowDrag (pane, pendingClickRow);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.y), e.x); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1, 0); }

    void setHover (int row, int x)
    {
        if (row == hoverRow && iconAt (x) == iconAt (hoverX))
            return;
        if (hoverRow >= 0)
            repaint (0, hoverRow * rowHeight, iconsRight, rowHeight);
        hoverRow = row;
        hoverX = x;
        if (hoverRow >= 0)
            repaint (0, hoverRow * rowHeight, iconsRight, rowHeight);
        const bool onIcon = row >= 0 && juce::isPositiveAndBelow (row, (int) owner.rows (pane).size())
                            && ! owner.rows (pane)[(size_t) row].isBank && iconAt (x) != RowIcon::None;
        setMouseCursor (onIcon ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Programs already loaded on the single click; a double-click on a bank renames it.
        const int row = rowAt (e.y);
        if (juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()) && e.x >= 22 && owner.rows (pane)[(size_t) row].isBank)
            owner.rowDoubleClicked (pane, row);
    }

    bool keyPressed (const juce::KeyPress& k) override { return owner.keyPressed (k); }

    juce::String getTooltip() override
    {
        const int row = rowAt (getMouseXYRelative().y);
        if (juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()) && ! owner.rows (pane)[(size_t) row].isBank)
        {
            const auto& er = owner.rows (pane)[(size_t) row];
            if (owner.isSaving())
                return owner.getModel().isFactoryBank (er.bank) ? "Factory programs can't be changed" : "Click to save here";
            if (auto* ep = owner.getModel().program ({ er.bank, er.index }); er.isNew || (ep != nullptr && ep->isEmpty()))
                return "Empty slot: drag or paste a program here";
        }
        if (juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()) && ! owner.rows (pane)[(size_t) row].isBank)
            switch (iconAt (getMouseXYRelative().x))
            {
                case RowIcon::Star:   return "Favourite";
                case RowIcon::Export: return "Export as .syx...";
                case RowIcon::Rename: return owner.getModel().isFactoryBank (owner.rows (pane)[(size_t) row].bank) ? juce::String() : "Rename...";
                case RowIcon::Delete: return owner.getModel().isFactoryBank (owner.rows (pane)[(size_t) row].bank) ? juce::String() : "Delete";
                case RowIcon::None:   break;
            }
        if (! juce::isPositiveAndBelow (row, (int) owner.rows (pane).size()))
            return {};
        const auto& r = owner.rows (pane)[(size_t) row];
        if (auto* p = owner.getModel().program ({ r.bank, r.index }); p != nullptr && ! r.isBank)
            return p->name + (p->source.isNotEmpty() ? "\nFrom: " + p->source : juce::String())
                 + (p->tags.size() > 0 ? "\nTags: " + p->tags.joinIntoString (", ") : juce::String())
                 + "\nClick to load into the TEO-5 edit buffer";
        return {};
    }

    // Drag and drop
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        // Banks move only within their own pane.
        return d.description.toString() == dragDescription
            || (d.description.toString() == bankDragDescription && d.sourceComponent.get() == this);
    }
    void itemDragEnter (const SourceDetails& d) override { bankDrag = d.description.toString() == bankDragDescription; }
    void itemDragMove (const SourceDetails& d) override
    {
        const int row = juce::jlimit (0, juce::jmax (0, (int) owner.rows (pane).size() - 1), rowAt (d.localPosition.y));
        if (owner.rows (pane).empty() && dropRow != 0)
        {
            dropRow = 0;
            repaint();
            return;
        }
        const bool below = (d.localPosition.y % rowHeight) > rowHeight / 2;
        if (row != dropRow || below != dropBelow)
        {
            dropRow = row;
            dropBelow = below;
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped (const SourceDetails&) override
    {
        const int row = dropRow;
        const bool below = dropBelow;
        dropRow = -1;
        repaint();
        if (row >= 0 && bankDrag)
            owner.dropBankAt (pane, row, below);
        else if (row >= 0)
            owner.dropProgramsAt (pane, row, below);
        bankDrag = false;
    }

    // y of the line a dropped bank lands on: above the target bank's header, or below its last visible row.
    int bankDropY() const
    {
        const auto& rows = owner.rows (pane);
        const auto& target = rows[(size_t) dropRow];
        int row = dropRow;
        while (row > 0 && ! rows[(size_t) row].isBank)
            --row; // the target bank's header
        if (target.isBank && ! dropBelow)
            return row * rowHeight;
        int last = row;
        while (last + 1 < (int) rows.size() && ! rows[(size_t) last + 1].isBank)
            ++last;
        return (last + 1) * rowHeight;
    }

private:
    LibraryDrawer& owner;
    const int pane;
    int dropRow = -1;
    bool dropBelow = false, bankDrag = false;
    int hoverRow = -1, hoverX = 0;
    int pendingClickRow = -1;
    bool dragStarted = false;
};

// ---- Save panel (WRITE) ---------------------------------------------------------------------------
// Sits over the front panel, next to the library, while a slot is chosen: what's being saved, where to, what can be
// heard now, and Confirm / Cancel.
class LibraryDrawer::SavePanel : public juce::Component
{
public:
    explicit SavePanel (LibraryDrawer& o) : owner (o)
    {
        for (auto* b : { &cancel, &confirm })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f3237));
            b->setColour (juce::TextButton::textColourOffId, Palette::silkscreen);
            addAndMakeVisible (b);
        }
        confirm.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3020));
        cancel.onClick = [this] { owner.cancelSave(); };
        confirm.onClick = [this] { owner.confirmSave(); };
        setSize (440, 250);
    }

    void update()
    {
        confirm.setEnabled (owner.saveTarget.isValid());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff26282d));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (juce::Colour (0xff555a62));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

        auto r = getLocalBounds().reduced (18, 14);
        r.removeFromBottom (40); // buttons
        g.setColour (Palette::silkscreen);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("Save program", r.removeFromTop (24), juce::Justification::centredLeft);
        r.removeFromTop (6);

        auto line = [&] (const juce::String& label, const juce::String& text, juce::Colour c)
        {
            auto row = r.removeFromTop (20);
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (Palette::silkscreen.withAlpha (0.6f));
            g.drawText (label, row.removeFromLeft (70), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.5f));
            g.setColour (c);
            g.drawFittedText (text, row, juce::Justification::centredLeft, 1, 0.85f);
        };
        line ("Saving", "\"" + owner.saveName + "\"" + (owner.saveFrom.isNotEmpty() ? ", from " + owner.saveFrom : juce::String()),
              Palette::silkscreen);

        const auto* target = owner.model.program (owner.saveTarget);
        const bool chosen = owner.saveTarget.isValid();
        const bool toSynth = chosen && owner.isHardwareBank (owner.saveTarget.bank);
        const bool replacing = target != nullptr && ! target->isEmpty();
        line ("Save to", ! chosen ? juce::String ("Click a slot in the library")
                         : owner.locationOf (owner.saveTarget) + (replacing ? ", replacing \"" + target->name + "\"" : juce::String (" (empty)")),
              chosen ? saveAmber : Palette::silkscreen.withAlpha (0.5f));
        r.removeFromTop (8);

        // What's in the edit buffer now.
        auto box = r.removeFromTop (58);
        g.setColour (juce::Colour (0xff1d1f23));
        g.fillRoundedRectangle (box.toFloat(), 5.0f);
        g.setColour (juce::Colour (0xff3a3d43));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 5.0f, 1.0f);
        box.reduce (10, 6);
        {
            // Speaker: a cone and two sound waves (24-unit icon drawn at 20 px).
            const auto icon = box.removeFromLeft (28).toFloat().withSizeKeepingCentre (20.0f, 20.0f);
            const auto t = juce::AffineTransform::scale (20.0f / 24.0f).translated (icon.getX(), icon.getY());
            juce::Path cone, waves;
            cone.startNewSubPath (4, 9); cone.lineTo (8, 9); cone.lineTo (13, 5); cone.lineTo (13, 19);
            cone.lineTo (8, 15); cone.lineTo (4, 15); cone.closeSubPath();
            waves.addCentredArc (12.0f, 12.0f, 5.0f, 5.0f, 0.0f, 0.785f, 2.356f, true);
            waves.addCentredArc (12.0f, 12.0f, 8.5f, 8.5f, 0.0f, 0.785f, 2.356f, true);
            g.setColour (replacing ? saveAmber : Palette::silkscreen.withAlpha (0.35f));
            g.fillPath (cone, t);
            g.strokePath (waves, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
        }
        g.setColour (Palette::silkscreen.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (replacing ? "Now in the TEO-5's edit buffer: \"" + target->name + "\". Play your MIDI controller or the "
                                      "TEO-5's keys to hear the sound you'd replace."
                          : chosen ? juce::String ("This slot is empty, so there's nothing to audition. Your edit is in the edit buffer.")
                                   : juce::String ("Select a slot and its program goes into the TEO-5's edit buffer, so you can play it "
                                                   "from your MIDI controller or the synth's keys before replacing it."),
                          box, juce::Justification::centredLeft, 3, 1.0f);
        r.removeFromTop (8);

        if (chosen)
        {
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (toSynth ? juce::Colour (0xfff0c27a) : Palette::silkscreen.withAlpha (0.6f));
            g.drawFittedText (toSynth ? juce::String::fromUTF8 ("\xe2\x9a\xa0 Confirm writes \"") + owner.saveName + "\" into the TEO-5's memory at "
                                            + owner.locationOf (owner.saveTarget) + "."
                                      : "Saved in the library only (" + owner.model.banks()[(size_t) owner.saveTarget.bank].name
                                            + "). Nothing is written to the TEO-5.",
                              r.removeFromTop (34), juce::Justification::topLeft, 2, 1.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14).removeFromBottom (30);
        confirm.setBounds (r.removeFromRight (100));
        r.removeFromRight (10);
        cancel.setBounds (r.removeFromRight (100));
    }

private:
    LibraryDrawer& owner;
    juce::TextButton cancel { "Cancel" }, confirm { "Confirm" };
};

// ---- Drawer -------------------------------------------------------------------------------------

LibraryDrawer::LibraryDrawer (LibraryModel& m, HardwareTransfer& t, MidiSync& s, ProgramState& ps)
    : model (m), transfer (t), sync (s), programState (ps)
{
    setWantsKeyboardFocus (true);
    const auto text = Palette::silkscreen;

    title.setFont (TEO5LookAndFeel::panelFont (14.0f));
    title.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (title);

    for (auto* b : { &fetchButton, &importButton, &saveAllButton, &newBankButton, &undoButton, &redoButton, &cancelButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232427));
        b->setColour (juce::TextButton::textColourOffId, text);
        addAndMakeVisible (b);
    }
    fetchButton.setTooltip ("Fetch programs from the TEO-5");
    importButton.setTooltip ("Import a .syx file (single programs or banks)");
    saveAllButton.setTooltip ("Write every NOT SENT program in your TEO-5 User banks to the synth (shows what it overwrites first)");
    newBankButton.setTooltip ("Make a new bank in MY BANKS");
    helpButton.setTooltip ("How the library works");
    helpButton.onClick = [this] { showHelp (! helpPanel.isVisible()); };
    addAndMakeVisible (helpButton);
    helpPanel.close.onClick = [this] { showHelp (false); };
    addChildComponent (helpPanel);
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");

    fetchButton.onClick = [this] { showFetchMenu(); };
    importButton.onClick = [this] { importSyx (currentBank()); };
    saveAllButton.onClick = [this] { saveAllChanges(); };
    newBankButton.onClick = [this]
    {
        askText ("New bank", "Bank name:", "New Bank", [this] (const juce::String& n) { model.addBank (n); });
    };
    undoButton.onClick = [this] { undoOrRedo (true); };
    pullAllButton.onClick = [this] { fetchAllUserBanks(); };
    pushAllButton.onClick = [this] { sendAllUserBanks(); };
    pullAllButton.setTooltip ("Fetch User banks 1-16 from the TEO-5, replacing the library's copies");
    pushAllButton.setTooltip ("Write the library's TEO-5 User banks to the synth");
    for (auto* b : { &pullAllButton, &pushAllButton })
        addAndMakeVisible (b);
    redoButton.onClick = [this] { undoOrRedo (false); };
    cancelButton.onClick = [this] { transfer.cancel(); };

    search.setTextToShowWhenEmpty ("Search names, tags (tag:pad)", text.withAlpha (0.4f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141517));
    search.setColour (juce::TextEditor::textColourId, text);
    search.setColour (juce::TextEditor::outlineColourId, Palette::pinstripe.withAlpha (0.6f));
    search.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible (search);

    favouritesOnly.setColour (juce::ToggleButton::textColourId, text);
    favouritesOnly.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffffc040));
    favouritesOnly.onClick = [this] { rebuildRows(); };
    addAndMakeVisible (favouritesOnly);

    const char* paneNames[] = { "TEO-5", "MY BANKS" };
    const char* paneDescriptions[] = { "User and Factory banks fetched from the synth",
                                       "Imported .syx files and your own banks" };
    for (int pane = 0; pane < numPanes; ++pane)
    {
        lists[(size_t) pane] = std::make_unique<ListView> (*this, pane);
        auto& vp = viewports[(size_t) pane];
        vp.setViewedComponent (lists[(size_t) pane].get(), false);
        vp.setScrollBarsShown (true, false);
        vp.setScrollBarThickness (8);
        addAndMakeVisible (vp);

        auto& t = paneTitles[(size_t) pane];
        t.setText (paneNames[pane], juce::dontSendNotification);
        t.setFont (TEO5LookAndFeel::panelFont (13.0f));
        t.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (t);
        auto& h = paneHints[(size_t) pane];
        h.setText (paneDescriptions[pane], juce::dontSendNotification);
        h.setFont (juce::FontOptions (13.0f));
        h.setColour (juce::Label::textColourId, text.withAlpha (0.7f));
        h.setJustificationType (juce::Justification::centredRight);
        h.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (h);
    }

    status.setColour (juce::Label::textColourId, text.withAlpha (0.75f));
    status.setFont (juce::FontOptions (13.0f));
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, Palette::pinstripe);
    addChildComponent (progressBar);
    cancelButton.setVisible (false);

    transfer.onFinished = [this]
    {
        setStatus (transfer.getStatus());
        if (transfer.lastSendCompleted())
            for (int b = 0; b < (int) model.banks().size(); ++b)
                if (sendingBankUuids.contains (model.banks()[(size_t) b].uuid))
                    model.clearUnsent (b);
        sendingBankUuids.clear();
    };

    savePanel = std::make_unique<SavePanel> (*this);
    model.addChangeListener (this);
    transfer.detectNamesFromLibrary(); // names for banks fetched before the name field was known
    rebuildRows();
    startTimerHz (10);
}

LibraryDrawer::~LibraryDrawer()
{
    transfer.onFinished = nullptr;
    model.removeChangeListener (this);
}

void LibraryDrawer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181a1c));
    g.setColour (Palette::pinstripe);
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);

    // Divider between the two panes, with a hint that programs travel between them.
    const int x = (viewports[0].getRight() + viewports[1].getX()) / 2;
    g.setColour (Palette::pinstripe.withAlpha (0.5f));
    g.fillRect (x, paneTitles[0].getY(), 1, viewports[0].getBottom() - paneTitles[0].getY());
    g.setColour (Palette::silkscreen.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x87\x84"), juce::Rectangle<int> (x - 9, viewports[0].getY() - 22, 18, 18),
                juce::Justification::centred, false);
}

void LibraryDrawer::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromRight (2);
    {
        auto top = r.removeFromTop (24);
        helpButton.setBounds (top.removeFromLeft (22).withSizeKeepingCentre (20, 20));
        top.removeFromLeft (6);
        title.setBounds (top);
    }
    r.removeFromTop (8);
    helpPanel.setBounds (getLocalBounds().reduced (24, 40).withTrimmedRight (2));

    auto buttons = r.removeFromTop (24);
    const int bw = (buttons.getWidth() - 5 * 4) / 6;
    for (auto* b : { &fetchButton, &importButton, &saveAllButton, &newBankButton, &undoButton, &redoButton })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (4);
    }
    r.removeFromTop (4);
    {
        auto row = r.removeFromTop (24);
        pullAllButton.setBounds (row.removeFromLeft ((row.getWidth() - 4) / 2));
        row.removeFromLeft (4);
        pushAllButton.setBounds (row);
    }
    r.removeFromTop (6);
    auto searchRow = r.removeFromTop (24);
    favouritesOnly.setBounds (searchRow.removeFromRight (100));
    searchRow.removeFromRight (6);
    search.setBounds (searchRow);
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (transfer.isBusy() ? 46 : 24);
    if (transfer.isBusy())
    {
        auto pr = bottom.removeFromTop (18);
        cancelButton.setBounds (pr.removeFromRight (64));
        pr.removeFromRight (6);
        progressBar.setBounds (pr);
        bottom.removeFromTop (4);
    }
    status.setBounds (bottom);
    r.removeFromBottom (4);

    const int gap = 16;
    const int paneW = (r.getWidth() - gap) / numPanes;
    for (int pane = 0; pane < numPanes; ++pane)
    {
        auto col = pane == 0 ? r.removeFromLeft (paneW) : r.removeFromRight (paneW);
        auto head = col.removeFromTop (24);
        paneTitles[(size_t) pane].setBounds (head.removeFromLeft (110));
        paneHints[(size_t) pane].setBounds (head);
        col.removeFromTop (4);
        viewports[(size_t) pane].setBounds (col);
        lists[(size_t) pane]->updateSize (viewports[(size_t) pane].getMaximumVisibleWidth());
    }
}

void LibraryDrawer::repaintLists()
{
    for (auto& l : lists)
        if (l != nullptr)
            l->repaint();
}

bool LibraryDrawer::isHardwareBank (int bank) const
{
    return juce::isPositiveAndBelow (bank, (int) model.banks().size())
        && model.banks()[(size_t) bank].kind == LibraryBank::Kind::Hardware;
}

// ---- Rows / selection -----------------------------------------------------------------------

void LibraryDrawer::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Positions may have changed: keep only selections that are still valid.
    std::set<std::pair<int, int>> valid;
    for (auto& sel : selection)
        if (model.program ({ sel.first, sel.second }) != nullptr)
            valid.insert (sel);
    selection = valid;
    if (model.program (loaded) == nullptr)
        loaded = {};
    if (saving && saveTarget.isValid())
    {
        // Banks may have moved or gone (a transfer finishing, say): follow the target's bank by its uuid.
        saveTarget.bank = -1;
        for (int b = 0; b < (int) model.banks().size(); ++b)
            if (model.banks()[(size_t) b].uuid == saveTargetBank)
                saveTarget.bank = b;
        if (! saveTargetValid())
        {
            saveTarget = {};
            if (std::exchange (auditioned, false))
                sync.restoreEditBuffer();
            setStatus ("The chosen slot is gone: click a slot to save into");
        }
        savePanel->update();
    }
    rebuildRows();
}

bool LibraryDrawer::isCollapsed (int bank) const
{
    return juce::isPositiveAndBelow (bank, (int) model.banks().size()) && expanded.count (model.banks()[(size_t) bank].uuid) == 0;
}

void LibraryDrawer::toggleCollapsed (int bank)
{
    if (! juce::isPositiveAndBelow (bank, (int) model.banks().size()))
        return;
    const auto& id = model.banks()[(size_t) bank].uuid;
    if (! expanded.erase (id))
        expanded.insert (id);
    rebuildRows();
}

void LibraryDrawer::rebuildRows()
{
    for (auto& rl : rowLists)
        rl.clear();
    const auto query = search.getText();
    const bool favs = favouritesOnly.getToggleState();
    const bool filtering = query.isNotEmpty() || favs;

    for (int b = 0; b < (int) model.banks().size(); ++b)
    {
        const auto& bank = model.banks()[(size_t) b];
        std::vector<Row> programs;
        if (filtering || ! isCollapsed (b))
            for (int i = 0; i < (int) bank.programs.size(); ++i)
                if (LibraryModel::matches (bank.programs[(size_t) i], query, favs))
                    programs.push_back ({ false, b, i });
        if (filtering && programs.empty())
            continue;
        auto& rowList = rowLists[(size_t) paneOf (bank)];
        rowList.push_back ({ true, b, -1 });
        rowList.insert (rowList.end(), programs.begin(), programs.end());
        if (bank.kind == LibraryBank::Kind::Disk && ! filtering && ! isCollapsed (b))
            rowList.push_back ({ false, b, (int) bank.programs.size(), true }); // always room for one more
    }

    updateToolbar();
    for (int pane = 0; pane < numPanes; ++pane)
        if (lists[(size_t) pane] != nullptr)
        {
            lists[(size_t) pane]->updateSize (viewports[(size_t) pane].getMaximumVisibleWidth());
            lists[(size_t) pane]->repaint();
        }
}

juce::Array<ProgramRef> LibraryDrawer::selectedRefs() const
{
    juce::Array<ProgramRef> refs;
    for (auto& rowList : rowLists) // in display order
        for (auto& r : rowList)
            if (! r.isBank && selection.count ({ r.bank, r.index }))
                refs.add ({ r.bank, r.index });
    return refs;
}

ProgramRef LibraryDrawer::primarySelection() const
{
    auto refs = selectedRefs();
    return refs.isEmpty() ? ProgramRef() : refs.getFirst();
}

int LibraryDrawer::currentBank() const
{
    if (auto p = primarySelection(); p.isValid())
        return p.bank;
    if (juce::isPositiveAndBelow (anchorRow, (int) rowLists[(size_t) anchorPane].size()))
        return rowLists[(size_t) anchorPane][(size_t) anchorRow].bank;
    return model.banks().empty() ? -1 : 0;
}

void LibraryDrawer::rowClicked (int pane, int row, const juce::MouseEvent& e)
{
    const auto& rowList = rowLists[(size_t) pane];
    if (! juce::isPositiveAndBelow (row, (int) rowList.size()))
        return;
    if (pane != anchorPane)
    {
        anchorPane = pane;
        anchorRow = -1;
    }
    const auto& r = rowList[(size_t) row];

    if (saving)
    {
        if (! r.isBank)
            setSaveTarget ({ r.bank, r.index });
        return;
    }
    if (r.isBank || r.isNew)
    {
        selection.clear();
        anchorRow = row;
    }
    else if (e.mods.isShiftDown() && anchorRow >= 0)
    {
        selection.clear();
        for (int i = juce::jmin (anchorRow, row); i <= juce::jmax (anchorRow, row); ++i)
            if (! rowList[(size_t) i].isBank)
                selection.insert ({ rowList[(size_t) i].bank, rowList[(size_t) i].index });
    }
    else if (e.mods.isCommandDown())
    {
        const std::pair<int, int> key { r.bank, r.index };
        if (! selection.erase (key))
            selection.insert (key);
        anchorRow = row;
    }
    else
    {
        selection = { { r.bank, r.index } };
        anchorRow = row;
    }
    repaintLists();
}

void LibraryDrawer::rowDoubleClicked (int pane, int row)
{
    const auto& rowList = rowLists[(size_t) pane];
    if (! juce::isPositiveAndBelow (row, (int) rowList.size()))
        return;
    const auto& r = rowList[(size_t) row];
    if (saving)
        return;
    if (r.isBank)
        renameBank (r.bank);
    else
        loadToEditBuffer ({ r.bank, r.index });
}

void LibraryDrawer::toggleFavourite (int pane, int row)
{
    const auto& r = rowLists[(size_t) pane][(size_t) row];
    if (auto* p = model.program ({ r.bank, r.index }))
        model.setFavorite ({ r.bank, r.index }, ! p->favorite);
}

void LibraryDrawer::beginRowDrag (int pane, int row)
{
    const auto& r = rowLists[(size_t) pane][(size_t) row];
    if (r.isBank || selection.empty())
        return;
    const int count = (int) selection.size();
    juce::Image img (juce::Image::ARGB, 180, rowHeight, true);
    {
        juce::Graphics g (img);
        g.setColour (Palette::pinstripe.withAlpha (0.85f));
        g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
        g.setColour (Palette::silkscreen);
        g.setFont (TEO5LookAndFeel::oledFont (14.0f));
        auto* p = model.program (primarySelection());
        g.drawText (count > 1 ? juce::String (count) + " programs" : (p != nullptr ? p->name : juce::String()),
                    img.getBounds().reduced (8, 0), juce::Justification::centredLeft);
    }
    startDragging (dragDescription, lists[(size_t) pane].get(), juce::ScaledImage (img), true);
}

void LibraryDrawer::beginBankDrag (int pane, int row)
{
    const auto& r = rowLists[(size_t) pane][(size_t) row];
    draggedBank = r.bank;
    juce::Image img (juce::Image::ARGB, 200, rowHeight, true);
    {
        juce::Graphics g (img);
        g.setColour (juce::Colour (0xff101113).withAlpha (0.9f));
        g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
        g.setColour (Palette::pinstripe);
        g.drawRoundedRectangle (img.getBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (Palette::silkscreen);
        g.setFont (TEO5LookAndFeel::panelFont (11.5f));
        g.drawText (model.banks()[(size_t) r.bank].name, img.getBounds().reduced (8, 0), juce::Justification::centredLeft, true);
    }
    startDragging (bankDragDescription, lists[(size_t) pane].get(), juce::ScaledImage (img), true);
}

void LibraryDrawer::dropBankAt (int pane, int row, bool below)
{
    const auto& rowList = rowLists[(size_t) pane];
    if (! juce::isPositiveAndBelow (row, (int) rowList.size()) || ! juce::isPositiveAndBelow (draggedBank, (int) model.banks().size()))
        return;
    const auto& r = rowList[(size_t) row];
    const int before = (r.isBank && ! below) ? r.bank : r.bank + 1;
    model.moveBank (draggedBank, before);
    draggedBank = -1;
    selection.clear();
}

void LibraryDrawer::rowIconClicked (int pane, int row, int icon)
{
    const auto& r = rowLists[(size_t) pane][(size_t) row];
    const ProgramRef ref { r.bank, r.index };
    auto* p = model.program (ref);
    if (p == nullptr)
        return;
    switch (icon)
    {
        case 0: exportPrograms ({ ref }); break;
        case 1: if (! model.isFactoryBank (ref.bank)) renameProgram (ref); break;
        case 2:
            if (! model.isFactoryBank (ref.bank))
                confirmDelete ({ ref });
            break;
        default: break;
    }
}

int LibraryDrawer::dropBankFor (int pane, int row) const
{
    const auto& rowList = rowLists[(size_t) pane];
    return juce::isPositiveAndBelow (row, (int) rowList.size()) ? rowList[(size_t) row].bank : -1;
}

void LibraryDrawer::dropProgramsAt (int pane, int row, bool below)
{
    auto refs = selectedRefs();
    if (refs.isEmpty())
        return;
    const auto& rowList = rowLists[(size_t) pane];

    if (rowList.empty())
    {
        // Empty MY BANKS pane: start a bank with the dropped programs.
        if (pane == 1)
            model.insertCopies (refs, model.addBank ("My Bank"), -1);
        selection.clear();
        return;
    }
    if (! juce::isPositiveAndBelow (row, (int) rowList.size()))
        return;

    const auto& r = rowList[(size_t) row];
    const int bank = r.bank;
    const bool copying = juce::ModifierKeys::currentModifiers.isAltDown();

    // One program dropped onto another slot of the same kind of bank: the two swap places. That's within the
    // TEO-5's User banks, or within one of your banks (between two of your banks it moves; see below).
    if (refs.size() == 1 && ! r.isBank && ! r.isNew && ! copying && ! model.isFactoryBank (refs[0].bank) && ! model.isFactoryBank (bank)
        && ((isHardwareBank (bank) && isHardwareBank (refs[0].bank)) || (! isHardwareBank (bank) && refs[0].bank == bank)))
    {
        model.swapPrograms (refs[0], { r.bank, r.index });
        setStatus (model.getUndoManager().getUndoDescription());
        selection.clear();
        return;
    }

    if (isHardwareBank (bank))
    {
        const int first = r.isBank ? 0 : r.index;
        // Within the TEO-5's User banks a drag moves (leaving an empty slot); from elsewhere it copies.
        bool fromUserBanks = ! copying;
        for (auto& ref : refs)
            fromUserBanks = fromUserBanks && isHardwareBank (ref.bank) && ! model.isFactoryBank (ref.bank);
        const int placed = fromUserBanks ? model.moveIntoSlots (refs, bank, first) : model.overwritePrograms (refs, bank, first);
        if (placed < refs.size())
            setStatus (juce::String (refs.size() - placed) + " program(s) didn't fit in the bank");
        else
            setStatus ("Replaced " + juce::String (placed) + " program(s). Send the bank to put them on the TEO-5");
    }
    else
    {
        auto* target = r.isBank ? nullptr : model.program ({ r.bank, r.index });
        const int before = r.isBank ? 0 : r.index + (below && ! (target != nullptr && target->isEmpty()) ? 1 : 0);
        bool allMine = true;
        for (auto& ref : refs)
            allMine = allMine && ! isHardwareBank (ref.bank);
        // Programs from the TEO-5 side are copied; within your own banks they move (Option-drag copies).
        if (allMine && ! copying)
            model.movePrograms (refs, bank, before);
        else
            model.insertCopies (refs, bank, before);
    }
    selection.clear();
}

void LibraryDrawer::selectPrograms (const juce::Array<ProgramRef>& refs)
{
    selection.clear();
    for (auto& r : refs)
        if (model.program (r) != nullptr)
            selection.insert ({ r.bank, r.index });
    repaintLists();
}

void LibraryDrawer::stepSelection (int delta)
{
    const auto& rowList = rowLists[(size_t) anchorPane];
    auto& viewport = viewports[(size_t) anchorPane];
    std::vector<int> programRows;
    for (int i = 0; i < (int) rowList.size(); ++i)
        if (! rowList[(size_t) i].isBank && ! rowList[(size_t) i].isNew)
            programRows.push_back (i);
    if (programRows.empty())
        return;

    int pos = 0;
    if (auto cur = primarySelection(); cur.isValid())
        for (int i = 0; i < (int) programRows.size(); ++i)
            if (rowList[(size_t) programRows[(size_t) i]].bank == cur.bank && rowList[(size_t) programRows[(size_t) i]].index == cur.index)
                pos = juce::jlimit (0, (int) programRows.size() - 1, i + delta);

    const int row = programRows[(size_t) pos];
    selection = { { rowList[(size_t) row].bank, rowList[(size_t) row].index } };
    anchorRow = row;
    viewport.setViewPosition (0, juce::jmax (0, row * rowHeight - viewport.getHeight() / 2));
    repaintLists();

    if (auto* p = model.program (primarySelection()); p != nullptr && onShowMessage)
        onShowMessage (p->name);
    pendingLoadTicks = 5; // load when the encoder stops for ~0.5 s
}

void LibraryDrawer::stepBank (int delta)
{
    const int b = juce::jlimit (0, juce::jmax (0, (int) model.banks().size() - 1), currentBank() + delta);
    for (int pane = 0; pane < numPanes; ++pane)
    for (int i = 0; i < (int) rowLists[(size_t) pane].size(); ++i)
        if (rowLists[(size_t) pane][(size_t) i].isBank && rowLists[(size_t) pane][(size_t) i].bank == b)
        {
            selection.clear();
            anchorPane = pane;
            anchorRow = i;
            viewports[(size_t) pane].setViewPosition (0, i * rowHeight);
            repaintLists();
            if (onShowMessage)
                onShowMessage (model.banks()[(size_t) b].name);
            return;
        }
}

void LibraryDrawer::timerCallback()
{
    const bool busy = transfer.isBusy();
    if (busy != progressBar.isVisible())
    {
        progressBar.setVisible (busy);
        cancelButton.setVisible (busy);
        updateToolbar();
        resized();
    }
    if (busy)
    {
        progress = transfer.getProgress();
        status.setText (transfer.getStatus(), juce::dontSendNotification);
    }
    if (pendingLoadTicks > 0 && --pendingLoadTicks == 0)
    {
        pendingLoadTicks = -1;
        if (auto ref = primarySelection(); ref.isValid())
            loadToEditBuffer (ref);
    }
}

void LibraryDrawer::setStatus (const juce::String& s)
{
    status.setText (s, juce::dontSendNotification);
}

// ---- Keyboard -------------------------------------------------------------------------------

bool LibraryDrawer::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (saving)
    {
        if (k == juce::KeyPress::escapeKey)
            cancelSave();
        else if (k == juce::KeyPress::returnKey && saveTarget.isValid())
            confirmSave();
        return true; // nothing else while choosing where to save
    }
    if (helpPanel.isVisible() && k == juce::KeyPress::escapeKey)
    {
        showHelp (false);
        return true;
    }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { undoOrRedo (true); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { undoOrRedo (false); return true; }
    if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { model.copyPrograms (selectedRefs()); setStatus ("Copied"); return true; }
    if (k == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        auto p = primarySelection();
        model.pastePrograms (currentBank(), p.isValid() ? p.index + 1 : -1);
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        for (auto& r : rowLists[(size_t) anchorPane])
            if (! r.isBank && ! r.isNew)
                selection.insert ({ r.bank, r.index });
        repaintLists();
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        auto refs = selectedRefs();
        const int before = refs.size();
        refs.removeIf ([this] (const ProgramRef& r) { return model.isFactoryBank (r.bank); });
        if (refs.size() < before)
            setStatus ("Factory programs can't be deleted");
        if (! refs.isEmpty())
            confirmDelete (refs);
        return true;
    }
    if (k == juce::KeyPress::returnKey)
    {
        if (auto r = primarySelection(); r.isValid())
            loadToEditBuffer (r);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
    {
        stepSelection (k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
        pendingLoadTicks = -1; // arrows only select; Return loads
        return true;
    }
    juce::ignoreUnused (mods);
    return false;
}

// ---- Menus ------------------------------------------------------------------------------------

void LibraryDrawer::rowMenu (int pane, int row)
{
    const auto& rowList = rowLists[(size_t) pane];
    if (! juce::isPositiveAndBelow (row, (int) rowList.size()))
        return;
    const auto r = rowList[(size_t) row];
    juce::PopupMenu m;

    if (r.isBank)
    {
        const int b = r.bank;
        const bool hw = model.banks()[(size_t) b].kind == LibraryBank::Kind::Hardware;
        const bool factoryBank = model.isFactoryBank (b); // read-only: the synth's factory programs can't change
        m.addItem ("Rename bank...", [this, b] { renameBank (b); });
        if (! factoryBank)
        {
            m.addItem ("Import .syx into this bank...", [this, b] { importSyx (b); });
            m.addItem ("Paste", model.hasClipboard(), false, [this, b] { model.pastePrograms (b, -1); });
        }
        m.addSeparator();
        m.addItem ("Export bank as .syx...", [this, b] { exportBank (b); });
        if (! factoryBank && ! (! hw && isFactoryPanel && isFactoryPanel()))
            m.addItem ("Send bank to TEO-5...", [this, b] { sendBankToHardware (b); });
        if (hw)
            m.addItem ("Fetch this bank again", [this, b]
            {
                const auto& bank = model.banks()[(size_t) b];
                transfer.fetchBanks ({ { bank.hardwareBank, bank.factory } });
            });
        m.addSeparator();
        m.addItem ("Delete bank", [this, b] { model.removeBank (b); });
    }
    else
    {
        const ProgramRef ref { r.bank, r.index };
        const auto refs = selectedRefs();
        const bool single = refs.size() <= 1;
        const auto* p = model.program (ref);
        const bool factoryBank = model.isFactoryBank (ref.bank);
        if (r.isNew || (p != nullptr && p->isEmpty()))
        {
            m.addItem ("Paste here", model.hasClipboard() && ! factoryBank, false, [this, ref] { model.pastePrograms (ref.bank, ref.index); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (lists[(size_t) pane].get())
                                 .withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }));
            return;
        }
        if (p == nullptr)
            return;
        m.addItem ("Load into edit buffer", [this, ref] { loadToEditBuffer (ref); });
        m.addItem ("Write to TEO-5 memory...", single, false, [this, ref] { writeToLocation (ref); });
        m.addSeparator();
        if (! factoryBank)
            m.addItem ("Rename...", single, false, [this, ref] { renameProgram (ref); });
        m.addItem ("Tags...", single, false, [this, ref] { editTags (ref); });
        m.addItem (p != nullptr && p->favorite ? "Remove from favourites" : "Add to favourites",
                   [this, ref, fav = p != nullptr && p->favorite] { model.setFavorite (ref, ! fav); });
        m.addSeparator();
        m.addItem ("Copy", [this, refs] { model.copyPrograms (refs); setStatus ("Copied " + juce::String (refs.size())); });
        if (! factoryBank)
        {
            m.addItem ("Paste after", model.hasClipboard(), false, [this, ref] { model.pastePrograms (ref.bank, ref.index + 1); });
            m.addItem ("Duplicate", [this, refs, ref]
            {
                model.copyPrograms (refs);
                model.pastePrograms (ref.bank, ref.index + 1);
            });
        }
        m.addItem ("Export as .syx...", [this, refs] { exportPrograms (refs); });
        if (! factoryBank)
        {
            m.addSeparator();
            m.addItem (single ? "Delete..." : "Delete " + juce::String (refs.size()) + " programs...", [this, refs] { confirmDelete (refs); });
        }
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (lists[(size_t) pane].get())
                         .withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }));
}

void LibraryDrawer::showFetchMenu()
{
    juce::PopupMenu m;
    const bool factory = isFactoryPanel && isFactoryPanel();
    const int bank = programState.bank;
    m.addItem ("Current edit buffer (into selected bank)", [this]
    {
        juce::Thread::launch ([this, alive = juce::Component::SafePointer<LibraryDrawer> (this)]
        {
            auto raw = sync.fetchEditBuffer();
            juce::MessageManager::callAsync ([alive, raw]
            {
                if (alive == nullptr) return;
                if (! raw) { alive->setStatus ("No reply from the TEO-5"); return; }
                auto name = alive->sync.hasNameLayout() ? alive->sync.nameOf (*raw) : juce::String ("Edit buffer");
                alive->model.insertProgram (alive->currentBank(), -1, LibraryModel::makeProgram (*raw, name, "TEO-5 edit buffer"));
            });
        });
    });
    m.addItem ("Bank " + juce::String (bank) + (factory ? " (factory)" : " (user)"), [this, bank, factory]
    {
        transfer.fetchBanks ({ { bank, factory } });
    });
    m.addItem ("All user banks (1-16)", [this]
    {
        juce::Array<HardwareTransfer::BankAddress> all;
        for (int b = 1; b <= 16; ++b)
            all.add ({ b, false });
        transfer.fetchBanks (all);
    });
    m.addItem ("All factory banks (1-16)", [this]
    {
        juce::Array<HardwareTransfer::BankAddress> all;
        for (int b = 1; b <= 16; ++b)
            all.add ({ b, true });
        transfer.fetchBanks (all);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fetchButton));
}

// ---- Actions ----------------------------------------------------------------------------------

void LibraryDrawer::loadToEditBuffer (ProgramRef ref)
{
    if (auto* p = model.program (ref); p != nullptr && p->raw != nullptr)
    {
        sync.loadProgram (*p->raw);
        programState.name = p->name;
        loaded = ref;
        repaintLists();
        setStatus ("Loaded \"" + p->name + "\" into the edit buffer");
    }
}

// Right-click > Write to TEO-5 memory: the same save as the panel's WRITE, for this program.
void LibraryDrawer::writeToLocation (ProgramRef ref)
{
    auto* p = model.program (ref);
    if (p == nullptr || p->raw == nullptr || saving)
        return;
    beginSave (*p->raw, p->name, locationOf (ref), ref);
}

void LibraryDrawer::sendBankToHardware (int b)
{
    if (! juce::isPositiveAndBelow (b, (int) model.banks().size()))
        return;
    const auto& bank = model.banks()[(size_t) b];
    // One entry per slot, so programs land in their own slots. Empty slots are skipped, except ones deleted from
    // this TEO-5 bank (NOT SENT): those are initialized with the Basic Program, as the deletion's counterpart.
    std::vector<juce::MemoryBlock> programs;
    int used = 0, initialized = 0;
    const auto basic = MidiSync::basicProgram();
    for (auto& p : bank.programs)
        if ((int) programs.size() < HardwareTransfer::programsPerBank)
        {
            if (p.raw != nullptr)
                programs.push_back (*p.raw);
            else if (p.unsent && ! basic.isEmpty())
            {
                programs.push_back (basic);
                ++initialized;
            }
            else
                programs.push_back ({});
            used += programs.back().isEmpty() ? 0 : 1;
        }
    if (used == 0)
        return;

    const HardwareTransfer::BankAddress target { bank.kind == LibraryBank::Kind::Hardware ? bank.hardwareBank : programState.bank,
                                                 bank.kind == LibraryBank::Kind::Hardware ? bank.factory : (isFactoryPanel && isFactoryPanel()) };
    if (target.factory)
    {
        setStatus ("Factory banks can't be overwritten. Send to a User bank instead");
        return;
    }
    const int n = (int) programs.size();
    confirm ("Send bank to TEO-5",
             "Overwrite " + juce::String (used) + " programs in " + HardwareTransfer::bankName (target) + " ("
                 + HardwareTransfer::locationName (target, 1) + " to " + HardwareTransfer::locationName (target, n) + ") with \""
                 + bank.name + "\"?\n\nThe programs currently stored there will be lost. Fetch the bank first if you want a backup."
                 + (initialized > 0 ? "\n\n" + juce::String (initialized) + " deleted slot" + (initialized == 1 ? " is" : "s are")
                                          + " initialized (Basic Program)." : juce::String()),
             "Overwrite " + juce::String (used) + " programs",
             [this, uuid = bank.uuid, programs = std::move (programs), target]() mutable
             {
                 sendingBankUuids = { uuid };
                 transfer.sendBank (std::move (programs), target);
             });
}

void LibraryDrawer::importSyx (int intoBank)
{
    chooser = std::make_unique<juce::FileChooser> ("Import TEO-5 SysEx", juce::File(), "*.syx;*.mid;*.bin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, intoBank] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults())
                                  paths.add (f.getFullPathName());
                              if (! paths.isEmpty())
                                  importFiles (paths, isHardwareBank (intoBank) ? -1 : intoBank);
                          });
}

bool LibraryDrawer::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (saving)
        return false;
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".syx"))
            return true;
    return false;
}

void LibraryDrawer::filesDropped (const juce::StringArray& files, int x, int y)
{
    // Imports go to MY BANKS: into the bank under the cursor there, else a new bank per multi-program file.
    auto& vp = viewports[1];
    int target = -1;
    if (vp.getBounds().contains (x, y))
        target = dropBankFor (1, (y - vp.getY() + vp.getViewPositionY()) / rowHeight);
    importFiles (files, target);
}

void LibraryDrawer::importFiles (const juce::StringArray& files, int target)
{
    int imported = 0;

    for (auto& path : files)
    {
        juce::File f (path);
        auto items = SyxIO::importFile (f);
        if (items.empty())
            continue;

        int bank = target;
        if (bank < 0 || items.size() > 1)
            bank = model.addBank (f.getFileNameWithoutExtension());

        std::vector<LibraryProgram> programs = model.banks()[(size_t) bank].programs;
        for (size_t i = 0; i < items.size(); ++i)
        {
            auto name = sync.hasNameLayout() ? sync.nameOf (items[i].raw) : juce::String();
            if (name.isEmpty())
                name = f.getFileNameWithoutExtension() + (items.size() > 1 ? " " + juce::String ((int) i + 1) : juce::String());
            programs.push_back (LibraryModel::makeProgram (items[i].raw, name, f.getFileName()));
        }
        model.setBankPrograms (bank, std::move (programs), "Import " + f.getFileName());
        imported += (int) items.size();
    }
    setStatus (imported > 0 ? "Imported " + juce::String (imported) + " program(s)" : "No TEO-5 programs found in the file(s)");
}

void LibraryDrawer::exportPrograms (juce::Array<ProgramRef> refs)
{
    if (refs.isEmpty())
        return;
    if (refs.size() == 1)
    {
        auto* p = model.program (refs.getFirst());
        if (p == nullptr || p->raw == nullptr)
            return;
        auto raw = p->raw;
        chooser = std::make_unique<juce::FileChooser> ("Export program", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (p->name) + ".syx"), "*.syx");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, raw] (const juce::FileChooser& fc)
                              {
                                  if (auto f = fc.getResult(); f != juce::File())
                                      setStatus (SyxIO::exportProgram (f.withFileExtension ("syx"), *raw) ? "Exported " + f.getFileName() : "Export failed");
                              });
        return;
    }

    // Several programs: one folder, one file each.
    std::vector<std::pair<juce::String, std::shared_ptr<const juce::MemoryBlock>>> items;
    for (auto& r : refs)
        if (auto* p = model.program (r); p != nullptr && p->raw != nullptr)
            items.push_back ({ p->name, p->raw });
    chooser = std::make_unique<juce::FileChooser> ("Export programs to folder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, items] (const juce::FileChooser& fc)
                          {
                              auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              for (auto& [name, raw] : items)
                                  SyxIO::exportProgram (dir.getNonexistentChildFile (juce::File::createLegalFileName (name), ".syx"), *raw);
                              setStatus ("Exported " + juce::String ((int) items.size()) + " programs");
                          });
}

void LibraryDrawer::exportBank (int b)
{
    if (! juce::isPositiveAndBelow (b, (int) model.banks().size()))
        return;
    const auto& bank = model.banks()[(size_t) b];
    std::vector<juce::MemoryBlock> programs;
    for (auto& p : bank.programs)
        if (p.raw != nullptr)
            programs.push_back (*p.raw);
    const int panelBank = bank.kind == LibraryBank::Kind::Hardware ? bank.hardwareBank : programState.bank;
    const bool factory = bank.kind == LibraryBank::Kind::Hardware && bank.factory;

    chooser = std::make_unique<juce::FileChooser> ("Export bank", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile (juce::File::createLegalFileName (bank.name) + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, programs, panelBank, factory] (const juce::FileChooser& fc)
                          {
                              if (auto f = fc.getResult(); f != juce::File())
                                  setStatus (SyxIO::exportBank (f.withFileExtension ("syx"), programs, panelBank, factory)
                                                 ? "Exported " + juce::String ((int) programs.size()) + " programs to " + f.getFileName()
                                                 : "Export failed");
                          });
}

void LibraryDrawer::renameProgram (ProgramRef ref)
{
    if (auto* p = model.program (ref))
        askText ("Rename program", "Name:", p->name, [this, ref] (const juce::String& n)
        {
            model.renameProgram (ref, n.substring (0, 20));
            if (loaded == ref)
                programState.name = n.substring (0, 20);
        });
}

void LibraryDrawer::editTags (ProgramRef ref)
{
    if (auto* p = model.program (ref))
        askText ("Tags", "Comma-separated tags (e.g. pad, warm, live):", p->tags.joinIntoString (", "),
                 [this, ref] (const juce::String& t)
                 {
                     juce::StringArray tags;
                     tags.addTokens (t, ",", "");
                     tags.trim();
                     tags.removeEmptyStrings();
                     tags.removeDuplicates (true);
                     model.setTags (ref, tags);
                 }, true); // an empty answer removes all tags
}

void LibraryDrawer::renameBank (int bank)
{
    if (juce::isPositiveAndBelow (bank, (int) model.banks().size()))
        askText ("Rename bank", "Name:", model.banks()[(size_t) bank].name,
                 [this, bank] (const juce::String& n) { model.renameBank (bank, n); });
}

void LibraryDrawer::askText (const juce::String& titleText, const juce::String& prompt, const juce::String& initial,
                             std::function<void (const juce::String&)> onOk, bool allowEmpty)
{
    auto* w = new juce::AlertWindow (titleText, prompt, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk, allowEmpty] (int r)
    {
        if (r == 1 && (allowEmpty || w->getTextEditorContents ("text").trim().isNotEmpty()))
            onOk (w->getTextEditorContents ("text").trim());
    }), true);
}

// Undo/Redo, saying in the status line what was undone or redone.
void LibraryDrawer::undoOrRedo (bool isUndo)
{
    auto& um = model.getUndoManager();
    const auto what = isUndo ? um.getUndoDescription() : um.getRedoDescription();
    if (! (isUndo ? um.undo() : um.redo()))
        return;
    selection.clear();
    setStatus ((isUndo ? "Undo: " : "Redo: ") + what);
}

void LibraryDrawer::fetchAllUserBanks()
{
    int unsent = 0;
    juce::StringArray banks;
    for (auto& b : model.banks())
        if (b.kind == LibraryBank::Kind::Hardware && ! b.factory)
            for (auto& p : b.programs)
                if (p.unsent)
                {
                    ++unsent;
                    banks.addIfNotAlreadyThere ("User " + juce::String (b.hardwareBank));
                }
    confirm ("Fetch all User banks",
             "Fetch User banks 1-16 from the TEO-5? The library's copies are replaced with what's on the synth."
                 + (unsent > 0 ? "\n\n" + juce::String (unsent) + " change" + (unsent == 1 ? "" : "s") + " not yet sent ("
                                     + banks.joinIntoString (", ") + ") will be replaced. Undo can bring them back."
                               : juce::String())
                 + "\n\nTakes about 50 seconds.",
             "Fetch 16 banks", [this]
             {
                 juce::Array<HardwareTransfer::BankAddress> all;
                 for (int b = 1; b <= 16; ++b)
                     all.add ({ b, false });
                 transfer.fetchBanks (all);
             });
}

void LibraryDrawer::sendAllUserBanks()
{
    // What would go out: everything not yet sent, or every User bank slot the library holds.
    const auto basic = MidiSync::basicProgram();
    auto collect = [this, &basic] (bool everything, juce::StringArray& uuids)
    {
        std::vector<HardwareTransfer::Write> writes;
        for (auto& b : model.banks())
        {
            if (b.kind != LibraryBank::Kind::Hardware || b.factory)
                continue;
            bool any = false;
            for (int i = 0; i < (int) b.programs.size() && i < HardwareTransfer::programsPerBank; ++i)
            {
                const auto& p = b.programs[(size_t) i];
                if (! everything && ! p.unsent)
                    continue;
                if (p.raw != nullptr)
                    writes.push_back ({ { b.hardwareBank, false }, i + 1, *p.raw });
                else if (p.unsent && ! basic.isEmpty())
                    writes.push_back ({ { b.hardwareBank, false }, i + 1, basic }); // deleted here: initialize it
                else
                    continue;
                any = true;
            }
            if (any)
                uuids.add (b.uuid);
        }
        return writes;
    };
    juce::StringArray changedUuids, allUuids;
    auto changes = collect (false, changedUuids);
    auto all = collect (true, allUuids);
    if (all.empty())
    {
        setStatus ("No TEO-5 User banks in the library yet. Fetch them first");
        return;
    }
    juce::StringArray changedBanks;
    for (auto& w : changes)
        changedBanks.addIfNotAlreadyThere ("User " + juce::String (w.target.panelBank));

    auto* w = new juce::AlertWindow ("Send User banks to the TEO-5",
                                     "This overwrites programs in the TEO-5's memory. Fetch first if you want a backup.\n\n"
                                     "Changes not yet sent: " + (changes.empty() ? juce::String ("none")
                                        : juce::String ((int) changes.size()) + " program" + (changes.size() == 1 ? "" : "s") + " in "
                                              + changedBanks.joinIntoString (", ") + " (a few seconds)")
                                     + "\nEverything: " + juce::String ((int) all.size()) + " programs in " + juce::String (allUuids.size())
                                     + " User bank" + (allUuids.size() == 1 ? "" : "s") + " (about "
                                     + juce::String (juce::jmax (1, juce::roundToInt ((double) all.size() * 0.35 / 60.0))) + " min)",
                                     juce::MessageBoxIconType::WarningIcon, this);
    if (! changes.empty())
        w->addButton ("Send changes only", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Send everything", 2);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, changes = std::move (changes), all = std::move (all), changedUuids, allUuids] (int r) mutable
        {
            if (r == 1)
                startWrites (std::move (changes), changedUuids);
            else if (r == 2)
                startWrites (std::move (all), allUuids);
        }), true);
}

void LibraryDrawer::startWrites (std::vector<HardwareTransfer::Write> writes, juce::StringArray bankUuids)
{
    if (! sync.hasOutput())
    {
        // The writes would go nowhere, but the NOT SENT marks would clear as if they'd arrived.
        setStatus ("No MIDI output: choose the TEO-5's port in the MIDI drawer, then send again");
        if (onShowMessage)
            onShowMessage ("No MIDI output");
        return;
    }
    sendingBankUuids = std::move (bankUuids);
    transfer.sendWrites (std::move (writes));
}

// Every way of deleting programs asks first.
void LibraryDrawer::confirmDelete (juce::Array<ProgramRef> refs)
{
    refs.removeIf ([this] (const ProgramRef& r) { auto* p = model.program (r); return p == nullptr || p->isEmpty() || model.isFactoryBank (r.bank); });
    if (refs.isEmpty())
        return;
    const auto first = model.program (refs[0])->name;
    const auto what = refs.size() == 1 ? "\"" + first + "\"" : juce::String (refs.size()) + " programs";
    confirm ("Delete " + (refs.size() == 1 ? juce::String ("program") : juce::String ("programs")),
             "Delete " + what + " from the library?\n\nThe slot" + (refs.size() == 1 ? " is" : "s are")
                 + " left empty. Programs on the TEO-5 aren't affected. You can undo this (Cmd+Z).",
             "Delete", [this, refs, what]
             {
                 model.removePrograms (refs);
                 selection.clear();
                 setStatus ("Deleted " + what + " (Cmd+Z to undo)");
             });
}

void LibraryDrawer::confirm (const juce::String& titleText, const juce::String& message, const juce::String& okText, std::function<void()> onOk)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, titleText, message, okText, "Cancel", this,
                                        juce::ModalCallbackFunction::create ([onOk] (int r) { if (r == 1) onOk(); }));
}

// ---- Saving (WRITE) ------------------------------------------------------------------------------

juce::Component& LibraryDrawer::getSavePanel() { return *savePanel; }

juce::String LibraryDrawer::locationOf (ProgramRef ref) const
{
    if (! juce::isPositiveAndBelow (ref.bank, (int) model.banks().size()))
        return {};
    const auto& b = model.banks()[(size_t) ref.bank];
    if (b.kind == LibraryBank::Kind::Hardware)
        return HardwareTransfer::locationName ({ b.hardwareBank, b.factory }, ref.index + 1);
    return b.name + " " + juce::String (ref.index + 1).paddedLeft ('0', 2);
}

bool LibraryDrawer::saveTargetValid() const
{
    if (! saveTarget.isValid() || ! juce::isPositiveAndBelow (saveTarget.bank, (int) model.banks().size()) || model.isFactoryBank (saveTarget.bank))
        return false;
    const auto& b = model.banks()[(size_t) saveTarget.bank];
    return b.kind == LibraryBank::Kind::Hardware ? saveTarget.index < LibraryModel::slotsPerBank
                                                 : saveTarget.index <= (int) b.programs.size(); // size = the empty slot after them
}

void LibraryDrawer::updateToolbar()
{
    const bool free = ! saving; // while choosing where to save, the library can't change under the choice
    for (auto* b : { &importButton, &saveAllButton, &newBankButton, &pullAllButton, &pushAllButton })
        b->setEnabled (free);
    fetchButton.setEnabled (free && ! transfer.isBusy());
    undoButton.setEnabled (free && model.getUndoManager().canUndo());
    redoButton.setEnabled (free && model.getUndoManager().canRedo());
}

bool LibraryDrawer::isSaveTarget (const Row& r) const
{
    return saving && ! r.isBank && r.bank == saveTarget.bank && r.index == saveTarget.index;
}

void LibraryDrawer::beginSave (const juce::MemoryBlock& raw, const juce::String& name, const juce::String& from, ProgramRef current)
{
    showHelp (false);
    saving = true;
    auditioned = false;
    saveRaw = raw;
    saveName = name;
    saveFrom = from;
    saveTarget = {};
    selection.clear();
    updateToolbar();

    // The panel's program is selected; if it isn't in a User bank or one of your banks, start at User 1, slot 01.
    ProgramRef show;
    if (model.program (current) != nullptr && ! model.isFactoryBank (current.bank))
    {
        selection = { { current.bank, current.index } };
        show = current;
    }
    else
        for (int b = 0; b < (int) model.banks().size(); ++b)
            if (const auto& bank = model.banks()[(size_t) b]; bank.kind == LibraryBank::Kind::Hardware && ! bank.factory && bank.hardwareBank == 1)
                show = { b, 0 };
    if (show.isValid())
        expanded.insert (model.banks()[(size_t) show.bank].uuid);
    rebuildRows();
    if (show.isValid() && selection.empty())
        setSaveTarget (show);

    // Scroll the row into view.
    for (int pane = 0; pane < numPanes && show.isValid(); ++pane)
        for (int i = 0; i < (int) rowLists[(size_t) pane].size(); ++i)
            if (const auto& r = rowLists[(size_t) pane][(size_t) i]; ! r.isBank && r.bank == show.bank && r.index == show.index)
                viewports[(size_t) pane].setViewPosition (0, juce::jmax (0, i * rowHeight - viewports[(size_t) pane].getHeight() / 3));

    savePanel->update();
    savePanel->setVisible (true);
    savePanel->toFront (false);
    setStatus (show.isValid() || ! model.banks().empty() ? "Choose where to save: click a slot in a TEO-5 User bank or in MY BANKS"
                                                         : "Fetch your TEO-5 User banks, or make a bank in MY BANKS, to save into");
    repaintLists();
}

void LibraryDrawer::setSaveTarget (ProgramRef ref)
{
    if (! saving || ! juce::isPositiveAndBelow (ref.bank, (int) model.banks().size()))
        return;
    if (model.isFactoryBank (ref.bank))
    {
        setStatus ("Factory programs can't be changed: pick a slot in a User bank or in MY BANKS");
        return;
    }
    if (isHardwareBank (ref.bank) && ref.index >= LibraryModel::slotsPerBank)
        return;
    saveTarget = ref;
    saveTargetBank = model.banks()[(size_t) ref.bank].uuid;
    // Audition: the program stored there goes into the edit buffer (the panel doesn't change).
    if (auto* p = model.program (ref); p != nullptr && p->raw != nullptr)
    {
        sync.sendToEditBuffer (*p->raw);
        auditioned = true;
    }
    else if (std::exchange (auditioned, false))
        sync.restoreEditBuffer();
    setStatus ({});
    savePanel->update();
    repaintLists();
}

void LibraryDrawer::confirmSave()
{
    if (! saving || ! saveTargetValid())
        return;
    const auto& bank = model.banks()[(size_t) saveTarget.bank];
    const bool toSynth = bank.kind == LibraryBank::Kind::Hardware;
    if (toSynth && ! sync.hasOutput())
    {
        // Nothing would reach the synth, yet the slot would count as stored there (no NOT SENT mark).
        setStatus ("No MIDI output: choose the TEO-5's port in the MIDI drawer, or save into MY BANKS");
        if (onShowMessage)
            onShowMessage ("No MIDI output");
        return;
    }
    std::optional<HardwareTransfer::BankAddress> written;
    const int program = saveTarget.index + 1;
    if (toSynth)
    {
        written = HardwareTransfer::BankAddress { bank.hardwareBank, false };
        sync.writeProgram (bank.hardwareBank, program, false, saveRaw);
    }
    const auto where = locationOf (saveTarget), bankName = bank.name;
    const auto ref = model.saveProgram (saveTarget, saveRaw, saveName, toSynth);
    sync.loadProgram (saveRaw); // the saved program is in the edit buffer, as stored from now on
    programState.name = saveName;
    loaded = ref;
    saving = auditioned = false;
    saveTarget = {};
    savePanel->setVisible (false);
    updateToolbar();
    selection.clear();
    if (ref.isValid())
        selection.insert ({ ref.bank, ref.index });
    setStatus (toSynth ? "Wrote \"" + saveName + "\" to " + where : "Saved \"" + saveName + "\" in " + bankName);
    repaintLists();
    if (onSaveFinished)
        onSaveFinished (true, written, program);
}

void LibraryDrawer::cancelSave()
{
    if (! saving)
        return;
    if (std::exchange (auditioned, false))
        sync.restoreEditBuffer(); // your edit goes back
    saving = false;
    saveTarget = {};
    savePanel->setVisible (false);
    updateToolbar();
    selection.clear();
    setStatus ("Not saved");
    repaintLists();
    if (onSaveFinished)
        onSaveFinished (false, std::nullopt, 0);
}

// ---- Sending changes ------------------------------------------------------------------------------

int LibraryDrawer::unsentCount() const
{
    int n = 0;
    for (auto& b : model.banks())
        if (b.kind == LibraryBank::Kind::Hardware && ! b.factory)
            for (int i = 0; i < (int) b.programs.size() && i < HardwareTransfer::programsPerBank; ++i)
                n += b.programs[(size_t) i].unsent ? 1 : 0;
    return n;
}

// Every NOT SENT slot in the TEO-5 User banks, with a line each saying what it overwrites on the synth.
std::vector<HardwareTransfer::Write> LibraryDrawer::unsentWrites (juce::StringArray& bankUuids, juce::StringArray& lines) const
{
    std::vector<HardwareTransfer::Write> writes;
    const auto basic = MidiSync::basicProgram();
    for (auto& b : model.banks())
    {
        if (b.kind != LibraryBank::Kind::Hardware || b.factory)
            continue;
        bool any = false;
        for (int i = 0; i < (int) b.programs.size() && i < HardwareTransfer::programsPerBank; ++i)
        {
            const auto& p = b.programs[(size_t) i];
            if (! p.unsent || (p.raw == nullptr && basic.isEmpty()))
                continue;
            writes.push_back ({ { b.hardwareBank, false }, i + 1, p.raw != nullptr ? *p.raw : basic }); // deleted: initialize it
            any = true;
            const auto onSynth = model.synthSlot (b.hardwareBank, i).name;
            const auto now = onSynth.isNotEmpty() ? "\"" + onSynth + "\"" : juce::String ("What's stored there");
            const auto by = p.raw == nullptr ? juce::String ("the Basic Program (slot deleted)")
                          : onSynth == p.name ? "your changed \"" + p.name + "\"" : "\"" + p.name + "\"";
            lines.add (HardwareTransfer::locationName ({ b.hardwareBank, false }, i + 1) + ": " + now + " is overwritten by " + by);
        }
        if (any)
            bankUuids.add (b.uuid);
    }
    return writes;
}

namespace
{
    juce::String listOf (const juce::StringArray& lines)
    {
        juce::StringArray shown;
        for (int i = 0; i < lines.size() && i < 12; ++i)
            shown.add (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + lines[i]);
        if (lines.size() > 12)
            shown.add ("and " + juce::String (lines.size() - 12) + " more");
        return shown.joinIntoString ("\n");
    }
}

void LibraryDrawer::saveAllChanges()
{
    juce::StringArray uuids, lines;
    auto writes = unsentWrites (uuids, lines);
    if (writes.empty())
    {
        setStatus ("Nothing to send: every program in your TEO-5 User banks is on the synth");
        return;
    }
    const int n = (int) writes.size();
    auto* w = new juce::AlertWindow ("Save " + juce::String (n) + " change" + (n == 1 ? "" : "s") + " to the TEO-5?",
                                     "These programs on the TEO-5 will be overwritten. This writes to the synth's memory and "
                                     "can't be undone from the editor.\n\n" + listOf (lines) + "\n\nThe NOT SENT marks are cleared afterwards.",
                                     juce::MessageBoxIconType::WarningIcon, this);
    w->addButton ("Confirm", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, writes = std::move (writes), uuids] (int r) mutable
    {
        if (r == 1)
            startWrites (std::move (writes), uuids);
    }), true);
}

void LibraryDrawer::closeWithUnsentCheck (std::function<void()> close)
{
    if (saving)
    {
        cancelSave(); // the editor closes the drawer
        return;
    }
    juce::StringArray uuids, lines;
    auto writes = unsentWrites (uuids, lines);
    if (writes.empty() || transfer.isBusy())
    {
        close();
        return;
    }
    const int n = (int) writes.size();
    auto* w = new juce::AlertWindow ("Send unsent changes to the TEO-5?",
                                     juce::String (n) + " change" + (n == 1 ? "" : "s") + " in your TEO-5 User banks "
                                         + (n == 1 ? "hasn't" : "haven't") + " been sent to the synth:\n\n" + listOf (lines)
                                         + "\n\nSending writes the patches to the TEO-5's memory, overwriting what's stored in those slots on the synth.",
                                     juce::MessageBoxIconType::WarningIcon, this);
    w->addButton ("Send and close", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Close without sending", 2);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, writes = std::move (writes), uuids, close, n] (int r) mutable
    {
        if (r == 1)
        {
            startWrites (std::move (writes), uuids);
            if (onShowMessage && transfer.isBusy()) // not if there's no MIDI output (startWrites said so)
                onShowMessage ("Sending " + juce::String (n) + " program" + (n == 1 ? "" : "s") + " to the TEO-5");
        }
        if (r != 0)
            close();
    }), true);
}

// ---- Guide ------------------------------------------------------------------------------------------

namespace
{
const char* const libraryGuide = R"(THE LIBRARY
Your programs, kept on this computer. The left column is the TEO-5's own banks as last fetched; the right column, MY BANKS, holds your own banks and imported .syx files.

THE TWO COLUMNS
- TEO-5: User banks 1-16 (16 slots each) and Factory banks, fetched from the synth. Factory banks are read-only.
- MY BANKS: your banks, any number of programs each. Every bank ends with an Empty slot to save into.
- A bank's header shows how many programs it holds, or how many changes are NOT SENT. Click the triangle to open or close a bank; double-click the name to rename it.

LOADING AND ORGANISING
- Click a program to load it into the TEO-5's edit buffer (nothing is written to memory). Shift-click and Cmd-click select several.
- Icons on each row: star (favourite), export as .syx, rename, delete. Right-click for more (tags, copy, paste, duplicate). Right-click > Write to TEO-5 memory saves that program the same way as WRITE on the panel (see below).
- Drag programs between banks: within the TEO-5 User banks or one of your banks a drop on another program swaps the two; Option-drag copies. Drag a bank's header to reorder banks.
- Search finds names, sources and tags (type tag:pad for tags only). Favourites shows starred programs only.
- Undo and Redo cover every change in the library.

SAVING YOUR EDIT (WRITE)
- Press WRITE on the front panel. The library opens with the panel's program selected, and the Save pop-up appears over the panel.
- Click a slot in a TEO-5 User bank or in MY BANKS. The program stored there goes into the TEO-5's edit buffer, so you can play it from your MIDI controller or the synth's keys to hear what you would replace. If the panel's program isn't in a User bank or one of your banks, User 1 slot 01 is chosen to start with.
- Confirm saves: into a TEO-5 User slot it's written to the synth's memory (and the library); into MY BANKS it's kept in the library only. The drawer closes and the panel shows the saved program.
- Cancel (or LIBRARY, or Esc) puts your edit back in the edit buffer and closes the drawer.

SENDING CHANGES TO THE TEO-5
- A slot in a TEO-5 User bank whose program differs from what the synth holds there is marked NOT SENT. Putting programs back where they were, or Undo, clears the mark again.
- Save all changes writes every NOT SENT slot to the TEO-5's memory. It lists what will be overwritten on the synth and asks you to Confirm first; the NOT SENT marks go once the programs are sent.
- Closing the library with the LIBRARY button while changes aren't sent asks whether to send them.
- TEO-5 -> Library re-reads all 16 User banks from the synth. Library -> TEO-5 writes the User banks back (changes only, or everything). Fetch reads one bank, the factory banks or the edit buffer.

FILES
- Import reads .syx files (single programs or whole banks) into MY BANKS; you can also drop .syx files on the library.
- Export saves a program, several programs or a whole bank as .syx.)";
}

LibraryDrawer::HelpPanel::HelpPanel()
{
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141517));
    text.setColour (juce::TextEditor::outlineColourId, Palette::pinstripe.withAlpha (0.5f));
    // Section headings (lines in capitals) in the panel's lettering, the rest as plain text, as in the sequencer's guide.
    for (auto& line : juce::StringArray::fromLines (libraryGuide))
    {
        const bool heading = line.length() < 60 && ! line.startsWith ("-") && line == line.toUpperCase()
                             && line.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        text.setFont (heading ? TEO5LookAndFeel::panelFont (13.0f) : juce::Font (juce::FontOptions (14.0f)));
        text.setColour (juce::TextEditor::textColourId, heading ? Palette::pinstripe.brighter (0.4f) : Palette::silkscreen);
        text.insertTextAtCaret (line + "\n");
    }
    addAndMakeVisible (text);
    addAndMakeVisible (close);
}

void LibraryDrawer::HelpPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff181a1c));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (Palette::pinstripe);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void LibraryDrawer::HelpPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    close.setBounds (r.removeFromBottom (26).removeFromRight (90));
    r.removeFromBottom (6);
    text.setBounds (r);
}

void LibraryDrawer::showHelp (bool show)
{
    helpPanel.setVisible (show);
    if (show)
    {
        helpPanel.toFront (false);
        helpPanel.text.moveCaretToTop (false);
    }
    helpButton.setToggleState (show, juce::dontSendNotification);
}

} // namespace teo5
