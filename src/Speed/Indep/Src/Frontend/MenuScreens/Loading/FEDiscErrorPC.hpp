#ifndef FEDISCERRORPC_H
#define FEDISCERRORPC_H

#include "Speed/Indep/Src/Frontend/MenuScreens/Common/FEMenuScreen.hpp"

// sub_6C2700: is the game disc in a drive (or the disc check disabled).
bool PCCheckGameDisc();

// speed.exe 1.3's DiscErrorPC.fng screen (CreateDiscErrorPC 0x59AB80, ctor
// sub_5981E0, vtable 0x8A1D74), the first boot flow screen: retry the disc
// check or quit.
// total size: 0x30
class DiscErrorPCScreen : public MenuScreen {
  public:
    DiscErrorPCScreen(ScreenConstructorData *sd);
    ~DiscErrorPCScreen() override {}
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override;

  private:
    int Unknown2C; // offset 0x2C, size 0x4
};

#endif
