#pragma once

// Every QSettings key ISFLab stores, in one place so that readers and writers agree.
namespace SettingsKeys {
inline constexpr auto AudioInputDevice = "audio/inputDevice";
inline constexpr auto AudioMode = "audio/mode";
inline constexpr auto AudioFile = "audio/file";
inline constexpr auto WindowGeometry = "mainWindow/geometry";
inline constexpr auto BinFlat = "mediaBin/shadersOnly";
inline constexpr auto BinEntries = "mediaBin/entries";
inline constexpr auto BinLastFolder = "mediaBin/lastFolder";
inline constexpr auto BinActive = "mediaBin/active";
inline constexpr auto BinDisabledTypes = "mediaBin/disabledTypes";
inline constexpr auto BinDisabledCategories = "mediaBin/disabledCategories";
inline constexpr auto BinNameFilter = "mediaBin/nameFilter";
inline constexpr auto PreviewResolution = "preview/fixedResolution";
inline constexpr auto PreviewTrackMouse = "preview/trackMouse";
inline constexpr auto PreviewColorPicker = "preview/colorPicker";
}
