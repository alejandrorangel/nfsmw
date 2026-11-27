#ifndef GAMEPLAY_GSTATE_H
#define GAMEPLAY_GSTATE_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.h"

// One state of an activity's state graph; its handlers run while it's current.
// total size: 0x28
class GState : public GRuntimeInstance {
  public:
    enum { kType = kGameplayObjType_State };

    GState(const Attrib::Key &stateKey);
    ~GState() override;

    GameplayObjType GetType() const override;

    bool IsTerminalState() const;
};

#endif
