// PC_GetOSLanguage natively: the first of the user's preferred languages the
// game ships, else English. (On PC this came from the install settings.)

#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <cstring>
#include <strings.h>

eLanguages PC_GetOSLanguage() {
    static const struct {
        const char *code;
        eLanguages language;
    } kLanguages[] = {
        {"en", eLANGUAGE_ENGLISH}, {"fr", eLANGUAGE_FRENCH},  {"de", eLANGUAGE_GERMAN}, {"it", eLANGUAGE_ITALIAN},
        {"es", eLANGUAGE_SPANISH}, {"nl", eLANGUAGE_DUTCH},   {"sv", eLANGUAGE_SWEDISH}, {"da", eLANGUAGE_DANISH},
        {"pl", eLANGUAGE_POLISH},  {"fi", eLANGUAGE_FINNISH},
    };
    eLanguages result = eLANGUAGE_ENGLISH;
    CFArrayRef preferred = CFLocaleCopyPreferredLanguages();
    if (preferred != nullptr) {
        for (CFIndex i = 0; i < CFArrayGetCount(preferred) && result == eLANGUAGE_ENGLISH; i++) {
            char code[16] = {};
            CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, i)), code, sizeof(code), kCFStringEncodingUTF8);
            bool matched = false;
            for (const auto &entry : kLanguages) {
                if (strncasecmp(code, entry.code, 2) == 0) {
                    result = entry.language;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                break;
            }
        }
        CFRelease(preferred);
    }
    return result;
}
