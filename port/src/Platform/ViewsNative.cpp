// epInitViews: gives the engine's views their cameras and per-view effects,
// as speed.exe 1.3 does @ 0x6C06A0 (PC eViews are 0x70 bytes from 0x9195E0;
// the cameras are statically allocated Camera objects there).
//
//   view 0   own camera                     view 8   own camera
//   view 1   player 1: camera, screen effects, rain, face pixelation (active)
//   view 2   player 2: camera, screen effects, rain
//   view 3   own camera, shares view 1's rain (active)
//   view 4   view 1's camera and rain (active)
//   view 15  own camera, view 1's rain (active)
//   PC view 15 (0x919C70, the shadow map view: static camera 0x986F70, set by
//   0x6C0883) is this decomp's EVIEW_SHADOWMAP1 (13).
// Render targets come with the 3D renderer.

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyE.hpp"
#include "Speed/Indep/Src/World/FacePixelate.hpp"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/ScreenEffects.hpp"

void epInitViews() {
    Camera *camera_view0 = new Camera();
    Camera *camera_player1 = new Camera();
    Camera *camera_player2 = new Camera();
    Camera *camera_view3 = new Camera();
    Camera *camera_view8 = new Camera();
    Camera *camera_view15 = new Camera();

    eView *view0 = eGetView(0, false);
    view0->pCamera = camera_view0;
    view0->Active = 1;

    eView *player1 = eGetView(1, false);
    player1->pCamera = camera_player1;
    player1->Active = 1;
    player1->ScreenEffects = new ScreenEffectDB();
    player1->ScreenEffects->SetMyView(player1);
    player1->Precipitation = new Rain(player1, RAIN);
    player1->facePixelation = new FacePixelation(player1);

    eView *player2 = eGetView(2, false);
    player2->pCamera = camera_player2;
    player2->ScreenEffects = new ScreenEffectDB();
    player2->ScreenEffects->SetMyView(player2);
    player2->Precipitation = new Rain(player2, RAIN);

    Rain *rain = player1->Precipitation;

    eView *view3 = eGetView(3, false);
    view3->pCamera = camera_view3;
    view3->Active = 1;
    view3->Precipitation = rain;
    view3->facePixelation = nullptr;

    eView *view4 = eGetView(4, false);
    view4->pCamera = camera_player1;
    view4->Active = 1;
    view4->Precipitation = rain;

    eView *view8 = eGetView(8, false);
    view8->pCamera = camera_view8;
    view8->Active = 1;

    eView *shadow_view = eGetView(EVIEW_SHADOWMAP1, false);
    shadow_view->pCamera = new Camera();
    shadow_view->Active = 1;

    eView *view15 = eGetView(15, false);
    view15->pCamera = camera_view15;
    view15->Active = 1;
    view15->Precipitation = rain;
}
