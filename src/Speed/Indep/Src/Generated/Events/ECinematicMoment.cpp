#include "ECinematicMoment.hpp"
#include "Speed/Indep/Src/Camera/CameraAI.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"

// PC: ECinematicMoment 0x621120, ~ECinematicMoment 0x640550,
// ECinematicMoment_MakeEvent_Callback 0x62B430. When the event runs (single
// local player only) it picks the generic ICE camera track to play and cuts
// the player's camera to CDActionIce.
ECinematicMoment::ECinematicMoment(const char *pGenericGroupName, const char *pGenericTrackName, float pEventRadius)
    : Event(0x20), //
      fGenericGroupName(pGenericGroupName), //
      fGenericTrackName(pGenericTrackName), //
      fEventRadius(pEventRadius) {}

ECinematicMoment::~ECinematicMoment() {
    if (IPlayer::Count(PLAYER_ALL) <= 1 && IPlayer::First(PLAYER_LOCAL) != nullptr) {
        TheICEManager.SetGenericCameraToPlay(fGenericGroupName, fGenericTrackName);
        CameraAI::SetAction(EVIEW_PLAYER1, "CDActionIce");
    }
}

const char *ECinematicMoment::GetEventName() {
    return "ECinematicMoment";
}

void ECinematicMoment_MakeEvent_Callback(const void *staticData) {
    // TODO(port): the static data's names are 32-bit pointers in the event
    // data (PORT_PTR32); nothing native fires this event from data yet.
    const ECinematicMoment::StaticData *data = static_cast<const ECinematicMoment::StaticData *>(staticData);
    new ECinematicMoment(data->fGenericGroupName, data->fGenericTrackName, data->fEventRadius);
}
