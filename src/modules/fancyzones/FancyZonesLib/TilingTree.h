#pragma once

#include <memory>
#include <utility>
#include <vector>

/**
 * Binary split ("dwindle") tiling tree used by Automatic FancyZones.
 * Every leaf holds one window. Inserting a window splits the target leaf in two,
 * removing a window lets its sibling take over the parent's space.
 * Pure layout logic: no Win32 calls are made, so windows are only used as keys.
 */
class TilingTree
{
public:
    enum class Orientation
    {
        SideBySide, // divider is vertical, children are left and right
        Stacked // divider is horizontal, children are top and bottom
    };

    using WindowRects = std::vector<std::pair<HWND, RECT>>;

    TilingTree() = default;
    ~TilingTree() = default;

    /**
     * Insert window by splitting the target window's tile. If target is not in the tree,
     * the newest window's tile is split instead.
     * The split direction follows the target tile's aspect ratio, the new window goes right or bottom.
     *
     * @returns false if window is null or already in the tree.
     */
    bool Insert(HWND window, HWND target, const RECT& area, int spacing);

    /**
     * @returns false if window is not in the tree.
     */
    bool Remove(HWND window);

    void Clear() noexcept;

    bool Contains(HWND window) const noexcept;
    bool Empty() const noexcept;
    size_t Size() const noexcept;

    /**
     * @returns the most recently inserted window still in the tree, or null if the tree is empty.
     */
    HWND NewestWindow() const noexcept;

    /**
     * Windows in depth-first order, which is also the zone index order.
     */
    std::vector<HWND> Windows() const;

    /**
     * Tile rectangles in depth-first order. Area is inset by spacing,
     * and neighboring tiles are separated by spacing.
     */
    WindowRects CalculateRects(const RECT& area, int spacing) const;

private:
    struct Node
    {
        HWND window{ nullptr }; // set for leaves only
        Orientation orientation{ Orientation::SideBySide };
        double ratio{ 0.5 };
        std::unique_ptr<Node> first;
        std::unique_ptr<Node> second;
        Node* parent{ nullptr };

        bool IsLeaf() const noexcept { return window != nullptr; }
    };

    Node* FindLeaf(HWND window) const noexcept;
    static void Collect(const Node* node, const RECT& rect, int spacing, WindowRects& result);

    std::unique_ptr<Node> m_root;
    std::vector<HWND> m_insertionOrder;
};
