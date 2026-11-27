#ifndef LUA_LUAGAMEHOOKS_H
#define LUA_LUAGAMEHOOKS_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

// The game functions bound into Lua by LuaBindery::BindToGameCode (PC
// 0x61E750); defined in LuaGameHooks.cpp. The comment is the Lua name.

struct lua_State;
class GRuntimeInstance;
class ISimable;

int Table_Randomize(lua_State *luaState);                       // Table.Randomize
void Activity_Run(GRuntimeInstance *activity);                  // Activity.Run
void Activity_Suspend(GRuntimeInstance *activity);              // Activity.Suspend
void Audio_SetFlag(const char *flag, bool set);                 // Audio.SetFlag
bool Audio_IsCopSpeechPlaying();                                // Audio.IsCopSpeechPlaying
void MiniMap_AddEngagedRace(GRuntimeInstance *activity);        // MiniMap.AddEngagedRace
int Bin_GetNumChallengesPassed(int bin);                        // Bin.GetNumChallengesPassed
int Bin_GetNumRacesWon(int bin);                                // Bin.GetNumRacesWon
int Math_RandomInt(int range);                                  // Math.RandomInt
void Camera_SetGenericCamera(const char *name, const char *sub); // Camera.SetGenericCamera
void NIS_Play(GRuntimeInstance *location, const char *nisName, const char *type, int flags, const char *cameraTrack, const char *shell); // NIS.Play
void Movie_PlayHackE3FMV();                                     // Movie.PlayHackE3FMV
void Debug_Print(const char *text);                             // Debug.Print
void Debug_PrintInstance(GRuntimeInstance *instance);           // Debug.PrintInstance
void Debug_ShowScreenMessage(const char *text, float time);     // Debug.ShowScreenMessage
void Debug_Assert(bool condition, const char *text);            // Debug.Assert
void Demo_SetRaceCompleteForFE(GRuntimeInstance *activity);     // Demo.SetRaceCompleteForFE
void Demo_StorePursuitRepForFE(GRuntimeInstance *activity);     // Demo.StorePursuitRepForFE
void HUD_ShowMessage(const char *text);                         // HUD.ShowMessage
void HUD_ShowTimeExtension(float seconds);                      // HUD.ShowTimeExtension
bool Platform_IsNextGen();                                      // Platform.IsNextGen

void Game_SabotageEngine(ISimable *racer, float time);
void Game_BlowEngine(ISimable *racer);
void Game_DetachCameraFromRacer(ISimable *racer);
void Game_ChallengeComplete(ISimable *racer);
void Game_ClearAIControl(int playerIndex);
void Game_ForceAIControl(int playerIndex);
void Game_InitRacers(GRuntimeInstance *startMarker);
void Game_WarpPlayerToTrigger(GRuntimeInstance *trigger);
void Game_ResetTrigger(GRuntimeInstance *trigger);
void Game_SetPlayerStartPosition(GRuntimeInstance *marker);
void Game_SetRacerGoal(int racerIndex, GRuntimeInstance *goal);
void Game_SetRacerLapsLeft(int racerIndex, int lapsLeft);
void Game_KnockoutRacer(ISimable *racer);
void Game_NotifyRacePlacement(GRuntimeInstance *activity, ISimable *racer, int place);
void Game_NotifySpeedTrapTriggered(GRuntimeInstance *activity, GRuntimeInstance *trap, ISimable *racer, float speed);
void Game_NotifyCheckpointReached(ISimable *racer, int checkpoint);
void Game_NotifyLapFinished(ISimable *racer, int lap);
void Game_NotifyRaceFinished(ISimable *racer);
void Game_SetRaceActivity(GRuntimeInstance *activity);
void Game_StartRace(GRuntimeInstance *activity);
void Game_StartRaceTimers();
void Game_SaveStartPositions();
void Game_RestoreStartPositions();
void Game_EnterPostRaceFlow();
bool FE_ShowOnlinePostRaceScreen();
void Game_SetCopsEnabled(bool enabled);
void Game_NoNewPursuitsOrCops();
void Game_ForcePursuitStart(int minHeat);
void Game_EnterEngagableTrigger(GRuntimeInstance *trigger);
void Game_ExitEngagableTrigger(GRuntimeInstance *trigger);
void Game_EnterGateZone(GRuntimeInstance *trigger, const char *zone);
void Game_ExitGateZone(GRuntimeInstance *trigger, const char *zone);
void Game_ShowRaceOverSummary();
void Game_HideRaceOverSummary();
void Game_SetTrafficSpeed(GRuntimeInstance *trigger, float speedDefault, float speedHighway);
void Game_SpawnCop(GRuntimeInstance *marker, const char *carName, bool inPursuit, bool roadBlock);
void Game_SpawnCharacter(GRuntimeInstance *character, GRuntimeInstance *spawn, GRuntimeInstance *target, float speed);
void Game_UnspawnCharacter(GRuntimeInstance *character);
void Game_SendCharacterStimulus(GRuntimeInstance *character, const char *stimulus);
void Game_JackKnife(GRuntimeInstance *character);
void Game_SetAllStaging(bool staging);
void Game_ShowPauseMenu();
void Game_AwardCash(ISimable *racer, float cash);
void Game_AwardPoints(ISimable *racer, float points);
void Game_ChallengeCompleted();
void Game_UnlockRace(GRuntimeInstance *activity);
void Game_NotifyCountdownDone();
void Game_ResetCopsForRestart();
void Game_ShowTriggerIcon(GRuntimeInstance *trigger);
void Game_HideTriggerIcon(GRuntimeInstance *trigger);
void Game_WarpToMarkerWhenRoaming(GRuntimeInstance *marker);
void Game_DoSpecialFinalization(GRuntimeInstance *activity);
bool Game_IsActiveSpeedTrap(GRuntimeInstance *trigger);
bool Game_IsActiveMenuGate(GRuntimeInstance *trigger);
float Game_GetSimTime();
int Game_GetRacerIndex(ISimable *racer);
ISimable *Game_GetRacerElement(int racerIndex);
ISimable *Game_GetPlayerElement(int playerIndex);
GRuntimeInstance *Game_GetRacerCharacter(int racerIndex);
int Game_GetNumRacers();
float Game_GetSimableSpeedKmh(ISimable *simable);
bool Game_RacerIsHuman(int racerIndex);
bool Game_PlayerIsLocal(ISimable *simable);
bool Game_IsRaceCompleted(GRuntimeInstance *activity);
bool Game_IsRaceUnlocked(GRuntimeInstance *activity);
bool Game_IsOnlineGame();
bool Game_IsLANGame();
bool Game_AllRacersDone();
bool Game_AllHumanPlayersDone();
float Game_SimableDistance(ISimable *simable, GRuntimeInstance *target);
float Game_SimableAngle(ISimable *simable, GRuntimeInstance *target);
bool Game_AllowEngageEvents();
bool Game_AllowMenuGates();
bool Game_AllowEngageSafehouse();
bool Game_DoSpecialSetup(GRuntimeInstance *activity);
int Game_GetPlayerBounty();
void Game_AwardPlayerBounty(int bounty);
void Game_JumpToCarLot();
void Game_AbandonRace();
void Game_JumpToSafeHouse();
void Game_SetTimer(const char *name, float interval);
void Game_KillTimer(const char *name);
void Game_ShowGPS(bool show);
void Game_NavigatePlayerTo(GRuntimeInstance *from, GRuntimeInstance *to, float distance);
void Game_NotifyFinished(GRuntimeInstance *activity);
bool Game_SkipCareerIntro();
void Game_SetChanceOfRain(float chance);
void Game_DoFade();
void Game_IntroduceRival();
void Game_PlayTutorial();
void Game_DoSafeHouseIntro(GRuntimeInstance *activity);
void Game_DoZoneMenuAction(GRuntimeInstance *trigger);
bool Game_SetCurrentTimeOfDay(GRuntimeInstance *activity);
void Game_ReloadWorld(GRuntimeInstance *activity);
bool Game_IsCareerMode();
bool Game_IsSplitScreen();
void Game_SetHasRapSheet();
void Game_SetWorldHeat(float heat);
void FE_ShowWinningPostRaceScreen();
void FE_ShowLosingPostRaceScreen();
void FE_ShowPostRaceScreen(bool winner);
void FE_ShowSpeedTrapScreen(float speedKmh, float bounty);
void Game_PreventPlayerBeingBusted();
int Game_CalculateRanking(ISimable *racer, int rank);

#endif
