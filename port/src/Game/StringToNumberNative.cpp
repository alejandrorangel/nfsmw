// StringToNumber (Speed/Indep/Libs/Support/Utility/UStringToNumber.h): maps
// enum values to names and back from a null-terminated table. speed.exe 1.3
// (ConvertNumberToString @ 0x5D0640) binary-searches sorted copies when it has
// them and scans the table otherwise; a scan gives the same results.
//
// The input layer names its actions through this (GameActionConverter), so
// without it no controller/keyboard bindings resolve.

#include "Speed/Indep/Libs/Support/Utility/UStringToNumber.h"

#include <cstring>

StringToNumber::StringToNumber(StringToNumberEntry *null_terminated_table)
    : numItems(0), unsorted_table(null_terminated_table), string_sorted_table(nullptr), number_sorted_table(nullptr) {
    while (unsorted_table[numItems].string != nullptr) {
        numItems++;
    }
}

StringToNumber::~StringToNumber() {}

int StringToNumber::ConvertStringToNumber(const char *string) {
    for (int i = 0; i < numItems; i++) {
        if (std::strcmp(unsorted_table[i].string, string) == 0) {
            return unsorted_table[i].number;
        }
    }
    return -1;
}

char *StringToNumber::ConvertNumberToString(int number) {
    for (int i = 0; i < numItems; i++) {
        if (unsorted_table[i].number == number) {
            return unsorted_table[i].string;
        }
    }
    return nullptr;
}
