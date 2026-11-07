// native.h - ra9fael.qr's Win32 side: the saved Wi-Fi profiles qrwifi offers.
//
// Windows keeps every network the user has joined as a profile, and its XML
// carries the passphrase. Asking for it with WLAN_PROFILE_GET_PLAINTEXT_KEY
// returns it in the clear without elevation for an all-user profile (no
// out flag) (measured 2026-11-08, see docs/spec.md §4.1). A per-user one
// (WLAN_PROFILE_USER, 0x2) hides its key from this context however it asks,
// and a group-policy one (0x1) hides it always; the row says which. A
// profile that cannot be read at all still lists, named and marked, instead
// of vanishing. So `qrwifi` lists the networks once connected instead of
// asking for the SSID and password again.
//
// This file only collects; logic.h parses the XML and decides what a QR code
// can carry, so the unit tests need no wireless adapter.

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <wlanapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "logic.h"
#include "winshun.h"

namespace ra9fael::qr {

// A failure that means the WLAN AutoConfig service is not running, rather than
// something unexpected.
inline bool wlanServiceDown(DWORD error)
{
    return error == ERROR_SERVICE_NOT_ACTIVE || error == ERROR_SERVICE_DISABLED
        || error == ERROR_INVALID_HANDLE;
}

struct WifiCollection {
    bool noAdapter = false;
    bool serviceDown = false;
    std::string problem; // a wlanapi error that is neither of the two above
    std::vector<WifiProfile> profiles; // connected first, then by SSID
};

namespace detail {

// One profile's XML and how it is stored, from the out flags of
// WlanGetProfile: an all-user profile (no flag) hands its key to this
// context, a per-user one (WLAN_PROFILE_USER, 0x2) hides it, and a
// group-policy one (0x1) hides it from every normal caller (measured
// 2026-11-08, docs/spec.md §4.1). A failed read is not an empty string any
// more — the caller lists the row with `ssid` alone.
struct ProfileRead {
    std::string xml;
    bool perUser = false;
    bool groupPolicy = false;
    std::string problem; // the WlanGetProfile error when the XML is empty
};

inline ProfileRead profileXml(HANDLE client, const GUID& interfaceId, const std::wstring& name)
{
    ProfileRead read;
    LPWSTR text = nullptr;
    DWORD granted = 0;
    DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
    const DWORD rc = ::WlanGetProfile(client, &interfaceId, name.c_str(), nullptr, &text, &flags, &granted);
    if (rc != ERROR_SUCCESS || !text) {
        read.problem = "WlanGetProfile error " + std::to_string(rc);
        return read;
    }
    read.xml = winshun::utf8(std::wstring(text));
    read.perUser = (flags & WLAN_PROFILE_USER) != 0; // 0x2: per-user; no flag means all-user
    read.groupPolicy = (flags & WLAN_PROFILE_GROUP_POLICY) != 0;
    ::WlanFreeMemory(text);
    return read;
}

// The SSID the interface is associated with; empty when it is not connected.
inline std::string currentSsid(HANDLE client, const GUID& interfaceId)
{
    DWORD size = 0;
    WLAN_OPCODE_VALUE_TYPE type = wlan_opcode_value_type_invalid;
    PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
    if (::WlanQueryInterface(client, &interfaceId, wlan_intf_opcode_current_connection, nullptr, &size,
            reinterpret_cast<PVOID*>(&attrs), &type)
            != ERROR_SUCCESS
        || !attrs)
        return {};
    std::string ssid;
    if (attrs->isState == wlan_interface_state_connected) {
        const DOT11_SSID& dot11 = attrs->wlanAssociationAttributes.dot11Ssid;
        std::size_t n = std::min<std::size_t>(dot11.uSSIDLength, DOT11_SSID_MAX_LENGTH);
        ssid.assign(reinterpret_cast<const char*>(dot11.ucSSID), n);
        while (!ssid.empty() && ssid.back() == '\0') // some drivers pad with NULs
            ssid.pop_back();
    }
    ::WlanFreeMemory(attrs);
    return ssid;
}

// The same SSID can be saved on more than one adapter: keep one row, the one
// that is connected, and prefer a profile whose key could be read.
inline void addOrMerge(std::vector<WifiProfile>& out, const WifiProfile& profile)
{
    for (WifiProfile& kept : out) {
        if (kept.ssid != profile.ssid)
            continue;
        if (kept.readFailed && !profile.readFailed) {
            // A readable profile beats a failed read of the same SSID.
            const bool connected = kept.connected || profile.connected;
            kept = profile;
            kept.connected = kept.connected || connected;
            return;
        }
        kept.connected = kept.connected || profile.connected;
        if (kept.key.empty() && !profile.key.empty()) {
            kept.key = profile.key;
            kept.keyPresent = kept.keyPresent || profile.keyPresent;
            kept.keyProtected = profile.keyProtected;
        }
        kept.oneX = kept.oneX || profile.oneX;
        if (kept.authText.empty())
            kept.authText = profile.authText;
        kept.groupPolicy = kept.groupPolicy || profile.groupPolicy;
        kept.perUser = kept.perUser || profile.perUser;
        kept.readFailed = kept.readFailed && profile.readFailed;
        return;
    }
    out.push_back(profile);
}

} // namespace detail

// Every saved profile of every wireless interface. A machine with no adapter,
// or one whose WLAN service is stopped, is reported rather than treated as an
// error the caller would show as "no networks".
inline WifiCollection savedWifiProfiles()
{
    WifiCollection out;
    HANDLE client = nullptr;
    DWORD negotiated = 0;
    const DWORD opened = ::WlanOpenHandle(2, nullptr, &negotiated, &client);
    if (opened != ERROR_SUCCESS) {
        out.serviceDown = wlanServiceDown(opened);
        if (!out.serviceDown)
            out.problem = "WlanOpenHandle error " + std::to_string(opened);
        return out;
    }

    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    const DWORD listed = ::WlanEnumInterfaces(client, nullptr, &interfaces);
    if (listed != ERROR_SUCCESS || !interfaces) {
        out.noAdapter = true;
        ::WlanCloseHandle(client, nullptr);
        return out;
    }
    if (interfaces->dwNumberOfItems == 0)
        out.noAdapter = true;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
        const WLAN_INTERFACE_INFO& info = interfaces->InterfaceInfo[i];
        const std::string connected = detail::currentSsid(client, info.InterfaceGuid);

        PWLAN_PROFILE_INFO_LIST profiles = nullptr;
        if (::WlanGetProfileList(client, &info.InterfaceGuid, nullptr, &profiles) != ERROR_SUCCESS || !profiles)
            continue;
        for (DWORD p = 0; p < profiles->dwNumberOfItems; ++p) {
            const std::wstring name(profiles->ProfileInfo[p].strProfileName);
            const detail::ProfileRead read = detail::profileXml(client, info.InterfaceGuid, name);
            WifiProfile profile;
            if (read.xml.empty()) {
                // A profile that cannot be read (permissions, GP) still gets
                // its row: dropping it here is how qrwifi once showed one
                // network where Windows had three.
                profile.readFailed = true;
                profile.ssid = winshun::utf8(name);
            } else {
                profile = parseWifiProfile(read.xml);
                if (profile.ssid.empty())
                    profile.ssid = winshun::utf8(name); // neither <name> nor <hex>
            }
            profile.perUser = read.perUser;
            profile.groupPolicy = read.groupPolicy;
            profile.connected = !connected.empty() && profile.ssid == connected;
            detail::addOrMerge(out.profiles, profile);
        }
        ::WlanFreeMemory(profiles);
    }
    ::WlanFreeMemory(interfaces);
    ::WlanCloseHandle(client, nullptr);

    std::stable_sort(out.profiles.begin(), out.profiles.end(),
                     [](const WifiProfile& a, const WifiProfile& b) {
                         if (a.connected != b.connected)
                             return a.connected;
                         return a.ssid < b.ssid;
                     });
    return out;
}

} // namespace ra9fael::qr
