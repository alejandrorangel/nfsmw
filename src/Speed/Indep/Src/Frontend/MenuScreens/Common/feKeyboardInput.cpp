#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feKeyboardInput.hpp"

#include "Speed/Indep/Src/Frontend/FEJoyInput.hpp"
#include "Speed/Indep/Src/Frontend/FEngFont.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEStrings.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#ifdef EA_PLATFORM_WIN32
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#endif

#ifdef EA_PORT_NATIVE
KeyboardEditString gKeyboardManager; // PC: 0x91D4B0
#endif

KeyboardEditString::KeyboardEditString() {
    TextInputObject = nullptr;
    MaxTextLength = 0;
    bMemSet(EditStringUCS2, 0, sizeof(EditStringUCS2));
    CursorPosUCS2 = 0;
    bMemSet(EditStringPacked, 0, sizeof(EditStringPacked));
    bMemSet(InitialString, 0, sizeof(InitialString));
    mEnabled = false;
}

void KeyboardEditString::SyncEditIntoPacked() {
    WideStringToPackedString(EditStringPacked, sizeof(EditStringPacked), EditStringUCS2);
}

char *KeyboardEditString::GetEditedString() {
    SyncEditIntoPacked();
    return EditStringPacked;
}

void KeyboardEditString::EndCapture() {
    mEnabled = false;
    TextInputObject = nullptr;
    bMemSet(EditStringUCS2, 0, sizeof(EditStringUCS2));
    bMemSet(EditStringPacked, 0, sizeof(EditStringPacked));
    bMemSet(InitialString, 0, sizeof(InitialString));
}

void KeyboardEditString::GetStringForDisplay(char *buffer, int size) {
    SyncEditIntoPacked();
    if (ModeFlags == 5) {
        FEngSNMakeHidden(buffer, size, EditStringUCS2);
    } else {
        bStrNCpy(buffer, EditStringPacked, size);
    }
}

void KeyboardEditString::RevertToOriginalString() {
    PackedStringToWideString(EditStringUCS2, 0x200, InitialString);
    SyncEditIntoPacked();
}

#ifdef EA_PLATFORM_WIN32
// PC typed text entry. Windows delivers WM_CHAR (an ANSI character code) to
// the window proc 0x6DB6C0, which hands it to NotifyChar through 0x5A95F0.

// PC: 0x57F1D0
void KeyboardEditString::StartCapture(FEngTextInputObject *txt, uint32 mode, const char *start_string, uint32 max_text_length) {
    TextInputObject = txt;
    ModeFlags = mode;
    if (static_cast<int>(max_text_length) > 0x9B) {
        max_text_length = 0x9B;
    }
    MaxTextLength = max_text_length;
    PackedStringToWideString(EditStringUCS2, 0x200, start_string);
    SyncEditIntoPacked();
    bStrNCpy(InitialString, start_string, sizeof(InitialString));
    CursorPosUCS2 = bStrLen(EditStringUCS2);
    KeysProcessed = 0;
    mEnabled = true;
}

// PC: 0x56C820. Letters and digits are always allowed; the mode adds symbols.
bool KeyboardEditString::PassesThroughFilter(int keycode) {
    // PC: the symbol tables at 0x8F53F0 (0x56C7C0) and 0x8F5474 (0x56C7F0)
    static const int kSymbols[] = {' ', '.', ',', '_', '@', '-', '`', '=', '[', ']', '\\', ';', '\'', '/', '<', '>', '?',
                                   ':', '"', '{', '}', '|', '+', '(', ')', '*', '&', '%', '#', '!', '~'};
    static const int kInvertedPunctuation[] = {0xBF, 0xA1, 'a'};

    if (!IsCapturing()) {
        return false;
    }
    if ((keycode >= 'a' && keycode <= 'z') || (keycode >= 'A' && keycode <= 'Z') || (keycode >= '0' && keycode <= '9')) {
        return true;
    }
    if (ModeFlags == 4 && (keycode == '@' || keycode == '.' || keycode == '_')) {
        return true;
    }
    if (ModeFlags == 5 && (keycode == ' ' || keycode == ',' || keycode == '@' || keycode == '.')) {
        return true;
    }
    if (ModeFlags != 3 && ModeFlags != 0) {
        return false;
    }
    if (keycode >= 0xC0 && keycode <= 0xFF) {
        return true;
    }
    for (unsigned int i = 0; i < sizeof(kInvertedPunctuation) / sizeof(kInvertedPunctuation[0]); i++) {
        if (kInvertedPunctuation[i] == keycode) {
            return true;
        }
    }
    for (unsigned int i = 0; i < sizeof(kSymbols) / sizeof(kSymbols[0]); i++) {
        if (kSymbols[i] == keycode) {
            return true;
        }
    }
    return false;
}

// PC: 0x5A3790. There's no cursor movement on PC (the WM_KEYDOWN Home/End/
// Left/Right cases in the window proc call empty functions): characters are
// appended and backspace deletes the last one.
void KeyboardEditString::ProcessHotkeys(int keycode) {
    if (!IsCapturing()) {
        return;
    }
    int keys_processed = KeysProcessed++;
    if (keycode == '\r') {
        // Ignore a return that's the first key, e.g. the press that opened the input.
        if (keys_processed != 0) {
            TextInputObject->ReturnPressed();
        }
    } else if (keycode == 0x1B) {
        TextInputObject->EscapePressed();
    } else if (keycode == '\b') {
        if (CursorPosUCS2 > 0) {
            CursorPosUCS2--;
            EditStringUCS2[CursorPosUCS2] = 0;
        }
    } else if (PassesThroughFilter(keycode) && CursorPosUCS2 < MaxTextLength) {
        EditStringUCS2[CursorPosUCS2] = static_cast<uint16>(keycode);
        CursorPosUCS2++;
    }
}

// PC: 0x5A8700
void KeyboardEditString::NotifyChar(int keycode) {
    if (IsCapturing()) {
        ProcessHotkeys(keycode);
    } else if (TheGameFlowManager.GetState() == GAMEFLOW_STATE_IN_FRONTEND && (keycode == 'q' || keycode == 'Q')) {
        // PC MenuScreen::BaseNotify 0x5A4820 answers this with the quit-game dialog.
        cFEng::Get()->QueueGameMessage(0x62799a4c, nullptr, 0xff);
    }
    // PC then feeds EasterEggs::KeyboardInput 0x65D9E0, but only when the flag
    // byte at 0x8F8410 is set, and it's 0 in .data and never written.
}
#endif

FEngTextInputObject::FEngTextInputObject(MenuScreen *pkg, FEString *obj, uint32 mode, const char *start_string, uint32 max_text_length)
    : DisplayString(obj), ParentPackage(pkg), mBlinkTime(0) {
    gKeyboardManager.StartCapture(this, mode, start_string, max_text_length);
}

FEngTextInputObject::~FEngTextInputObject() {
    gKeyboardManager.EndCapture();
}

void FEngTextInputObject::ReturnPressed() {
    if (gKeyboardManager.GetModeFlags() == 6) {
        if (bStrLen(gKeyboardManager.GetEditedString()) == 0) {
            return;
        }
    }
    cFEngJoyInput::Get()->FlushActions();
    RedrawString(false);
    ParentPackage->NotificationMessage(0xda5b8712, DisplayString, 0, 0);
    gKeyboardManager.EndCapture();
    ParentPackage->FEngEndTextInput();
}

void FEngTextInputObject::EscapePressed() {
    gKeyboardManager.RevertToOriginalString();
    RedrawString(false);
    ParentPackage->NotificationMessage(0xc9d30688, DisplayString, 0, 0);
    gKeyboardManager.EndCapture();
    ParentPackage->FEngEndTextInput();
}

void FEngTextInputObject::RedrawString(bool pIncludeCursor) {
    if (DisplayString != nullptr) {
        char buffer[156];

        gKeyboardManager.GetStringForDisplay(buffer, 0x9C);
        if (pIncludeCursor) {
            mBlinkTime++;
            if (mBlinkTime > 0x59) {
                mBlinkTime = 0;
            }

            if (mBlinkTime < 0x2D) {
                bStrCat(buffer, buffer, "|");
            } else {
                bStrCat(buffer, buffer, " ");
            }
        }

        u16 widestring[156];
        bStrCpy(widestring, buffer);
        FEngFont *font = FindFont(DisplayString->Handle);
        int width = DisplayString->MaxWidth;
        int flags = DisplayString->Flags;
        i16 *fitstring = reinterpret_cast<i16 *>(widestring);

        while (*fitstring != 0) {
            if (font->GetLineWidth(fitstring, flags, 0, false) <= static_cast<float>(width)) {
                break;
            }
            fitstring++;
        }

        FESetString(DisplayString, fitstring);
    }
}

void FEngTextInputObject::Notify(uint32 msg) {
    if (msg == FEMSG_SCREEN_TICK) {
        RedrawString(true);
    } else if (msg == __BUTTON_PRESSED__) {
        ReturnPressed();
    }
}
