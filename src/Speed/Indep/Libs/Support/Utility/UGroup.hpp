#ifndef SUPPORT_UTILITY_UEALIBS_H
#define SUPPORT_UTILITY_UEALIBS_H

#include <types.h>

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#define MAKE_UDATA_TYPE(x) (x << 16)

inline unsigned int UDataGroupTag(unsigned int type, unsigned int index) {
    if (index != -1) {
        return (type & 0xFFFF0000) | index;
    } else {
        return type;
    }
}

inline unsigned int UDataGroupType(unsigned int tag) {
    return tag >> 16;
}

inline unsigned int UDataGroupIndex(unsigned int tag) {
    return tag & 0xFFFF;
}

struct TagStruct {
    unsigned int tag;     // offset 0x0, size 0x4
    unsigned int data[3]; // offset 0x4, size 0xC
};

class UData {
  public:
    // total size: 0x10
    unsigned int fTag;           // offset 0x0, size 0x4
    unsigned int fIndexed : 1;   // offset 0x4, size 0x4
    unsigned int fEmbedded : 1;  // offset 0x4, size 0x4
    unsigned int fAllocated : 1; // offset 0x4, size 0x4
    unsigned int fSection : 5;   // offset 0x4, size 0x4
    unsigned int fSize : 24;     // offset 0x4, size 0x4
    unsigned int fCount;         // offset 0x8, size 0x4
    union {
        PORT_PTR32(void) fPointer; // offset 0x0, size 0x4
        unsigned int fOffset;      // offset 0x0, size 0x4
    }; // offset 0xC, size 0x4

    unsigned int DataCount() const {
        return fCount;
    }

    unsigned int Tag() const {
        return fTag;
    }

    // An indexed tag's type, with the index bits replaced by spaces ('xy  ').
    unsigned int Type() const {
        unsigned int t = fTag;
        if (fIndexed) {
            t = (t & 0xFFFF2020) | 0x2020;
        }
        return t;
    }

    unsigned int DataSize() const {
        return fSize;
    }

    const void *GetDataConst() const {
        const void *data;
        if (fEmbedded) {
            data = reinterpret_cast<const char *>(this) + fOffset;
        } else {
            data = fPointer;
        }
        return data;
    }

    void *GetData() const {
        return const_cast<void *>(GetDataConst());
    }
};

// total size: 0x10
class UGroup {
  public:
    // total size: 0xC
    struct SerializationControls {
        unsigned int fSuppressDuplicates; // offset 0x0, size 0x4
        unsigned int fAlign[2];           // offset 0x4, size 0x8
    };

    // total size: 0xC (PC: vtable at 0x0, mSkipData at 0x4, mSkipGroups at 0x5)
    class Processor {
      public:
        Processor() : mSkipData(false), mSkipGroups(false) {}

        virtual bool StartGroup(const UGroup *g) = 0;
        virtual bool ProcessData(const UGroup *g, const UData *d) = 0;
        virtual void EndGroup(const UGroup *g) = 0;

        void SkipData() {
            mSkipData = true;
        }

        void SkipGroups() {
            mSkipGroups = true;
        }

        bool CheckSkipData() {
            bool skip = mSkipData;
            mSkipData = false;
            return skip;
        }

        bool CheckSkipGroups() {
            bool skip = mSkipGroups;
            mSkipGroups = false;
            return skip;
        }

      private:
        bool mSkipData;   // offset 0x4, size 0x1
        bool mSkipGroups; // offset 0x5, size 0x1
    };

    static const UGroup *Deserialize(const void *serializedData, bool resolveOffsets, unsigned int deltaAddress);
    static const UGroup *Deserialize(unsigned int numParts, const unsigned int *dataLengths, const void **serializedData, unsigned int deltaAddress);
    unsigned int GroupCountType(unsigned int type) const;
    const UGroup *GroupLocateFirst(unsigned int type, unsigned int baseIndex, unsigned int maxIndex) const;
    const UGroup *GroupLocateTag(unsigned int typeIndexTag) const;
    unsigned int DataCountType(unsigned int type) const;
    const UData *DataLocateFirst(unsigned int type, unsigned int baseIndex, unsigned int maxIndex) const;
    const UData *DataLocateTag(unsigned int typeIndexTag) const;
    const void *GetArray() const;
    void ProcessDepthFirst(Processor &p) const;
    void ProcessBreadthFirst(Processor &p) const;

    unsigned int Tag() const {
        return fTag;
    }

    unsigned int Type() const {
        unsigned int t = fTag;
        if (fIndexed) {
            t = (t & 0xFFFF2020) | 0x2020;
        }
        return t;
    }

    unsigned int GroupCount() const {
        return fGroupCount;
    }

    unsigned int DataCount() const {
        return fDataCount;
    }

    const UGroup *GroupBegin() const {
        return static_cast<const UGroup *>(GetArray());
    }

    const UGroup *GroupEnd() const {
        return GroupBegin() + fGroupCount;
    }

    const UData *DataBegin() const {
        return reinterpret_cast<const UData *>(GroupEnd());
    }

    const UData *DataEnd() const {
        return DataBegin() + fDataCount;
    }

    const UGroup *GroupLocate(unsigned int type, unsigned int index) const {
        return GroupLocateTag(UDataGroupTag(type, index));
    }

    const UData *DataLocate(unsigned int type, unsigned int index) const {
        return DataLocateTag(UDataGroupTag(type, index));
    }

  public:
    unsigned int fTag;             // offset 0x0, size 0x4
    unsigned int fIndexed : 1;     // offset 0x4, size 0x4
    unsigned int fEmbedded : 1;    // offset 0x4, size 0x4
    unsigned int fAllocated : 1;   // offset 0x4, size 0x4
    unsigned int fDataSorted : 1;  // offset 0x4, size 0x4
    unsigned int fGroupSorted : 1; // offset 0x4, size 0x4
    unsigned int fGroupCount : 27; // offset 0x4, size 0x4
    unsigned int fDataCount;       // offset 0x8, size 0x4
    union {
        PORT_PTR32(void) fPointer; // offset 0x0, size 0x4
        unsigned int fOffset;      // offset 0x0, size 0x4
    }; // offset 0xC, size 0x4

  private:
    friend struct UGroupResolverData;
    void ResolveOffsets(const struct UGroupResolverData &resolverData) const;
};

#if defined(EA_PORT_NATIVE)
// Loaded in place: both headers must keep their 32-bit layout.
static_assert(sizeof(UData) == 0x10, "UData is 0x10 bytes on disk");
static_assert(sizeof(UGroup) == 0x10, "UGroup is 0x10 bytes on disk");
#endif

#endif
