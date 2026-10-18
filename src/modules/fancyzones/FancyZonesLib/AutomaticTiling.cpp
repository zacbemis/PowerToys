#include "pch.h"
#include "AutomaticTiling.h"

#include <string_view>
#include <unordered_map>

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t EnableVariableName[] = L"POWERTOYS_FANCYZONES_AUTOTILE";
}

namespace
{
    std::unordered_map<std::wstring, TilingTree>& Trees()
    {
        static std::unordered_map<std::wstring, TilingTree> trees;
        return trees;
    }
}

bool AutomaticTiling::IsEnabled() noexcept
{
    static const bool enabled = [] {
        wchar_t value[8]{};
        const DWORD length = GetEnvironmentVariableW(NonLocalizable::EnableVariableName, value, ARRAYSIZE(value));
        return length > 0 && length < ARRAYSIZE(value) && std::wstring_view(value) == L"1";
    }();

    return enabled;
}

TilingTree& AutomaticTiling::Tree(const FancyZonesDataTypes::WorkAreaId& workAreaId)
{
    return Trees()[workAreaId.toString()];
}

void AutomaticTiling::EraseTree(const FancyZonesDataTypes::WorkAreaId& workAreaId)
{
    Trees().erase(workAreaId.toString());
}
