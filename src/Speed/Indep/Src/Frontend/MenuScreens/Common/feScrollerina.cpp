#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feScrollerina.hpp"

#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEImages.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feWidget.hpp"
#ifdef EA_PLATFORM_WIN32
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngMouse.hpp"
#endif

void ScrollerSlot::SetScript(uint32 script_hash) {
    for (ScrollerSlotNode *node = FEStrings.GetHead(); node != FEStrings.EndOfList(); node = node->GetNext()) {
        if (node != nullptr && node->String != nullptr) {
            FEngSetScript(node->String, script_hash, true);
        }
    }

    if (pBacking != nullptr) {
        FEngSetScript(pBacking, script_hash, true);
    }
}

void ScrollerSlot::FindSize() {
    float top = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    if (pBacking != nullptr) {
        top = FEngGetTopLeftY(pBacking);
        left = FEngGetTopLeftX(pBacking);
        right = left + FEngGetSizeX(pBacking);
        bottom = top + FEngGetSizeY(pBacking);
    }
    vTopLeft.x = left;
    vTopLeft.y = top;
    vSize.x = bAbs(left - right);
    vSize.y = bAbs(top - bottom);
}

void ScrollerSlot::Show() {
    if (!FEStrings.IsEmpty()) {
        for (ScrollerSlotNode *node = FEStrings.GetHead(); node != FEStrings.EndOfList(); node = node->GetNext()) {
            FEngSetVisible(node->String);
        }
        FEngSetVisible(pBacking);
    }
}

void ScrollerSlot::Hide() {
    if (!FEStrings.IsEmpty()) {
        for (ScrollerSlotNode *node = FEStrings.GetHead(); node != FEStrings.EndOfList(); node = node->GetNext()) {
            FEngSetInvisible(node->String);
        }
        FEngSetInvisible(pBacking);
    }
}

Scrollerina::Scrollerina(const char *parent_pkg, const char *backing, const char *scrollbar, bool vert, bool resize, bool wrapped,
                         bool alwaysShowBacking)
    : pParentPkg(parent_pkg),                                //
      iNumSlots(0),                                          //
      iNumData(0),                                           //
      iViewHeadDataIndex(0),                                 //
      SelectedDatum(nullptr),                                //
      TopDatum(nullptr),                                     //
      SelectedSlot(nullptr),                                 //
      pBacking(nullptr),                                     //
      ScrollBar(parent_pkg, scrollbar, vert, resize, false), //
      vTopLeft(0.0f, 0.0f),                                  //
      vSize(0.0f, 0.0f),                                     //
      bHasScrollBar(true),                                   //
      bViewNeedsSync(false),                                 //
      bWrapped(wrapped),                                     //
      bAlwaysShowBacking(alwaysShowBacking),                 //
      bVertical(vert),                                       //
      mouseDownMsg(__PAD_ACCEPT__),                          //
      bInClickToSelectMode(false), pScrollRegion(nullptr) {
    if (backing != nullptr) {
        pBacking = FEngFindImage(parent_pkg, backing);
        FEngGetTopLeft(pBacking, vTopLeft.x, vTopLeft.y);
        FEngGetSize(pBacking, vSize.x, vSize.y);
    } else {
        bHasScrollBar = false;
    }
}

void Scrollerina::AddSlot(ScrollerSlot *slot) {
    Slots.AddTail(slot);
    iNumSlots++;
    slot->FindSize();
    this->FindSize();
    if (SelectedSlot == nullptr) {
        SelectedSlot = Slots.GetHead();
    }
}

ScrollerDatum *Scrollerina::AddData(const char *string) {
    ScrollerDatum *datum = new ("ScrollerDatum", 0) ScrollerDatum(string, 0);

    Data.AddTail(datum);

    return datum;
}

void Scrollerina::AddData(ScrollerDatum *datum) {
    Data.AddTail(datum);
    iNumData++;
    if (TopDatum == nullptr) {
        iViewHeadDataIndex = 1;
        TopDatum = Data.GetHead();
    }
    if (SelectedDatum == nullptr) {
        SelectedDatum = Data.GetHead();
    }
}

ScrollerDatum *Scrollerina::FindDatumInSlot(ScrollerSlot *to_find) {
    if (Slots.IsEmpty() || Data.IsEmpty() || to_find == nullptr) {
        return nullptr;
    }

    ScrollerDatum *datum = TopDatum;
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        if (slot == to_find) {
            return datum;
        }
        if (datum == Data.EndOfList()) {
            break;
        }
        datum = datum->GetNext();
    }

    return nullptr;
}

ScrollerSlot *Scrollerina::FindSlotWithDatum(ScrollerDatum *to_find) {
    if (Slots.IsEmpty() || Data.IsEmpty() || to_find == nullptr) {
        return nullptr;
    }

    ScrollerDatum *datum = TopDatum;
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        if (datum == to_find) {
            return slot;
        }
        if (datum == Data.EndOfList()) {
            break;
        }

        datum = datum->GetNext();
    }
    return nullptr;
}

void Scrollerina::ScrollNext() {
    if (bWrapped) {
        g_pEAXSound->PlayUISoundFX(UISND_COMMON_UP);
        ScrollWrapped(eSD_NEXT);
    } else {
        g_pEAXSound->PlayUISoundFX(GetSelectedDatum() == GetLastDatum() ? UISND_COMMON_WRONG : UISND_COMMON_UP);
        Scroll(eSD_NEXT);
    }
}

void Scrollerina::ScrollPrev() {
    if (bWrapped) {
        g_pEAXSound->PlayUISoundFX(UISND_COMMON_UP);
        ScrollWrapped(eSD_PREV);
    } else {
        g_pEAXSound->PlayUISoundFX(GetSelectedDatum() == GetFirstDatum() ? UISND_COMMON_WRONG : UISND_COMMON_UP);
        Scroll(eSD_PREV);
    }
}

bool Scrollerina::Scroll(eScrollDir dir) {
    bool ret = false;
    if (Slots.IsEmpty() || Data.IsEmpty()) {
        return false;
    }

    if (bViewNeedsSync) {
        SyncViewToSelection();
    } else {
        ScrollerDatum *new_datum = SelectedDatum;
        ScrollerDatum *new_view = TopDatum;
        uint32 new_view_head = iViewHeadDataIndex;

        if (dir == eSD_NEXT) {
            do {
                if (new_datum == Data.GetTail())
                    return false;
                new_datum = new_datum->GetNext();
                if (GetNodeIndex(new_datum) >= new_view_head + iNumSlots) {
                    new_view = new_view->GetNext();
                    new_view_head++;
                }
            } while (!new_datum->IsEnabled());
        } else if (dir == eSD_PREV) {
            do {
                if (new_datum == Data.GetHead())
                    return false;
                new_datum = new_datum->GetPrev();
                if (new_datum == new_view->GetPrev()) {
                    new_view_head--;
                    new_view = new_datum;
                }
            } while (!new_datum->IsEnabled());
        }

        if (new_datum != SelectedDatum) {
            SelectedDatum = new_datum;
            iViewHeadDataIndex = new_view_head;
            TopDatum = new_view;
            ret = true;
            ScrollSelection(dir);
            SetDisabledScripts();
        }
    }
    Update(true);
    return ret;
}

bool Scrollerina::ScrollWrapped(eScrollDir dir) {
    bool ret = false;
    if (Slots.IsEmpty() || Data.IsEmpty()) {
        return false;
    }

    if (bViewNeedsSync) {
        SyncViewToSelection();
    } else {
        ScrollerDatum *new_datum = SelectedDatum;
        ScrollerDatum *new_view = TopDatum;
        uint32 new_view_head = iViewHeadDataIndex;

        if (dir == eSD_NEXT) {
            do {
                if (new_datum == Data.GetTail()) {
                    new_datum = Data.GetHead();
                    new_view_head = 1;
                    new_view = new_datum;
                } else {
                    new_datum = new_datum->GetNext();
                    if (GetNodeIndex(new_datum) >= new_view_head + iNumSlots) {
                        new_view = new_view->GetNext();
                        new_view_head++;
                    }
                }
            } while (!new_datum->IsEnabled());
        } else if (dir == eSD_PREV) {
            do {
                if (new_datum == Data.GetHead()) {
                    new_datum = Data.GetTail();
                    new_view_head = iNumData - iNumSlots + 1;
                    new_view = new_datum;
                } else {
                    new_datum = new_datum->GetPrev();
                    if (new_datum == new_view->GetPrev()) {
                        new_view_head--;
                        new_view = new_datum;
                    }
                }
            } while (!new_datum->IsEnabled());
        }

        if (new_datum != SelectedDatum) {
            SelectedDatum = new_datum;
            iViewHeadDataIndex = new_view_head;
            TopDatum = new_view;
            ret = true;
            ScrollSelection(dir);
            SetDisabledScripts();
        }
    }
    Update(true);
    return ret;
}

bool Scrollerina::MoveSelected(eScrollDir dir, bool bprint) {
    bool ret = false;
    if (Slots.IsEmpty() || Data.IsEmpty()) {
        return false;
    }

    if (bViewNeedsSync) {
        SyncViewToSelection();
    } else {
        if (dir == eSD_NEXT) {
            g_pEAXSound->PlayUISoundFX(GetSelectedDatum() == GetLastDatum() ? UISND_COMMON_WRONG : UISND_COMMON_UP);
            if (SelectedDatum == Data.GetTail()) {
                return false;
            }
            ScrollerDatum *nextDatum = SelectedDatum->GetNext();
            ScrollerDatum *removedDatum = Data.Remove(SelectedDatum);
            Data.AddAfter(nextDatum, SelectedDatum);
            if (TopDatum == SelectedDatum) {
                TopDatum = SelectedDatum->GetPrev();
            } else {
                if (GetNodeIndex(SelectedDatum) >= iViewHeadDataIndex + iNumSlots) {
                    iViewHeadDataIndex++;
                    TopDatum = TopDatum->GetNext();
                }
            }
        } else if (dir == eSD_PREV) {
            g_pEAXSound->PlayUISoundFX(GetSelectedDatum() == GetFirstDatum() ? UISND_COMMON_WRONG : UISND_COMMON_UP);
            if (SelectedDatum == Data.GetHead()) {
                return false;
            }
            ScrollerDatum *prevDatum = SelectedDatum->GetPrev();
            ScrollerDatum *removedDatum = Data.Remove(SelectedDatum);
            Data.AddBefore(prevDatum, SelectedDatum);
            if (TopDatum == SelectedDatum) {
                TopDatum = SelectedDatum;
                iViewHeadDataIndex--;
            } else {
                if (TopDatum->Prev == SelectedDatum) {
                    TopDatum = TopDatum->GetPrev();
                }
            }
        }
        ScrollSelection(dir);
        ret = true;
    }
    Update(bprint);
    return ret;
}

bool Scrollerina::ScrollSelection(eScrollDir dir) {
    bool ret = false;
    ScrollerSlot *slot = SelectedSlot;

    if (dir == eSD_NEXT) {
        if (slot == Slots.GetTail()) {
            return false;
        }
        ScrollerDatum *datum = FindDatumInSlot(slot);
        do {
            slot = slot->GetNext();
            datum = datum->GetNext();
            if ((slot == nullptr) || slot == Slots.GetTail())
                break;
        } while (!datum->IsEnabled());
    } else if (dir == eSD_PREV) {
        if (slot == Slots.GetHead()) {
            return false;
        }
        ScrollerDatum *datum = FindDatumInSlot(slot);
        do {
            slot = slot->GetPrev();
            datum = datum->GetPrev();
            if ((slot == nullptr) || slot == Slots.GetHead())
                break;
        } while (!datum->IsEnabled());
    }

    if (slot != SelectedSlot) {
        SelectedSlot->UnHighlight();
        SelectedSlot = slot;
        slot->Highlight();
        ret = true;
    }

    return ret;
}

void Scrollerina::SyncViewToSelection() {
    if (Data.IsEmpty() || Slots.IsEmpty()) {
        return;
    }
    if (iNumData <= iNumSlots) {
        return;
    }

    if (GetNodeIndex(SelectedDatum) <= iNumData - iNumSlots + 1) {
        TopDatum = SelectedDatum;
        SelectedSlot = Slots.GetHead();
    } else {
        TopDatum = Data.GetNode(iNumData - iNumSlots);
        SelectedSlot = FindSlotWithDatum(SelectedDatum);
    }

    bViewNeedsSync = false;
    SetDisabledScripts();
    HighlightSelected();
    CountListIndices();
}

void Scrollerina::SetDisabledScripts() {
    ScrollerDatum *datum = FindDatumInSlot(Slots.GetHead());
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        if (datum->IsEnabled()) {
            if (datum != SelectedDatum) {
                slot->Enable();
            }
        } else {
            slot->Disable();
        }
        datum = datum->GetNext();
    }
}

void Scrollerina::Print() {
    ScrollerDatum *datum = TopDatum;
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        if ((datum != nullptr) && datum != Data.EndOfList()) {
            ScrollerDatumNode *data_string = datum->Strings.GetHead();
            slot->Show();
            for (ScrollerSlotNode *slot_string = slot->FEStrings.GetHead(); slot_string != slot->FEStrings.EndOfList();
                 slot_string = slot_string->GetNext()) {
                if (data_string->LanguageHash) {
                    FEngSetLanguageHash(reinterpret_cast<FEString *>(slot_string->String), data_string->LanguageHash);
                } else {
                    FEPrintf(reinterpret_cast<FEString *>(slot_string->String), "%s", data_string->String);
                }

                data_string = data_string->GetNext();
                if (data_string == datum->Strings.EndOfList() && slot_string->GetNext() != slot->FEStrings.EndOfList()) {
                    break;
                }
            }
            datum = datum->GetNext();
        } else {
            slot->Hide();
        }
    }
}

void Scrollerina::DrawScrollBar() {
    if (bHasScrollBar) {
        ScrollBar.Update(iNumSlots, iNumData, iViewHeadDataIndex, GetSelectedNodeIndex());
    }
}

#ifdef EA_PLATFORM_WIN32
// PC 0x586680: the node holding this string.
ScrollerDatumNode *ScrollerDatum::Find(const char *to_find) {
    for (ScrollerDatumNode *node = Strings.GetHead(); node != Strings.EndOfList(); node = node->GetNext()) {
        if (bStrICmp(node->String, to_find) == 0) {
            return node;
        }
    }
    return nullptr;
}

// PC 0x586950
ScrollerDatum *Scrollerina::FindDatum(const char *to_find) {
    for (ScrollerDatum *datum = Data.GetHead(); datum != Data.EndOfList(); datum = datum->GetNext()) {
        if (datum->Find(to_find) != nullptr) {
            return datum;
        }
    }
    return nullptr;
}

// PC 0x586D90: the selected datum's first string.
const char *Scrollerina::GetActiveSelection() {
    if (Data.IsEmpty() || SelectedDatum == nullptr) {
        return nullptr;
    }
    ScrollerDatumNode *node = SelectedDatum->Strings.GetHead();
    return node != SelectedDatum->Strings.EndOfList() ? node->String : "";
}

// PC 0x586D40
void Scrollerina::DeleteScrollerData() {
    while (!Data.IsEmpty()) {
        ScrollerDatum *datum = Data.GetHead();
        datum->Remove();
        delete datum;
    }
    SelectedDatum = nullptr;
    TopDatum = nullptr;
    iNumData = 0;
}

// PC 0x5A8D10: select the index'th datum after the head (a view reset and
// that many scrolls).
void Scrollerina::SetSelectedDatum(uint32 index) {
    if (!Slots.IsEmpty()) {
        TopDatum = Data.GetHead();
        SelectedDatum = TopDatum;
        SelectedSlot = Slots.GetHead();
    }
    for (; index != 0; index--) {
        if (!bWrapped) {
            g_pEAXSound->PlayUISoundFX(SelectedDatum == Data.GetTail() ? UISND_COMMON_WRONG : UISND_COMMON_UP);
            Scroll(eSD_NEXT);
        } else {
            g_pEAXSound->PlayUISoundFX(UISND_COMMON_UP);
            ScrollWrapped(eSD_NEXT);
        }
    }
}
#endif

void Scrollerina::Update(bool print) {
    if (print) {
        Print();
    }
    DrawScrollBar();
}

void Scrollerina::Enable(ScrollerDatum *datum) {
    if (datum == nullptr) {
        return;
    }
    if (Slots.IsEmpty() || Data.IsEmpty()) {
        return;
    }
    datum->Enable();
    ScrollerSlot *slot = FindSlotWithDatum(datum);
    if (slot != nullptr && slot != SelectedSlot) {
        slot->Enable();
    }
}

// UNSOLVED
void Scrollerina::Disable(ScrollerDatum *datum) {
    if (datum == nullptr) {
        return;
    }
    if (Slots.IsEmpty() || Data.IsEmpty()) {
        return;
    }
    datum->Disable();
    ScrollerSlot *slot = FindSlotWithDatum(datum);
#ifdef EA_PLATFORM_WIN32
    // PC 0x5A5790: moves the selection off the datum without wrapping (so a
    // list with nothing enabled doesn't spin), then disables its slot unless
    // the selection landed on the end slot it moved toward.
    if (slot == nullptr) {
        return;
    }
    if (datum == SelectedDatum) {
        ScrollerSlot *end;
        if (Scroll(eSD_NEXT)) {
            end = Slots.GetTail();
        } else if (Scroll(eSD_PREV)) {
            end = Slots.GetHead();
        } else {
            return;
        }
        if (slot == end) {
            return;
        }
    }
    slot->Disable();
    return;
#endif
    if (slot != nullptr) {
        if (datum == SelectedDatum) {
            if (!ScrollWrapped(eSD_NEXT)) {
                ScrollWrapped(eSD_PREV);
            }
        }

        slot->Disable();
    }
}

void Scrollerina::CountListIndices() {
    bool found_view = false;
    iNumSlots = 0;
    iViewHeadDataIndex = 1;
    iNumData = 0;

    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        iNumSlots++;
    }

    for (ScrollerDatum *datum = Data.GetHead(); datum != Data.EndOfList(); datum = datum->GetNext()) {
        iNumData++;
        if (!found_view && datum != TopDatum) {
            iViewHeadDataIndex++;
        } else {
            found_view = true;
        }
    }
}

uint32 Scrollerina::GetNodeIndex(ScrollerDatum *datum) {
    uint32 i = 1;
    for (ScrollerDatum *node = Data.GetHead(); node != Data.EndOfList(); node = node->GetNext()) {
        if (datum == node)
            return i;
        i++;
    }
    return 0;
}

uint32 Scrollerina::GetNodeIndex(ScrollerSlot *slot) {
    uint32 i = 1;
    for (ScrollerSlot *node = Slots.GetHead(); node != Slots.EndOfList(); node = node->GetNext()) {
        if (slot == node)
            return i;
        i++;
    }
    return 0;
}

void Scrollerina::SetSelected(ScrollerSlot *slot) {
    if (slot != nullptr && slot->IsEnabled()) {
        ScrollerDatum *datum = FindDatumInSlot(slot);
        if (datum != nullptr) {
            UnHighlightSelected();
            SelectedSlot = slot;
            SelectedDatum = datum;
            HighlightSelected();
            Update(true);
            bViewNeedsSync = false;
        }
    }
}

#ifdef EA_PLATFORM_WIN32
// PC (speed.exe 1.3 @ 0x59B470): scroll the view by rows without moving the
// selection; the highlight follows the selected datum and goes when it leaves
// the view (which then needs a sync before the selection can move).
bool Scrollerina::ScrollView(int dir) {
    if (Slots.IsEmpty() || Data.IsEmpty() || iNumData <= iNumSlots) {
        return false;
    }
    ScrollerDatum *old_top = TopDatum;
    ScrollerSlot *slot = SelectedSlot;
    for (; dir > 0; dir--) {
        if (iViewHeadDataIndex > iNumData - iNumSlots) {
            break;
        }
        if (slot != nullptr) {
            slot = slot == Slots.GetHead() ? nullptr : slot->GetPrev();
        }
        TopDatum = TopDatum->GetNext();
        iViewHeadDataIndex++;
    }
    for (; dir < 0; dir++) {
        if (TopDatum == Data.GetHead()) {
            break;
        }
        if (slot != nullptr) {
            SelectedSlot->UnHighlight();
            slot = slot == Slots.GetTail() ? nullptr : slot->GetNext();
        }
        TopDatum = TopDatum->GetPrev();
        iViewHeadDataIndex--;
    }
    if (slot != SelectedSlot) {
        if (SelectedSlot != nullptr) { // the PC doesn't check
            SelectedSlot->UnHighlight();
        }
        SelectedSlot = slot;
        if (slot != nullptr) {
            slot->Highlight();
        }
    }
    if (old_top == TopDatum) {
        return false;
    }
    uint32 selected = GetNodeIndex(SelectedDatum);
    if (selected < iViewHeadDataIndex || selected >= iNumSlots + iViewHeadDataIndex) {
        bViewNeedsSync = true;
        FEngSetScript(pParentPkg, 0x06745352, 0x0016A259, true);
    } else {
        bViewNeedsSync = false;
        if (SelectedSlot == nullptr) {
            SelectedSlot = FindSlotWithDatum(SelectedDatum);
            HighlightSelected();
        }
    }
    SetDisabledScripts();
    Print();
    DrawScrollBar();
    return true;
}

// PC (speed.exe 1.3 @ 0x59B7F0)
void Scrollerina::PageUp() {
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        ScrollView(-1);
    }
}

// PC (speed.exe 1.3 @ 0x59B830)
void Scrollerina::PageDown() {
    for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList(); slot = slot->GetNext()) {
        ScrollView(1);
    }
}

// PC (speed.exe 1.3 @ 0x5A5540; the PC symbol says
// UIEATraxScreen::ScrollTrackPlayability, but it is this), on
// FEMSG_MOUSE_CHANGED: the wheel over the list scrolls the view; over an
// enabled row the mouse selects it (or, in click-to-select mode, a click
// does) and a click on the selected row sends the mouse-down message; then
// the scroll bar.
void Scrollerina::UpdateMouse() {
    float x, y;
    FEngGetMousePos(x, y);
    if (FEngTestForIntersection(x, y, vTopLeft, vSize)) {
        float wheel;
        FEngGetMouseWheel(wheel);
        if (wheel > 0.0f) {
            ScrollView(-1);
            cFEng::Get()->QueuePackageMessage(0x704, pParentPkg, nullptr);
        } else if (wheel < 0.0f) {
            ScrollView(1);
            cFEng::Get()->QueuePackageMessage(0x148CD7, pParentPkg, nullptr);
        }
        ScrollerSlot *selected = SelectedSlot;
        ScrollerDatum *datum = TopDatum;
        for (ScrollerSlot *slot = Slots.GetHead(); slot != Slots.EndOfList() && datum != Data.EndOfList();
             slot = slot->GetNext(), datum = datum->GetNext()) {
            if (!datum->IsEnabled()) {
                continue;
            }
            bVector2 top_left;
            bVector2 size;
            slot->GetTopLeft(top_left);
            slot->GetSize(size);
            if (!FEngTestForIntersection(x, y, top_left, size)) {
                continue;
            }
            if (!bInClickToSelectMode) {
                if (selected != slot) {
                    SetSelected(slot);
                }
                if (mouseDownMsg != 0 && gFEMouseState.LeftPressed) {
                    cFEng::Get()->QueueGameMessage(mouseDownMsg, pParentPkg, 0xFF);
                }
            } else if (gFEMouseState.LeftPressed) {
                if (selected != slot) {
                    SetSelected(slot);
                } else if (mouseDownMsg != 0) {
                    cFEng::Get()->QueueGameMessage(mouseDownMsg, pParentPkg, 0xFF);
                }
            }
            break;
        }
    }
    ScrollBar.UpdateMouse();
    int scroll = ScrollBar.GetMouseScroll();
    if (scroll == eSD_NONE) {
        DrawScrollBar();
    } else if (scroll == eSD_PAGE_NEXT) {
        PageDown();
    } else if (scroll == eSD_PAGE_PREV) {
        PageUp();
    } else {
        ScrollView(scroll);
    }
}
#endif

void Scrollerina::FindSize() {
    if (pBacking == nullptr) {
        bVector2 top_left;
        bVector2 size;
        ScrollerSlot *slot;
        float top;
        float bottom;
        float left;
        float right;

        slot = Slots.GetHead();
        slot->GetTopLeft(top_left);
        slot->GetSize(size);
        top = top_left.y;
        bottom = top + vSize.y;
        left = top_left.x;
        right = left + vSize.x;

        while (slot != Slots.EndOfList()) {
            slot->GetTopLeft(top_left);
            slot->GetSize(size);
            top = bMin(top, top_left.y);
            bottom = bMax(bottom, top_left.y + size.y);
            left = bMin(left, top_left.x);
            right = bMax(right, top_left.x + size.x);
            slot = slot->GetNext();
        }

        if (pScrollRegion != nullptr) {
            FEngGetTopLeft(pScrollRegion, top_left.x, top_left.y);
            FEngGetSize(pScrollRegion, size.x, size.y);
            top = bMin(top, top_left.y);
            bottom = bMax(bottom, top_left.y + size.y);
            left = bMin(left, top_left.x);
            right = bMax(right, top_left.x + size.x);
        }

        vTopLeft.x = left;
        vTopLeft.y = top;
        vSize.x = bAbs(right - left);
        vSize.y = bAbs(bottom - top);
    }
}
