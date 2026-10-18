#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <FancyZonesLib/Layout.h>
#include <FancyZonesLib/LayoutAssignedWindows.h>
#include <FancyZonesLib/TilingTree.h>

class ZonesOverlay;

class WorkArea
{
    WorkArea(HINSTANCE hinstance, const FancyZonesDataTypes::WorkAreaId& uniqueId, const FancyZonesUtils::Rect& workAreaRect);

public:
    ~WorkArea();

    static std::unique_ptr<WorkArea> Create(HINSTANCE hinstance, const FancyZonesDataTypes::WorkAreaId& uniqueId, const FancyZonesDataTypes::WorkAreaId& parentUniqueId, const FancyZonesUtils::Rect& workAreaRect)
    {
        auto self = std::unique_ptr<WorkArea>(new WorkArea(hinstance, uniqueId, workAreaRect));
        if (!self->Init(hinstance, parentUniqueId))
        {
            return nullptr;
        }

        return self;
    }

    inline bool Init([[maybe_unused]] HINSTANCE hinstance, const FancyZonesDataTypes::WorkAreaId& parentUniqueId)
    {
#ifndef UNIT_TESTS
        if (!InitWindow(hinstance))
        {
            return false;
        }
#endif
        InitLayout(parentUniqueId);
        
        return true;
    }
    
    FancyZonesDataTypes::WorkAreaId UniqueId() const noexcept { return { m_uniqueId }; }
    const std::unique_ptr<Layout>& GetLayout() const noexcept { return m_layout; }
    const LayoutAssignedWindows& GetLayoutWindows() const noexcept { return m_layoutWindows; }
    const HWND GetWorkAreaWindow() const noexcept { return m_window; }
    const GUID GetLayoutId() const noexcept;
    const FancyZonesUtils::Rect& GetWorkAreaRect() const noexcept { return m_workAreaRect; }
    
    void InitLayout();
    void InitSnappedWindows();
    void UpdateWindowPositions();

    bool Snap(HWND window, const ZoneIndexSet& zones, bool updatePosition = true);
    bool Unsnap(HWND window);

    void ShowZones(const ZoneIndexSet& highlight, HWND draggedWindow = nullptr);
    void HideZones();
    void FlashZones();
    void ShowLayoutNameLabel(const std::wstring& name);
    void ShowMonitorRotationPreview(const std::vector<RECT>& windowRects, size_t monitorNumber, std::optional<bool> reverse, bool animateRotation);
    
    void CycleWindows(HWND window, bool reverse);

    // Automatic tiling spike, see AutomaticTiling.h
    bool IsAutomaticTiling() const noexcept;
    bool IsTiled(HWND window) const;
    bool Tile(HWND window, HWND target);
    bool Untile(HWND window);
    void ReflowTiles();

    // Dragging a tiled window's border resizes the tiles next to it along with it
    void BeginTileResize(HWND window);
    void UpdateTileResize(HWND window);
    void EndTileDrag();

    // Dropping a dragged window on a tile splits that tile, with the window on the side it was dropped on.
    // The cursor is in screen coordinates.
    void ShowTileDrop(HWND window, POINT cursor);
    void HideTileDrop();
    bool DropTile(HWND window, POINT cursor);

protected:
    static LRESULT CALLBACK s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;

private:
    struct TileDrop
    {
        HWND target; // tile to split, null when there are no tiles yet
        TilingTree::Side side;
        RECT rect; // where the dropped window goes, in work area coordinates
    };

    bool InitWindow(HINSTANCE hinstance);
    void InitLayout(const FancyZonesDataTypes::WorkAreaId& parentUniqueId);
    
    void CalculateZoneSet();
    void SyncTiledWindows();
    void CheckTiles();
    void ForceTileSize(HWND window, const RECT& tile);
    RECT TileArea() const noexcept;
    RECT ToScreen(RECT tile) const noexcept;
    TilingTree::WindowRects ApplyTileZones();
    bool IsInTile(HWND window, const RECT& tile) const noexcept;
    void ApplyTileResize();
    void ReflowTilesLive(HWND resizingWindow);
    std::optional<TileDrop> DropAt(HWND window, POINT cursor) const;
    void SetWorkAreaWindowAsTopmost(HWND draggedWindow) noexcept;

    LRESULT WndProc(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    
    const FancyZonesUtils::Rect m_workAreaRect{};
    const FancyZonesDataTypes::WorkAreaId m_uniqueId;
    HWND m_window{}; // Hidden tool window used to represent current monitor desktop work area.
    std::unique_ptr<Layout> m_layout;
    LayoutAssignedWindows m_layoutWindows{};
    std::unique_ptr<ZonesOverlay> m_zonesOverlay;
    std::unordered_set<HWND> m_misplacedWindows; // tiled windows seen away from their tile once
    std::unordered_set<HWND> m_selfPlacingWindows; // tiled windows that keep moving away from their tile
    std::unordered_set<HWND> m_forcedSizeWindows; // tiled windows whose minimum size is bigger than their tile
    HWND m_resizingWindow{}; // tiled window whose border is being dragged
    bool m_liveResizePending{}; // the dragged border's latest position is waiting for the next frame
    std::unordered_map<HWND, std::pair<RECT, ULONGLONG>> m_liveResizeRequests; // last rect and tick count sent to each tile during a drag
    std::optional<RECT> m_shownDrop; // drop preview currently in the overlay
};
