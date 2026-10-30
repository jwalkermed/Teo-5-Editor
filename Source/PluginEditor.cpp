#include "PluginEditor.h"
#include "UI/WriteLocationDialog.h"
#include <map>

using teo5::PanelComponent;

TEO5Editor::TEO5Editor (TEO5Processor& p)
    : AudioProcessorEditor (p),
      processor (p),
      panel (p.getAPVTS(), p.getProgramState().name, p.getProgramState().bank, p.getProgramState().program),
      drawer (p.getMidiSync(), p.getProgramState()),
      library (p.getLibrary(), p.getTransfer(), p.getMidiSync(), p.getProgramState()),
      modMatrix (p.getAPVTS()),
      sequencer (p.getMidiSync(), p.getProgramState(), p.getAPVTS(), [&p]
      {
          return teo5::SequencerDrawer::HostPosition { p.hostPlaying.load(), p.hostPpq.load(), p.seqStartPpq.load(),
                                                       p.wrapperType != juce::AudioProcessor::wrapperType_Standalone };
      }),
      globalDrawer (teo5::SettingsDrawer::Kind::Global, p.getMidiSync(), p.getProgramState(), p.getAPVTS()),
      programDrawer (teo5::SettingsDrawer::Kind::Program, p.getMidiSync(), p.getProgramState(), p.getAPVTS())
{
    addAndMakeVisible (panel);
    addChildComponent (drawer);
    addChildComponent (library);
    addChildComponent (modMatrix);
    addChildComponent (sequencer);
    addChildComponent (globalDrawer);
    addChildComponent (programDrawer);
    addAndMakeVisible (drawerTab);
    addAndMakeVisible (libraryTab);
    addAndMakeVisible (modMatrixTab);
    addAndMakeVisible (sequencerTab);
    addAndMakeVisible (globalTab);
    addAndMakeVisible (programTab);
    drawerTab.setTooltip ("MIDI settings");
    libraryTab.setTooltip ("Library");
    modMatrixTab.setTooltip ("Mod matrix");
    sequencerTab.setTooltip ("Sequencer");
    globalTab.setTooltip ("Global settings");
    programTab.setTooltip ("Program settings");
    globalTab.onClick = [this] { setGlobalOpen (! globalDrawer.isVisible()); };
    programTab.onClick = [this] { setProgramOpen (! programDrawer.isVisible()); };
    drawerTab.onClick = [this] { setDrawerOpen (! drawer.isVisible()); };
    libraryTab.onClick = [this] { setLibraryOpen (! library.isVisible()); };
    modMatrixTab.onClick = [this] { setModMatrixOpen (! modMatrix.isVisible()); };
    sequencerTab.onClick = [this] { setSequencerOpen (! sequencer.isVisible()); };

    panel.onPanelAction = [this] (const juce::String& a) { handlePanelAction (a); };
    panel.onControlMenu = [this] (const juce::String& id) { showModulationMenu (id); };
    panel.storedValue = [this] (const juce::String& id) { return processor.getMidiSync().storedValue (id); };
    modMatrix.storedValue = panel.storedValue;
    modMatrix.programVersion = [this] { return processor.getProgramState().sequenceVersion; }; // new program: new history
    // Sequencer header: bank drop-down (32 TEO-5 banks + MY BANKS) and PATCH arrows.
    sequencer.myBanks = [this]
    {
        std::vector<std::pair<int, juce::String>> mine;
        const auto& banks = processor.getLibrary().banks();
        for (int b = 0; b < (int) banks.size(); ++b)
            if (banks[(size_t) b].kind == teo5::LibraryBank::Kind::Disk)
                mine.push_back ({ b, banks[(size_t) b].name });
        return mine;
    };
    sequencer.selectedBank = [this]
    {
        if (browsedBankValid())
            return teo5::SequencerDrawer::firstLibraryBankId + browseLibraryBank;
        const auto& ps = processor.getProgramState();
        return (ps.factory ? 16 : 0) + ps.bank;
    };
    sequencer.onBankChosen = [this] (int id)
    {
        if (id >= teo5::SequencerDrawer::firstLibraryBankId)
        {
            // One of your banks: its programs go to the edit buffer one at a time, starting with the first.
            browseLibraryBank = id - teo5::SequencerDrawer::firstLibraryBankId;
            browseIndex = -1;
            stepLibraryBank (1);
            return;
        }
        browseLibraryBank = -1; // a TEO-5 bank: switch the synth to it at the same program number
        panel.goTo ((id - 1) % 16 + 1, processor.getProgramState().program, id > 16);
    };
    sequencer.onPatchStep = [this] (int delta)
    {
        if (browsedBankValid())
        {
            stepLibraryBank (delta);
            return;
        }
        // A TEO-5 bank: step through its 16 programs only, wrapping within the bank.
        const auto& ps = processor.getProgramState();
        panel.goTo (ps.bank, (ps.program - 1 + delta + 16) % 16 + 1, ps.factory);
    };
    sequencer.locationText = [this]
    {
        if (browsedBankValid() && juce::isPositiveAndBelow (browseIndex, 999))
            return processor.getLibrary().banks()[(size_t) browseLibraryBank].name + " " + juce::String (browseIndex + 1).paddedLeft ('0', 2);
        const auto& ps = processor.getProgramState();
        return (ps.factory ? "Factory " : "User ") + juce::String (ps.bank) + "-" + juce::String (ps.program).paddedLeft ('0', 2);
    };

    // In a DAW whose MIDI clock the TEO-5 follows, the OLED shows the DAW's tempo as EXT BPM.
    panel.externalBpm = [this]() -> std::optional<double>
    {
        if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone && processor.getMidiSync().clockIsExternal()
            && processor.hostBpm.load() > 0.0)
            return processor.hostBpm.load();
        return std::nullopt;
    };

    // The OLED's favourite star is the library's favourite flag for the current program.
    panel.getOled().favourite = [this]
    {
        if (auto* p = processor.getLibrary().program (currentLibraryProgram()))
            return p->favorite ? 1 : 0;
        return -1;
    };
    panel.getOled().onFavouriteClicked = [this]
    {
        auto& lib = processor.getLibrary();
        const auto ref = currentLibraryProgram();
        if (auto* p = lib.program (ref))
            lib.setFavorite (ref, ! p->favorite);
        else
            oledMessage ("FAVOURITE", "Fetch this bank in the library first");
    };
    panel.isActionLit = [this] (const juce::String& a)
    {
        return (a == "compare" && comparing) || (a == "library" && library.isVisible());
    };
    panel.getOled().onDoubleClick = [this] { renameCurrentProgram(); };
    processor.getMidiSync().onRemoteChange = [this] (const juce::String& id) { panel.showParameter (id); };
    processor.getMidiSync().onProgramChanged = [this]
    {
        comparing = false;
        browseLibraryBank = -1; // the synth changed program: the sequencer header follows it
        panel.showProgram (processor.getProgramState().factory);
    };
    processor.getMidiSync().onNameFieldFound = [this] { processor.getTransfer().detectNamesFromLibrary(); };

    panel.showProgram (p.getProgramState().factory); // restore FACTORY + display from the saved state
    library.isFactoryPanel = [this] { return panel.isFactory(); };
    library.onShowMessage = [this] (const juce::String& s) { oledMessage ("LIBRARY", s); };

    const double aspect = (double) PanelComponent::designWidth / (double) PanelComponent::designHeight;
    const int defaultHeight = juce::roundToInt (defaultWidth / aspect);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (defaultWidth * 0.7), juce::roundToInt (defaultHeight * 0.7),
                     juce::roundToInt (defaultWidth * 1.5), juce::roundToInt (defaultHeight * 1.5));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (aspect);

    setSize (defaultWidth, defaultHeight);

    // Arrow keys step through programs while no drawer is open; clicks anywhere bring the focus back here.
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TEO5Editor> (this)]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

TEO5Editor::~TEO5Editor()
{
    processor.getMidiSync().onRemoteChange = nullptr;
    processor.getMidiSync().onProgramChanged = nullptr;
    processor.getMidiSync().onNameFieldFound = nullptr;
}

void TEO5Editor::handlePanelAction (const juce::String& action)
{
    auto& sync = processor.getMidiSync();
    auto& ps = processor.getProgramState();

    if (action == "programChange")
    {
        comparing = false;
        browseLibraryBank = -1; // a TEO-5 location was picked: stop browsing a library bank
        sync.selectProgram (ps.bank, ps.program, panel.isFactory()); // also stores factory in ProgramState
    }
    else if (action == "init")
        initializePreset();
    else if (action == "panic")
    {
        sync.panic();
        oledMessage ("PANIC", "All notes off");
    }
    else if (action == "library")
        setLibraryOpen (! library.isVisible());
    else if (action == "global")
        setGlobalOpen (! globalDrawer.isVisible());
    else if (action == "program")
        setProgramOpen (! programDrawer.isVisible());
    else if (action == "compare")
        toggleCompare();
    else if (action == "write")
        writeCurrentProgram();
    else if (action == "select+" || action == "select-")
        library.stepSelection (action.endsWith ("+") ? 1 : -1);
    else if (action == "value+" || action == "value-")
        library.stepBank (action.endsWith ("+") ? 1 : -1);
    else if (action == "tap")
        tapTempo();
}

// TAP TEMPO has no MIDI message on the TEO-5, so the editor times the taps and sets Clock BPM (NRPN 157).
void TEO5Editor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.empty() && now - tapTimes.back() > 2000.0)
        tapTimes.clear(); // a pause starts a new measurement
    tapTimes.push_back (now);
    if (tapTimes.size() > 5)
        tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() < 2)
    {
        oledMessage ("TAP TEMPO", "Keep tapping");
        return;
    }
    const double interval = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
    const int bpm = juce::jlimit (30, 250, juce::roundToInt (60000.0 / interval));
    processor.setNative ("clockBpm", bpm);
    oledMessage ("TAP TEMPO", juce::String (bpm) + " BPM");
}

void TEO5Editor::oledMessage (const juce::String& title, const juce::String& text)
{
    panel.getOled().showParameter (title, text);
}

// COMPARE, as on the hardware: toggle between the edited program and the program as it was loaded.
void TEO5Editor::toggleCompare()
{
    auto& sync = processor.getMidiSync();
    if (! comparing)
    {
        compareStash = sync.captureCurrent();
        if (compareStash.isEmpty())
        {
            // No program has come from the synth yet: fetch it so a second press works.
            sync.requestEditBuffer();
            oledMessage ("COMPARE", "Reading the program from the TEO-5. Press again");
            return;
        }
        comparing = true;
        sync.loadProgram (processor.getProgramState().raw, true);
        oledMessage ("COMPARE", "Stored program");
    }
    else
    {
        comparing = false;
        sync.loadProgram (compareStash, false);
        oledMessage ("COMPARE", "Your edit");
    }
    panel.repaint();
}

void TEO5Editor::writeCurrentProgram()
{
    auto& sync = processor.getMidiSync();
    auto& ps = processor.getProgramState();
    if (comparing)
        toggleCompare(); // write the edit, not the stored version

    auto raw = sync.captureCurrent();
    if (raw.isEmpty())
    {
        sync.requestEditBuffer(); // no program has come from the synth yet: fetch it so a second press works
        oledMessage ("WRITE", "Reading the program from the TEO-5. Press again");
        return;
    }
    // Ask where to store it, starting at the panel's location (or User 1-01 if FACTORY is lit: factory programs
    // can't be changed).
    const bool factory = panel.isFactory();
    teo5::chooseWriteLocation (this, ps.name, factory ? 1 : ps.bank, factory ? 1 : ps.program, processor.getLibrary(),
                               [this, raw] (int bank, int prog)
                               {
                                   auto& s = processor.getMidiSync();
                                   s.writeProgram (bank, prog, false, raw);
                                   s.setOriginal (raw);
                                   oledMessage ("WRITE", "Stored in " + teo5::HardwareTransfer::locationName ({ bank, false }, prog));
                               });
}

// The library entry for the program on the panel: the one last loaded from the library if it's still the one
// playing, else the fetched TEO-5 bank's program at the panel's location.
teo5::ProgramRef TEO5Editor::currentLibraryProgram() const
{
    auto& lib = processor.getLibrary();
    const auto& ps = processor.getProgramState();
    if (auto ref = library.loadedProgram(); lib.program (ref) != nullptr && lib.program (ref)->name == ps.name)
        return ref;
    for (int b = 0; b < (int) lib.banks().size(); ++b)
    {
        const auto& bank = lib.banks()[(size_t) b];
        if (bank.kind == teo5::LibraryBank::Kind::Hardware && bank.hardwareBank == ps.bank && bank.factory == ps.factory
            && juce::isPositiveAndBelow (ps.program - 1, (int) bank.programs.size()) && ! bank.programs[(size_t) ps.program - 1].isEmpty())
            return { b, ps.program - 1 };
    }
    return {};
}

bool TEO5Editor::browsedBankValid() const
{
    const auto& banks = processor.getLibrary().banks();
    return juce::isPositiveAndBelow (browseLibraryBank, (int) banks.size())
        && banks[(size_t) browseLibraryBank].kind == teo5::LibraryBank::Kind::Disk;
}

// The next (delta = 1) or previous program in the browsed library bank, skipping empty slots; loads it.
void TEO5Editor::stepLibraryBank (int delta)
{
    if (! browsedBankValid())
        return;
    const auto& progs = processor.getLibrary().banks()[(size_t) browseLibraryBank].programs;
    const int n = (int) progs.size();
    int i = browseIndex;
    for (int tries = 0; tries < n; ++tries)
    {
        i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
        if (! progs[(size_t) i].isEmpty())
        {
            browseIndex = i;
            const int keep = browseLibraryBank;
            library.loadToEditBuffer ({ browseLibraryBank, i });
            browseLibraryBank = keep;
            return;
        }
    }
    oledMessage ("BANK", "This bank has no programs");
}

// Loads the TEO-5's Basic Program into the edit buffer, like GLOBAL > Basic Program on the synth.
// It never writes to memory: WRITE stores it.
void TEO5Editor::initializePreset()
{
    const auto raw = teo5::MidiSync::basicProgram();
    if (raw.isEmpty())
    {
        oledMessage ("INIT", "Basic Program not available");
        return;
    }
    if (comparing)
        toggleCompare();
    processor.getMidiSync().loadProgram (raw);
    oledMessage ("INITIALIZE", processor.getProgramState().name.isNotEmpty() ? processor.getProgramState().name
                                                                               : juce::String ("Basic Program"));
}

void TEO5Editor::renameCurrentProgram()
{
    auto& ps = processor.getProgramState();
    auto* w = new juce::AlertWindow ("Rename program", "Program name (up to 20 characters):", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", ps.name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        auto name = w->getTextEditorContents ("name").trim().substring (0, 20);
        if (r == 1 && name.isNotEmpty())
        {
            processor.getProgramState().name = name;
            processor.getMidiSync().sendEditBuffer(); // the name lives in the program data
        }
    }), true);
}

void TEO5Editor::setDrawerOpen (bool shouldBeOpen)
{
    if (shouldBeOpen)
        drawer.refreshPorts();
    setRightDrawerOpen (drawer, drawerTab, shouldBeOpen);
}

void TEO5Editor::setGlobalOpen (bool shouldBeOpen) { setRightDrawerOpen (globalDrawer, globalTab, shouldBeOpen); }
void TEO5Editor::setProgramOpen (bool shouldBeOpen) { setRightDrawerOpen (programDrawer, programTab, shouldBeOpen); }

// One right-hand drawer at a time.
void TEO5Editor::closeRightDrawersExcept (juce::Component& keep)
{
    for (auto [d, tab] : { std::pair<juce::Component*, DrawerTab*> { &drawer, &drawerTab }, { &globalDrawer, &globalTab },
                           { &programDrawer, &programTab } })
        if (d != &keep && d->isVisible())
        {
            d->setVisible (false);
            tab->open = false;
            tab->repaint();
        }
}

void TEO5Editor::setRightDrawerOpen (juce::Component& d, DrawerTab& tab, bool shouldBeOpen)
{
    if (shouldBeOpen)
        closeRightDrawersExcept (d);
    d.setVisible (shouldBeOpen);
    tab.open = shouldBeOpen;
    tab.repaint();
    resized();
}

// One left-hand drawer at a time.
void TEO5Editor::closeLeftDrawersExcept (juce::Component& keep)
{
    if (&keep != &library && library.isVisible()) setLibraryOpen (false);
    if (&keep != &modMatrix && modMatrix.isVisible()) setModMatrixOpen (false);
    if (&keep != &sequencer && sequencer.isVisible()) setSequencerOpen (false);
}

void TEO5Editor::setLibraryOpen (bool shouldBeOpen)
{
    if (shouldBeOpen)
        closeLeftDrawersExcept (library);
    library.setVisible (shouldBeOpen);
    libraryTab.open = shouldBeOpen;
    libraryTab.repaint();
    if (shouldBeOpen)
        library.grabKeyboardFocus();
    resized();
}

namespace
{
// The mod destination a panel control edits (a name from modDestinationNames), or "" if it has none.
juce::String modDestinationFor (const juce::String& paramId)
{
    static const std::map<juce::String, juce::String> map {
        { "masterVolume", "Voice Volume" }, { "overdrive", "Overdrive" }, { "vintage", "Vintage Amount" },
        { "lfo1Freq", "LFO 1 Freq" }, { "lfo2Freq", "LFO 2 Freq" }, { "lfo1Amt", "LFO 1 Amt" }, { "lfo2Amt", "LFO 2 Amt" },
        { "fxTime", "FX Time" }, { "fxMix", "FX Mix" }, { "fxMisc", "FX Misc" },
        { "revMix", "Reverb Mix" }, { "revSize", "Reverb Size" }, { "revPredelay", "Reverb PreDelay" },
        { "revDecay", "Reverb Decay" }, { "revTone", "Reverb Tone" },
        { "osc1Freq", "Osc 1 Frequency" }, { "osc1PW", "Osc 1 Pulse Width" }, { "osc2Freq", "Osc 2 Frequency" },
        { "osc2Detune", "Osc 2 Detune" }, { "xMod", "X-Mod Level" },
        { "resonance", "Resonance" }, { "cutoff", "Cutoff" }, { "filterState", "Filter State" },
        { "osc1On", "Osc 1 Level" }, { "subOn", "Sub Level" }, { "osc2On", "Osc 2 Level" }, { "noiseOn", "Noise" },
    };
    if (auto it = map.find (paramId); it != map.end())
        return it->second;
    for (auto env : { "1", "2" })
        for (auto stage : { "Attack", "Decay", "Sustain", "Release", "Amt" })
            if (paramId == juce::String ("env") + env + stage)
                return juce::String ("Env ") + env + " " + stage;
    return {};
}

// The mod source a panel control belongs to (a name from modSourceNames), or "".
juce::String modSourceFor (const juce::String& paramId)
{
    if (paramId.startsWith ("lfo1")) return "LFO 1";
    if (paramId.startsWith ("lfo2")) return "LFO 2";
    if (paramId.startsWith ("env1")) return "Env 1";
    if (paramId.startsWith ("env2")) return "Env 2";
    if (paramId == "osc2On") return "Osc 2";
    if (paramId == "noiseOn") return "Noise";
    return {};
}
} // namespace

// Right-click on a panel control: modulate it from a source, jump to slots already modulating it, or (for a
// control that belongs to a source) send that source to any destination. A new slot starts at amount none, and
// the mod matrix opens on it so the amount can be set.
void TEO5Editor::showModulationMenu (const juce::String& paramId)
{
    const auto destName = modDestinationFor (paramId), srcName = modSourceFor (paramId);
    if (destName.isEmpty() && srcName.isEmpty())
        return; // no mod destination or source: no menu
    const auto& sources = teo5::modSourceNames();
    const auto& dests = teo5::modDestinationNames();
    const int destIndex = dests.indexOf (destName), srcIndex = sources.indexOf (srcName);
    const int maxDest = teo5::findById ("mod1Dest") != nullptr ? teo5::findById ("mod1Dest")->maxValue : dests.size() - 1;
    const bool full = modMatrix.firstFreeSlot() == 0;

    auto signedAmount = [] (int a) { return (a > 0 ? "+" : "") + juce::String (a); };
    auto newSlot = [this] (int src, int dest) { addModulation (src, dest); };

    juce::PopupMenu menu;
    if (auto* def = teo5::findById (paramId))
        menu.addSectionHeader (def->name);
    if (destIndex > 0)
    {
        juce::PopupMenu from;
        for (int s = 1; s < sources.size(); ++s)
            from.addItem (sources[s], [newSlot, s, destIndex] { newSlot (s, destIndex); });
        menu.addSubMenu ("Modulate " + destName + " from", from, ! full);

        if (const auto used = modMatrix.slotsFor (destIndex); ! used.isEmpty())
        {
            menu.addSeparator();
            menu.addSectionHeader ("Already modulated by");
            for (int slot : used)
                menu.addItem ("Slot " + juce::String (slot) + ": " + sources[modMatrix.sourceOf (slot)] + " ("
                                  + signedAmount (modMatrix.amountOf (slot)) + ")",
                              [this, slot, destName, s = sources[modMatrix.sourceOf (slot)]]
                              {
                                  setModMatrixOpen (true);
                                  modMatrix.focusSlot (slot, "Slot " + juce::String (slot) + ": " + s + " to " + destName + ". Drag the amount.");
                              });
        }
    }
    if (srcIndex > 0)
    {
        // Destinations grouped as in the TEO-5's list (Appendix B).
        static const std::pair<const char*, std::pair<int, int>> groups[] {
            { "Oscillators", { 1, 14 } }, { "Filter", { 15, 17 } }, { "Effects", { 18, 25 } }, { "LFOs", { 26, 31 } },
            { "Envelopes", { 32, 43 } }, { "Voice", { 44, 48 } }, { "Mod amounts", { 49, 64 } },
        };
        juce::PopupMenu to;
        for (auto& [group, range] : groups)
        {
            juce::PopupMenu sub;
            for (int d = range.first; d <= juce::jmin (range.second, maxDest); ++d)
                sub.addItem (dests[d], [newSlot, srcIndex, d] { newSlot (srcIndex, d); });
            if (sub.getNumItems() > 0)
                to.addSubMenu (group, sub);
        }
        menu.addSeparator();
        menu.addSubMenu ("Send " + srcName + " to", to, ! full);
    }
    if (full)
    {
        menu.addSeparator();
        menu.addItem ("All 16 mod slots are in use", false, false, [] {});
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void TEO5Editor::addModulation (int source, int destination)
{
    if (const int slot = modMatrix.assign (source, destination))
    {
        setModMatrixOpen (true);
        modMatrix.focusSlot (slot, "New in slot " + juce::String (slot) + ": " + teo5::modSourceNames()[source] + " to "
                                       + teo5::modDestinationNames()[destination] + ", amount 0 (no change yet). Drag the amount to set it.");
    }
}

void TEO5Editor::setModMatrixOpen (bool shouldBeOpen)
{
    if (shouldBeOpen)
        closeLeftDrawersExcept (modMatrix);
    modMatrix.setVisible (shouldBeOpen);
    modMatrixTab.open = shouldBeOpen;
    modMatrixTab.repaint();
    resized();
}

void TEO5Editor::setSequencerOpen (bool shouldBeOpen)
{
    if (shouldBeOpen)
        closeLeftDrawersExcept (sequencer);
    sequencer.setVisible (shouldBeOpen);
    sequencerTab.open = shouldBeOpen;
    sequencerTab.repaint();
    resized();
}

bool TEO5Editor::anyDrawerOpen() const
{
    for (const juce::Component* d : std::initializer_list<const juce::Component*> { &drawer, &library, &modMatrix, &sequencer,
                                                                                 &globalDrawer, &programDrawer })
        if (d->isVisible())
            return true;
    return false;
}

bool TEO5Editor::keyPressed (const juce::KeyPress& key)
{
    if (anyDrawerOpen() || key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key == juce::KeyPress::leftKey)  { panel.stepBankOrPatch (0, -1); return true; }
    if (key == juce::KeyPress::rightKey) { panel.stepBankOrPatch (0, 1);  return true; }
    if (key == juce::KeyPress::upKey)    { panel.stepBankOrPatch (1, 0);  return true; }
    if (key == juce::KeyPress::downKey)  { panel.stepBankOrPatch (-1, 0); return true; }
    return false;
}

void TEO5Editor::mouseUp (const juce::MouseEvent&)
{
    // After clicks on the panel (or a tab that closed the last drawer), take the focus so the arrow keys work.
    if (! anyDrawerOpen() && ! hasKeyboardFocus (false))
        grabKeyboardFocus();
}

void TEO5Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void TEO5Editor::resized()
{
    const float scale = (float) getWidth() / (float) PanelComponent::designWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));

    const int cheek = juce::roundToInt (17.0f * scale);
    const int drawerW = juce::jmin (340, getWidth() / 3);
    drawer.setBounds (getWidth() - cheek - drawerW, 0, drawerW, getHeight());
    const int settingsW = juce::jmin (380, getWidth() / 3);
    globalDrawer.setBounds (getWidth() - cheek - settingsW, 0, settingsW, getHeight());
    programDrawer.setBounds (globalDrawer.getBounds());

    // Three tabs down the right end cheek.
    {
        const int h[] = { 84, 96, 64 }, gap = 10;
        int y = (getHeight() - (h[0] + h[1] + h[2] + 2 * gap)) / 2;
        for (auto [tab, i] : { std::pair<DrawerTab*, int> { &globalTab, 0 }, { &programTab, 1 }, { &drawerTab, 2 } })
        {
            tab->setBounds (getWidth() - cheek, y, cheek, h[i]);
            y += h[i] + gap;
        }
    }

    const int libraryW = juce::jmin (900, getWidth() * 11 / 20); // two columns: TEO-5 | MY BANKS
    library.setBounds (cheek, 0, libraryW, getHeight());
    const int modMatrixW = juce::jmin (540, getWidth() / 2);
    modMatrix.setBounds (cheek, 0, modMatrixW, getHeight());

    const int sequencerW = juce::jmin (1170, getWidth() - cheek - 40);
    sequencer.setBounds (cheek, 0, sequencerW, getHeight());

    // Three tabs down the left end cheek.
    const int tabH[] = { 104, 124, 124 }, tabGap = 10;
    int y = (getHeight() - (tabH[0] + tabH[1] + tabH[2] + 2 * tabGap)) / 2;
    for (auto [tab, i] : { std::pair<DrawerTab*, int> { &libraryTab, 0 }, { &modMatrixTab, 1 }, { &sequencerTab, 2 } })
    {
        tab->setBounds (0, y, cheek, tabH[i]);
        y += tabH[i] + tabGap;
    }
}

void TEO5Editor::DrawerTab::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f, 0.0f);
    g.setColour (juce::Colour (open ? 0xff2f7fd0 : 0xff1a1b1d));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (juce::Colour (0xff2f7fd0).withAlpha (open ? 1.0f : 0.5f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    g.setColour (juce::Colour (0xffe6e6e6));
    g.setFont (teo5::TEO5LookAndFeel::panelFont (juce::jmin (9.0f, b.getWidth() * 0.62f)));
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, b.getCentreX(), b.getCentreY()));
    g.drawText (text, juce::Rectangle<float> (b.getHeight(), b.getWidth()).withCentre (b.getCentre()),
                juce::Justification::centred, false);
}
