#ifndef _UIOPTIONWIDGETS
#define _UIOPTIONWIDGETS

#include <types.h>

#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feWidget.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/FEIconScrollerMenu.hpp"

// 0x5C
class OMAudio : public IconOption {
  public:
    OMAudio(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMAudio() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMVideo : public IconOption {
  public:
    OMVideo(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMVideo() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMGameplay : public IconOption {
  public:
    OMGameplay(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMGameplay() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMPlayer : public IconOption {
  public:
    OMPlayer(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMPlayer() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMController : public IconOption {
  public:
    OMController(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMController() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMEATrax : public IconOption {
  public:
    OMEATrax(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMEATrax() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMTrailers : public IconOption {
  public:
    OMTrailers(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMTrailers() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMOnline : public IconOption {
  public:
    OMOnline(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMOnline() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0x5C
class OMCredits : public IconOption {
  public:
    OMCredits(uint32 tex_hash, uint32 name_hash, uint32 desc_hash) : IconOption(tex_hash, name_hash, desc_hash) {}
    ~OMCredits() override {}
    void React(const char *pkg_name, uint32 data, FEObject *obj, uint32 param1, uint32 param2) override;
};

// 0xA4
class AOSFXMasterVol : public FESliderWidget {
  public:
    AOSFXMasterVol(bool enabled) : FESliderWidget(enabled) {}
    ~AOSFXMasterVol() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0xA4
class AOCarVol : public FESliderWidget {
  public:
    AOCarVol(bool enabled) : FESliderWidget(enabled) {}
    ~AOCarVol() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0xA4
class AOSpeechVol : public FESliderWidget {
  public:
    AOSpeechVol(bool enabled) : FESliderWidget(enabled) {}
    ~AOSpeechVol() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0xA4
class AOFEMusicVol : public FESliderWidget {
  public:
    AOFEMusicVol(bool enabled) : FESliderWidget(enabled) {}
    ~AOFEMusicVol() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0xA4
class AOIGMusicVol : public FESliderWidget {
  public:
    AOIGMusicVol(bool enabled) : FESliderWidget(enabled) {}
    ~AOIGMusicVol() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x64
class AOInteractiveMusicMode : public FEToggleWidget {
  public:
    AOInteractiveMusicMode(bool enabled) : FEToggleWidget(enabled) {}
    ~AOInteractiveMusicMode() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class AOEATraxMusicMode : public FEToggleWidget {
  public:
    AOEATraxMusicMode(bool enabled) : FEToggleWidget(enabled) {}
    ~AOEATraxMusicMode() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class AOAudioMode : public FEToggleWidget {
  public:
    AOAudioMode(bool enabled) : FEToggleWidget(enabled) {}
    ~AOAudioMode() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

#if !defined(EA_PLATFORM_WIN32)
// 0x64 made up name
class VOScreenResolution : public FEToggleWidget {
  public:
    VOScreenResolution(bool enabled) : FEToggleWidget(enabled) {}
    ~VOScreenResolution() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};
#endif

// 0x64
class VOWideScreen : public FEToggleWidget {
  public:
    VOWideScreen(bool enabled) : FEToggleWidget(enabled) {}
    ~VOWideScreen() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GODamage : public FEToggleWidget {
  public:
    GODamage(bool enabled) : FEToggleWidget(enabled) {}
    ~GODamage() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GOAutoSave : public FEToggleWidget {
  public:
    GOAutoSave(bool enabled) : FEToggleWidget(enabled) {}
    ~GOAutoSave() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GOJumpCams : public FEToggleWidget {
  public:
    GOJumpCams(bool enabled) : FEToggleWidget(enabled) {}
    ~GOJumpCams() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GORearview : public FEToggleWidget {
  public:
    GORearview(bool enabled) : FEToggleWidget(enabled) {}
    ~GORearview() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GOSpeedoUnits : public FEToggleWidget {
  public:
    GOSpeedoUnits(bool enabled) : FEToggleWidget(enabled) {}
    ~GOSpeedoUnits() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GORacingMiniMap : public FEToggleWidget {
  public:
    GORacingMiniMap(bool enabled) : FEToggleWidget(enabled) {}
    ~GORacingMiniMap() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class GOExploringMiniMap : public FEToggleWidget {
  public:
    GOExploringMiniMap(bool enabled) : FEToggleWidget(enabled) {}
    ~GOExploringMiniMap() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// ===== PO* classes (extend FEToggleWidget) =====

// 0x64
class POTransmission : public FEToggleWidget {
  public:
    POTransmission(bool enabled) : FEToggleWidget(enabled) {}
    ~POTransmission() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class PODriveCam : public FEToggleWidget {
  public:
    PODriveCam(bool enabled) : FEToggleWidget(enabled) {}
    ~PODriveCam() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class POGauges : public FEToggleWidget {
  public:
    POGauges(bool enabled) : FEToggleWidget(enabled) {}
    ~POGauges() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class POPosition : public FEToggleWidget {
  public:
    POPosition(bool enabled) : FEToggleWidget(enabled) {}
    ~POPosition() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class POScore : public FEToggleWidget {
  public:
    POScore(bool enabled) : FEToggleWidget(enabled) {}
    ~POScore() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class POSplitTime : public FEToggleWidget {
  public:
    POSplitTime(bool enabled) : FEToggleWidget(enabled) {}
    ~POSplitTime() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class POLeaderBoard : public FEToggleWidget {
  public:
    POLeaderBoard(bool enabled) : FEToggleWidget(enabled) {}
    ~POLeaderBoard() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x64
class COVibration : public FEToggleWidget {
  public:
    COVibration(int player_num, bool enabled) : FEToggleWidget(enabled) {}
    ~COVibration() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetFocus(const char *parent_pkg) override;
    void UnsetFocus() override;
};

// 0x64
class COConfig : public FEToggleWidget {
  public:
    COConfig(bool enabled) : FEToggleWidget(enabled) {
        SetBackingOffsetX(-295.0f);
    }
    ~COConfig() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

#if defined(EA_PLATFORM_WIN32)
#include "Speed/PC/Src/PCGraphicsOptions.hpp"

// The PC video options screen's working copy (UIOptionsScreen +0x144 and
// +0x148; the original is at +0x14C/+0x150).
struct PCVideoOptionsEdit {
    PCGraphicsOptions *Options; // copy of gPCGraphicsOptions
    int PerformanceLevel;       // copy of gPCPerformanceLevel
};
extern PCVideoOptionsEdit *gPCVideoOptionsEdit; // 0x91CA20

// 0x5C (ctor 0x51A7D0)
class VOScreenResolution : public FEToggleWidget {
  public:
    VOScreenResolution(bool enabled) : FEToggleWidget(enabled) {}
    ~VOScreenResolution() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A8F0)
class DOVSync : public FEToggleWidget {
  public:
    DOVSync(bool enabled) : FEToggleWidget(enabled) {}
    ~DOVSync() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A850)
class DOCarLOD : public FEToggleWidget {
  public:
    DOCarLOD(bool enabled) : FEToggleWidget(enabled) {}
    ~DOCarLOD() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A830)
class DOCarReflectDetail : public FEToggleWidget {
  public:
    DOCarReflectDetail(bool enabled) : FEToggleWidget(enabled) {}
    ~DOCarReflectDetail() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A930)
class DORainEnable : public FEToggleWidget {
  public:
    DORainEnable(bool enabled) : FEToggleWidget(enabled) {}
    ~DORainEnable() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A8D0)
class DOOverBright : public FEToggleWidget {
  public:
    DOOverBright(bool enabled) : FEToggleWidget(enabled) {}
    ~DOOverBright() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x5C (ctor 0x51A950)
class DOVisualTreatment : public FEToggleWidget {
  public:
    DOVisualTreatment(bool enabled) : FEToggleWidget(enabled) {}
    ~DOVisualTreatment() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
};

// 0x9C (ctor 0x51A7F0)
class VOLevelOfDetail : public FESliderWidget {
  public:
    VOLevelOfDetail(bool enabled) : FESliderWidget(enabled) {}
    ~VOLevelOfDetail() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A890)
class DOFullScreenAA : public FESliderWidget {
  public:
    DOFullScreenAA(bool enabled) : FESliderWidget(enabled) {}
    ~DOFullScreenAA() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A8B0)
class DOTextureFiltering : public FESliderWidget {
  public:
    DOTextureFiltering(bool enabled) : FESliderWidget(enabled) {}
    ~DOTextureFiltering() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A870)
class DOWorldLOD : public FESliderWidget {
  public:
    DOWorldLOD(bool enabled) : FESliderWidget(enabled) {}
    ~DOWorldLOD() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A910)
class DORoadReflectDetail : public FESliderWidget {
  public:
    DORoadReflectDetail(bool enabled) : FESliderWidget(enabled) {}
    ~DORoadReflectDetail() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A970)
class DOShadowDetail : public FESliderWidget {
  public:
    DOShadowDetail(bool enabled) : FESliderWidget(enabled) {}
    ~DOShadowDetail() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

// 0x9C (ctor 0x51A810)
class DOCarReflectRate : public FESliderWidget {
  public:
    DOCarReflectRate(bool enabled) : FESliderWidget(enabled) {}
    ~DOCarReflectRate() override {}
    void Act(const char *parent_pkg, uint32 data) override;
    void Draw() override;
    void SetInitialValues() override;
};

#endif

#endif
