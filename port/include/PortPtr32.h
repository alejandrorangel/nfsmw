// 32-bit compressed pointers for game data that's loaded in place.
//
// Some formats (the attribute vault in particular) are "load and go": the file
// is used directly in memory and a fixup table patches 4-byte pointer slots.
// Natively pointers are 8 bytes, so the structs that live in that data use
// PortPtr32<T> for their pointer fields (via PORT_PTR32(T), which is plain T*
// in the original 32-bit builds). Every struct then keeps its on-disk size and
// layout, and the game's own type-size tables stay correct.
//
// Encoding (PortPtr32.cpp):
//   0                    null
//   1 .. 0x7FFFFFFF      offset from the game heap base (main memory pool)
//   0x80000000 | index   anything else, through a small intern table
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

uint32_t PortPtr32Encode(const void *p);
void *PortPtr32Decode(uint32_t bits);

template <typename T> class PortPtr32 {
  public:
    PortPtr32() = default; // trivial: lives inside loaded data and unions
    PortPtr32(T *p) : mBits(PortPtr32Encode(p)) {}
    PortPtr32(std::nullptr_t) : mBits(0) {}
    template <typename U, typename = typename std::enable_if<std::is_convertible<U *, T *>::value>::type>
    PortPtr32(const PortPtr32<U> &other) : mBits(other.Raw()) {}

    PortPtr32 &operator=(T *p) {
        mBits = PortPtr32Encode(p);
        return *this;
    }
    PortPtr32 &operator=(std::nullptr_t) {
        mBits = 0;
        return *this;
    }

    T *Get() const {
        return static_cast<T *>(PortPtr32Decode(mBits));
    }
    operator T *() const {
        return Get();
    }
    T *operator->() const {
        return Get();
    }
    typename std::add_lvalue_reference<T>::type operator*() const {
        return *Get();
    }

    // The stored 32 bits, for slots that may hold an inline value instead.
    uint32_t Raw() const {
        return mBits;
    }
    void SetRaw(uint32_t bits) {
        mBits = bits;
    }

  private:
    uint32_t mBits;
};

static_assert(sizeof(PortPtr32<void>) == 4, "PortPtr32 must stay 4 bytes");
static_assert(std::is_trivially_copyable<PortPtr32<void>>::value, "PortPtr32 must be trivially copyable");
