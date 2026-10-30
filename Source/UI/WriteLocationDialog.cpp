#include "WriteLocationDialog.h"
#include "../Library/HardwareTransfer.h"

namespace teo5
{

juce::String libraryNameAt (const LibraryModel& library, int bank, int slot)
{
    for (auto& b : library.banks())
        if (b.kind == LibraryBank::Kind::Hardware && ! b.factory && b.hardwareBank == bank
            && juce::isPositiveAndBelow (slot - 1, (int) b.programs.size()))
            return b.programs[(size_t) slot - 1].name;
    return {};
}

void chooseWriteLocation (juce::Component* parent, const juce::String& programName, int bank, int slot,
                          const LibraryModel& library, std::function<void (int bank, int slot)> chosen)
{
    auto* w = new juce::AlertWindow ("Write to TEO-5 memory", "Store \"" + programName + "\" in:",
                                     juce::MessageBoxIconType::WarningIcon, parent);
    juce::StringArray banks, slots;
    for (int i = 1; i <= 16; ++i)
    {
        banks.add ("User bank " + juce::String (i));
        slots.add ("Program " + juce::String (i).paddedLeft ('0', 2));
    }
    w->addComboBox ("bank", banks, "Bank");
    w->addComboBox ("slot", slots, "Program");
    auto* bankBox = w->getComboBoxComponent ("bank");
    auto* slotBox = w->getComboBoxComponent ("slot");
    bankBox->setSelectedItemIndex (juce::jlimit (1, 16, bank) - 1, juce::dontSendNotification);
    slotBox->setSelectedItemIndex (juce::jlimit (1, 16, slot) - 1, juce::dontSendNotification);

    // What's there now, as far as the library knows.
    auto occupant = std::make_shared<juce::Label>();
    occupant->setSize (360, 40);
    occupant->setJustificationType (juce::Justification::centredLeft);
    auto update = [occupant, bankBox, slotBox, &library]
    {
        const int b = bankBox->getSelectedItemIndex() + 1, s = slotBox->getSelectedItemIndex() + 1;
        const auto name = libraryNameAt (library, b, s);
        occupant->setText (name.isNotEmpty() ? "Replaces \"" + name + "\" (" + HardwareTransfer::locationName ({ b, false }, s) + ")"
                                             : "Replaces whatever is stored in " + HardwareTransfer::locationName ({ b, false }, s)
                                                   + " (that bank hasn't been fetched)",
                           juce::dontSendNotification);
    };
    bankBox->onChange = update;
    slotBox->onChange = update;
    update();
    w->addCustomComponent (occupant.get());

    w->addButton ("Overwrite", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, occupant, onChosen = std::move (chosen)] (int result)
    {
        if (result == 1 && onChosen)
            onChosen (w->getComboBoxComponent ("bank")->getSelectedItemIndex() + 1,
                      w->getComboBoxComponent ("slot")->getSelectedItemIndex() + 1);
    }), true);
}

} // namespace teo5
