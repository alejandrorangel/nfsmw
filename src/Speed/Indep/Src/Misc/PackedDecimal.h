#ifndef PACKEDDECIMAL_H__
#define PACKEDDECIMAL_H__

#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include "types.h"

template <int b> inline int RaiseToPower(int power) {
    if (power == 0) {
        return 1;
    }
    return RaiseToPower<b>(power - 1) * b;
}

// total size: 0x2
template <typename N, int a, int b, int c, int d> class FloatingPoint {
  public:
    // Functions
    FloatingPoint() {}

    // Normalises val to a mantissa in [a^(b-1), a^b) and a power-of-a exponent
    // (PC: FixedPoint::___tbd, 0x5EA390).
    FloatingPoint(float val) {
        bool neg = false;
        int exp = 0;
        int man = 0;
        if (val < 0.0f) {
            neg = true;
            val *= -1.0f;
        }
        if (val > 0.0f) {
            while (static_cast<float>(GetNormalizedLower()) > val) {
                exp--;
                val *= static_cast<float>(a);
            }
            man = static_cast<int>(val);
            while (man >= GetNormalizedUpper()) {
                man /= a;
                exp++;
            }
        }
        if (neg) {
            man = -man;
        }
        mExp = exp;
        mMan = man;
    }

    // PC: 0x5DE810.
    operator float() const {
        int exp = mExp;
        int man = mMan;
        int factor = RaiseToPower<a>(bAbs(exp));
        if (exp < 0) {
            return static_cast<float>(man) / static_cast<float>(factor);
        }
        return static_cast<float>(man) * static_cast<float>(factor);
    }

    operator int() const {
        return static_cast<int>(static_cast<float>(*this));
    }

  private:
    static int GetNormalizedLower() {
        const int lower = RaiseToPower<a>(b - 1);
        return lower;
    }

    static int GetNormalizedUpper() {
        const int upper = RaiseToPower<a>(b);
        return upper;
    }

    // Members
    N mExp : c; // offset 0x0, size 0x2
    N mMan : d; // offset 0x0, size 0x2
};

// total size: 0x2
template <typename N, int a, int b> class FixedPoint {
  public:
    FixedPoint<N, a, b>() {}
    FixedPoint<N, a, b>(float val) {
        mWord = PORT_FTOU(N, val * GetScale());
    }

    operator float() const {
        return static_cast<float>(mWord) / GetScale();
    }

  private:
    static int GetScale() {
        static const unsigned int scale = RaiseToPower<a>(b);

        return scale;
    }

    N mWord; // offset 0x0, size 0x2
};

typedef FloatingPoint<int8, 10, 1, 3, 5> decimal8;
typedef FloatingPoint<int16, 10, 3, 5, 11> decimal16; // Decl: 107

typedef FixedPoint<int16, 10, 2> decfix16;
typedef FixedPoint<uint16, 10, 2u> udecfix16; // Decl: 110

#endif
