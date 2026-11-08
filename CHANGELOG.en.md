# Changelog

[中文](CHANGELOG.md)

## [Unreleased]


## [0.2.0] - 2026-11-09

- Remove the qrwifi command and WLAN/profile/password handling. Wi-Fi QR sharing now belongs to ra9fael.wifi.
- Keep text/link/clipboard QR generation, copying, preview and PNG saving. Explicitly activate and focus the preview when it opens.

## [0.1.1] - 2026-11-09

### Fixed

- Embed the executable icon and include the ICO and resource script in the package. Embed multi-size PNG frames in the Qt GUI library and set the application icon; refuse packaging when the GUI DLL is missing.

### Notes

- Local tag and package only. See docs/icon-validation.en.md for the measured coverage; no GitHub release. Restart the plugin process after upgrading.
