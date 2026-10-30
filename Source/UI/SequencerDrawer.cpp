// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "SequencerDrawer.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{

namespace
{
const juce::Colour bg (0xff181a1c), panel2 (0xff1e2024), panel3 (0xff26282d), edge (0xff34373e), edgeHi (0xff4a4f59),
                   dim (0xff8d939c), recordColour (0xffffc23d), tieColour (0xff8fd18b), noteBlue (0xff5aa6ff), waitingAmber (0xffef9f27),
                   hatchA (0xff202226), hatchB (0xff2c2e33);

juce::Font mono (float h, bool bold = false) { return juce::Font (juce::FontOptions ("Menlo", h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font panel (float h) { return TEO5LookAndFeel::panelFont (h); }

juce::String noteName (int n)
{
    if (n < 0 || n > 127)
        return "?";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1); // 60 = C4
}

void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour a, juce::Colour b)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (a);
    g.fillRect (r);
    g.setColour (b);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 6.0f)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 2.0f);
}

void label (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float size = 7.5f, juce::Colour c = dim,
            juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (panel (size));
    g.drawText (text, r, j, false);
}

// Step length in beats for the Clock Divide setting (the sequencer steps at the clock division).
double beatsPerStep (int clockDiv)
{
    static const double div[] = { 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return div[juce::jlimit (0, 7, clockDiv)];
}

int paramValue (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
        return p->get();
    return 0;
}

void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, int value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}
} // namespace

// ---- Look: the drawer's buttons and sliders -----------------------------------------------------

class SequencerDrawer::Look : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool led = b.getProperties()["led"], on = b.getToggleState();
        g.setColour (down ? panel3.brighter (0.08f) : panel3);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (on && ! led ? Palette::pinstripe : highlighted ? edgeHi : edge);
        g.drawRoundedRectangle (r, 2.5f, on && ! led ? 1.5f : 1.0f);
        if (led)
        {
            auto l = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
            if (on && ! b.getProperties()["waiting"])
            {
                g.setColour (Palette::ledOn.withAlpha (0.35f));
                g.fillEllipse (l.expanded (3.0f));
            }
            // Amber while PLAY waits for the DAW to start.
            g.setColour (on ? (b.getProperties()["waiting"] ? waitingAmber : Palette::ledOn) : juce::Colour (0xff4a1a10));
            g.fillEllipse (l);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        if (b.getProperties()["led"])
            r.removeFromLeft (16.0f);
        g.setColour (Palette::silkscreen.withAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.setFont (panel (b.getProperties().getWithDefault ("fontSize", b.getButtonText().length() <= 2 ? 9.5f : 7.5f)));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
        g.setColour (juce::Colour (0xff3a3d44));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (noteBlue);
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
        g.fillEllipse (pos - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f);
    }

    int getSliderThumbRadius (juce::Slider&) override { return 6; }
};

// ---- Lanes: two rows of 32 steps, five note lanes each ------------------------------------------

class SequencerDrawer::LanesView : public juce::Component
{
public:
    explicit LanesView (SequencerDrawer& o) : owner (o) {}

    struct Geometry
    {
        float colsX, colW, numH = 15, modeH = 13, laneH, gap = 2, blockH, blockY[2];
    };

    Geometry geometry() const
    {
        Geometry g;
        g.colsX = namesW;
        g.colW = ((float) getWidth() - g.colsX) / 32.0f;
        g.blockH = ((float) getHeight() - blockGap) / 2.0f;
        g.laneH = juce::jmax (11.0f, (g.blockH - g.numH - g.modeH - 2 * g.gap - 4 * g.gap) / 5.0f);
        g.blockY[0] = 0.0f;
        g.blockY[1] = g.blockH + blockGap;
        return g;
    }

    // Step under a point (-1 if none); `ruler` tells whether it's on the step-number row.
    int stepAt (juce::Point<float> p, bool& ruler) const
    {
        const auto g = geometry();
        ruler = false;
        if (p.x < g.colsX)
            return -1;
        const int k = juce::jlimit (0, 31, (int) ((p.x - g.colsX) / g.colW));
        for (int b = 0; b < 2; ++b)
            if (p.y >= g.blockY[b] && p.y < g.blockY[b] + g.blockH)
            {
                ruler = p.y < g.blockY[b] + g.numH + 1;
                return b * 32 + k;
            }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        bool ruler = false;
        const int step = stepAt (e.position, ruler);
        if (e.mods.isPopupMenu())
        {
            // Right-click: select the step under the pointer (unless it's already selected), then the clear menu.
            if (step >= 0 && ! ruler && owner.selected.count (step) == 0)
                owner.selectStep (step, {});
            owner.showStepMenu();
            return;
        }
        if (step < 0)
            return;
        // On a note: select and hear that note. Elsewhere on the step (its number, the mode strip, an empty
        // slot): select the step and hear all its notes. Either sounds while the mouse is held. Dragging along
        // the step numbers sets the length.
        onRuler = ruler;
        const int lane = laneAt (e.position, step);
        if (lane >= 0 && ! e.mods.isAnyModifierKeyDown())
            owner.selectNote (step, lane); // a note, or an empty box to put one in
        else
            owner.selectStep (step, e.mods);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onRuler && ! draggingLength && e.getDistanceFromDragStart() > 4)
        {
            draggingLength = true;
            owner.stopAudition();
        }
        if (! draggingLength)
            return;
        // Follow the pointer along the numbers, even if it drifts into the lanes.
        const auto g = geometry();
        const int block = e.position.y < g.blockY[1] - blockGap * 0.5f ? 0 : 1;
        const int k = juce::jlimit (0, 31, (int) ((e.position.x - g.colsX) / g.colW));
        owner.setLength (block * 32 + k + 1);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingLength = onRuler = false;
        owner.stopAudition();
    }

    // The note lane under a point in a step's column, or -1 (step numbers, mode strip, gaps).
    int laneAt (juce::Point<float> p, int step) const
    {
        const auto g = geometry();
        const float y = g.blockY[step / 32];
        for (int lane = 0; lane < Sequence::numLanes; ++lane)
        {
            const float top = laneY (g, y, lane);
            if (p.y >= top && p.y < top + g.laneH)
                return lane;
        }
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool ruler = false;
        const int step = stepAt (e.position, ruler);
        setMouseCursor (ruler && step >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto geo = geometry();
        const auto& seq = owner.sequence();
        const int length = seq.length;

        for (int block = 0; block < 2; ++block)
        {
            const float y = geo.blockY[block];
            label (g, { 0, y, namesW, geo.numH }, block == 0 ? "STEP 1-32" : "STEP 33-64", 6.5f);
            label (g, { 0, y + geo.numH + geo.gap, namesW, geo.modeH }, "MODE", 6.0f);
            for (int lane = 0; lane < Sequence::numLanes; ++lane)
                label (g, { 0, laneY (geo, y, lane), namesW, geo.laneH }, "NOTE " + juce::String (lane + 1), 6.5f);

            for (int k = 0; k < 32; ++k)
            {
                const int i = block * 32 + k;
                const float cx = geo.colsX + (float) k * geo.colW, cw = geo.colW - 2.0f;
                const bool in = i < length, end = i == length - 1;

                if (k == 16)
                {
                    g.setColour (Palette::pinstripe.withAlpha (0.6f));
                    g.fillRect (cx - 1.5f, y, 1.0f, geo.blockH);
                }

                // Length ruler.
                auto num = juce::Rectangle<float> (cx, y, cw, geo.numH);
                g.setColour (end ? Palette::pinstripe : in ? Palette::pinstripe.withAlpha (0.22f) : juce::Colour (0xff18191c));
                g.fillRoundedRectangle (num, 2.0f);
                g.setColour (end ? juce::Colours::white : in ? Palette::silkscreen : dim);
                g.setFont (mono (cw < 26 ? 8.0f : 9.0f));
                g.drawText (juce::String (i + 1), num, juce::Justification::centred);

                // Mode strip.
                auto mode = juce::Rectangle<float> (cx, y + geo.numH + geo.gap, cw, geo.modeH);
                const auto m = seq.modeOf (i);
                if (m == Sequence::StepMode::Rest && ! allEmpty (seq, i))
                {
                    hatch (g, mode, panel3, edge);
                    label (g, mode, "REST", juce::jmin (8.5f, cw * 0.25f), Palette::silkscreen.withAlpha (0.75f), juce::Justification::centred);
                }
                else if (m == Sequence::StepMode::Tie)
                {
                    g.setColour (tieColour);
                    g.fillRoundedRectangle (mode, 2.0f);
                    label (g, mode, "TIE", juce::jmin (8.5f, cw * 0.25f), juce::Colour (0xff0e1a0d), juce::Justification::centred);
                }

                for (int lane = 0; lane < Sequence::numLanes; ++lane)
                {
                    const juce::Rectangle<float> slotArea { cx, laneY (geo, y, lane), cw, geo.laneH };
                    paintSlot (g, seq, lane, i, slotArea);
                    if (lane == owner.selectedNoteLane() && owner.selection().count (i) && owner.selection().size() == 1)
                    {
                        g.setColour (owner.recordStep() == i ? recordColour : Palette::silkscreen); // the selected note
                        g.drawRoundedRectangle (slotArea.reduced (0.5f), 2.0f, 1.6f);
                    }
                }

                // Out-of-length steps are dimmed but kept.
                if (! in)
                {
                    g.setColour (bg.withAlpha (0.6f));
                    g.fillRect (juce::Rectangle<float> (cx, y + geo.numH + 1, cw, geo.blockH - geo.numH - 1));
                }

                // Step state: selected, next recorded step, playhead, just entered.
                auto col = juce::Rectangle<float> (cx - 1, y + geo.numH + 1, cw + 2, geo.blockH - geo.numH - 1);
                if (i == owner.flashingStep())
                {
                    g.setColour (recordColour.withAlpha (0.25f));
                    g.fillRoundedRectangle (col, 3.0f);
                }
                if (i == owner.recordStep())
                {
                    g.setColour (recordColour.withAlpha (0.12f));
                    g.fillRoundedRectangle (col, 3.0f);
                    g.setColour (recordColour);
                    g.drawRoundedRectangle (col, 3.0f, 1.5f);
                }
                if (owner.selection().count (i))
                {
                    g.setColour (Palette::silkscreen.withAlpha (0.7f));
                    g.drawRoundedRectangle (col, 3.0f, 1.2f);
                }
                if (i == owner.playheadStep())
                {
                    g.setColour (Palette::ledOn);
                    g.drawRoundedRectangle (col, 3.0f, 1.5f);
                }
            }
        }
    }

private:
    static constexpr float namesW = 52.0f, blockGap = 12.0f;

    static float laneY (const Geometry& g, float blockY, int lane)
    {
        return blockY + g.numH + g.gap + g.modeH + g.gap + (float) lane * (g.laneH + g.gap);
    }

    static bool allEmpty (const Sequence& s, int step)
    {
        for (int l = 0; l < Sequence::numLanes; ++l)
            if (! s.isEmpty (l, step))
                return false;
        return true;
    }

    void paintSlot (juce::Graphics& g, const Sequence& seq, int lane, int step, juce::Rectangle<float> slot)
    {
        const bool tiesOut = seq.at (lane, step).tieToNext && step + 1 < Sequence::numSteps;
        const float fontH = juce::jmin (9.5f, slot.getHeight() * 0.55f);
        if (seq.isHeld (lane, step))
        {
            g.setColour (panel2);
            g.fillRoundedRectangle (slot, 2.0f);
            g.setColour (tieColour.withAlpha (0.85f));
            auto bar = slot.withSizeKeepingCentre (slot.getWidth(), 6.0f).withX (slot.getX() - 4.0f);
            g.fillRoundedRectangle (bar.withWidth (slot.getWidth() + (tiesOut ? 6.0f : 4.0f)), 3.0f);
        }
        else if (seq.sounds (lane, step))
        {
            const auto& s = seq.at (lane, step);
            g.setColour (panel2);
            g.fillRoundedRectangle (slot, 2.0f);
            g.setColour (noteBlue.withAlpha ((float) s.velocity / 127.0f));
            g.fillRect (slot.withTop (slot.getBottom() - 3.0f).reduced (1.0f, 0.0f));
            g.setColour (Palette::silkscreen);
            g.setFont (mono (fontH));
            g.drawText (noteName (s.note), slot, juce::Justification::centred);
            if (tiesOut)
            {
                g.setColour (tieColour);
                g.fillRoundedRectangle (juce::Rectangle<float> (slot.getRight() - 5.0f, slot.getCentreY() - 3.0f, 7.0f, 6.0f), 3.0f);
            }
        }
        else if (seq.isMuted (lane, step))
        {
            hatch (g, slot, hatchA, hatchB);
            g.setColour (juce::Colour (0xff6d737c));
            g.setFont (mono (fontH));
            g.drawText (noteName (seq.at (lane, step).note), slot, juce::Justification::centred);
        }
        else
        {
            g.setColour (panel2);
            g.fillRoundedRectangle (slot, 2.0f);
            g.setColour (juce::Colour (0xff474c55));
            g.fillEllipse (slot.getCentreX() - 1.0f, slot.getCentreY() - 1.0f, 2.0f, 2.0f);
        }
    }

    SequencerDrawer& owner;
    bool draggingLength = false;
    bool onRuler = false; // the press started on the step numbers (a drag there sets the length)
};

// ---- One note in the step panel ------------------------------------------------------------------

class SequencerDrawer::NoteRow : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    NoteRow (SequencerDrawer& o, int laneIndex, int stepIndex) : owner (o), lane (laneIndex), step (stepIndex)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Hold the mouse on the note to hear it");
        auto& seq = owner.sequence();
        held = seq.isHeld (lane, step);
        if (held)
            return;
        auto& s = seq.at (lane, step);
        velocity.setSliderStyle (juce::Slider::LinearHorizontal);
        velocity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        velocity.setRange (1.0, 127.0, 1.0);
        velocity.setValue (s.velocity, juce::dontSendNotification);
        velocity.setTitle ("Velocity of " + noteName (s.note));
        // Dragging plays the note at each new velocity on the TEO-5 (at most every 90 ms).
        velocity.onValueChange = [this]
        {
            auto& slot = owner.sequence().at (lane, step);
            slot.velocity = (int) velocity.getValue();
            repaint();
            owner.edited ("Velocity of " + noteName (slot.note) + " in step " + juce::String (step + 1));
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastAudition > 90 && owner.sequence().sounds (lane, step))
            {
                lastAudition = now;
                owner.sync.playNote (slot.note, slot.velocity, 200);
            }
        };
        addAndMakeVisible (velocity);

        mute.setButtonText (seq.isMuted (lane, step) ? "MUTED" : "MUTE");
        mute.setToggleState (seq.isMuted (lane, step), juce::dontSendNotification);
        mute.onClick = [this]
        {
            auto& sq = owner.sequence();
            sq.setMuted (lane, step, ! sq.isMuted (lane, step));
            owner.edited ((sq.isMuted (lane, step) ? "Muted " : "Unmuted ") + noteName (sq.at (lane, step).note) + " in step " + juce::String (step + 1));
            juce::MessageManager::callAsync ([d = juce::Component::SafePointer<SequencerDrawer> (&owner)] { if (d) d->rebuildInspector(); });
        };
        addAndMakeVisible (mute);

        remove.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        remove.setTooltip ("Remove this note");
        remove.onClick = [this]
        {
            owner.sequence().removeNote (lane, step);
            owner.edited ("Removed a note from step " + juce::String (step + 1));
            juce::MessageManager::callAsync ([d = juce::Component::SafePointer<SequencerDrawer> (&owner)] { if (d) d->rebuildInspector(); });
        };
        addAndMakeVisible (remove);
    }

    // Pressing on the note plays it on the TEO-5 for as long as the mouse is held down.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        // A held (tied) slot plays the note it sustains, from the step where it started.
        auto& seq = owner.sequence();
        const int from = held ? seq.heldFrom (lane, step) : step;
        if (from < 0)
            return;
        const auto& s = seq.at (lane, from);
        auditioning = s.note;
        owner.sync.noteOn (s.note, s.velocity);
        // This becomes the selected note (keys then replace it).
        owner.selectedLane = lane;
        owner.lanes->repaint();
        for (auto* row : owner.noteRows)
            row->repaint();
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { stopAudition(); }
    ~NoteRow() override { stopAudition(); }
    void stopAudition()
    {
        if (auditioning >= 0)
            owner.sync.noteOff (std::exchange (auditioning, -1));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& seq = owner.sequence();
        auto r = getLocalBounds().toFloat();
        if (lane == owner.selectedLane)
        {
            g.setColour (Palette::silkscreen.withAlpha (0.6f)); // the selected note
            g.drawRoundedRectangle (r.withWidth (40.0f).reduced (0.5f), 3.0f, 1.2f);
        }
        if (auditioning >= 0)
        {
            g.setColour (Palette::ledOn.withAlpha (0.18f)); // sounding while held
            g.fillRoundedRectangle (r.withWidth (40.0f), 3.0f);
        }
        if (held)
        {
            g.setColour (tieColour);
            g.setFont (mono (12.0f, true));
            g.drawText (noteName (seq.heldNote (lane, step)), r.removeFromLeft (42), juce::Justification::centredLeft);
            g.setColour (Palette::silkscreen.withAlpha (0.8f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Held from the step before (tie)", r, juce::Justification::centredLeft, true);
            return;
        }
        const bool muted = seq.isMuted (lane, step);
        g.setColour (muted ? juce::Colour (0xff6d737c) : Palette::silkscreen);
        g.setFont (mono (12.0f, true));
        g.drawText (noteName (seq.at (lane, step).note), r.removeFromLeft (38), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (mono (11.0f));
        g.drawText (juce::String (seq.at (lane, step).velocity), valueArea.toFloat(), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (38);
        remove.setBounds (r.removeFromRight (22));
        r.removeFromRight (6);
        mute.setBounds (r.removeFromRight (50));
        r.removeFromRight (6);
        valueArea = r.removeFromRight (28);
        r.removeFromRight (8);
        velocity.setBounds (r);
    }

private:
    SequencerDrawer& owner;
    const int lane, step;
    bool held = false;
    int auditioning = -1; // the note sounding while the mouse is held on it
    juce::Slider velocity;
    juce::TextButton mute, remove;
    juce::Rectangle<int> valueArea;
    juce::uint32 lastAudition = 0;
};

// ---- Drawer ----------------------------------------------------------------------------------------

SequencerDrawer::SequencerDrawer (MidiSync& s, ProgramState& ps, juce::AudioProcessorValueTreeState& state,
                                  std::function<HostPosition()> host)
    : sync (s), programState (ps), apvts (state), hostPosition (std::move (host))
{
    look = std::make_unique<Look>();
    setLookAndFeel (look.get());
    setOpaque (true);

    lanes = std::make_unique<LanesView> (*this);
    addAndMakeVisible (*lanes);

    for (auto* b : { &lenMinus, &lenPlus, &playButton, &recButton, &modePlay, &modeRest, &modeTie, &trOctDown, &trDown, &trUp,
                     &trOctUp, &scopeAll, &scopeSel, &octDown, &octUp, &chordButton, &restButton, &tieButton, &backButton, &nextButton })
        addAndMakeVisible (b);
    for (auto* b : { &playButton, &recButton, &chordButton })
        b->getProperties().set ("led", true);

    lenMinus.onClick = [this] { setLength (sequence().length - 1); };
    lenPlus.onClick = [this] { setLength (sequence().length + 1); };
    // PLAY starts/stops the TEO-5's own sequencer (NRPN 4198, not the spec's 4188); with Live's MIDI clock it follows the DAW.
    playButton.onClick = [this] { setParam (apvts, "seqPlay", paramValue (apvts, "seqPlay") ? 0 : 1); updateButtons(); };
    playButton.setTooltip ("Start or stop the TEO-5's sequencer");
    recButton.onClick = [this] { setRecording (! recording); };
    recButton.setTooltip ("Step entry: each key you play goes into the amber step");

    auto setMode = [this] (Sequence::StepMode m)
    {
        for (int st : selected)
            if (! (m == Sequence::StepMode::Tie && st == 0))
                sequence().setStepMode (st, m);
        edited (juce::String (m == Sequence::StepMode::Play ? "PLAY" : m == Sequence::StepMode::Rest ? "REST" : "TIE") + " on " + (selected.size() == 1 ? "step " + juce::String (firstSelected() + 1) : juce::String ((int) selected.size()) + " steps"));
        rebuildInspector();
    };
    modePlay.onClick = [setMode] { setMode (Sequence::StepMode::Play); };
    modeRest.onClick = [setMode] { setMode (Sequence::StepMode::Rest); };
    modeTie.onClick = [setMode] { setMode (Sequence::StepMode::Tie); };
    modeRest.setTooltip ("Silence the step. Its notes are kept, so PLAY brings them back");
    modeTie.setTooltip ("Hold the notes of the step before");

    trOctDown.onClick = [this] { transposeBy (-12); };
    trDown.onClick = [this] { transposeBy (-1); };
    trUp.onClick = [this] { transposeBy (1); };
    trOctUp.onClick = [this] { transposeBy (12); };
    scopeAll.onClick = [this] { transposeSelected = false; transposed = 0; updateButtons(); repaint(); };
    scopeSel.onClick = [this] { transposeSelected = true; transposed = 0; updateButtons(); repaint(); };

    octDown.onClick = [this] { keyboardBase = juce::jmax (24, keyboardBase - 12); resized(); repaint(); };
    octUp.onClick = [this] { keyboardBase = juce::jmin (84, keyboardBase + 12); resized(); repaint(); };
    chordButton.onClick = [this]
    {
        chord = ! chord;
        if (! chord && recording && chordStep == cursor)
            advanceCursor();
        chordStep = -1;
        updateButtons();
        repaint();
    };
    chordButton.setTooltip ("Keys stack on one step to make a chord");
    restButton.onClick = [this] { restPressed(); };
    tieButton.onClick = [this] { tiePressed(); };
    backButton.onClick = [this]
    {
        if (recording)
        {
            cursor = (cursor - 1 + sequence().length) % sequence().length;
            selected = { cursor };
            chordStep = -1;
        }
        else
        {
            selected = { juce::jmax (0, firstSelected() - 1) };
        }
        rebuildInspector();
        lanes->repaint();
        repaint();
    };

    for (auto* b : { &patchPrev, &patchNext })
        addAndMakeVisible (b);
    patchPrev.onClick = [this] { if (onPatchStep) onPatchStep (-1); };
    patchNext.onClick = [this] { if (onPatchStep) onPatchStep (1); };
    patchPrev.setTooltip ("Previous program in this bank");
    patchNext.setTooltip ("Next program in this bank");
    bankBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff141517));
    bankBox.setColour (juce::ComboBox::outlineColourId, Palette::pinstripe.withAlpha (0.6f));
    bankBox.setColour (juce::ComboBox::textColourId, Palette::silkscreen);
    bankBox.setColour (juce::ComboBox::arrowColourId, Palette::pinstripe);
    bankBox.setTooltip ("Bank: a TEO-5 bank (switches the synth) or one of your banks (loads its programs into the edit buffer)");
    bankBox.onChange = [this] { if (onBankChosen && bankBox.getSelectedId() > 0) onBankChosen (bankBox.getSelectedId()); };
    addAndMakeVisible (bankBox);
    refreshBankBox();

    resetButton.onClick = [this]
    {
        // Back to the sequence stored in the program as loaded (or last written).
        if (! SysEx::isProgramSize (programState.raw.getSize()))
            return;
        sequence() = Sequence::read (programState.raw);
        cursor = juce::jmin (cursor, sequence().length - 1);
        selectedLane = -1;
        edited ("Reset to the stored sequence");
        historyMessage = "Reset to the sequence stored in the program (Undo brings your changes back).";
        historyMessageUntil = juce::Time::getMillisecondCounter() + 5000;
        rebuildInspector();
        updateButtons();
    };
    resetButton.setTooltip ("Restore the sequence stored in this program, undoing all changes since it was loaded");
    addAndMakeVisible (resetButton);
    readButton.onClick = [this] { readFromSynth (false); };
    readButton.setTooltip ("Read the sequence that's on the TEO-5 now (e.g. one you recorded on the synth)");
    addAndMakeVisible (readButton);
    for (auto* b : { &readButton, &resetButton, &undoButton, &redoButton })
        b->getProperties().set ("fontSize", 10.5f); // larger than the other small buttons: easier to read
    undoButton.onClick = [this] { undoRedo (true); };
    redoButton.onClick = [this] { undoRedo (false); };
    undoButton.setTooltip ("Undo the last change to the sequence");
    redoButton.setTooltip ("Redo");
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    helpButton.setTooltip ("How the sequencer works");
    helpButton.onClick = [this] { showHelp (! helpPanel.isVisible()); };
    addAndMakeVisible (helpButton);
    helpPanel.close.onClick = [this] { showHelp (false); };
    addChildComponent (helpPanel);

    // NEXT: the step after, without entering anything (BACK's counterpart).
    nextButton.onClick = [this]
    {
        if (recording)
        {
            advanceCursor();
            selected = { cursor };
        }
        else
        {
            selected = { juce::jmin (sequence().length - 1, firstSelected() + 1) };
        }
        rebuildInspector();
        lanes->repaint();
        repaint();
    };
    nextButton.setTooltip ("Move to the next step without entering a note");
    backButton.setTooltip ("Move to the previous step");

    keyboard.setOctaveForMiddleC (4); // 60 = C4, as in the lanes
    keyboard.setScrollButtonsVisible (false);
    keyboard.setVelocity (0.8f, true);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff2f2f0));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff141517));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff222222));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Palette::ledOn.withAlpha (0.18f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Palette::ledOn.withAlpha (0.45f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff6b6b6b));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboardState.addListener (this);
    addAndMakeVisible (keyboard);

    syncFromSynth();
    startTimerHz (30);
}

SequencerDrawer::~SequencerDrawer()
{
    keyboardState.removeListener (this);
    noteRows.clear();
    setLookAndFeel (nullptr);
}

void SequencerDrawer::visibilityChanged()
{
    if (isVisible())
    {
        syncFromSynth();
        // The synth may have a sequence recorded on it since: read it (not over changes still waiting to be sent).
        if (sendTicks == 0 && ! sendPending)
            readFromSynth (true);
    }
}

void SequencerDrawer::readFromSynth (bool quiet)
{
    if (! sync.hasOutput())
        return;
    beforeRead = sequence();
    readPending = true;
    readQuiet = quiet;
    if (! quiet)
    {
        historyMessage = "Reading the sequence from the TEO-5...";
        historyMessageUntil = juce::Time::getMillisecondCounter() + 3000;
        repaint (statusArea);
    }
    sync.requestEditBuffer();
}

// ---- Editing -------------------------------------------------------------------------------------

void SequencerDrawer::edited (const juce::String& what)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (sequence() != stable)
    {
        const bool sameGesture = what == lastEditWhat && now - lastEditTime < 1500 && ! undoStack.empty();
        if (! sameGesture)
        {
            undoStack.push_back ({ stable, what });
            if (undoStack.size() > 200)
                undoStack.erase (undoStack.begin());
        }
        redoStack.clear();
        stable = sequence();
        lastEditWhat = what;
        lastEditTime = now;
    }
    sendTicks = 5; // send ~150 ms after the last change
    updateButtons();
    lanes->repaint();
    repaint (headerArea.getUnion (statusArea));
}

void SequencerDrawer::resetHistory()
{
    undoStack.clear();
    redoStack.clear();
    stable = sequence();
    lastEditWhat.clear();
}

// Undo/Redo: puts the sequence back and says in the status line what was undone or redone.
void SequencerDrawer::undoRedo (bool isUndo)
{
    auto& from = isUndo ? undoStack : redoStack;
    auto& to = isUndo ? redoStack : undoStack;
    if (from.empty())
        return;
    auto [seq, what] = from.back();
    from.pop_back();
    to.push_back ({ sequence(), what });
    sequence() = seq;
    stable = seq;
    lastEditWhat.clear();
    cursor = juce::jmin (cursor, sequence().length - 1);
    selectedLane = -1;
    historyMessage = (isUndo ? "Undo: " : "Redo: ") + what;
    historyMessageUntil = juce::Time::getMillisecondCounter() + 5000;
    sendTicks = 5;
    rebuildInspector();
    updateButtons();
    lanes->repaint();
    repaint();
}

void SequencerDrawer::selectStep (int step, const juce::ModifierKeys& mods, bool audition)
{
    step = juce::jlimit (0, Sequence::numSteps - 1, step);
    selectedLane = -1;
    if (mods.isShiftDown())
    {
        selected.clear();
        for (int k = juce::jmin (anchor, step); k <= juce::jmax (anchor, step); ++k)
            selected.insert (k);
    }
    else if (mods.isCommandDown())
    {
        if (selected.count (step) && selected.size() > 1)
            selected.erase (step);
        else
            selected.insert (step);
    }
    else
    {
        selected = { step };
        anchor = step;
        // Let you hear what's on the step, for as long as the mouse is held.
        if (audition)
        {
            stopAudition();
            auto& seq = sequence();
            for (int l = 0; l < Sequence::numLanes; ++l)
                if (seq.sounds (l, step))
                {
                    sync.noteOn (seq.at (l, step).note, seq.at (l, step).velocity);
                    soundingNotes.push_back (seq.at (l, step).note);
                }
        }
    }
    if (recording)
    {
        cursor = step;
        chordStep = -1;
    }
    rebuildInspector();
    lanes->repaint();
    repaint();
}

void SequencerDrawer::selectNote (int step, int lane)
{
    selectStep (step, {}, false);
    auto& seq = sequence();
    selectedLane = lane; // an empty box too: the next key goes into it
    if (seq.isEmpty (lane, step))
    {
        rebuildInspector();
        lanes->repaint();
        return;
    }
    // Play it (a held slot plays the note it sustains) while the mouse is held.
    stopAudition();
    const int from = seq.isHeld (lane, step) ? seq.heldFrom (lane, step) : step;
    if (from >= 0)
    {
        sync.noteOn (seq.at (lane, from).note, seq.at (lane, from).velocity);
        soundingNotes.push_back (seq.at (lane, from).note);
    }
    rebuildInspector();
    lanes->repaint();
}

void SequencerDrawer::stopAudition()
{
    for (int n : soundingNotes)
        sync.noteOff (n);
    soundingNotes.clear();
}

void SequencerDrawer::setLength (int length)
{
    length = juce::jlimit (1, Sequence::numSteps, length);
    if (length == sequence().length)
        return;
    sequence().length = length;
    cursor = juce::jmin (cursor, length - 1);
    edited ("Length " + juce::String (length));
}

void SequencerDrawer::setRecording (bool shouldRecord)
{
    recording = shouldRecord;
    // Only one records at a time: the editor's REC turns the synth's RECORD off (and the other way round, in
    // timerCallback), so a key is never recorded by both.
    if (recording && paramValue (apvts, "seqRecord"))
    {
        setParam (apvts, "seqRecord", 0);
        historyMessage = "REC is on in the editor, so RECORD on the TEO-5 was turned off.";
        historyMessageUntil = juce::Time::getMillisecondCounter() + 4000;
    }
    if (recording)
        cursor = juce::jmin (firstSelected(), sequence().length - 1);
    chordStep = -1;
    updateButtons();
    lanes->repaint();
    repaint();
}

void SequencerDrawer::showHelp (bool show)
{
    helpPanel.setVisible (show);
    if (show)
    {
        helpPanel.toFront (false);
        helpPanel.text.moveCaretToTop (false);
    }
    helpButton.setToggleState (show, juce::dontSendNotification);
}

namespace
{
const char* const sequencerGuide = R"(THE TEO-5 SEQUENCER
The TEO-5 stores a step sequence in every program: up to 64 steps, each playing up to five notes (a chord). This drawer shows the sequence of the program in the TEO-5's edit buffer and changes it there. Nothing is stored in the synth's memory until you WRITE the program.

AT THE TOP
- Bank drop-down: pick a TEO-5 bank (User 1-16, Factory 1-16) or one of your library banks (MY BANKS). A TEO-5 bank switches the synth; one of your banks loads its programs into the edit buffer.
- PATCH < >: the previous or next program in the chosen bank. It stays in that bank and wraps around.
- LENGTH - / +: the number of steps (1-64). You can also drag along the step numbers to set the length.
- From TEO-5 / Reset / Undo / Redo (right, above the steps): From TEO-5 reads the sequence that's on the synth now (for example one you recorded with the synth's own RECORD button); it also happens by itself whenever you open this drawer. Reset puts back the sequence stored in the program (as loaded, or as last written). Undo and Redo step through your changes (a read from the TEO-5 included), and the line on the left says what was undone or redone. The history starts over when you change program.
- If the line on the left warns that the TEO-5's Clock Mode is In or In Thru, set it to In No S/S in the GLOBAL drawer: otherwise Live's Play starts the sequencer even with PLAY off.
- PLAY: starts and stops the TEO-5's sequencer (the same as PLAY on the synth). With the synth's MIDI Clock set to In No S/S, the sequence runs only while Live plays; PLAY turns amber and the line below says "Waiting for Live". Every time Live starts, the sequence starts again at step 1.
- REC: step entry (see RECORDING below).
- STEP: the step being played, recorded or selected, and the length.

THE STEPS
Steps 1-32 are on the first block, 33-64 on the second. Each step has five note lanes, one note each. A note has a blue bar under it; a green bar is a note held (tied) on from the step before; a hatched step is a REST.
- Click a step's number (or anywhere on the step that isn't a note) to select it and hear all its notes, for as long as you hold the mouse.
- Click a note to select just that note and hear it while you hold. The selected note has an outline. Keys you then play replace that note (see below). You can also click an empty box to select it; the next key goes into that box.
- Shift-click selects a range; Cmd-click adds or removes single steps.
- Right-click: "Copy step" (or the selected steps) copies their notes, ties and rests; "Paste" puts them in from the first selected step on, replacing what's there (a paste past the end makes the sequence longer). Copied steps can be pasted into another program too. "Clear step" removes notes; "Clear all sequencer data" empties every step (it asks first; the length is kept). Undo reverses any of these.
- The playhead (a lit step number) follows Live while the sequence plays.

THE STEP PANEL (bottom left)
Shows the notes of the selected step.
- PLAY / REST / TIE: what the step does. REST silences it (its notes are kept, so PLAY brings them back). TIE holds the notes of the step before through this step.
- Hold the mouse on a note's name to hear it on the TEO-5 for as long as you hold. This also selects the note. A held (tied) note plays the note it sustains.
- Drag a velocity slider to set how hard the note plays; you hear it as you drag.
- MUTE silences one note but keeps it; x removes it.

TRANSPOSE (bottom middle)
Moves notes up or down by a semitone (-1 / +1) or an octave (-OCT / +OCT), for all steps or only the selected ones. Shift-click steps first to transpose a range. The panel's own Transpose setting is separate. Transpose is off while the sequence is playing.

KEYBOARD AND RECORDING (bottom right)
- Clicking a key adds that note to the selected step and plays it. If a single note is selected, the key replaces that note instead.
- OCT - / OCT +: shift the on-screen keyboard's range.
- RECORDING: press REC. The amber box is the step being recorded. The editor's REC and the TEO-5's own RECORD never record at the same time: turning one on turns the other off, so a key is never recorded twice. Each key you play goes into that step and the box moves to the next step. Keys can come from the on-screen keyboard, the DAW track the editor is on, or the TEO-5's own keys.
- CHORD: keys stack on the same step (up to five) instead of moving on; press NEXT (the REST button reads NEXT) to move on. In CHORD mode keys only fill empty boxes and never overwrite notes already there: to replace notes, clear the step (right-click) or select one box and play a key.
- Editing one note while recording: click a note (or an empty box) in the record step to select it; each key then goes into that box and the record box stays on the step. With CHORD on and no box selected, keys fill the empty boxes one by one.
- REST: leaves the step silent and moves on. TIE: holds the notes of the step before and moves on.
- < BACK / NEXT >: move the record box (or the selection) one step back or forward without entering anything.
- You can record while the sequence plays. The TEO-5 also sends the notes its sequencer plays (MIDI Arp Notes); those are recognised and not entered, so only the keys you play are recorded. (A key with exactly the pitch and velocity of a note in the sequence, or the pitch of a note playing around that moment, can't be told apart, so it isn't recorded.)

THE SYNTH'S OWN RECORDING
If you record on the TEO-5 itself (its RECORD button, or RECORD on the editor's panel), the steps fill in here live as you play: the synth reports every change, and the step that just changed is highlighted. Turning RECORD on clears the whole sequence (the synth starts again from step 1), so only what you record remains; when RECORD goes off, the cleared sequence is sent to the TEO-5 too, so no old steps linger past the new length. The whole recording pass, clear included, is one undo step. From TEO-5 reads the whole sequence if you ever need to.

SENDING AND SAVING
Every change goes to the TEO-5's edit buffer straight away, so you hear it at once. While the sequence is playing, a change goes out at the end of the current pass instead (the TEO-5 stops its sequencer when it receives an edit buffer, so the editor sends it just as the loop starts over and restarts playback at step 1); you hear it from the next pass. To keep it, press WRITE on the panel and choose a User location. The sequence is also saved with your Live project.

RECORDING THE SEQUENCE INTO LIVE
Turn on MIDI Arp Notes in the GLOBAL drawer (it also sends the sequencer's notes), make a MIDI track in Live with MIDI From = Network (Teo-5), MIDI To = No Output and Monitor Off, arm it and record while the sequence plays.)";
}

SequencerDrawer::HelpPanel::HelpPanel()
{
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141517));
    text.setColour (juce::TextEditor::outlineColourId, Palette::pinstripe.withAlpha (0.5f));
    // Section headings (lines in capitals) in the panel's lettering, the rest as plain text.
    for (auto& line : juce::StringArray::fromLines (sequencerGuide))
    {
        const auto head = line.upToFirstOccurrenceOf (" (", false, false); // "THE STEP PANEL (bottom left)"
        const bool heading = line.length() < 60 && ! line.startsWith ("-") && head == head.toUpperCase()
                             && head.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        text.setFont (heading ? TEO5LookAndFeel::panelFont (13.0f) : juce::Font (juce::FontOptions (14.0f)));
        text.setColour (juce::TextEditor::textColourId, heading ? Palette::pinstripe.brighter (0.4f) : Palette::silkscreen);
        text.insertTextAtCaret (line + "\n");
    }
    addAndMakeVisible (text);
    addAndMakeVisible (close);
}

void SequencerDrawer::HelpPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff181a1c));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (Palette::pinstripe);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void SequencerDrawer::HelpPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    close.setBounds (r.removeFromBottom (26).removeFromRight (90));
    r.removeFromBottom (6);
    text.setBounds (r);
}

// While the TEO-5's sequencer runs it sends the notes it plays (MIDI Arp Notes), which arrive like keys played
// on the synth. They are compared with the sequence the synth actually has (not the editor's copy, which may
// hold edits not sent yet): a note with the same pitch and (nearly) the same velocity as a note anywhere in that
// sequence, or the same pitch as one in the steps around the playhead, is taken to be the sequencer, not a key.
// Measured: the TEO-5 sends sequence notes 1 higher in velocity than stored (114 -> 115), hence the tolerance.
bool SequencerDrawer::isSequenceNote (int note, int velocity) const
{
    const auto& seq = onSynth;
    auto inStep = [&seq] (int st, int n, int v)
    {
        for (int l = 0; l < Sequence::numLanes; ++l)
            if (seq.sounds (l, st) && seq.at (l, st).note == n && (v < 0 || std::abs (seq.at (l, st).velocity - v) <= 3))
                return true;
        return false;
    };
    for (int st = 0; st < seq.length; ++st)
        if (inStep (st, note, velocity))
            return true;
    if (playhead >= 0)
        for (int d = -1; d <= 1; ++d)
            if (inStep ((playhead + d + seq.length) % seq.length, note, -1))
                return true;
    return false;
}

void SequencerDrawer::showStepMenu()
{
    const int n = (int) selected.size();
    juce::PopupMenu m;
    m.addItem (n == 1 ? "Clear step " + juce::String (firstSelected() + 1) : "Clear " + juce::String (n) + " selected steps", n > 0, false, [this]
    {
        for (int st : selected)
            sequence().clearStep (st);
        edited (selected.size() == 1 ? "Cleared step " + juce::String (firstSelected() + 1) : "Cleared " + juce::String ((int) selected.size()) + " steps");
        rebuildInspector();
        updateButtons();
        lanes->repaint();
    });
    m.addSeparator();
    // Copy / paste whole steps (all five notes, ties and rests). Pasting starts at the first selected step.
    m.addItem (n == 1 ? "Copy step " + juce::String (firstSelected() + 1) : "Copy " + juce::String (n) + " steps", n > 0, false, [this]
    {
        stepClipboard.clear();
        for (int st : selected) // in order; gaps in a Cmd-click selection close up
            stepClipboard.push_back (sequence().copyStep (st));
        historyMessage = "Copied " + juce::String ((int) stepClipboard.size()) + (stepClipboard.size() == 1 ? " step" : " steps");
        historyMessageUntil = juce::Time::getMillisecondCounter() + 3000;
        repaint (statusArea);
    });
    const int k = (int) stepClipboard.size();
    m.addItem (k == 0 ? juce::String ("Paste") : "Paste " + juce::String (k) + (k == 1 ? " step" : " steps") + " at step " + juce::String (firstSelected() + 1),
               k > 0, false, [this]
    {
        auto& seq = sequence();
        const int at = firstSelected();
        int last = at;
        for (int i = 0; i < (int) stepClipboard.size() && at + i < Sequence::numSteps; ++i)
        {
            seq.pasteStep (at + i, stepClipboard[(size_t) i]);
            last = at + i;
        }
        if (last + 1 > seq.length)
            seq.length = last + 1; // pasted past the end: the sequence grows to include it
        selected.clear();
        for (int st = at; st <= last; ++st)
            selected.insert (st);
        const int k2 = last - at + 1;
        edited ("Pasted " + juce::String (k2) + (k2 == 1 ? " step" : " steps") + " at step " + juce::String (at + 1));
        rebuildInspector();
        updateButtons();
        lanes->repaint();
    });
    m.addSeparator();
    m.addItem ("Clear all sequencer data...", [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear the sequence",
            "Remove every note from all 64 steps? The length stays " + juce::String (sequence().length)
                + ".\n\nThis changes the TEO-5's edit buffer only; the program in memory keeps its sequence until you WRITE.",
            "Clear all", "Cancel", this,
            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SequencerDrawer> (this)] (int r)
            {
                if (r != 1 || safe == nullptr)
                    return;
                safe->sequence().clearAll();
                safe->edited ("Cleared the whole sequence");
                safe->rebuildInspector();
                safe->updateButtons();
                safe->lanes->repaint();
            }));
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }));
}

void SequencerDrawer::advanceCursor()
{
    cursor = (cursor + 1) % sequence().length;
    selected = { cursor };
    chordStep = -1;
}

void SequencerDrawer::enterNote (int note, int velocity)
{
    auto& seq = sequence();
    const int target = recording ? cursor : firstSelected();
    lastEnteredStep = target;

    // A selected note in the target step: the key replaces just that note, and the step stays where it is.
    if (selectedLane >= 0 && selected.size() == 1 && firstSelected() == target)
    {
        if (seq.isHeld (selectedLane, target) || seq.at (selectedLane, target).tieToNext)
            seq.removeNote (selectedLane, target); // a held slot becomes a note of its own
        auto& slot = seq.at (selectedLane, target);
        slot.note = juce::jlimit (0, 127, note);
        slot.velocity = juce::jlimit (1, 127, velocity);
        slot.rest = false;
        flashStep = target;
        flashTicks = 12;
        edited ("Changed a note in step " + juce::String (target + 1) + " to " + noteName (note));
        rebuildInspector();
        updateButtons();
        return;
    }

    if (recording)
    {
        // Single notes replace the step (step entry). In CHORD mode notes only go into empty boxes: existing notes
        // are never overwritten (clear the step, or select one box to replace its note).
        if (chord && seq.addNote (cursor, note, velocity, false) < 0)
        {
            historyMessage = "Step " + juce::String (cursor + 1) + " is full. Right-click to clear it, or click a note to replace it.";
            historyMessageUntil = juce::Time::getMillisecondCounter() + 4000;
            repaint (statusArea);
            return;
        }
        if (! chord)
            seq.addNote (cursor, note, velocity, true);
        flashStep = cursor;
        flashTicks = 12;
        selected = { cursor };
        if (chord)
            chordStep = cursor;
        else
            advanceCursor();
    }
    else
    {
        seq.addNote (firstSelected(), note, velocity, false);
        flashStep = firstSelected();
        flashTicks = 12;
    }
    edited ("Entered " + noteName (note) + " in step " + juce::String ((recording ? lastEnteredStep : firstSelected()) + 1));
    rebuildInspector();
    updateButtons();
}

void SequencerDrawer::restPressed()
{
    auto& seq = sequence();
    if (recording)
    {
        if (! (chord && chordStep == cursor)) // after a chord, the button moves on (it reads NEXT)
            seq.setStepMode (cursor, Sequence::StepMode::Rest);
        flashStep = cursor;
        flashTicks = 12;
        advanceCursor();
    }
    else
    {
        for (int st : selected)
            seq.setStepMode (st, Sequence::StepMode::Rest);
    }
    edited (recording ? "REST at step " + juce::String (flashStep + 1) : "REST on the selected steps");
    rebuildInspector();
    updateButtons();
}

void SequencerDrawer::tiePressed()
{
    auto& seq = sequence();
    if (recording)
    {
        if (cursor > 0)
            seq.setStepMode (cursor, Sequence::StepMode::Tie);
        flashStep = cursor;
        flashTicks = 12;
        advanceCursor();
    }
    else
    {
        for (int st : selected)
            if (st > 0)
                seq.setStepMode (st, Sequence::StepMode::Tie);
    }
    edited (recording ? "TIE at step " + juce::String (flashStep + 1) : "TIE on the selected steps");
    rebuildInspector();
    updateButtons();
}

void SequencerDrawer::transposeBy (int semitones)
{
    auto& seq = sequence();
    if (transposeSelected)
        for (int st : selected)
            seq.transpose (st, st, semitones);
    else
        seq.transpose (0, Sequence::numSteps - 1, semitones);
    transposed += semitones;
    const int st = firstSelected();
    for (int l = 0; l < Sequence::numLanes; ++l)
        if (seq.sounds (l, st))
            sync.playNote (seq.at (l, st).note, seq.at (l, st).velocity, 220);
    edited (juce::String ("Transposed ") + (transposeSelected ? "the selected steps " : "") + (semitones > 0 ? "+" : "") + juce::String (semitones));
    rebuildInspector();
    repaint();
}

void SequencerDrawer::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    const int vel = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    sync.playNote (note, vel, 250);
    enterNote (note, vel);
}

// ---- Keeping up with the synth --------------------------------------------------------------------

void SequencerDrawer::syncFromSynth()
{
    const auto& seq = programState.sequence;
    // While the TEO-5 records, show the step it just filled.
    if (paramValue (apvts, "seqRecord") && seenVersion >= 0)
    {
        const auto& previous = lastSeen;
        int changed = -1;
        for (int st = 0; st < Sequence::numSteps; ++st)
            for (int l = 0; l < Sequence::numLanes; ++l)
            {
                const auto& a = seq.at (l, st);
                const auto& b = previous.at (l, st);
                if (a.note != b.note || a.velocity != b.velocity || a.rest != b.rest || a.tieToNext != b.tieToNext)
                    changed = st;
            }
        if (changed >= 0)
        {
            flashStep = changed;
            flashTicks = 20;
            selected = { changed };
            synthRecordStatus = "The TEO-5 recorded step " + juce::String (changed + 1) + "; next is step "
                              + juce::String ((changed + 1) % seq.length + 1) + ".";
        }
    }
    lastSeen = seq;
    onSynth = seq; // what the TEO-5 has now
    if (programState.sequenceVersion != seenVersion && readPending)
    {
        // Our own read (From TEO-5, or opening the drawer): one undo step, and say what changed.
        readPending = false;
        int changed = 0;
        for (int st = 0; st < Sequence::numSteps; ++st)
            for (int l = 0; l < Sequence::numLanes; ++l)
                if (! (seq.at (l, st) == beforeRead.at (l, st)))
                {
                    ++changed;
                    break;
                }
        if (seq != beforeRead)
        {
            undoStack.push_back ({ beforeRead, "Read the sequence from the TEO-5" });
            redoStack.clear();
        }
        stable = seq;
        lastEditWhat.clear();
        if (! readQuiet || changed > 0)
        {
            historyMessage = changed == 0 && seq.length == beforeRead.length
                                 ? juce::String ("Read the sequence from the TEO-5: no changes.")
                                 : "Read the sequence from the TEO-5: " + juce::String (changed) + (changed == 1 ? " step" : " steps")
                                       + " changed (Undo goes back).";
            historyMessageUntil = juce::Time::getMillisecondCounter() + 5000;
        }
    }
    else if (programState.sequenceVersion != seenVersion)
        resetHistory(); // a different program (or the synth's own recording): undo starts over
    seenVersion = programState.sequenceVersion;
    selectedLane = -1;

    for (auto it = selected.begin(); it != selected.end();)
        it = *it >= Sequence::numSteps ? selected.erase (it) : std::next (it);
    if (selected.empty())
        selected = { 0 };
    cursor = juce::jmin (cursor, seq.length - 1);
    rebuildInspector();
    updateButtons();
    lanes->repaint();
    repaint();
}

juce::String SequencerDrawer::headerText() const
{
    const auto where = locationText ? locationText()
                                    : (programState.factory ? "Factory " : "User ") + juce::String (programState.bank) + "-"
                                          + juce::String (programState.program).paddedLeft ('0', 2);
    return programState.name + juce::String::fromUTF8 ("  \xc2\xb7  ") + where;
}

// Bank drop-down and PATCH arrows first, then the program name and location (cut short if space runs out).
void SequencerDrawer::layoutProgramArrows()
{
    constexpr int x0 = 140, boxW = 170; // chosen by the user from a draggable mockup
    bankBox.setBounds (x0, 6, boxW, 22);
    patchPrev.setBounds (bankBox.getRight() + 8, 7, 22, 20);
    patchNext.setBounds (patchPrev.getRight() + 54, 7, 22, 20);
    const float textX = (float) patchNext.getRight() + 12.0f;
    headerTextWidth = juce::jmax (0.0f, juce::jmin (juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.0f), headerText()) + 4.0f,
                                                    (float) getWidth() - 420.0f - textX));
}

// The drop-down lists all 32 TEO-5 banks, then the library's own banks (MY BANKS).
void SequencerDrawer::refreshBankBox()
{
    auto mine = myBanks ? myBanks() : std::vector<std::pair<int, juce::String>> {};
    if (mine != shownMyBanks || bankBox.getNumItems() == 0)
    {
        shownMyBanks = mine;
        bankBox.clear (juce::dontSendNotification);
        bankBox.addSectionHeading ("TEO-5");
        for (int b = 1; b <= 16; ++b)
            bankBox.addItem ("TEO-5 User " + juce::String (b), b);
        for (int b = 1; b <= 16; ++b)
            bankBox.addItem ("TEO-5 Factory " + juce::String (b), 16 + b);
        if (! mine.empty())
        {
            bankBox.addSeparator();
            bankBox.addSectionHeading ("MY BANKS");
            for (auto& [index, name] : mine)
                bankBox.addItem (name, firstLibraryBankId + index);
        }
    }
    const int id = selectedBank ? selectedBank() : 0;
    if (id != bankBox.getSelectedId())
        bankBox.setSelectedId (id, juce::dontSendNotification);
}

void SequencerDrawer::timerCallback()
{
    refreshBankBox();
    if (const auto text = headerText(); text != shownHeader)
    {
        shownHeader = text;
        layoutProgramArrows();
        repaint (headerArea);
    }

    if (programState.sequenceVersion != seenVersion)
        syncFromSynth();

    // Notes from the DAW track or the TEO-5's keys enter steps while recording, but not while the sequence plays:
    // the TEO-5 sends the notes its sequencer plays (MIDI Arp Notes), which would otherwise be entered as if
    // played, moving the record step along. The on-screen keyboard still enters notes.
    {
        const auto now = hostPosition ? hostPosition() : HostPosition();
        hostedCached = now.hosted;
        const bool warn = now.hosted && (sync.clockMode() == 3 || sync.clockMode() == 4);
        if (warn != clockWarning)
        {
            clockWarning = warn;
            repaint (statusArea);
        }
        const bool running = paramValue (apvts, "seqPlay") != 0 && (now.playing || ! now.hosted);
        if (running != sequenceRunning)
        {
            if (sequenceRunning && ! running)
            {
                // Playback stopped: release every pitch the sequence plays, in case the TEO-5 left any on.
                juce::Array<int> pitches;
                for (int st = 0; st < Sequence::numSteps; ++st)
                    for (int l = 0; l < Sequence::numLanes; ++l)
                        if (! onSynth.isEmpty (l, st))
                            pitches.addIfNotAlreadyThere (onSynth.at (l, st).note);
                sync.releaseNotes (pitches);
            }
            sequenceRunning = running;
            updateButtons(); // transpose is off while the sequence plays
            repaint();
        }
    }
    // Notes from the TEO-5 and from the DAW track are both checked: the DAW track may be receiving the TEO-5 too
    // (Live's "All Ins"), so the sequencer's notes can arrive either way. A key arriving from both (within 80 ms)
    // is entered once.
    int note = 0, vel = 0;
    bool fromSynth = false;
    while (sync.popPlayedNote (note, vel, fromSynth))
    {
        if (! recording || ! isShowing())
            continue;
        if (paramValue (apvts, "seqPlay") != 0 && isSequenceNote (note, vel))
            continue;
        const auto now = juce::Time::getMillisecondCounter();
        if (note == lastInputNote && now - lastInputTime < 80)
            continue;
        lastInputNote = note;
        lastInputTime = now;
        enterNote (note, vel);
    }

    // Playhead from the DAW's song position, counted from where the sequence last started at step 1
    // (the TEO-5 follows Live's MIDI clock, and the processor restarts it on every DAW start).
    int ph = -1;
    const auto pos = hostPosition ? hostPosition() : HostPosition();
    const bool seqPlay = paramValue (apvts, "seqPlay") != 0;
    if (pos.playing && seqPlay)
        ph = (int) std::floor (juce::jmax (0.0, pos.ppq - pos.startPpq) / beatsPerStep (paramValue (apvts, "clockDiv")))
             % sequence().length;
    const bool waiting = seqPlay && pos.hosted && ! pos.playing;
    if (waiting != waitingForHost)
    {
        waitingForHost = waiting;
        playButton.getProperties().set ("waiting", waiting);
        playButton.repaint();
        repaint (statusArea);
    }
    if (ph != playhead)
    {
        playhead = ph;
        lanes->repaint();
        repaint (headerArea.getUnion (statusArea));
    }

    if (flashTicks > 0 && --flashTicks == 0)
    {
        flashStep = -1;
        lanes->repaint();
    }

    // Sending the sequence means sending the edit buffer, and the TEO-5 stops its sequencer when one arrives
    // (measured). So while it plays, a change waits for the sequence's last step and goes out together with
    // Play: the sequencer restarts at step 1 just where the loop would start anyway, and the change is heard
    // from the next pass. If the position isn't known (no DAW), it's sent and restarted straight away.
    if (sendTicks > 0 && --sendTicks == 0)
        sendPending = true;
    if (sendPending)
    {
        const int last = sequence().length - 1;
        if (! sequenceRunning)
        {
            sync.sendSequence();
            onSynth = sequence();
            sendPending = false;
        }
        else if (playhead < 0 || (playhead == last && lastSeenPlayhead != last))
        {
            sync.sendSequence();
            onSynth = sequence();
            sync.resumeSequencer();
            sendPending = false;
        }
        repaint (statusArea);
    }
    lastSeenPlayhead = playhead;

    // The synth's own recording, note by note: show it (and the step just recorded) without resetting the history.
    if (programState.liveSequenceVersion != seenLiveVersion)
    {
        seenLiveVersion = programState.liveSequenceVersion;
        const auto& seq = sequence();
        for (int st = 0; st < Sequence::numSteps; ++st)
            for (int l = 0; l < Sequence::numLanes; ++l)
                if (! (seq.at (l, st) == lastSeen.at (l, st)) && ! seq.isEmpty (l, st)) // not the clear at RECORD
                {
                    flashStep = st;
                    flashTicks = 20;
                    selected = { st };
                    synthRecordStatus = "The TEO-5 recorded step " + juce::String (st + 1) + ".";
                }
        lastSeen = seq;
        onSynth = seq;
        stable = seq;
        cursor = juce::jmin (cursor, seq.length - 1);
        rebuildInspector();
        updateButtons();
        lanes->repaint();
        repaint();
    }

    // RECORD turned on at the synth (or the panel): the editor's REC goes off, so only the synth records.
    {
        const bool synthRecording = paramValue (apvts, "seqRecord") != 0;
        // A recording pass on the synth (which starts by clearing the sequence, see MidiSync) is one undo step.
        sync.followSynthRecording();
        if (synthRecording && ! synthWasRecording)
        {
            historyMessage = "RECORD is on at the TEO-5: the sequence was cleared, and the steps fill in here as you record. Undo brings the old one back.";
            historyMessageUntil = juce::Time::getMillisecondCounter() + 5000;
        }
        const auto& before = programState.beforeSynthRecording;
        if (! synthRecording && synthWasRecording && before != sequence())
        {
            undoStack.push_back ({ before, "Recorded on the TEO-5" });
            redoStack.clear();
            stable = sequence();
            updateButtons();
        }
        if (synthRecording && ! synthWasRecording && recording)
        {
            setRecording (false);
            historyMessage = "RECORD is on at the TEO-5, so the editor's REC was turned off and the sequence was cleared. The steps fill in here as you record.";
            historyMessageUntil = juce::Time::getMillisecondCounter() + 5000;
        }
        synthWasRecording = synthRecording;
    }

    // While the TEO-5 itself records, it sends each changed sequence byte as an NRPN (MidiSync follows them),
    // so the steps fill in live; nothing needs to be read back. RECORD on the synth arrives as NRPN 4199.
    if (! paramValue (apvts, "seqRecord"))
        synthRecordStatus.clear();

    if (playButton.getToggleState() != (paramValue (apvts, "seqPlay") != 0))
        updateButtons();
}

// ---- Layout and painting --------------------------------------------------------------------------

void SequencerDrawer::rebuildInspector()
{
    noteRows.clear();
    const int st = firstSelected();
    auto& seq = sequence();
    if (selected.size() == 1)
        for (int l = 0; l < Sequence::numLanes; ++l)
            if (! seq.isEmpty (l, st))
                addAndMakeVisible (noteRows.add (new NoteRow (*this, l, st)));
    updateButtons();
    resized();
    repaint (inspectorBox);
}

void SequencerDrawer::updateButtons()
{
    const auto mode = sequence().modeOf (firstSelected());
    const bool single = selected.size() == 1;
    modePlay.setToggleState (single && mode == Sequence::StepMode::Play, juce::dontSendNotification);
    modeRest.setToggleState (single && mode == Sequence::StepMode::Rest, juce::dontSendNotification);
    modeTie.setToggleState (single && mode == Sequence::StepMode::Tie, juce::dontSendNotification);
    modeTie.setEnabled (! (single && firstSelected() == 0));
    playButton.setToggleState (paramValue (apvts, "seqPlay") != 0, juce::dontSendNotification);
    recButton.setToggleState (recording, juce::dontSendNotification);
    chordButton.setToggleState (chord, juce::dontSendNotification);
    scopeAll.setToggleState (! transposeSelected, juce::dontSendNotification);
    scopeSel.setToggleState (transposeSelected, juce::dontSendNotification);
    restButton.setButtonText (recording && chord && chordStep == cursor ? "NEXT" : "REST");
    lenMinus.setEnabled (sequence().length > 1);
    // Transposing while the sequence plays would stop it (the TEO-5 stops on every new edit buffer), so it's off then.
    for (auto* b : { &trOctDown, &trDown, &trUp, &trOctUp })
    {
        b->setEnabled (! sequenceRunning);
        b->setTooltip (sequenceRunning ? "Transpose is off while the sequence plays" : juce::String());
    }
    undoButton.setEnabled (! undoStack.empty());
    resetButton.setEnabled (SysEx::isProgramSize (programState.raw.getSize()) && Sequence::read (programState.raw) != sequence());
    redoButton.setEnabled (! redoStack.empty());
    undoButton.setTooltip (undoStack.empty() ? "Undo" : "Undo: " + undoStack.back().second);
    redoButton.setTooltip (redoStack.empty() ? "Redo" : "Redo: " + redoStack.back().second);
    lenPlus.setEnabled (sequence().length < Sequence::numSteps);
}

juce::String SequencerDrawer::statusText() const
{
    if (historyMessage.isNotEmpty() && juce::Time::getMillisecondCounter() < historyMessageUntil)
        return historyMessage; // what Undo/Redo just did
    if (synthRecordStatus.isNotEmpty())
        return synthRecordStatus;
    // In a DAW, the TEO-5 must ignore the DAW's Start/Stop, or Live's Play starts the sequencer whatever PLAY says.
    if (clockWarning)
        return juce::String ("The TEO-5's Clock Mode is ") + (sync.clockMode() == 3 ? "In" : "In Thru")
             + ": Live's Play starts the sequencer even with PLAY off. Set it to In No S/S in GLOBAL.";
    if (sendPending && sequenceRunning)
        return "Your change goes to the TEO-5 at the end of this pass, so playback doesn't stop.";
    if (recording && sequenceRunning)
        return "Recording at step " + juce::String (cursor + 1) + " while the sequence plays. Keys you play are entered; notes "
               "the sequence itself is playing are not.";
    if (recording)
        return "Recording at step " + juce::String (cursor + 1) + ". Play a key here, on the DAW track or on the TEO-5. "
             + (chord ? "Keys stack into a chord; NEXT moves on." : "REST skips a step, TIE stretches the last note.");
    if (playhead >= 0)
        return "Playing step " + juce::String (playhead + 1) + " of " + juce::String (sequence().length) + ", following the DAW.";
    if (waitingForHost)
        return "Waiting for Live. The sequence starts from step 1 when Live plays.";
    // With MIDI Clock In, the TEO-5's sequencer only runs while clock arrives (the editor can't see the synth's clock setting).
    if (paramValue (apvts, "seqPlay"))
        return "PLAY is on. If the TEO-5's MIDI Clock is set to In, it plays only while Live is playing and sending clock "
               "to the TEO-5's port (Live: Settings > Link, Tempo & MIDI > Sync).";
    return "Length " + juce::String (sequence().length) + " steps. Click a step to edit it; drag along the step numbers to change the length.";
}

void SequencerDrawer::resized()
{
    const int w = getWidth(), h = getHeight();
    headerArea = { 0, 0, w, 35 };
    statusArea = { 0, 35, w, 22 };

    layoutProgramArrows();
    helpButton.setBounds (10, 7, 20, 20);
    redoButton.setBounds (w - 80, 36, 68, 21);   // top right, above the steps
    undoButton.setBounds (w - 152, 36, 68, 21);
    resetButton.setBounds (w - 224, 36, 68, 21);
    readButton.setBounds (w - 336, 36, 108, 21);
    helpPanel.setBounds (getLocalBounds().withTrimmedTop (35).reduced (12, 6));
    recButton.setBounds (w - 172, 5, 58, 24);
    playButton.setBounds (w - 240, 5, 60, 24);
    lenPlus.setBounds (w - 280, 5, 26, 24);
    lenMinus.setBounds (w - 342, 5, 26, 24);

    const int bottomH = juce::jlimit (150, 230, juce::roundToInt (h * 0.36));
    const int top = h - bottomH - 10;
    lanes->setBounds (12, 60, w - 26, top - 16 - 60);

    inspectorBox = { 12, top, 330, bottomH };
    transposeBox = { inspectorBox.getRight() + 12, top, 282, bottomH };
    keyboardBox = { transposeBox.getRight() + 12, top, w - 14 - (transposeBox.getRight() + 12), bottomH };

    // Step panel.
    auto r = inspectorBox.reduced (10, 8);
    r.removeFromTop (22);
    auto seg = r.removeFromTop (22);
    modePlay.setBounds (seg.removeFromLeft (60)); seg.removeFromLeft (6);
    modeRest.setBounds (seg.removeFromLeft (60)); seg.removeFromLeft (6);
    modeTie.setBounds (seg.removeFromLeft (52));
    r.removeFromTop (8);
    r.removeFromBottom (34); // two lines of hints (rows shrink a little to make room, never below 16 px)
    const int rowH = juce::jlimit (16, 22, r.getHeight() / juce::jmax (1, noteRows.size()) - 3);
    for (auto* row : noteRows)
    {
        row->setBounds (r.removeFromTop (rowH));
        r.removeFromTop (3);
    }

    // Transpose.
    r = transposeBox.reduced (10, 8);
    r.removeFromTop (24);
    auto row = r.removeFromTop (24);
    trOctDown.setBounds (row.removeFromLeft (48)); row.removeFromLeft (5);
    trDown.setBounds (row.removeFromLeft (34)); row.removeFromLeft (5);
    row.removeFromLeft (64); row.removeFromLeft (5);
    trUp.setBounds (row.removeFromLeft (34)); row.removeFromLeft (5);
    trOctUp.setBounds (row.removeFromLeft (48));
    r.removeFromTop (8);
    row = r.removeFromTop (24);
    scopeAll.setBounds (row.removeFromLeft (122)); row.removeFromLeft (8);
    scopeSel.setBounds (row.removeFromLeft (122));

    // Keyboard and entry buttons.
    r = keyboardBox.reduced (10, 8);
    auto bar = r.removeFromTop (22);
    bar.removeFromLeft (96);
    auto place = [&bar] (juce::Button& b, int bw) { b.setBounds (bar.removeFromRight (bw)); bar.removeFromRight (4); };
    place (nextButton, 50);
    place (backButton, 50);
    place (tieButton, 32);
    place (restButton, 40);
    place (chordButton, 56);
    place (octUp, 38);
    bar.removeFromRight (46); // range label
    place (octDown, 38);
    r.removeFromTop (8);
    r.removeFromBottom (22);
    keyboard.setAvailableRange (keyboardBase, keyboardBase + 24);
    keyboard.setLowestVisibleKey (keyboardBase);
    keyboard.setKeyWidth ((float) r.getWidth() / 15.0f);
    keyboard.setBounds (r);
}

void SequencerDrawer::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    g.fillAll (bg);
    g.setColour (Palette::pinstripe);
    g.fillRect (w - 2.0f, 0.0f, 2.0f, (float) getHeight());

    // Header.
    label (g, { 36, 6, 104, 22 }, "SEQUENCER", 13.0f, Palette::silkscreen);
    g.setColour (dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (headerText(), juce::Rectangle<float> ((float) patchNext.getRight() + 12.0f, 6, headerTextWidth, 22),
                juce::Justification::centredLeft, true);
    label (g, juce::Rectangle<float> ((float) patchPrev.getRight(), 5, (float) (patchNext.getX() - patchPrev.getRight()), 24),
           "PATCH", 9.0f, dim, juce::Justification::centred);
    label (g, { w - 418, 5, 70, 24 }, "LENGTH", 9.5f, dim, juce::Justification::centredRight); // right next to the - button
    g.setColour (Palette::silkscreen);
    g.setFont (mono (14.0f, true));
    g.drawText (juce::String (sequence().length), juce::Rectangle<float> (w - 314, 5, 32, 24), juce::Justification::centred);

    auto readout = juce::Rectangle<float> (w - 106, 5, 92, 24); // sized to its contents
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (readout, 2.5f);
    g.setColour (juce::Colour (0xff2a1410));
    g.drawRoundedRectangle (readout, 2.5f, 1.0f);
    label (g, readout.withWidth (32).translated (6, 0), "STEP", 8.5f, juce::Colour (0xffc0705a));
    const int shown = recording ? cursor : playhead >= 0 ? playhead : firstSelected();
    g.setColour (Palette::ledOn);
    g.setFont (mono (17.0f, true));
    g.drawText (juce::String (shown + 1), readout.withTrimmedLeft (38).withWidth (22), juce::Justification::centredRight);
    label (g, readout.withTrimmedLeft (63), "/ " + juce::String (sequence().length), 8.5f, juce::Colour (0xffc0705a));

    g.setColour (Palette::pinstripe);
    g.fillRect (12.0f, 34.0f, w - 26.0f, 1.0f);

    // Status line.
    const bool synthRec = synthRecordStatus.isNotEmpty() || paramValue (apvts, "seqRecord");
    g.setColour (recording || synthRec ? Palette::ledOn : playhead >= 0 ? juce::Colour (0xff38c172)
                 : waitingForHost || clockWarning ? waitingAmber : dim);
    g.fillEllipse (14, 42, 7, 7);
    g.setColour (Palette::silkscreen);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (synthRec && synthRecordStatus.isEmpty() ? juce::String ("The TEO-5 is recording: steps you enter on the synth show up here.")
                                                       : statusText(),
                juce::Rectangle<float> (28, 37, w - 370, 17), juce::Justification::centredLeft, true);

    // Bottom boxes.
    g.setColour (Palette::pinstripe);
    g.fillRect (12.0f, (float) inspectorBox.getY() - 6.0f, w - 26.0f, 1.0f);
    for (auto b : { inspectorBox, transposeBox, keyboardBox })
    {
        g.setColour (panel2);
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }

    // Step panel heading and hint.
    auto r = inspectorBox.reduced (10, 8).toFloat();
    auto head = r.removeFromTop (16);
    const int first = firstSelected(), last = selected.empty() ? 0 : *selected.rbegin();
    label (g, head, selected.size() > 1 ? "STEPS " + juce::String (first + 1) + "-" + juce::String (last + 1) : "STEP " + juce::String (first + 1),
           9.0f, Palette::silkscreen);
    juce::String hint;
    if (selected.size() > 1)
    {
        label (g, head, juce::String ((int) selected.size()) + " SELECTED", 7.0f, dim, juce::Justification::centredRight);
        hint = "PLAY, REST and TIE apply to every selected step.";
    }
    else
    {
        int count = 0;
        for (int l = 0; l < Sequence::numLanes; ++l)
            count += sequence().sounds (l, first) ? 1 : 0;
        label (g, head, juce::String (count) + "/5 NOTES", 9.0f, dim, juce::Justification::centredRight);
        hint = noteRows.isEmpty() ? "No notes. Play a key to add one."
             : selectedLane >= 0 ? "A note is selected: keys replace it. Click the step number for the whole step."
                                 : "Hold a note's name to hear it; click it to select it.\nDrag a slider to set the velocity.";
    }
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (hint, r.removeFromBottom (32).toNearestInt(), juce::Justification::bottomLeft, 2, 1.0f);

    // Transpose heading, amount and hint.
    r = transposeBox.reduced (10, 8).toFloat();
    head = r.removeFromTop (16);
    label (g, head, "TRANSPOSE", 9.0f, Palette::silkscreen);
    label (g, head, transposeSelected ? juce::String ((int) selected.size()) + (selected.size() == 1 ? " STEP" : " STEPS") : "WHOLE SEQUENCE",
           9.0f, dim, juce::Justification::centredRight);
    g.setColour (Palette::silkscreen);
    g.setFont (mono (13.0f, true));
    g.drawText ((transposed > 0 ? "+" : "") + juce::String (transposed) + " st",
                juce::Rectangle<float> ((float) trDown.getRight() + 5, (float) trDown.getY(), 64, 24), juce::Justification::centred);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (sequenceRunning ? "Transpose is off while the sequence plays: changing the notes would stop playback. Stop the sequence to transpose."
                                      : "Moves the notes themselves. Shift-click steps to transpose a range; the panel's Transpose setting is separate.",
                      juce::Rectangle<int> (transposeBox.getX() + 10, scopeAll.getBottom() + 10, transposeBox.getWidth() - 20,
                                            transposeBox.getBottom() - scopeAll.getBottom() - 16),
                      juce::Justification::topLeft, 4);

    // Keyboard heading, range and hint.
    r = keyboardBox.reduced (10, 8).toFloat();
    // "KEYBOARD" turns red while recording (there's no room for more at a readable size).
    label (g, r.removeFromTop (22).withWidth (96), "KEYBOARD", 9.5f, recording ? Palette::ledOn : dim);
    label (g, juce::Rectangle<float> ((float) octDown.getRight() + 2, (float) octDown.getY(), 44, 22),
           noteName (keyboardBase) + "-" + noteName (keyboardBase + 24), 8.5f, dim, juce::Justification::centred);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (recording ? "Each key enters a note at the amber step and moves on. Notes from your MIDI keyboard work too."
                          : "Keys add notes to the selected step (up to 5) and play them on the TEO-5.",
                juce::Rectangle<float> ((float) keyboardBox.getX() + 10, (float) keyboard.getBottom() + 4, (float) keyboardBox.getWidth() - 20, 16),
                juce::Justification::centredLeft, true);
}

} // namespace teo5
