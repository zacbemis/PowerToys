#pragma once

#include <FancyZonesLib/FancyZonesDataTypes.h>
#include <FancyZonesLib/TilingTree.h>

// Phase 0 spike of Automatic FancyZones: Hyprland style automatic tiling.
// Enabled by setting POWERTOYS_FANCYZONES_AUTOTILE=1 in the environment before PowerToys starts.
// Monitors using the "No layout" (Blank) layout then tile their windows automatically.
// All state is accessed from the FancyZones main thread only.
namespace AutomaticTiling
{
    // No gaps between tiles for now
    constexpr int TileSpacing = 0;

    bool IsEnabled() noexcept;

    // Trees are kept here rather than in WorkArea, because work areas are recreated
    // on display and virtual desktop changes.
    TilingTree& Tree(const FancyZonesDataTypes::WorkAreaId& workAreaId);
    void EraseTree(const FancyZonesDataTypes::WorkAreaId& workAreaId);

    // Windows Snap moves and resizes windows on its own, without FancyZones knowing, which fights the tiles.
    // While any monitor tiles automatically, snapping windows dragged to screen edges is turned off, then put back
    // as it was. Window arrangement itself stays on, as turning it off also stops maximized windows from being
    // dragged. The Windows 11 snap layouts flyout can't be turned off this way, as Explorer only reads that setting
    // when it starts. The original setting is kept in a file, so it is still restored after an unclean exit.
    void SuppressWindowsSnap(bool suppress);
}
