// Runtime side of the generated link stubs (see port/tools/gen_stubs.py).
#include <cstdio>
#include <cstdlib>

// rec points at a writable record: one "reported" flag byte, then the
// NUL-terminated symbol name. Each stub reports itself once.
extern "C" void port_stub_log(unsigned char *rec) {
    static const bool fatal = std::getenv("NFSMW_STUBS_FATAL") != nullptr;
    if (rec[0] == 0) {
        rec[0] = 1;
        std::fprintf(stderr, "[stub] %s\n", reinterpret_cast<const char *>(rec + 1));
    }
    if (fatal) {
        std::abort();
    }
}
