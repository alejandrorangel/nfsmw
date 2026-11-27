#include "EDeliverMessage.hpp"
#include "Speed/Indep/Src/Misc/Hermes.h"

// PC: EDeliverMessage 0x62BA50, ~EDeliverMessage 0x621490,
// EDeliverMessage_MakeEvent_Callback 0x62BB00.
// Hermes::Message::Post queues one of these; the message is copied into the
// event queue and delivered to its port when the event runs.

EDeliverMessage::EDeliverMessage(Hermes::Message *pMessage, UCrc32 pPort)
    : Event(0x10),                                             //
      fMessage(EventManager::EmbedField(this, pMessage)), //
      fPort(pPort) {}

EDeliverMessage::~EDeliverMessage() {
    fMessage->Send(fPort); // mPort = fPort; Deliver()
}

const char *EDeliverMessage::GetEventName() {
    return "EDeliverMessage";
}

void EDeliverMessage_MakeEvent_Callback(const void *staticData) {
    const EDeliverMessage::StaticData *data = static_cast<const EDeliverMessage::StaticData *>(staticData);
    new EDeliverMessage(data->fMessage, data->fPort);
}
