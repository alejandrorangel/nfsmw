#include "EPlayRaceMovie.hpp"
#include "EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/InGameMovieScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/InGameTutorialScreen.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"

// PC: EPlayRaceMovie 0x62F820, ~EPlayRaceMovie (trivial),
// EPlayRaceMovie_MakeEvent_Callback 0x62F970. Fades out and puts the in-game
// movie screen up (the tutorial screen for the tutorial movies); the screen
// posts MNotifyMovieFinished when it's dismissed.
EPlayRaceMovie::EPlayRaceMovie(const char *pMovieName)
    : Event(0x10), //
      fMovieName(EventManager::EmbedField(this, pMovieName)) {
    new EFadeScreenOn(false);
    if (bStrICmp("drag_tutorial", fMovieName) == 0 || bStrICmp("speedtrap_tutorial", fMovieName) == 0 ||
        bStrICmp("tollbooth_tutorial", fMovieName) == 0 || bStrICmp("pursuit_tutorial", fMovieName) == 0 ||
        bStrICmp("bounty_tutorial", fMovieName) == 0) {
        InGameAnyTutorialScreen::LaunchMovie(fMovieName, nullptr);
    } else {
        InGameAnyMovieScreen::LaunchMovie(fMovieName);
    }
}

EPlayRaceMovie::~EPlayRaceMovie() {}

const char *EPlayRaceMovie::GetEventName() {
    return "EPlayRaceMovie";
}

void EPlayRaceMovie_MakeEvent_Callback(const void *staticData) {
    const EPlayRaceMovie::StaticData *data = static_cast<const EPlayRaceMovie::StaticData *>(staticData);
    new EPlayRaceMovie(data->fMovieName);
}
