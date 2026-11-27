#include "EAutoSave.hpp"
#include "Speed/Indep/Src/Frontend/MemoryCard/MemoryCard.hpp"

// PC: EAutoSave 0x620920, ~EAutoSave 0x620930, EAutoSave_MakeEvent_Callback
// 0x62AA20. The destructor asks the memory card for an autosave, which starts
// once the HUD is up and no SMS is pending (MemoryCard::Tick).
EAutoSave::EAutoSave() : Event(0x10) {}

EAutoSave::~EAutoSave() {
    if (MemoryCard::GetInstance()->ShouldDoAutoSave(false)) {
        MemoryCard::GetInstance()->RequestAutoSave();
    }
}

const char *EAutoSave::GetEventName() {
    return "EAutoSave";
}

void EAutoSave_MakeEvent_Callback(const void *staticData) {
    new EAutoSave();
}
