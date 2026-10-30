#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <set>
#include "../Library/LibraryModel.h"
#include "../Library/HardwareTransfer.h"
#include "../Midi/MidiSync.h"

namespace teo5
{

// Left-side librarian drawer: banks from the TEO-5 and from disk, search, favourites/tags,
// drag-and-drop reorder, copy/paste, rename, .syx import/export, undo/redo, hardware transfers.
class LibraryDrawer : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    LibraryDrawer (LibraryModel&, HardwareTransfer&, MidiSync&, ProgramState&);
    ~LibraryDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // SELECT encoder: move through the (filtered) programs; loads the highlighted one after a short pause.
    void stepSelection (int delta);
    // VALUE encoder: jump between banks.
    void stepBank (int delta);

    std::function<bool()> isFactoryPanel;                         // FACTORY button state
    std::function<void (const juce::String& line)> onShowMessage; // brief OLED message

    // ---- Used by the list view ----
    struct Row
    {
        bool isBank = false;
        int bank = -1, index = -1;
    };
    // Pane 0 lists the TEO-5's banks (fetched User/Factory banks), pane 1 the user's own banks (imported/custom).
    static constexpr int numPanes = 2;
    const std::vector<Row>& rows (int pane) const { return rowLists[(size_t) pane]; }
    static int paneOf (const LibraryBank& b) { return b.kind == LibraryBank::Kind::Hardware ? 0 : 1; }
    bool isHardwareBank (int bank) const;
    bool isSelected (ProgramRef r) const { return selection.count ({ r.bank, r.index }) > 0; }
    bool isCollapsed (int bank) const;
    LibraryModel& getModel() { return model; }
    ProgramRef loadedProgram() const { return loaded; }

    void rowClicked (int pane, int row, const juce::MouseEvent&);
    void rowDoubleClicked (int pane, int row);
    void rowMenu (int pane, int row);
    void toggleFavourite (int pane, int row);
    void toggleCollapsed (int bank);
    // Dropping into a TEO-5 bank replaces the programs from that slot on (marked unsent); dropping into one
    // of the user's banks inserts copies, or moves programs that already live in the user's banks.
    void dropProgramsAt (int pane, int row, bool below);
    void beginRowDrag (int pane, int row);
    // Banks can be dragged to a new place within their pane.
    void beginBankDrag (int pane, int row);
    void dropBankAt (int pane, int row, bool below);
    // The icons after a program's star: 0 write to TEO-5, 1 export .syx, 2 rename, 3 delete.
    void rowIconClicked (int pane, int row, int icon);
    int dropBankFor (int pane, int row) const;
    int dragCount() const { return (int) selection.size(); }
    void selectPrograms (const juce::Array<ProgramRef>&);
    // Load a library program into the TEO-5 edit buffer (never writes memory); it becomes the "loaded" one.
    void loadToEditBuffer (ProgramRef);

private:
    class ListView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildRows();
    juce::Array<ProgramRef> selectedRefs() const;
    ProgramRef primarySelection() const;
    int currentBank() const;

    void showFetchMenu();
    void importFiles (const juce::StringArray& files, int targetBank);
    void repaintLists();
    void importSyx (int intoBank);
    void exportPrograms (juce::Array<ProgramRef>);
    void exportBank (int bank);
    void addCurrentProgram();
    void writeToLocation (ProgramRef);
    void sendBankToHardware (int bank);
    void renameProgram (ProgramRef);
    void editTags (ProgramRef);
    void renameBank (int bank);
    // allowEmpty: OK with an empty field still calls onOk (e.g. to clear all tags); otherwise it's ignored.
    void askText (const juce::String& title, const juce::String& prompt, const juce::String& initial,
                  std::function<void (const juce::String&)> onOk, bool allowEmpty = false);
    void confirmDelete (juce::Array<ProgramRef>);
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk);
    void setStatus (const juce::String&);

    LibraryModel& model;
    HardwareTransfer& transfer;
    MidiSync& sync;
    ProgramState& programState;

    juce::Label title { {}, "LIBRARY" };
    juce::TextButton fetchButton { "Fetch" }, importButton { "Import" }, addButton { "+ Current" }, newBankButton { "+ Bank" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton pullAllButton { juce::String::fromUTF8 ("\xe2\x86\x93 TEO-5 \xe2\x86\x92 Library (all User banks)") },
                     pushAllButton { juce::String::fromUTF8 ("\xe2\x86\x91 Library \xe2\x86\x92 TEO-5 (all User banks)") };
    void undoOrRedo (bool isUndo);
    void fetchAllUserBanks();   // replaces the library's copies of User 1-16 with the synth's
    void sendAllUserBanks();    // writes the library's User banks to the synth (changes only, or everything)
    void startWrites (std::vector<HardwareTransfer::Write>, juce::StringArray bankUuids);
    juce::TextEditor search;
    juce::ToggleButton favouritesOnly { "Favourites" };
    std::array<std::unique_ptr<ListView>, numPanes> lists;
    std::array<juce::Viewport, numPanes> viewports;
    std::array<juce::Label, numPanes> paneTitles, paneHints;
    juce::Label status;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::TextButton cancelButton { "Cancel" };

    std::array<std::vector<Row>, numPanes> rowLists;
    std::set<std::pair<int, int>> selection;
    int anchorPane = 0, anchorRow = -1;
    std::set<juce::String> expanded; // bank uuids; banks start collapsed
    ProgramRef loaded;
    int pendingLoadTicks = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray sendingBankUuids; // their NOT SENT marks clear once the send completes
    int draggedBank = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryDrawer)
};

} // namespace teo5
