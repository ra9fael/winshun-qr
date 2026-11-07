// qr.exe: the program of the plugin ra9fael.qr. The same split as WinShun's
// own winshun.capture (src/capture-plugin): the preview window is built on
// Qt, and WinShun has Qt where it is installed, so this starter — without Qt
// and with the C runtime linked in — adds WinShun's Qt folder to the folders
// DLLs are looked for in and loads QrApp.dll from its own folder: that one
// imports Qt, which the loader then finds there. Nothing else is searched
// (no PATH, no current folder): the application's folder, System32 and
// WinShun's.
//
// Which folder: the host's shared-Qt contract declares `"qt": "host"` in
// plugin.json and sets WS_QT_DIR (WinShun 0.22.0 and later); WS_APP_DIR is
// the older way (the host's own folder) and stays as the fallback.
//
// `qr.exe --preview (--text <payload> | --clipboard) [--app-dir <folder>]`
// opens a preview window directly (no WinShun, no protocol): how the window
// is checked.

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using Entry = int(__cdecl*)(const wchar_t* appDir);

int fail(int code, const char* what, DWORD error = 0)
{
    // stderr goes to plugins.log; stdout is the protocol's alone.
    if (error)
        std::fprintf(stderr, "qr: %s (error %lu)\n", what, error);
    else
        std::fprintf(stderr, "qr: %s\n", what);
    return code;
}

std::wstring folderOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring environment(const wchar_t* name)
{
    std::wstring value(32768, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
        return {};
    value.resize(length);
    return value;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool preview = false;
    std::wstring appDir;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        preview = preview || arg == L"--preview" || arg == L"--text" || arg == L"--clipboard";
        if (arg == L"--app-dir" && i + 1 < argc)
            appDir = argv[i + 1];
    }
    if (appDir.empty())
        appDir = environment(L"WS_QT_DIR"); // the shared-Qt contract (0.22.0+)
    if (appDir.empty())
        appDir = environment(L"WS_APP_DIR"); // older hosts: their own folder
    if (appDir.empty())
        return fail(2, "no WinShun Qt folder: WS_QT_DIR and WS_APP_DIR are both unset (--preview takes --app-dir)");
    if (::GetFileAttributesW((appDir + L"\\Qt6Core.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
        return fail(3, "no Qt6Core.dll in the WinShun Qt folder: this WinShun cannot run the plugin");
    // The preview uses the same host runtime as a normal plugin start.
    if (!::SetEnvironmentVariableW(L"WS_APP_DIR", appDir.c_str()))
        return fail(3, "WS_APP_DIR could not be passed on", ::GetLastError());

    std::wstring self(32768, L'\0');
    const DWORD selfLength = ::GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (selfLength == 0 || selfLength >= self.size())
        return fail(4, "own path unknown", ::GetLastError());
    self.resize(selfLength);

    if (!::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) || !::AddDllDirectory(appDir.c_str()))
        return fail(5, "DLL folders not set", ::GetLastError());
    const std::wstring dll = folderOf(self) + L"\\QrApp.dll";
    HMODULE module = ::LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!module)
        return fail(6, "QrApp.dll not loaded (a WinShun with another Qt?)", ::GetLastError());
    const char* name = preview ? "winshunQrPreview" : "winshunQrMain";
    const auto entry = reinterpret_cast<Entry>(::GetProcAddress(module, name));
    if (!entry)
        return fail(7, preview ? "QrApp.dll has no winshunQrPreview" : "QrApp.dll has no winshunQrMain");
    return entry(appDir.c_str());
}
