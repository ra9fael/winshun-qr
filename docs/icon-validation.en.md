# Icon fix 0.1.1

[中文](icon-validation.md)

## Problem

Embedding an ICO alone can leave native windows using a generic icon.

## Cause

Executable resources do not assign window icons. IDI_ICON1 is a string resource name. Qt windows need an explicit application icon resource.

## Changes

Embed the executable icon and include the ICO and resource script in the package. Embed multi-size PNG frames in the Qt GUI library and set the application icon; refuse packaging when the GUI DLL is missing.

## Measured

2026-11-09: built a committed snapshot with MSVC 19.51 / x64 Release; CTest 1/1 passed. Resource and real plugin process checks: 24/24 passed (docs/icon-checks.json), including the named resource, eight sizes, protocol initialization and clean shutdown. Large/small icons and pixels passed on actual Qt native windows. Qt.Tool behavior is preserved (no taskbar or Alt+Tab entry).

## Not tested

Physical taskbar/Alt+Tab interaction, every plugin feature, hardware paths, dragging across actual monitors, UAC secure desktop and all Windows versions were not retested. This run creates local tags and packages only; nothing is published.
