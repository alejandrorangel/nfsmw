// UTL::COM::Object::_IList: the interface table behind IUnknown::QueryInterface
// (they were link stubs, so every QueryInterface failed). speed.exe 1.3: a
// vector of {handle, IUnknown*} sorted by handle -- ctor 0x5D8660 (reserve),
// Add 0x5D7930 (insert at lower_bound), Find 0x5D59F0 (lower_bound, match),
// Remove 0x5D5A50 (erase the entry for that interface).

#include "Speed/Indep/Libs/Support/Utility/UCOM.h"

#include <algorithm>

namespace UTL {
namespace COM {

namespace {
template <typename It> It LowerBound(It first, It last, void *handle) {
    return std::lower_bound(first, last, handle, [](const auto &pair, void *h) {
        return reinterpret_cast<uintptr_t>(pair.handle) < reinterpret_cast<uintptr_t>(h);
    });
}
} // namespace

Object::_IList::_IList(unsigned int icount) {
    reserve(icount);
}

Object::_IList::~_IList() {}

void Object::_IList::Add(HINTERFACE handle, IUnknown *ref) {
    insert(LowerBound(begin(), end(), handle), _IPair(handle, ref));
}

IUnknown *Object::_IList::Find(HINTERFACE handle) {
    auto it = LowerBound(begin(), end(), handle);
    if (it != end() && it->handle == handle) {
        return it->ref;
    }
    return nullptr;
}

void *Object::_IList::Find(const IUnknown *pUnk) const {
    for (const _IPair &pair : *this) {
        if (pair.ref == pUnk) {
            return pair.handle;
        }
    }
    return nullptr;
}

void Object::_IList::Remove(IUnknown *pUnk) {
    for (auto it = begin(); it != end(); ++it) {
        if (it->ref == pUnk) {
            erase(it);
            return;
        }
    }
}

} // namespace COM
} // namespace UTL
