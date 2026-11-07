// probe_profiles.cpp - dump per-profile WLAN read facts: rc, granted access,
// out-flags (per-user / all-user / group policy), <protected>, and whether the
// call is stable across repeats. Explains "sometimes the key cannot be read".
//
// Build: cl /nologo /EHsc /W3 probe_profiles.cpp wlanapi.lib /Fe:probe_profiles.exe
#include <winsock2.h>
#include <windows.h>
#include <wlanapi.h>
#include <cstdio>

int main()
{
    HANDLE client = nullptr;
    DWORD negotiated = 0;
    const DWORD opened = ::WlanOpenHandle(2, nullptr, &negotiated, &client);
    printf("WlanOpenHandle: %lu (negotiated %lu)\n", opened, negotiated);
    if (opened != ERROR_SUCCESS)
        return 1;

    PWLAN_INTERFACE_INFO_LIST ifaces = nullptr;
    if (::WlanEnumInterfaces(client, nullptr, &ifaces) != ERROR_SUCCESS || !ifaces)
        return 1;
    printf("interfaces: %lu\n", ifaces->dwNumberOfItems);

    for (DWORD i = 0; i < ifaces->dwNumberOfItems; ++i) {
        const GUID& g = ifaces->InterfaceInfo[i].InterfaceGuid;
        PWLAN_PROFILE_INFO_LIST list = nullptr;
        if (::WlanGetProfileList(client, &g, nullptr, &list) != ERROR_SUCCESS || !list)
            continue;
        printf("interface %lu: %lu profiles\n", i, list->dwNumberOfItems);
        for (DWORD p = 0; p < list->dwNumberOfItems; ++p) {
            const wchar_t* name = list->ProfileInfo[p].strProfileName;
            wprintf(L"  [%s]\n", name);
            for (int attempt = 1; attempt <= 2; ++attempt) {
                for (int plain = 1; plain >= 0; --plain) {
                    LPWSTR xml = nullptr;
                    DWORD granted = 0;
                    DWORD flags = plain ? WLAN_PROFILE_GET_PLAINTEXT_KEY : 0;
                    const DWORD rc = ::WlanGetProfile(client, &g, name, nullptr, &xml, &flags, &granted);
                    int protectedKey = -1;
                    char key = '?';
                    if (rc == ERROR_SUCCESS && xml) {
                        protectedKey = wcsstr(xml, L"<protected>true</protected>") != nullptr;
                        key = wcsstr(xml, L"<keyMaterial>") != nullptr ? 'K' : '-';
                    }
                    printf("    attempt%d %s: rc=%lu granted=%lu outflags=0x%lX protected=%d key=%c\n",
                           attempt, plain ? "plaintext" : "plain=no ", rc, granted, flags,
                           protectedKey, key);
                    if (xml)
                        ::WlanFreeMemory(xml);
                }
            }
        }
        ::WlanFreeMemory(list);
    }
    ::WlanFreeMemory(ifaces);
    ::WlanCloseHandle(client, nullptr);
    return 0;
}
