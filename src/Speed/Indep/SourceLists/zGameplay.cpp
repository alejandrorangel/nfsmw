// TODO remove
#ifdef _MSC_VER
#pragma warning(disable : 4716)
#endif

#include "Speed/Indep/Src/Gameplay/GManager.cpp"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.cpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.cpp"
#include "Speed/Indep/Src/Gameplay/GTimer.cpp"
#include "Speed/Indep/Src/Gameplay/GVault.cpp"

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.cpp"
#include "Speed/Indep/Src/Gameplay/GObjectBlock.cpp"
#include "Speed/Indep/Src/Gameplay/GActivity.cpp"
#include "Speed/Indep/Src/Gameplay/GHandler.cpp"
#include "Speed/Indep/Src/Gameplay/GTrigger.cpp"
#include "Speed/Indep/Src/Gameplay/GCharacter.cpp"
#include "Speed/Indep/Src/Gameplay/GSpeedTrap.cpp"
#include "Speed/Indep/Src/Gameplay/GIcon.cpp"
#endif
#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Lua/LuaRuntime.cpp"
#include "Speed/Indep/Src/Lua/LuaBindery.cpp"
#include "Speed/Indep/Src/Lua/LuaAttributes.cpp"
#include "Speed/Indep/Src/Lua/LuaPostOffice.cpp"
#include "Speed/Indep/Src/Lua/LuaGameHooks.cpp"
#endif
