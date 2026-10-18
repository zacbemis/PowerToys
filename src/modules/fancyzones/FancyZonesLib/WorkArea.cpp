#include "pch.h"
#include "WorkArea.h"

#include <common/logger/logger.h>

#include "FancyZonesData/AppliedLayouts.h"
#include "FancyZonesData/AppZoneHistory.h"
#include "FancyZonesData/CustomLayouts.h"
#include "ZonesOverlay.h"
#include "Settings.h"
#include <FancyZonesLib/AutomaticTiling.h>
#include <FancyZonesLib/FancyZonesWindowProcessing.h>
#include <FancyZonesLib/FancyZonesWindowProperties.h>
#include <FancyZonesLib/VirtualDesktop.h>
#include <FancyZonesLib/WindowUtils.h>

// disabling warning 4458 - declaration of 'identifier' hides class member
// to avoid warnings from GDI files - can't add winRT directory to external code
// in the Cpp.Build.props
#pragma warning(push)
#pragma warning(disable : 4458)
#include <gdiplus.h>
#pragma warning(pop)

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t ToolWindowClassName[] = L"FancyZones_ZonesOverlay";
    const wchar_t ToolWindowName[] = L"FancyZones_ZonesOverlay";
}

namespace TileSizeCheck
{
    constexpr UINT_PTR TimerId = 1;
    constexpr UINT DelayMillis = 250;

    std::optional<RECT> VisibleFrame(HWND window) noexcept
    {
        RECT frame{};
        if (!IsWindow(window) || FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        {
            return std::nullopt;
        }

        return frame;
    }

    // Windows that can't be resized stay where they are
    bool CanTile(HWND window) noexcept
    {
        return FancyZonesWindowProcessing::IsProcessableAutomatically(window) &&
               FancyZonesWindowUtils::HasStyle(GetWindowLong(window, GWL_STYLE), WS_THICKFRAME);
    }
}

namespace LiveResize
{
    constexpr UINT_PTR TimerId = 2;

    // About one display frame
    constexpr UINT IntervalMillis = 16;

    // A tile that hasn't finished its last resize by then gets the next one anyway
    constexpr ULONGLONG BusyWindowTimeoutMillis = 100;
}

using namespace FancyZonesUtils;

namespace
{
    // The reason for using this class is the need to call ShowWindow(window, SW_SHOWNORMAL); on each
    // newly created window for it to be displayed properly. The call sometimes has side effects when
    // a fullscreen app is running, and happens when the resolution change event is triggered
    // (e.g. when running some games).
    // This class will serve as a pool of windows for which this call was already done.
    class WindowPool
    {
        std::vector<HWND> m_pool;
        std::mutex m_mutex;

        HWND ExtractWindow()
        {
            std::unique_lock lock(m_mutex);

            if (m_pool.empty())
            {
                return NULL;
            }

            HWND window = m_pool.back();
            m_pool.pop_back();
            return window;
        }

    public:
        HWND NewZonesOverlayWindow(Rect position, HINSTANCE hinstance, WorkArea* owner)
        {
            HWND windowFromPool = ExtractWindow();
            if (windowFromPool == NULL)
            {
                HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, NonLocalizable::ToolWindowClassName, NonLocalizable::ToolWindowName, WS_POPUP, position.left(), position.top(), position.width(), position.height(), nullptr, nullptr, hinstance, owner);
                Logger::info("Creating new ZonesOverlay window, hWnd = {}", (void*)window);
                FancyZonesWindowUtils::MakeWindowTransparent(window);

                // According to ShowWindow docs, we must call it with SW_SHOWNORMAL the first time
                ShowWindow(window, SW_SHOWNORMAL);
                ShowWindow(window, SW_HIDE);
                return window;
            }
            else
            {
                Logger::info("Reusing ZonesOverlay window from pool, hWnd = {}", (void*)windowFromPool);
                SetWindowLongPtrW(windowFromPool, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
                MoveWindow(windowFromPool, position.left(), position.top(), position.width(), position.height(), TRUE);
                return windowFromPool;
            }
        }

        void FreeZonesOverlayWindow(HWND window)
        {
            Logger::info("Freeing ZonesOverlay window into pool, hWnd = {}", (void*)window);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            ShowWindow(window, SW_HIDE);

            std::unique_lock lock(m_mutex);
            m_pool.push_back(window);
        }

        ~WindowPool()
        {
            for (HWND window : m_pool)
            {
                DestroyWindow(window);
            }
        }
    };

    WindowPool windowPool;
}

WorkArea::WorkArea(HINSTANCE hinstance, const FancyZonesDataTypes::WorkAreaId& uniqueId, const FancyZonesUtils::Rect& workAreaRect) :
    m_uniqueId(uniqueId),
    m_workAreaRect(workAreaRect)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = s_WndProc;
    wcex.hInstance = hinstance;
    wcex.lpszClassName = NonLocalizable::ToolWindowClassName;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wcex);
}

WorkArea::~WorkArea()
{
    // Tear down the renderer (joining its background thread) before returning
    // the HWND to the pool. Otherwise, the render thread can still be drawing
    // through m_renderTarget into an HWND that has already been recycled by a
    // subsequent NewZonesOverlayWindow call.
    m_zonesOverlay.reset();
    if (m_window)
    {
        KillTimer(m_window, TileSizeCheck::TimerId);
        KillTimer(m_window, LiveResize::TimerId);
    }

    windowPool.FreeZonesOverlayWindow(m_window);
}

bool WorkArea::Snap(HWND window, const ZoneIndexSet& zones, bool updatePosition)
{
    if (!m_layout || zones.empty())
    {
        return false;
    }

    for (ZoneIndex zone : zones)
    {
        if (static_cast<size_t>(zone) >= m_layout->Zones().size())
        {
            return false;
        }
    }

    m_layoutWindows.Assign(window, zones);

    // Tile indices change on every reflow, so they are not worth remembering
    if (!IsAutomaticTiling())
    {
        AppZoneHistory::instance().SetAppLastZones(window, m_uniqueId, m_layout->Id(), zones);
    }

    if (updatePosition)
    {
        const auto rect = m_layout->GetCombinedZonesRect(zones);
        const auto adjustedRect = FancyZonesWindowUtils::AdjustRectForSizeWindowToRect(window, rect, m_window);
        FancyZonesWindowUtils::SaveWindowSizeAndOrigin(window);
        FancyZonesWindowUtils::SizeWindowToRect(window, adjustedRect);
    }

    return FancyZonesWindowProperties::StampZoneIndexProperty(window, zones);
}

bool WorkArea::Unsnap(HWND window)
{
    if (!m_layout)
    {
        return false;
    }
    
    m_layoutWindows.Dismiss(window);
    AppZoneHistory::instance().RemoveAppLastZone(window, m_uniqueId, m_layout->Id());
    FancyZonesWindowProperties::RemoveZoneIndexProperty(window);

    return true;
}

const GUID WorkArea::GetLayoutId() const noexcept
{
    if (m_layout)
    {
        return m_layout->Id();
    }

    return GUID{};
}

void WorkArea::ShowZones(const ZoneIndexSet& highlight, HWND draggedWindow/* = nullptr*/)
{
    if (m_layout && m_zonesOverlay)
    {
        SetWorkAreaWindowAsTopmost(draggedWindow);
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), highlight, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
        m_zonesOverlay->Show();
    }
}

void WorkArea::HideZones()
{
    if (m_zonesOverlay)
    {
        m_zonesOverlay->Hide();
    }
}

void WorkArea::FlashZones()
{
    if (m_layout && m_zonesOverlay)
    {
        SetWorkAreaWindowAsTopmost(nullptr);
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), {}, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
        m_zonesOverlay->Flash();
    }
}

void WorkArea::ShowLayoutNameLabel(const std::wstring& name)
{
    if (m_zonesOverlay && FancyZonesSettings::settings().layoutNameLabelEnabled)
    {
        const auto& settings = FancyZonesSettings::settings();
        const ZonesOverlay::LayoutNameLabelOptions options{
            .textColor = FancyZonesUtils::HexToRGB(settings.layoutNameLabelTextColor),
            .backgroundColor = FancyZonesUtils::HexToRGB(settings.layoutNameLabelBackgroundColor, RGB(38, 38, 38)),
            .fontSize = settings.layoutNameLabelFontSize,
            .padding = settings.layoutNameLabelPadding,
            .durationMillis = settings.layoutNameLabelDuration,
            .placement = settings.layoutNameLabelPlacement,
        };
        m_zonesOverlay->ShowLayoutName(name, options);
    }
}

void WorkArea::ShowMonitorRotationPreview(const std::vector<RECT>& windowRects, size_t monitorNumber, std::optional<bool> reverse, bool animateRotation)
{
    if (!m_zonesOverlay)
    {
        return;
    }

    std::vector<RECT> localWindowRects;
    localWindowRects.reserve(windowRects.size());
    for (const auto& rect : windowRects)
    {
        localWindowRects.push_back(RECT{
            .left = rect.left - m_workAreaRect.left(),
            .top = rect.top - m_workAreaRect.top(),
            .right = rect.right - m_workAreaRect.left(),
            .bottom = rect.bottom - m_workAreaRect.top(),
        });
    }

    SetWorkAreaWindowAsTopmost(nullptr);
    m_zonesOverlay->DrawMonitorRotationPreview(localWindowRects, monitorNumber, reverse, animateRotation);
    m_zonesOverlay->Show();
}

void WorkArea::InitLayout()
{
    InitLayout({});

    if (m_window && m_layout)
    {
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), {}, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
    }
}

void WorkArea::UpdateWindowPositions()
{
    const auto& snappedWindows = m_layoutWindows.SnappedWindows();
    for (const auto& [window, zones] : snappedWindows)
    {
        Snap(window, zones, true);
    }
}

void WorkArea::CycleWindows(HWND window, bool reverse)
{
    m_layoutWindows.CycleWindows(window, reverse);
}

bool WorkArea::IsAutomaticTiling() const noexcept
{
    // Spanning zones across monitors is not supported by the spike
    return AutomaticTiling::IsEnabled() && m_layout && m_layout->Type() == FancyZonesDataTypes::ZoneSetLayoutType::Blank && m_uniqueId.monitorId.monitor;
}

bool WorkArea::IsTiled(HWND window) const
{
    return IsAutomaticTiling() && AutomaticTiling::Tree(m_uniqueId).Contains(window);
}

bool WorkArea::Tile(HWND window, HWND target)
{
    if (!IsAutomaticTiling() || !TileSizeCheck::CanTile(window))
    {
        return false;
    }

    if (!AutomaticTiling::Tree(m_uniqueId).Insert(window, target, TileArea(), AutomaticTiling::TileSpacing))
    {
        return false;
    }

    ReflowTiles();
    return true;
}

bool WorkArea::Untile(HWND window)
{
    if (!IsAutomaticTiling() || !AutomaticTiling::Tree(m_uniqueId).Remove(window))
    {
        return false;
    }

    m_layoutWindows.Dismiss(window);
    m_misplacedWindows.erase(window);
    m_selfPlacingWindows.erase(window);
    m_forcedSizeWindows.erase(window);
    m_liveResizeRequests.erase(window);
    FancyZonesWindowProperties::RemoveZoneIndexProperty(window);

    // A window closed or moved away in the middle of a border drag ends that drag
    if (m_resizingWindow == window)
    {
        KillTimer(m_window, LiveResize::TimerId);
        m_liveResizePending = false;
        m_resizingWindow = nullptr;
    }
    ReflowTiles();
    return true;
}

void WorkArea::ReflowTiles()
{
    if (!IsAutomaticTiling())
    {
        return;
    }

    const auto rects = ApplyTileZones();
    for (size_t i = 0; i < rects.size(); i++)
    {
        // Placing a window again where it already is makes some apps drift, so only move windows that need it
        const auto& [window, tile] = rects[i];
        const bool inTile = IsInTile(window, tile);
        Snap(window, { static_cast<ZoneIndex>(i) }, !inTile);

        if (!inTile && m_forcedSizeWindows.contains(window))
        {
            ForceTileSize(window, tile);
        }
    }

    // Windows move asynchronously and may refuse to shrink, so check where they ended up a moment later
    if (m_window && !rects.empty())
    {
        SetTimer(m_window, TileSizeCheck::TimerId, TileSizeCheck::DelayMillis, nullptr);
    }
}

void WorkArea::BeginTileResize(HWND window)
{
    m_resizingWindow = window;
}

void WorkArea::UpdateTileResize(HWND window)
{
    if (window != m_resizingWindow || !IsAutomaticTiling() || m_liveResizePending)
    {
        return;
    }

    // A dragged border can move many times per frame. Following each move floods the other tiles with
    // resizes, and apps that repaint slowly then hold up the drag, so the latest position is applied once per frame.
    m_liveResizePending = SetTimer(m_window, LiveResize::TimerId, LiveResize::IntervalMillis, nullptr) != 0;
    if (!m_liveResizePending)
    {
        ApplyTileResize();
    }
}

void WorkArea::ApplyTileResize()
{
    const auto frame = TileSizeCheck::VisibleFrame(m_resizingWindow);
    if (!frame.has_value())
    {
        return;
    }

    // The dragged border moves the dividers along it, and the tiles on their other side follow
    RECT rect = frame.value();
    MapWindowRect(nullptr, m_window, &rect);
    if (AutomaticTiling::Tree(m_uniqueId).ResizeTile(m_resizingWindow, rect, TileArea(), AutomaticTiling::TileSpacing))
    {
        ReflowTilesLive(m_resizingWindow);
    }
}

void WorkArea::EndTileDrag()
{
    if (m_liveResizePending)
    {
        KillTimer(m_window, LiveResize::TimerId);
        m_liveResizePending = false;
    }

    // Catch up with where the border was released before placing every window normally
    if (m_resizingWindow && IsAutomaticTiling())
    {
        ApplyTileResize();
    }

    m_resizingWindow = nullptr;
    m_liveResizeRequests.clear();
    ReflowTiles();
}

void WorkArea::CheckTiles()
{
    // Tiles are only checked once a drag has ended
    if (!IsAutomaticTiling() || m_resizingWindow)
    {
        return;
    }

    bool recheck = false;
    const auto rects = AutomaticTiling::Tree(m_uniqueId).CalculateRects(TileArea(), AutomaticTiling::TileSpacing);
    for (size_t i = 0; i < rects.size(); i++)
    {
        const auto& [window, tile] = rects[i];
        const auto frame = TileSizeCheck::VisibleFrame(window);
        if (!frame.has_value() || IsIconic(window) || IsZoomed(window))
        {
            continue;
        }

        // A window kept bigger than its tile by its minimum size is resized anyway, like in Hyprland.
        // If it grows back by itself after that, it is left alone rather than fought over.
        const RECT screenTile = ToScreen(tile);
        const bool oversized = frame->right - frame->left > screenTile.right - screenTile.left ||
                               frame->bottom - frame->top > screenTile.bottom - screenTile.top;
        if (oversized && m_forcedSizeWindows.insert(window).second)
        {
            ForceTileSize(window, tile);
            recheck = true;
        }

        const bool misplaced = frame->left != screenTile.left || frame->top != screenTile.top;
        if (!misplaced || m_selfPlacingWindows.contains(window))
        {
            m_misplacedWindows.erase(window);
            continue;
        }

        // The window may still be moving, so put it back once before deciding it places itself
        if (m_misplacedWindows.insert(window).second)
        {
            Snap(window, { static_cast<ZoneIndex>(i) });
            recheck = true;
        }
        else
        {
            m_misplacedWindows.erase(window);
            m_selfPlacingWindows.insert(window);
        }
    }

    if (recheck)
    {
        SetTimer(m_window, TileSizeCheck::TimerId, TileSizeCheck::DelayMillis, nullptr);
    }
}

void WorkArea::ForceTileSize(HWND window, const RECT& tile)
{
    // Skipping WM_WINDOWPOSCHANGING skips the window's minimum size too.
    // Asynchronous so that a hung window can't block FancyZones.
    const RECT rect = FancyZonesWindowUtils::AdjustRectForSizeWindowToRect(window, tile, m_window);
    constexpr UINT flags = SWP_NOSENDCHANGING | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
    SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags);
}

RECT WorkArea::ToScreen(RECT tile) const noexcept
{
    MapWindowRect(m_window, nullptr, &tile);
    return tile;
}

std::optional<WorkArea::TileDrop> WorkArea::DropAt(HWND window, POINT cursor) const
{
    if (!IsAutomaticTiling() || !m_window)
    {
        return std::nullopt;
    }

    ScreenToClient(m_window, &cursor);
    const auto& tree = AutomaticTiling::Tree(m_uniqueId);
    const auto rects = tree.CalculateRects(TileArea(), AutomaticTiling::TileSpacing);
    if (rects.empty())
    {
        RECT whole = TileArea();
        InflateRect(&whole, -AutomaticTiling::TileSpacing, -AutomaticTiling::TileSpacing);
        return TileDrop{ nullptr, TilingTree::Side::Left, whole };
    }

    // Over its own tile the window just goes back where it was
    for (const auto& [tileWindow, tile] : rects)
    {
        if (tileWindow == window && PtInRect(&tile, cursor))
        {
            return std::nullopt;
        }
    }

    // Anywhere else the window first leaves its tile, so the other tiles are measured without it
    for (const auto& [tileWindow, tile] : tree.CalculateRects(TileArea(), AutomaticTiling::TileSpacing, window))
    {
        if (PtInRect(&tile, cursor))
        {
            const auto side = TilingTree::SideAt(tile, cursor);
            return TileDrop{ tileWindow, side, TilingTree::DropRect(tile, side, AutomaticTiling::TileSpacing) };
        }
    }

    return std::nullopt;
}

void WorkArea::ShowTileDrop(HWND window, POINT cursor)
{
    const bool tileable = IsTiled(window) || TileSizeCheck::CanTile(window);
    const auto drop = tileable ? DropAt(window, cursor) : std::nullopt;
    if (!drop.has_value() || !m_zonesOverlay)
    {
        HideTileDrop();
        return;
    }

    // Redraw only when the highlighted half changes, not on every mouse move
    if (m_shownDrop.has_value() && EqualRect(&m_shownDrop.value(), &drop->rect))
    {
        return;
    }

    m_shownDrop = drop->rect;
    ZonesMap preview;
    preview.emplace(0, Zone(drop->rect, 0));
    SetWorkAreaWindowAsTopmost(window);
    m_zonesOverlay->DrawActiveZoneSet(preview, { 0 }, Colors::GetZoneColors(), false);
    m_zonesOverlay->Show();
}

void WorkArea::HideTileDrop()
{
    if (m_shownDrop.has_value() && m_zonesOverlay)
    {
        m_zonesOverlay->Hide();
    }

    m_shownDrop.reset();
}

bool WorkArea::DropTile(HWND window, POINT cursor)
{
    HideTileDrop();
    if (!IsAutomaticTiling())
    {
        return false;
    }

    auto& tree = AutomaticTiling::Tree(m_uniqueId);
    const bool tiled = tree.Contains(window);
    if (!tiled && !TileSizeCheck::CanTile(window))
    {
        return false;
    }

    // Dropped on another tile, the window splits it and takes the side it was dropped on
    if (const auto drop = DropAt(window, cursor))
    {
        if (tiled)
        {
            tree.MoveBeside(window, drop->target, drop->side);
        }
        else
        {
            tree.InsertBeside(window, drop->target, drop->side);
        }
    }
    else if (!tiled)
    {
        return false;
    }

    ReflowTiles();
    return true;
}

TilingTree::WindowRects WorkArea::ApplyTileZones()
{
    auto rects = AutomaticTiling::Tree(m_uniqueId).CalculateRects(TileArea(), AutomaticTiling::TileSpacing);

    ZonesMap zones;
    for (size_t i = 0; i < rects.size(); i++)
    {
        const auto index = static_cast<ZoneIndex>(i);
        zones.emplace(index, Zone(rects[i].second, index));
    }

    m_layout->SetZones(std::move(zones));
    return rects;
}

bool WorkArea::IsInTile(HWND window, const RECT& tile) const noexcept
{
    const auto frame = TileSizeCheck::VisibleFrame(window);
    const RECT screenTile = ToScreen(tile);
    return frame.has_value() && EqualRect(&frame.value(), &screenTile);
}

void WorkArea::ReflowTilesLive(HWND resizingWindow)
{
    // Keeping up with the mouse, windows are moved directly instead of snapped, and aren't checked afterwards
    const ULONGLONG now = GetTickCount64();
    for (const auto& [window, tile] : ApplyTileZones())
    {
        if (window == resizingWindow || IsInTile(window, tile))
        {
            continue;
        }

        // A window still working through its last resize gets the next one once it's done,
        // instead of a queue of them, unless it takes too long
        if (const auto request = m_liveResizeRequests.find(window); request != m_liveResizeRequests.end())
        {
            const auto& [requestedRect, requestTime] = request->second;
            const auto frame = TileSizeCheck::VisibleFrame(window);
            const bool done = frame.has_value() && EqualRect(&frame.value(), &requestedRect);
            if (!done && now - requestTime < LiveResize::BusyWindowTimeoutMillis)
            {
                continue;
            }
        }

        ForceTileSize(window, tile);
        m_liveResizeRequests[window] = { ToScreen(tile), now };
    }
}

#pragma region private

bool WorkArea::InitWindow(HINSTANCE hinstance)
{
    m_window = windowPool.NewZonesOverlayWindow(m_workAreaRect, hinstance, this);
    if (!m_window)
    {
        Logger::error(L"No work area window");
        return false;
    }

    m_zonesOverlay = std::make_unique<ZonesOverlay>(m_window);
    return true;
}

void WorkArea::InitLayout(const FancyZonesDataTypes::WorkAreaId& parentUniqueId)
{
    Logger::info(L"Initialize layout on {}, work area rect = {}x{}", m_uniqueId.toString(), m_workAreaRect.width(), m_workAreaRect.height());

    const bool isLayoutAlreadyApplied = AppliedLayouts::instance().IsLayoutApplied(m_uniqueId);
    if (!isLayoutAlreadyApplied)
    {
        if (!AppliedLayouts::instance().CloneLayout(parentUniqueId, m_uniqueId))
        {
            AppliedLayouts::instance().ApplyDefaultLayout(m_uniqueId);
        }

        AppliedLayouts::instance().SaveData();
    }

    CalculateZoneSet();
}

void WorkArea::InitSnappedWindows()
{
    static bool updatePositionOnceOnStartFlag = true;
    Logger::info(L"Init work area {} windows, update positions = {}", m_uniqueId.toString(), updatePositionOnceOnStartFlag);

    for (const auto& window : VirtualDesktop::instance().GetWindowsFromCurrentDesktop())
    {
        auto indexes = FancyZonesWindowProperties::RetrieveZoneIndexProperty(window);
        if (indexes.size() == 0)
        {
            continue;
        }

        if (!m_uniqueId.monitorId.monitor) // one work area across monitors
        {
            Snap(window, indexes, updatePositionOnceOnStartFlag);
        }
        else
        {
            const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
            if (monitor && m_uniqueId.monitorId.monitor == monitor)
            {
                // prioritize snapping on the current monitor if the window was snapped to several work areas
                Snap(window, indexes, updatePositionOnceOnStartFlag);
            }
            else
            {
                // if the window is not snapped on the current monitor, then check the others
                auto savedIndexes = AppZoneHistory::instance().GetAppLastZoneIndexSet(window, m_uniqueId, GetLayoutId());
                if (savedIndexes == indexes)
                {
                    Snap(window, indexes, updatePositionOnceOnStartFlag);
                }
            }
        }
    }

    updatePositionOnceOnStartFlag = false;
}

void WorkArea::CalculateZoneSet()
{
    auto appliedLayout = AppliedLayouts::instance().GetDeviceLayout(m_uniqueId);
    if (!appliedLayout.has_value())
    {
        Logger::error(L"Layout wasn't applied. Can't init layout on work area {}x{}", m_workAreaRect.width(), m_workAreaRect.height());
        return;
    }

    // For custom layouts the spacing, sensitivity radius and zone count live in the custom
    // layout definition (custom-layouts.json), while the applied-layouts.json snapshot keeps a
    // copy taken at apply time. Editing those properties only rewrites custom-layouts.json, so
    // without this sync the snapshot stays stale and the edits don't take effect until the layout
    // is re-applied (see GH #44058). Re-derive the scalar properties from the current custom
    // layout using the same logic the apply path uses (CustomLayouts::GetLayout also derives the
    // canvas zone count from the zone list, keeping it consistent with Layout::Init validation).
    if (appliedLayout->type == FancyZonesDataTypes::ZoneSetLayoutType::Custom)
    {
        if (const auto refreshed = CustomLayouts::instance().GetLayout(appliedLayout->uuid))
        {
            appliedLayout = refreshed;
        }
    }

    m_layout = std::make_unique<Layout>(appliedLayout.value());
    m_layout->Init(m_workAreaRect, m_uniqueId.monitorId.monitor);

    if (IsAutomaticTiling())
    {
        SyncTiledWindows();
    }
    else if (AutomaticTiling::IsEnabled())
    {
        AutomaticTiling::EraseTree(m_uniqueId);
    }
}

void WorkArea::SyncTiledWindows()
{
    // Drop windows that went away while this work area wasn't active, then tile the ones already open
    auto& tree = AutomaticTiling::Tree(m_uniqueId);
    for (HWND window : tree.Windows())
    {
        if (!IsWindow(window) || IsIconic(window) || !IsWindowVisible(window))
        {
            tree.Remove(window);
        }
    }

    for (HWND window : VirtualDesktop::instance().GetWindowsFromCurrentDesktop())
    {
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONULL) == m_uniqueId.monitorId.monitor &&
            TileSizeCheck::CanTile(window))
        {
            tree.Insert(window, nullptr, TileArea(), AutomaticTiling::TileSpacing);
        }
    }

    ReflowTiles();
}

RECT WorkArea::TileArea() const noexcept
{
    // Zones are relative to the work area window
    return RECT{ 0, 0, m_workAreaRect.width(), m_workAreaRect.height() };
}

LRESULT WorkArea::WndProc(UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_NCDESTROY:
    {
        ::DefWindowProc(m_window, message, wparam, lparam);
        SetWindowLongPtr(m_window, GWLP_USERDATA, 0);
    }
    break;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wparam == TileSizeCheck::TimerId)
        {
            KillTimer(m_window, TileSizeCheck::TimerId);
            CheckTiles();
            return 0;
        }

        if (wparam == LiveResize::TimerId)
        {
            KillTimer(m_window, LiveResize::TimerId);
            m_liveResizePending = false;
            if (m_resizingWindow && IsAutomaticTiling())
            {
                ApplyTileResize();
            }

            return 0;
        }

        return DefWindowProc(m_window, message, wparam, lparam);

    default:
    {
        return DefWindowProc(m_window, message, wparam, lparam);
    }
    }
    return 0;
}

void WorkArea::SetWorkAreaWindowAsTopmost(HWND draggedWindow) noexcept
{
    if (!m_window)
    {
        return;
    }

    HWND windowInsertAfter = draggedWindow ? draggedWindow : HWND_TOPMOST;

    constexpr UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;
    SetWindowPos(m_window, windowInsertAfter, 0, 0, 0, 0, flags);
}

#pragma endregion

LRESULT CALLBACK WorkArea::s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto thisRef = reinterpret_cast<WorkArea*>(GetWindowLongPtr(window, GWLP_USERDATA));
    if ((thisRef == nullptr) && (message == WM_CREATE))
    {
        auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        thisRef = static_cast<WorkArea*>(createStruct->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisRef));
    }

    return (thisRef != nullptr) ? thisRef->WndProc(message, wparam, lparam) :
                                  DefWindowProc(window, message, wparam, lparam);
}
