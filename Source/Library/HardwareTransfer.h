#pragma once

#include "LibraryModel.h"
#include "../Midi/MidiSync.h"

namespace teo5
{

// Background transfers between the TEO-5 and the library, one at a time.
class HardwareTransfer : private juce::Thread
{
public:
    HardwareTransfer (MidiSync&, LibraryModel&);
    ~HardwareTransfer() override;

    struct BankAddress { int panelBank; bool factory; };

    // Fetch whole banks (16 programs each) into hardware banks in the library.
    void fetchBanks (juce::Array<BankAddress>);
    // Write programs to consecutive locations in one hardware bank (caller has confirmed with the user).
    // An empty MemoryBlock leaves that slot on the synth as it is.
    void sendBank (std::vector<juce::MemoryBlock> programs, BankAddress target);
    // Write programs to any locations, in order (caller has confirmed with the user).
    struct Write { BankAddress target; int program; juce::MemoryBlock data; };
    void sendWrites (std::vector<Write>);

    // If the name field isn't known yet, find it from programs already in the library (message thread).
    void detectNamesFromLibrary();

    void cancel();
    bool isBusy() const { return isThreadRunning(); }
    float getProgress() const { return progress.load(); }
    juce::String getStatus() const;
    // True when the last bank send went out completely (not cancelled). Valid in onFinished.
    bool lastSendCompleted() const { return sendCompleted.load(); }

    std::function<void()> onFinished; // message thread

    static constexpr int programsPerBank = 16;
    static juce::String bankName (BankAddress);
    static juce::String locationName (BankAddress, int program); // e.g. "U3-07"

private:
    void run() override;
    void setStatus (const juce::String&);

    MidiSync& sync;
    LibraryModel& library;

    enum class Job { None, Fetch, Send } job = Job::None;
    juce::Array<BankAddress> fetchList;
    std::vector<Write> sendList;

    std::atomic<float> progress { 0.0f };
    std::atomic<bool> sendCompleted { false };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true); // guards callbacks posted to the message thread
    mutable juce::CriticalSection statusLock;
    juce::String status;
};

} // namespace teo5
