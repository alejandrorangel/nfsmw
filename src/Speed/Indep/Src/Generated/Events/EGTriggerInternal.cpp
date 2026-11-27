#include "EGTriggerInternal.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ctor 0x6388F0, EGTriggerInternal_MakeEvent_Callback 0x6389F0. A gameplay
// trigger's world trigger fired for a simable: hand it to the GTrigger (which
// posts MTriggerEnter/Inside/Exit for the scripts). Traffic doesn't trip
// gameplay triggers. The work happens in the constructor, as on PC.

EGTriggerInternal::EGTriggerInternal(unsigned int pTriggerKey, uintptr_t phSimable, int pTriggerStimulus)
    : Event(0x20), fTriggerKey(pTriggerKey), fhSimable(phSimable), fTriggerStimulus(pTriggerStimulus) {
    if (phSimable == 0) {
        return;
    }
    ISimable *isim = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(phSimable));
    if (isim == nullptr) {
        return;
    }
    ITrafficAI *traffic;
    if (isim->QueryInterface(&traffic)) {
        return;
    }
    GTrigger *trigger = static_cast<GTrigger *>(GManager::Get().FindInstance(pTriggerKey));
    if (trigger != nullptr) {
        trigger->NotifySimableTrigger(isim, pTriggerStimulus);
    }
}

EGTriggerInternal::~EGTriggerInternal() {}

const char *EGTriggerInternal::GetEventName() {
    return "EGTriggerInternal";
}

void EGTriggerInternal_MakeEvent_Callback(const void *staticData) {
    new EGTriggerInternal(static_cast<const EGTriggerInternal::StaticData *>(staticData)->fTriggerKey,
                          gEventDynamicData.fhSimable, gEventDynamicData.fTriggerStimulus);
}
