# Changelog

[中文](CHANGELOG.md)

## [Unreleased]

## [0.2.3] - 2026-11-22

### Changed

- Each preview now opens its own window: the ones already open stay, and each closes on its own, instead of the new one replacing the old. A new window opens beside the newest one; with none open, the last place and size are kept.

## [0.2.2] - 2026-11-22

### Fixed

- A preview opened from a result's right-click menu no longer stays behind other programs: besides the foreground right the host grants, the window goes on asking for the foreground for a moment after it appears, and takes it back once when the launcher hides and hands it to the program it covered.
- The preview no longer turns black after a session unlock, a resume, the display coming back or a composition restart: it repaints on those and every five seconds as the last resort.

## [0.2.1] - 2026-11-09

- Use the same native preview window in QR and Wi-Fi: plugin icon, one elided content line, titlebar pin toggle, and no topmost flag by default. Image actions stay in the context menu.
- Mouse-wheel zoom scales the image and window together within the monitor work area.
- Keep focused QR previews open after focus changes; resize with window borders and drag anywhere in the content to move.
- Close the focused preview with Esc, Ctrl+D or Ctrl+W. Right-click to copy the original image, save PNG, restore the default size or close.
- Keep QR modules crisp while resizing, retain the current image snapshot for menu actions, and prevent five-minute idle shutdown while a preview is open.
- Build, test and publish only on new version tags or manual runs; verification defaults to no publication.

## [0.2.0] - 2026-11-09

- Remove the qrwifi command and WLAN/profile/password handling. Wi-Fi QR sharing now belongs to ra9fael.wifi.
- Keep text/link/clipboard QR generation, copying, preview and PNG saving. Explicitly activate and focus the preview when it opens.

## [0.1.1] - 2026-11-09

### Fixed

- Embed the executable icon and include the ICO and resource script in the package. Embed multi-size PNG frames in the Qt GUI library and set the application icon; refuse packaging when the GUI DLL is missing.

### Notes

- Local tag and package only. See docs/icon-validation.en.md for the measured coverage; no GitHub release. Restart the plugin process after upgrading.
