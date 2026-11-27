#include "./UGroup.hpp"
#include <cstdio>


#include <types.h>

// PC: 0x5C7880
void UDataGroupEncodeTag(unsigned int tag, bool indexed, char *str) {
    unsigned int type;
    unsigned int i;

    *str++ = '{';

    if (indexed) {
        type = UDataGroupType(tag);
        i = 2;
    } else {
        type = tag;
        i = 4;
    }

    while (i-- > 0) {
        *str++ = type >> (i << 3);
    }
    if (indexed) {
        sprintf(str, "%04x", UDataGroupIndex(tag));
        str += 4;
    }

    *str++ = '}';
    *str++ = 0;
}

// STRIPPED
unsigned int UDataGroupDecodeTag(const char *str) {}

// Lower-bound binary search over a tag-sorted array of 0x10-byte records.
// On return tagarray points at the first record whose tag is >= tag; the
// result is that record's tag, or 0 when the search ran off the end.
// PC: 0x5C79A0
static unsigned int SearchTagArray(const TagStruct *&tagarray, unsigned int length, unsigned int tag) {
    if (length == 0) {
        return 0;
    }

    const TagStruct *p = tagarray;
    unsigned int spanlen = length;
    while (spanlen != 0) {
        unsigned int pivot = spanlen >> 1;
        unsigned int pivotTag = p[pivot].tag;
        if (tag < pivotTag) {
            spanlen = pivot;
        } else if (tag > pivotTag) {
            p += pivot + 1;
            spanlen = spanlen - 1 - pivot;
        } else {
            // back up to the first of a run of equal tags
            while (pivot != 0 && p[pivot - 1].tag == tag) {
                pivot--;
            }
            tagarray = &p[pivot];
            return p[pivot].tag;
        }
    }

    unsigned int pos = static_cast<unsigned int>(p - tagarray);
    if (pos < length) {
        if (p->tag < tag) {
            pos++;
        }
        p = &tagarray[pos];
        tagarray = p;
        if (pos < length) {
            return p->tag;
        }
    } else {
        tagarray = p;
    }
    return 0;
}

static unsigned int SearchTagArray(const UData *&dataarray, unsigned int length, unsigned int tag) {
    return SearchTagArray(reinterpret_cast<const TagStruct *&>(dataarray), length, tag);
}

static unsigned int SearchTagArray(const UGroup *&grouparray, unsigned int length, unsigned int tag) {
    return SearchTagArray(reinterpret_cast<const TagStruct *&>(grouparray), length, tag);
}

// The serialized layout split into parts (sections), used to turn the
// offsets stored in a serialized group tree into pointers.
// total size: 0x10
struct UGroupResolverData {
    unsigned int fNumParts;     // offset 0x0, size 0x4
    const unsigned int *fSizes; // offset 0x4, size 0x4
    const void **fPointers;     // offset 0x8, size 0x4
    unsigned int fDeltaAddress; // offset 0xC, size 0x4

    // basePtr + dataOffset, measured from the start of part 0, mapped into
    // whichever part it falls in. With a delta address the stored value p is
    // a pointer from an earlier resolve that just moves by the delta.
    // PC: 0x5C77D0
    void *ConvertOffsetToPointer(const void *basePtr, unsigned int dataOffset, unsigned int p) const {
        if (fDeltaAddress != 0) {
            if (p == 0) {
                return nullptr;
            }
#if defined(EA_PORT_NATIVE)
            // p is an encoded PortPtr32; the delta is a signed 32-bit move.
            return static_cast<char *>(PortPtr32Decode(p)) + static_cast<int>(fDeltaAddress);
#else
            return reinterpret_cast<void *>(fDeltaAddress + p);
#endif
        }

        unsigned int offset =
            static_cast<unsigned int>(static_cast<const char *>(basePtr) - static_cast<const char *>(fPointers[0])) + dataOffset;
        unsigned int part = 0;
        for (unsigned int i = 1; i < fNumParts; i++) {
            if (offset < fSizes[part]) {
                break;
            }
            offset -= fSizes[part];
            part++;
        }
        return const_cast<char *>(static_cast<const char *>(fPointers[part])) + offset;
    }
};

#if defined(EA_PORT_NATIVE)
// Resolved UGroup/UData pointers are 4-byte PortPtr32s, which stay cheap only
// while the data lives inside the game heap. Say so once if it doesn't.
static void CheckResolvedPointer(const PortPtr32<void> &ptr) {
    static bool sWarned = false;
    if (!sWarned && (ptr.Raw() & 0x80000000u) != 0) {
        sWarned = true;
        std::fprintf(stderr, "[ugroup] resolved group data at %p is outside the game heap\n", ptr.Get());
    }
}
#endif

// Replaces every embedded offset in the tree with a pointer.
// PC: 0x5CF5F0
void UGroup::ResolveOffsets(const UGroupResolverData &resolverData) const {
    UGroup *self = const_cast<UGroup *>(this);
    void *array = resolverData.ConvertOffsetToPointer(this, this->fOffset << 4, this->fOffset);
    self->fPointer = array;
    self->fEmbedded = 0;
#if defined(EA_PORT_NATIVE)
    CheckResolvedPointer(self->fPointer);
#endif

    for (const UGroup *g = this->GroupBegin(); g != this->GroupEnd(); ++g) {
        g->ResolveOffsets(resolverData);
    }

    for (const UData *d = this->DataBegin(); d != this->DataEnd(); ++d) {
        UData *data = const_cast<UData *>(d);
        void *pointer = resolverData.ConvertOffsetToPointer(d, d->fOffset, d->fOffset);
        data->fPointer = pointer;
        data->fEmbedded = 0;
    }
}

// PC: 0x5D3C50
const UGroup *UGroup::Deserialize(const void *serializedData, bool resolveOffsets, unsigned int deltaAddress) {
    if (resolveOffsets) {
        unsigned int length = 0x40000000;
        UGroupResolverData resolverData;
        resolverData.fNumParts = 1;
        resolverData.fSizes = &length;
        resolverData.fPointers = &serializedData;
        resolverData.fDeltaAddress = deltaAddress;

        const UGroup *g = static_cast<const UGroup *>(serializedData);
        g->ResolveOffsets(resolverData);
        return g;
    }
    return static_cast<const UGroup *>(serializedData);
}

// PC: 0x5D20F0
const UGroup *UGroup::Deserialize(unsigned int numParts, const unsigned int *dataLengths, const void **serializedData, unsigned int deltaAddress) {
    UGroupResolverData resolverData;
    resolverData.fNumParts = numParts;
    resolverData.fSizes = dataLengths;
    resolverData.fPointers = serializedData;
    resolverData.fDeltaAddress = deltaAddress;

    const UGroup *g = static_cast<const UGroup *>(serializedData[0]);
    g->ResolveOffsets(resolverData);
    return g;
}

// The child groups, followed by the data headers.
// PC: 0x5C8140
const void *UGroup::GetArray() const {
    const void *array;
    if (this->fEmbedded) {
        array = this + this->fOffset;
    } else {
        array = this->fPointer;
    }
    return array;
}

// PC: 0x5CEC10
unsigned int UGroup::GroupCountType(unsigned int type) const {
    unsigned int count = 0;
    for (const UGroup *g = this->GroupBegin(); g != this->GroupEnd(); ++g) {
        if (g->Type() == type) {
            count++;
        }
    }
    return count;
}

// First group of a type: the plain tag for baseIndex == -1, otherwise the
// lowest index in [baseIndex, maxIndex]. GroupEnd() if there is none.
// PC: 0x5CEC70
const UGroup *UGroup::GroupLocateFirst(unsigned int type, unsigned int baseIndex, unsigned int maxIndex) const {
    const UGroup *g = this->GroupBegin();
    if (g != nullptr) {
        if (baseIndex == 0xFFFFFFFF) {
            if (SearchTagArray(g, this->fGroupCount, type) != type) {
                return this->GroupEnd();
            }
        } else {
            unsigned int tag = SearchTagArray(g, this->fGroupCount, UDataGroupTag(type, baseIndex));
            if (((tag & 0xFFFF2020) | 0x2020) != type || UDataGroupIndex(tag) > maxIndex) {
                return this->GroupEnd();
            }
        }
    }
    return g;
}

// PC: 0x5CED30
const UGroup *UGroup::GroupLocateTag(unsigned int typeIndexTag) const {
    const UGroup *g = this->GroupBegin();
    if (g != nullptr) {
        if (!this->fGroupSorted) {
            const UGroup *end = this->GroupEnd();
            while (g < end && g->fTag != typeIndexTag) {
                ++g;
            }
        } else if (SearchTagArray(g, this->fGroupCount, typeIndexTag) != typeIndexTag) {
            return this->GroupEnd();
        }
    }
    return g;
}

// PC: 0x5CEF70
unsigned int UGroup::DataCountType(unsigned int type) const {
    unsigned int count = 0;
    for (const UData *d = this->DataBegin(); d != this->DataEnd(); ++d) {
        if (d->Type() == type) {
            count++;
        }
    }
    return count;
}

// PC: 0x5CEFE0
const UData *UGroup::DataLocateFirst(unsigned int type, unsigned int baseIndex, unsigned int maxIndex) const {
    const UData *d = this->DataBegin();
    if (d != nullptr) {
        if (baseIndex == 0xFFFFFFFF) {
            if (SearchTagArray(d, this->fDataCount, type) != type) {
                return this->DataEnd();
            }
        } else {
            unsigned int tag = SearchTagArray(d, this->fDataCount, UDataGroupTag(type, baseIndex));
            if (((tag & 0xFFFF2020) | 0x2020) != type || UDataGroupIndex(tag) > maxIndex) {
                return this->DataEnd();
            }
        }
    }
    return d;
}

// PC: 0x5CF0D0
const UData *UGroup::DataLocateTag(unsigned int typeIndexTag) const {
    const UData *d = this->DataBegin();
    if (d != nullptr) {
        if (!this->fDataSorted) {
            const UData *end = this->DataEnd();
            while (d < end && d->fTag != typeIndexTag) {
                ++d;
            }
        } else if (SearchTagArray(d, this->fDataCount, typeIndexTag) != typeIndexTag) {
            return this->DataEnd();
        }
    }
    return d;
}

// Children first, then this group's data.
// PC: 0x5CF480
void UGroup::ProcessDepthFirst(Processor &p) const {
    bool keepProcessing = p.StartGroup(this);
    if (keepProcessing) {
        for (unsigned int index = 0; index < this->fGroupCount; index++) {
            if (p.CheckSkipGroups()) {
                break;
            }
            this->GroupBegin()[index].ProcessDepthFirst(p);
        }
    }
    for (unsigned int index = 0; keepProcessing && !p.CheckSkipData() && index < this->fDataCount; index++) {
        keepProcessing = p.ProcessData(this, &this->DataBegin()[index]);
    }
    p.EndGroup(this);
}

// This group's data, then the children.
// PC: 0x5CF530
void UGroup::ProcessBreadthFirst(Processor &p) const {
    bool keepProcessing = p.StartGroup(this);
    if (keepProcessing) {
        for (unsigned int index = 0; !p.CheckSkipData() && index < this->fDataCount; index++) {
            if (!p.ProcessData(this, &this->DataBegin()[index])) {
                p.EndGroup(this);
                return;
            }
        }
        if (!p.CheckSkipGroups()) {
            for (unsigned int index = 0; index < this->fGroupCount; index++) {
                this->GroupBegin()[index].ProcessBreadthFirst(p);
                if (p.CheckSkipGroups()) {
                    break;
                }
            }
        }
    }
    p.EndGroup(this);
}
