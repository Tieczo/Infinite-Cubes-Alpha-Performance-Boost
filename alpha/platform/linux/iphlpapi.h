// The adapter record cube::System declares (its script functions are left out of 64-bit builds).
#pragma once
#include <windows.h>
typedef struct IP_ADAPTER_INFO {
    struct IP_ADAPTER_INFO *Next;
    UINT Type;
    UINT AddressLength;
    BYTE Address[8];
} IP_ADAPTER_INFO;
