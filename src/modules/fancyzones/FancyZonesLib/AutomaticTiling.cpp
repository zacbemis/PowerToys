#include "pch.h"
#include "AutomaticTiling.h"

#include <filesystem>
#include <unordered_map>

#include <common/SettingsAPI/settings_helpers.h>
#include <common/utils/json.h>

#include <FancyZonesLib/ModuleConstants.h>
#include <FancyZonesLib/Settings.h>

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t WindowsSnapFileName[] = L"\\automatic-tiling-windows-snap.json";
    const wchar_t DockMovingID[] = L"dock-moving";
}

namespace
{
    std::unordered_map<std::wstring, TilingTree>& Trees()
    {
        static std::unordered_map<std::wstring, TilingTree> trees;
        return trees;
    }

    std::wstring WindowsSnapFile()
    {
        return PTSettingsHelper::get_module_save_folder_location(NonLocalizable::ModuleKey) + NonLocalizable::WindowsSnapFileName;
    }

    // Snapping windows dragged to the top, left or right of a monitor
    bool IsDockMovingOn() noexcept
    {
        BOOL on = FALSE;
        return SystemParametersInfoW(SPI_GETDOCKMOVING, 0, &on, 0) && on;
    }

    void SetDockMoving(bool on) noexcept
    {
        // The documentation says the value goes in pvParam, but Windows reads it from uiParam
        SystemParametersInfoW(SPI_SETDOCKMOVING, on, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
}

bool AutomaticTiling::IsEnabled() noexcept
{
    return FancyZonesSettings::settings().automaticTiling;
}

TilingTree& AutomaticTiling::Tree(const FancyZonesDataTypes::WorkAreaId& workAreaId)
{
    return Trees()[workAreaId.toString()];
}

void AutomaticTiling::EraseTree(const FancyZonesDataTypes::WorkAreaId& workAreaId)
{
    Trees().erase(workAreaId.toString());
}

void AutomaticTiling::SuppressWindowsSnap(bool suppress)
{
    const auto file = WindowsSnapFile();
    std::error_code error;
    const bool suppressed = std::filesystem::exists(file, error);

    if (suppress)
    {
        // Remember the original settings only once, so a restart after a crash doesn't save the suppressed ones
        if (!suppressed)
        {
            json::JsonObject state;
            state.SetNamedValue(NonLocalizable::DockMovingID, json::value(IsDockMovingOn()));
            json::to_file(file, state);
        }

        if (IsDockMovingOn())
        {
            SetDockMoving(false);
        }
    }
    else if (suppressed)
    {
        if (const auto state = json::from_file(file))
        {
            SetDockMoving(state->GetNamedBoolean(NonLocalizable::DockMovingID, true));
        }

        std::filesystem::remove(file, error);
    }
}
