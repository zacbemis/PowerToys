#include "pch.h"
#include "TilingTree.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
    RECT Inset(const RECT& rect, int amount) noexcept
    {
        return RECT{ rect.left + amount, rect.top + amount, rect.right - amount, rect.bottom - amount };
    }

    // Start and end of a split's rect along the axis its divider moves on
    std::pair<LONG, LONG> SplitAxis(const RECT& rect, TilingTree::Orientation orientation) noexcept
    {
        return orientation == TilingTree::Orientation::SideBySide ? std::pair{ rect.left, rect.right } : std::pair{ rect.top, rect.bottom };
    }

    LONG DividerPosition(const RECT& rect, TilingTree::Orientation orientation, double ratio) noexcept
    {
        const auto [start, end] = SplitAxis(rect, orientation);
        return start + static_cast<LONG>(std::lround((static_cast<double>(end) - start) * ratio));
    }

    std::pair<RECT, RECT> SplitRect(const RECT& rect, TilingTree::Orientation orientation, double ratio, int spacing)
    {
        // Each side of the divider gives up half of the spacing, so the gap between tiles equals spacing
        const int firstGap = spacing / 2;
        const int secondGap = spacing - firstGap;
        const LONG divider = DividerPosition(rect, orientation, ratio);

        RECT first = rect;
        RECT second = rect;
        if (orientation == TilingTree::Orientation::SideBySide)
        {
            first.right = divider - firstGap;
            second.left = divider + secondGap;
        }
        else
        {
            first.bottom = divider - firstGap;
            second.top = divider + secondGap;
        }

        return { first, second };
    }
}

bool TilingTree::Insert(HWND window, HWND target, const RECT& area, int spacing)
{
    if (!window || Contains(window))
    {
        return false;
    }

    if (!m_root)
    {
        m_root = std::make_unique<Node>();
        m_root->window = window;
        m_insertionOrder.push_back(window);
        return true;
    }

    Node* targetLeaf = FindLeaf(target);
    if (!targetLeaf)
    {
        targetLeaf = FindLeaf(NewestWindow());
    }

    RECT targetRect{};
    for (const auto& [tileWindow, rect] : CalculateRects(area, spacing))
    {
        if (tileWindow == targetLeaf->window)
        {
            targetRect = rect;
            break;
        }
    }

    const bool wide = (targetRect.right - targetRect.left) >= (targetRect.bottom - targetRect.top);
    SplitLeaf(targetLeaf, window, wide ? Orientation::SideBySide : Orientation::Stacked, false);
    return true;
}

bool TilingTree::InsertBeside(HWND window, HWND target, Side side)
{
    if (!window || Contains(window))
    {
        return false;
    }

    if (!m_root)
    {
        m_root = std::make_unique<Node>();
        m_root->window = window;
        m_insertionOrder.push_back(window);
        return true;
    }

    Node* targetLeaf = FindLeaf(target);
    if (!targetLeaf)
    {
        return false;
    }

    const bool sideBySide = side == Side::Left || side == Side::Right;
    SplitLeaf(targetLeaf, window, sideBySide ? Orientation::SideBySide : Orientation::Stacked, side == Side::Left || side == Side::Top);
    return true;
}

bool TilingTree::MoveBeside(HWND window, HWND target, Side side)
{
    if (window == target || !Contains(window) || !Contains(target))
    {
        return false;
    }

    Remove(window);
    return InsertBeside(window, target, side);
}

bool TilingTree::SwapToward(HWND window, Side side)
{
    Node* leaf = FindLeaf(window);
    if (!leaf)
    {
        return false;
    }

    const Orientation orientation = side == Side::Left || side == Side::Right ? Orientation::SideBySide : Orientation::Stacked;
    const bool towardSecond = side == Side::Right || side == Side::Bottom;
    for (Node *child = leaf, *parent = leaf->parent; parent; child = parent, parent = parent->parent)
    {
        if (parent->orientation != orientation || (parent->first.get() == child) != towardSecond)
        {
            continue;
        }

        // The window and what is across the divider trade slots, so each takes the other's place
        Node* leafParent = leaf->parent;
        std::unique_ptr<Node>& own = SlotOf(leaf);
        std::unique_ptr<Node>& across = towardSecond ? parent->second : parent->first;
        std::swap(own, across);
        own->parent = leafParent;
        across->parent = parent;
        return true;
    }

    return false;
}

bool TilingTree::ReplaceWindow(HWND window, HWND replacement)
{
    Node* leaf = FindLeaf(window);
    if (!leaf || !replacement || Contains(replacement))
    {
        return false;
    }

    leaf->window = replacement;
    std::replace(m_insertionOrder.begin(), m_insertionOrder.end(), window, replacement);
    return true;
}

bool TilingTree::Remove(HWND window)
{
    Node* leaf = FindLeaf(window);
    if (!leaf)
    {
        return false;
    }

    std::erase(m_insertionOrder, window);

    if (leaf == m_root.get())
    {
        m_root.reset();
        return true;
    }

    // The sibling takes over the parent's place in the tree
    Node* parent = leaf->parent;
    std::unique_ptr<Node> sibling = parent->first.get() == leaf ? std::move(parent->second) : std::move(parent->first);
    sibling->parent = parent->parent;

    std::unique_ptr<Node>& parentSlot = !parent->parent ? m_root :
                                        parent->parent->first.get() == parent ? parent->parent->first :
                                                                                parent->parent->second;
    parentSlot = std::move(sibling);
    return true;
}

void TilingTree::Clear() noexcept
{
    m_root.reset();
    m_insertionOrder.clear();
}

bool TilingTree::Contains(HWND window) const noexcept
{
    return FindLeaf(window) != nullptr;
}

bool TilingTree::Empty() const noexcept
{
    return !m_root;
}

size_t TilingTree::Size() const noexcept
{
    return m_insertionOrder.size();
}

HWND TilingTree::NewestWindow() const noexcept
{
    return m_insertionOrder.empty() ? nullptr : m_insertionOrder.back();
}

std::vector<HWND> TilingTree::Windows() const
{
    std::vector<HWND> result;
    for (const auto& [window, _] : CalculateRects(RECT{}, 0))
    {
        result.push_back(window);
    }

    return result;
}

TilingTree::WindowRects TilingTree::CalculateRects(const RECT& area, int spacing, HWND without) const
{
    WindowRects result;
    if (m_root && !(m_root->IsLeaf() && m_root->window == without))
    {
        Collect(m_root.get(), Inset(area, spacing), spacing, without, result);
    }

    return result;
}

bool TilingTree::ResizeTile(HWND window, const RECT& newRect, const RECT& area, int spacing)
{
    Node* leaf = FindLeaf(window);
    if (!leaf)
    {
        return false;
    }

    RECT tile{};
    for (const auto& [tileWindow, rect] : CalculateRects(area, spacing))
    {
        if (tileWindow == window)
        {
            tile = rect;
        }
    }

    // The divider along an edge belongs to the nearest split that has the tile on that side of it
    const auto moveEdge = [&](Orientation orientation, bool tileIsFirst, LONG position) {
        for (Node *child = leaf, *parent = leaf->parent; parent; child = parent, parent = parent->parent)
        {
            if (parent->orientation == orientation && (parent->first.get() == child) == tileIsFirst)
            {
                // Earlier edges may have moved this split, so find its current rect
                for (const auto& [split, rect] : Splits(area, spacing))
                {
                    if (split == parent)
                    {
                        return SetDividerPosition(parent, rect, position, spacing);
                    }
                }
            }
        }

        return false;
    };

    const int firstGap = spacing / 2;
    const int secondGap = spacing - firstGap;
    bool moved = false;
    if (newRect.left != tile.left)
    {
        moved |= moveEdge(Orientation::SideBySide, false, newRect.left - secondGap);
    }

    if (newRect.right != tile.right)
    {
        moved |= moveEdge(Orientation::SideBySide, true, newRect.right + firstGap);
    }

    if (newRect.top != tile.top)
    {
        moved |= moveEdge(Orientation::Stacked, false, newRect.top - secondGap);
    }

    if (newRect.bottom != tile.bottom)
    {
        moved |= moveEdge(Orientation::Stacked, true, newRect.bottom + firstGap);
    }

    return moved;
}

TilingTree::Side TilingTree::SideAt(const RECT& tile, POINT point) noexcept
{
    // Comparing the distances to each edge, relative to the tile's size, cuts it along its diagonals
    const double width = max(1L, tile.right - tile.left);
    const double height = max(1L, tile.bottom - tile.top);
    const double left = (static_cast<double>(point.x) - tile.left) / width;
    const double top = (static_cast<double>(point.y) - tile.top) / height;
    const double right = 1 - left;
    const double bottom = 1 - top;

    const double nearest = min(min(left, right), min(top, bottom));
    if (nearest == left)
    {
        return Side::Left;
    }

    if (nearest == right)
    {
        return Side::Right;
    }

    return nearest == top ? Side::Top : Side::Bottom;
}

RECT TilingTree::DropRect(const RECT& tile, Side side, int spacing) noexcept
{
    const bool sideBySide = side == Side::Left || side == Side::Right;
    const auto [first, second] = SplitRect(tile, sideBySide ? Orientation::SideBySide : Orientation::Stacked, 0.5, spacing);
    return side == Side::Left || side == Side::Top ? first : second;
}

HWND TilingTree::TileAtEdge(const WindowRects& tiles, Side edge, POINT point) noexcept
{
    // Tiles along an edge reach furthest that way
    const auto reach = [edge](const RECT& tile) noexcept {
        switch (edge)
        {
        case Side::Left:
            return -tile.left;
        case Side::Top:
            return -tile.top;
        case Side::Right:
            return tile.right;
        default:
            return tile.bottom;
        }
    };

    // Distance from point to the tile's extent along the edge, zero if point is level with it
    const auto distance = [edge, point](const RECT& tile) noexcept {
        const bool sideBySide = edge == Side::Left || edge == Side::Right;
        const LONG position = sideBySide ? point.y : point.x;
        const LONG start = sideBySide ? tile.top : tile.left;
        const LONG end = sideBySide ? tile.bottom : tile.right;
        return position < start ? start - position : position >= end ? position - end + 1 : 0;
    };

    HWND nearest = nullptr;
    LONG nearestReach = 0;
    LONG nearestDistance = 0;
    for (const auto& [window, tile] : tiles)
    {
        const LONG tileReach = reach(tile);
        const LONG tileDistance = distance(tile);
        if (!nearest || tileReach > nearestReach || (tileReach == nearestReach && tileDistance < nearestDistance))
        {
            nearest = window;
            nearestReach = tileReach;
            nearestDistance = tileDistance;
        }
    }

    return nearest;
}

void TilingTree::SplitLeaf(Node* leaf, HWND window, Orientation orientation, bool windowFirst)
{
    // The leaf becomes a split holding its window and the new one side by side or stacked
    auto existing = std::make_unique<Node>();
    existing->window = leaf->window;
    existing->parent = leaf;

    auto added = std::make_unique<Node>();
    added->window = window;
    added->parent = leaf;

    leaf->orientation = orientation;
    leaf->ratio = 0.5;
    leaf->window = nullptr;
    leaf->first = windowFirst ? std::move(added) : std::move(existing);
    leaf->second = windowFirst ? std::move(existing) : std::move(added);

    m_insertionOrder.push_back(window);
}

std::vector<std::pair<TilingTree::Node*, RECT>> TilingTree::Splits(const RECT& area, int spacing) const
{
    std::vector<std::pair<Node*, RECT>> result;
    std::function<void(Node*, const RECT&)> visit = [&](Node* node, const RECT& rect) {
        if (node->IsLeaf())
        {
            return;
        }

        result.emplace_back(node, rect);
        const auto [first, second] = SplitRect(rect, node->orientation, node->ratio, spacing);
        visit(node->first.get(), first);
        visit(node->second.get(), second);
    };

    if (m_root)
    {
        visit(m_root.get(), Inset(area, spacing));
    }

    return result;
}

bool TilingTree::SetDividerPosition(Node* split, const RECT& rect, LONG position, int spacing) noexcept
{
    const int firstGap = spacing / 2;
    const int secondGap = spacing - firstGap;
    const auto [start, end] = SplitAxis(rect, split->orientation);
    const LONG lowest = start + MinimumTileExtent + firstGap;
    const LONG highest = end - MinimumTileExtent - secondGap;
    if (lowest > highest)
    {
        return false;
    }

    const LONG clamped = std::clamp(position, lowest, highest);
    if (clamped == DividerPosition(rect, split->orientation, split->ratio))
    {
        return false;
    }

    split->ratio = (static_cast<double>(clamped) - start) / (static_cast<double>(end) - start);
    return true;
}

TilingTree::Node* TilingTree::FindLeaf(HWND window) const noexcept
{
    if (!window || !m_root)
    {
        return nullptr;
    }

    std::function<Node*(Node*)> find = [&](Node* node) -> Node* {
        if (node->IsLeaf())
        {
            return node->window == window ? node : nullptr;
        }

        Node* found = find(node->first.get());
        return found ? found : find(node->second.get());
    };

    return find(m_root.get());
}

std::unique_ptr<TilingTree::Node>& TilingTree::SlotOf(const Node* node) noexcept
{
    if (!node->parent)
    {
        return m_root;
    }

    return node->parent->first.get() == node ? node->parent->first : node->parent->second;
}

void TilingTree::Collect(const Node* node, const RECT& rect, int spacing, HWND without, WindowRects& result)
{
    if (node->IsLeaf())
    {
        result.emplace_back(node->window, rect);
        return;
    }

    // Like Remove, a left out window's sibling takes the whole split
    const auto isLeftOut = [without](const Node* child) { return child->IsLeaf() && child->window == without; };
    if (isLeftOut(node->first.get()))
    {
        Collect(node->second.get(), rect, spacing, without, result);
        return;
    }

    if (isLeftOut(node->second.get()))
    {
        Collect(node->first.get(), rect, spacing, without, result);
        return;
    }

    const auto [first, second] = SplitRect(rect, node->orientation, node->ratio, spacing);
    Collect(node->first.get(), first, spacing, without, result);
    Collect(node->second.get(), second, spacing, without, result);
}
