// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "ModMatrixDrawer.h"
#include "TEO5LookAndFeel.h"
#include "../Params/ParamTable.h"

namespace teo5
{

namespace
{
    const juce::Colour rowA (0xff1c1d20), rowB (0xff202124), comboBg (0xff141517);
    constexpr int amountCentre = 127; // the TEO-5 stores 0-254, 127 = none

    // Drop-downs in the panel's lettering instead of the default bold system font.
    struct DrawerLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Font getComboBoxFont (juce::ComboBox&) override { return TEO5LookAndFeel::panelFont (10.5f); }
        juce::Font getPopupMenuFont() override { return TEO5LookAndFeel::panelFont (11.0f); }
        // Undo / Redo: as the sequencer's buttons.
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? juce::Colour (0xff26282d).brighter (0.08f) : juce::Colour (0xff26282d));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (highlighted && b.isEnabled() ? juce::Colour (0xff4a4f59) : juce::Colour (0xff34373e));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour (Palette::silkscreen.withAlpha (b.isEnabled() ? 1.0f : 0.35f));
            g.setFont (TEO5LookAndFeel::panelFont (10.5f));
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
        }
        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }
    };

    DrawerLookAndFeel& drawerLookAndFeel()
    {
        static DrawerLookAndFeel lf;
        return lf;
    }

    juce::String slotId (int slot, const char* what) { return "mod" + juce::String (slot) + what; }

    int nativeValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
            return p->get();
        return 0;
    }
}

// ---- Amount: bipolar bar, filled from the centre --------------------------------------------------

class ModMatrixDrawer::AmountBar : public juce::Slider
{
public:
    AmountBar() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setDoubleClickReturnValue (false, 0.0);
        setMouseDragSensitivity (400);
        setVelocityBasedMode (false);
    }

    bool dimmed = false;
    std::optional<int> ghost;           // slot turned off: the amount it comes back with (dashed outline)
    std::function<double()> resetValue; // double-click: the program's stored amount
    std::function<void()> onContextMenu; // right-click: the slot's menu (clear amount / clear slot)
    std::function<void()> onEditStart;   // just before a drag, double-click or wheel changes the amount

    // Right-click never moves the amount; it opens the slot's menu.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu();
            return;
        }
        if (onEditStart)
            onEditStart();
        juce::Slider::mouseDown (e);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (onEditStart)
            onEditStart();
        juce::Slider::mouseWheelMove (e, w);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onEditStart)
            onEditStart();
        setValue (resetValue ? resetValue() : (double) amountCentre, juce::sendNotificationSync);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f, 3.5f);
        g.setColour (comboBg);
        g.fillRoundedRectangle (b, 2.0f);
        g.setColour (Palette::pinstripe.withAlpha (0.45f));
        g.drawRoundedRectangle (b, 2.0f, 1.0f);

        const float min = (float) getMinimum(), max = (float) getMaximum();
        const float centreX = b.getX() + b.getWidth() * ((float) amountCentre - min) / (max - min);
        const float valueX = b.getX() + b.getWidth() * ((float) getValue() - min) / (max - min);
        auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centreX, valueX), b.getY() + 2.0f,
                                                                juce::jmax (centreX, valueX), b.getBottom() - 2.0f);
        g.setColour ((dimmed ? Palette::silkscreen.withAlpha (0.25f) : Palette::ledOn).withAlpha (dimmed ? 0.25f : 0.85f));
        g.fillRect (fill);

        if (ghost.has_value())
        {
            const float ghostX = b.getX() + b.getWidth() * ((float) *ghost - min) / (max - min);
            auto outline = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centreX, ghostX), b.getY() + 2.0f,
                                                                       juce::jmax (centreX, ghostX), b.getBottom() - 2.0f);
            juce::Path p;
            p.addRectangle (outline);
            const float dashes[] = { 3.0f, 2.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (p, p, dashes, 2);
            g.setColour (Palette::ledOn.withAlpha (0.55f));
            g.fillPath (p);
            g.setColour (juce::Colour (0xffef9f27));
            g.setFont (TEO5LookAndFeel::panelFont (8.5f));
            g.drawText ("OFF", b.reduced (6.0f, 0.0f), *ghost < amountCentre ? juce::Justification::centredLeft
                                                                              : juce::Justification::centredRight, false);
            return;
        }

        g.setColour (Palette::silkscreen.withAlpha (0.35f));
        g.fillRect (centreX - 0.5f, b.getY() + 1.0f, 1.0f, b.getHeight() - 2.0f);

        const int v = juce::roundToInt (getValue()) - amountCentre;
        g.setColour (Palette::silkscreen.withAlpha (dimmed ? 0.4f : 1.0f));
        g.setFont (TEO5LookAndFeel::panelFont (9.0f));
        g.drawText (v > 0 ? "+" + juce::String (v) : juce::String (v), b.reduced (6.0f, 0.0f),
                    v < 0 ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
    }
};

// ---- One slot ------------------------------------------------------------------------------------

class ModMatrixDrawer::SlotRow : public juce::Component
{
public:
    SlotRow (ModMatrixDrawer& o, juce::AudioProcessorValueTreeState& s, int slotNumber) : owner (o), apvts (s), slot (slotNumber)
    {
        amount.resetValue = [this]
        {
            if (owner.storedValue)
                if (auto v = owner.storedValue (slotId (slot, "Amt")))
                    return (double) *v;
            return (double) amountCentre;
        };
        amount.onContextMenu = [this] { owner.touchedSlot (slot); showMenu(); };
        amount.onEditStart = [this] { owner.userEdit (slot, "amount of slot " + juce::String (slot)); };
        source.addItemList (modSourceNames(), 1);
        if (auto* def = findById (slotId (slot, "Dest")))
            destination.addItemList (choiceNames (*def), 1);

        // A drop-down's change is an edit only when it follows a click on it (the attachment also changes it
        // when the value comes from the synth or an undo).
        source.onChange = [this] { comboChanged ("source"); };
        destination.onChange = [this] { comboChanged ("destination"); };
        for (auto* c : { &source, &destination })
        {
            c->addMouseListener (this, false);
            c->setColour (juce::ComboBox::backgroundColourId, comboBg);
            c->setColour (juce::ComboBox::textColourId, Palette::silkscreen);
            c->setColour (juce::ComboBox::outlineColourId, Palette::pinstripe.withAlpha (0.45f));
            c->setColour (juce::ComboBox::arrowColourId, Palette::silkscreen.withAlpha (0.6f));
            c->setLookAndFeel (&drawerLookAndFeel());
            addAndMakeVisible (c);
        }
        addAndMakeVisible (amount);

        sourceAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, slotId (slot, "Src"), source);
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, slotId (slot, "Amt"), amount);
        destAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, slotId (slot, "Dest"), destination);
    }

    ~SlotRow() override
    {
        source.setLookAndFeel (nullptr);
        destination.setLookAndFeel (nullptr);
    }

    // A slot does something only with both a source and a destination (assigned) and a non-zero amount.
    bool isAssigned() const
    {
        return nativeValue (apvts, slotId (slot, "Src")) != 0 && nativeValue (apvts, slotId (slot, "Dest")) != 0;
    }
    bool isActive() const { return isAssigned() && nativeValue (apvts, slotId (slot, "Amt")) != amountCentre; }

    // The LED: turns an assigned slot off (amount to none, remembering it) and back on. The TEO-5 has no
    // per-slot switch, so "off" is an amount of zero; the remembered amount is kept by the editor only.
    void toggle()
    {
        if (! isAssigned())
            return;
        const int src = nativeValue (apvts, slotId (slot, "Src")), dest = nativeValue (apvts, slotId (slot, "Dest"));
        if (isActive())
        {
            remembered = Remembered { nativeValue (apvts, slotId (slot, "Amt")), src, dest };
            set ("Amt", amountCentre);
            return;
        }
        int back = amountCentre;
        if (remembered.has_value() && remembered->source == src && remembered->destination == dest)
            back = remembered->amount;
        else if (owner.storedValue)
            if (auto v = owner.storedValue (slotId (slot, "Amt")))
                back = *v;
        if (back != amountCentre)
            set ("Amt", back);
        remembered.reset();
    }

    void refresh()
    {
        const bool active = isActive(), assigned = isAssigned();
        // Forget a remembered amount once the slot is reassigned or its amount changes some other way.
        if (remembered.has_value() && (active || ! assigned
                                       || remembered->source != nativeValue (apvts, slotId (slot, "Src"))
                                       || remembered->destination != nativeValue (apvts, slotId (slot, "Dest"))))
            remembered.reset();
        std::optional<int> ghost;
        if (assigned && ! active && remembered.has_value())
            ghost = remembered->amount;
        if (active != wasActive || assigned != wasAssigned || ghost != amount.ghost)
        {
            wasActive = active;
            wasAssigned = assigned;
            amount.dimmed = ! active;
            amount.ghost = ghost;
            for (auto* c : { &source, &destination })
                c->setColour (juce::ComboBox::textColourId, Palette::silkscreen.withAlpha (active ? 1.0f : 0.45f));
            repaint();
        }
        amount.repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.x < numberWidth && wasAssigned ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.touchedSlot (slot);
        if (e.eventComponent == &source || e.eventComponent == &destination)
        {
            armedCombo = (juce::ComboBox*) e.eventComponent;
            owner.comboOpen = true;
            return;
        }
        if (! e.mods.isPopupMenu() && e.x < numberWidth && isAssigned())
        {
            owner.userEdit (slot, "slot " + juce::String (slot) + (isActive() ? " off" : " on"));
            toggle();
        }
    }

    void comboChanged (const char* what)
    {
        auto* box = juce::String (what) == "source" ? &source : &destination;
        if (armedCombo != box)
            return;
        armedCombo = nullptr;
        owner.comboOpen = false;
        owner.userEdit (slot, juce::String (what) + " of slot " + juce::String (slot));
    }

    // The drop-down closed without a change: it's no longer about to edit.
    void disarmIfClosed()
    {
        if (armedCombo != nullptr && ! armedCombo->isPopupActive() && ++closedTicks > 8)
        {
            armedCombo = nullptr;
            owner.comboOpen = false;
        }
        if (armedCombo == nullptr || armedCombo->isPopupActive())
            closedTicks = 0;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (slot % 2 ? rowA : rowB);
        auto r = getLocalBounds().removeFromLeft (numberWidth).toFloat();
        TEO5LookAndFeel::drawLed (g, { r.getX() + 9.0f, r.getCentreY() }, 3.2f, wasActive);
        if (wasAssigned && ! wasActive)
        {
            g.setColour (Palette::ledOn.withAlpha (0.5f)); // assigned but off: a ring shows the LED can be clicked
            g.drawEllipse (r.getX() + 9.0f - 4.5f, r.getCentreY() - 4.5f, 9.0f, 9.0f, 1.0f);
        }
        g.setColour (Palette::silkscreen.withAlpha (wasActive ? 1.0f : 0.5f));
        g.setFont (TEO5LookAndFeel::panelFont (10.0f));
        g.drawText (juce::String (slot), r.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && e.eventComponent == this)
            showMenu();
    }

    void showMenu()
    {
        // The slot as stored in the loaded program, if known: "Restore" puts it back (e.g. after clearing it).
        std::optional<int> storedSrc, storedAmt, storedDest;
        if (owner.storedValue)
        {
            storedSrc = owner.storedValue (slotId (slot, "Src"));
            storedAmt = owner.storedValue (slotId (slot, "Amt"));
            storedDest = owner.storedValue (slotId (slot, "Dest"));
        }
        const bool known = storedSrc && storedAmt && storedDest;
        const bool changed = known && (*storedSrc != nativeValue (apvts, slotId (slot, "Src"))
                                       || *storedAmt != nativeValue (apvts, slotId (slot, "Amt"))
                                       || *storedDest != nativeValue (apvts, slotId (slot, "Dest")));

        juce::PopupMenu m;
        m.addItem (2, "Clear amount");
        m.addItem (1, "Clear slot " + juce::String (slot));
        m.addSeparator();
        m.addItem (3, "Restore slot " + juce::String (slot) + " to patch default", changed);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, storedSrc, storedAmt, storedDest] (int r)
        {
            const auto n = juce::String (slot);
            if (r >= 1)
                owner.userEdit (slot, r == 3 ? "restore slot " + n : r == 1 ? "clear slot " + n : "clear amount of slot " + n);
            if ((r == 1 || r == 3) && owner.focusedSlot == slot)
                owner.clearFocus(); // the new slot was cleared or put back: no longer outlined
            if (r == 3)
            {
                set ("Src", *storedSrc);
                set ("Amt", *storedAmt);
                set ("Dest", *storedDest);
                remembered.reset();
                return;
            }
            if (r == 1)
            {
                set ("Src", 0);
                set ("Dest", 0);
            }
            if (r >= 1)
                set ("Amt", amountCentre);
        });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        r.removeFromLeft (numberWidth);
        source.setBounds (r.removeFromLeft (sourceWidth));
        r.removeFromLeft (gap);
        destination.setBounds (r.removeFromRight (destWidth));
        r.removeFromRight (gap);
        amount.setBounds (r);
    }

    static constexpr int numberWidth = 38, sourceWidth = 132, destWidth = 158, gap = 8;

private:
    void set (const char* what, int value)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (slotId (slot, what))))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
            p->endChangeGesture();
        }
    }

    ModMatrixDrawer& owner;
    juce::AudioProcessorValueTreeState& apvts;
    const int slot;
    bool wasActive = true, wasAssigned = true;
    struct Remembered { int amount, source, destination; };
    std::optional<Remembered> remembered;
    juce::ComboBox* armedCombo = nullptr;
    int closedTicks = 0;
    juce::ComboBox source, destination;
    AmountBar amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAtt, destAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;
};

// ---- Drawer --------------------------------------------------------------------------------------

ModMatrixDrawer::ModMatrixDrawer (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    const auto text = Palette::silkscreen;
    title.setFont (TEO5LookAndFeel::panelFont (14.0f));
    title.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (title);

    summary.setFont (TEO5LookAndFeel::panelFont (9.0f));
    summary.setColour (juce::Label::textColourId, text.withAlpha (0.75f));
    summary.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (summary);

    defaultHint = "Click an LED to turn a slot off or on. Drag an amount; double-click restores it. Right-click an amount "
                  "or slot to clear it or restore it.";
    hint.setText (defaultHint, juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setColour (juce::Label::textColourId, text.withAlpha (0.5f));
    hint.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (hint);

    for (int i = 1; i <= numSlots; ++i)
        addAndMakeVisible (rows.add (new SlotRow (*this, apvts, i)));

    for (auto* b : { &undoButton, &redoButton })
    {
        b->setLookAndFeel (&drawerLookAndFeel());
        addAndMakeVisible (b);
    }
    undoButton.onClick = [this] { undoRedo (true); };
    redoButton.onClick = [this] { undoRedo (false); };
    undoButton.setTooltip ("Undo the last change to the mod matrix");
    redoButton.setTooltip ("Redo");
    stable = capture();

    timerCallback();
    startTimerHz (15);
}

ModMatrixDrawer::~ModMatrixDrawer()
{
    undoButton.setLookAndFeel (nullptr);
    redoButton.setLookAndFeel (nullptr);
}

int ModMatrixDrawer::activeCount() const
{
    int n = 0;
    for (auto* r : rows)
        n += r->isActive() ? 1 : 0;
    return n;
}

void ModMatrixDrawer::timerCallback()
{
    for (auto* r : rows)
    {
        r->refresh();
        r->disarmIfClosed();
    }
    // Another program: a fresh history.
    if (programVersion && programVersion() != seenProgramVersion)
    {
        const bool first = seenProgramVersion < 0; // just opened: nothing to forget
        seenProgramVersion = programVersion();
        if (! first)
        {
            undoStack.clear();
            redoStack.clear();
            clearFocus();
        }
    }
    // Follow every change (from the synth, the panel or an edit here) except while a drop-down is about to edit.
    if (! comboOpen)
        stable = capture();
    if (historyUntil != 0 && juce::Time::getMillisecondCounter() > historyUntil)
    {
        historyUntil = 0;
        historyMessage.clear();
        updateHint();
    }
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    summary.setText (juce::String (activeCount()) + " of " + juce::String (numSlots) + " slots active", juce::dontSendNotification);
}

int ModMatrixDrawer::firstFreeSlot() const
{
    for (int s = 1; s <= numSlots; ++s)
        if (nativeValue (apvts, slotId (s, "Src")) == 0 && nativeValue (apvts, slotId (s, "Dest")) == 0)
            return s;
    return 0;
}

juce::Array<int> ModMatrixDrawer::slotsFor (int destination) const
{
    juce::Array<int> found;
    for (int s = 1; s <= numSlots; ++s)
        if (nativeValue (apvts, slotId (s, "Dest")) == destination && nativeValue (apvts, slotId (s, "Src")) != 0)
            found.add (s);
    return found;
}

int ModMatrixDrawer::sourceOf (int slot) const { return nativeValue (apvts, slotId (slot, "Src")); }
int ModMatrixDrawer::amountOf (int slot) const { return nativeValue (apvts, slotId (slot, "Amt")) - amountCentre; }

int ModMatrixDrawer::assign (int source, int destination)
{
    const int slot = firstFreeSlot();
    if (slot == 0)
        return 0;
    stable = capture();
    pushStep ("add " + modSourceNames()[source] + " to " + modDestinationNames()[destination] + " in slot " + juce::String (slot));
    auto set = [this, slot] (const char* what, int value)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (slotId (slot, what))))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
    };
    set ("Amt", amountCentre); // none first, so assigning changes nothing until the amount is dragged
    set ("Src", source);
    set ("Dest", destination);
    return slot;
}

void ModMatrixDrawer::focusSlot (int slot, const juce::String& message)
{
    focusedSlot = slot;
    focusMessage = message;
    historyMessage.clear();
    updateHint();
    repaint();
}

void ModMatrixDrawer::clearFocus()
{
    if (focusedSlot == 0)
        return;
    focusedSlot = 0;
    focusMessage.clear();
    updateHint();
    repaint();
}

void ModMatrixDrawer::touchedSlot (int slot)
{
    if (focusedSlot != 0 && slot != focusedSlot)
        clearFocus();
}

void ModMatrixDrawer::updateHint()
{
    // The outlined slot's message, else what was just undone or redone, else the usual help.
    const bool focus = focusedSlot != 0 && focusMessage.isNotEmpty();
    hint.setText (focus ? focusMessage : historyMessage.isNotEmpty() ? historyMessage : defaultHint, juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, focus ? Palette::ledOn.brighter (0.4f)
                                               : Palette::silkscreen.withAlpha (historyMessage.isNotEmpty() ? 0.85f : 0.5f));
}

void ModMatrixDrawer::visibilityChanged()
{
    if (! isVisible())
        clearFocus();
}

ModMatrixDrawer::State ModMatrixDrawer::capture() const
{
    State s {};
    for (int slot = 1; slot <= numSlots; ++slot)
    {
        s[(size_t) (slot - 1) * 3] = nativeValue (apvts, slotId (slot, "Src"));
        s[(size_t) (slot - 1) * 3 + 1] = nativeValue (apvts, slotId (slot, "Amt"));
        s[(size_t) (slot - 1) * 3 + 2] = nativeValue (apvts, slotId (slot, "Dest"));
    }
    return s;
}

void ModMatrixDrawer::apply (const State& s)
{
    const char* parts[] = { "Amt", "Src", "Dest" }; // amount first, as when a slot is assigned
    const int index[] = { 1, 0, 2 };
    for (int slot = 1; slot <= numSlots; ++slot)
        for (int k = 0; k < 3; ++k)
        {
            const int v = s[(size_t) (slot - 1) * 3 + (size_t) index[k]];
            if (nativeValue (apvts, slotId (slot, parts[k])) == v)
                continue;
            if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (slotId (slot, parts[k]))))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); // goes to the TEO-5 like any edit
                p->endChangeGesture();
            }
        }
}

void ModMatrixDrawer::pushStep (const juce::String& what)
{
    // A burst of the same edit (mouse-wheel steps) is one step.
    const auto now = juce::Time::getMillisecondCounter();
    const bool same = what == lastWhat && now - lastEditAt < 600 && ! undoStack.empty();
    lastWhat = what;
    lastEditAt = now;
    if (! same)
    {
        undoStack.push_back ({ stable, what });
        if (undoStack.size() > 200)
            undoStack.erase (undoStack.begin());
    }
    redoStack.clear();
    historyMessage.clear();
    historyUntil = 0;
    updateHint();
    undoButton.setEnabled (true);
    redoButton.setEnabled (false);
}

void ModMatrixDrawer::userEdit (int slot, const juce::String& what)
{
    if (slot != 0)
        touchedSlot (slot);
    pushStep (what);
}

void ModMatrixDrawer::undoRedo (bool undo)
{
    auto& from = undo ? undoStack : redoStack;
    auto& to = undo ? redoStack : undoStack;
    if (from.empty())
        return;
    auto step = from.back();
    from.pop_back();
    to.push_back ({ capture(), step.what });
    apply (step.state);
    stable = step.state;
    lastWhat.clear();
    historyMessage = (undo ? "Undo: " : "Redo: ") + step.what;
    historyUntil = juce::Time::getMillisecondCounter() + 5000;
    if (focusedSlot != 0 && (sourceOf (focusedSlot) == 0 || nativeValue (apvts, slotId (focusedSlot, "Dest")) == 0))
        clearFocus(); // the outlined slot was undone
    else
        focusMessage.clear(); // keep the outline, show what was undone
    updateHint();
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
}

void ModMatrixDrawer::paintOverChildren (juce::Graphics& g)
{
    if (juce::isPositiveAndBelow (focusedSlot - 1, rows.size()))
    {
        g.setColour (juce::Colour (0xfff2a33a)); // amber, as on the mockup
        g.drawRoundedRectangle (rows[focusedSlot - 1]->getBounds().toFloat().expanded (1.0f, 0.0f), 3.0f, 1.5f);
    }
}

void ModMatrixDrawer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181a1c));
    g.setColour (Palette::pinstripe);
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);

    // Column headings, aligned with the row layout.
    if (rows.isEmpty())
        return;
    auto r = rows.getFirst()->getBounds().withY (40).withHeight (16);
    g.setFont (TEO5LookAndFeel::panelFont (8.5f));
    g.setColour (Palette::silkscreen.withAlpha (0.6f));
    auto cols = r;
    g.drawText ("SLOT", cols.removeFromLeft (SlotRow::numberWidth), juce::Justification::centredLeft, false);
    g.drawText ("SOURCE", cols.removeFromLeft (SlotRow::sourceWidth).withTrimmedLeft (4), juce::Justification::centredLeft, false);
    cols.removeFromLeft (SlotRow::gap);
    g.drawText ("DESTINATION", cols.removeFromRight (SlotRow::destWidth).withTrimmedLeft (4), juce::Justification::centredLeft, false);
    cols.removeFromRight (SlotRow::gap);
    g.drawText ("AMOUNT", cols, juce::Justification::centred, false);
}

void ModMatrixDrawer::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    r.removeFromRight (2);
    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (160));
    redoButton.setBounds (top.removeFromRight (64).withSizeKeepingCentre (64, 22));
    top.removeFromRight (6);
    undoButton.setBounds (top.removeFromRight (64).withSizeKeepingCentre (64, 22));
    top.removeFromRight (12);
    summary.setBounds (top);

    r.removeFromTop (24); // column headings
    hint.setBounds (r.removeFromBottom (36)); // two lines at the larger size
    const int rowH = juce::jmin (28, r.getHeight() / numSlots);
    for (auto* row : rows)
        row->setBounds (r.removeFromTop (rowH));
}

} // namespace teo5
