#include "pch.h"
#include "TilingTree.h"

#include <cmath>
#include <functional>

namespace
{
    std::pair<RECT, RECT> SplitRect(const RECT& rect, TilingTree::Orientation orientation, double ratio, int spacing)
    {
        // Each side of the divider gives up half of the spacing, so the gap between tiles equals spacing
        const int firstGap = spacing / 2;
        const int secondGap = spacing - firstGap;

        RECT first = rect;
        RECT second = rect;
        if (orientation == TilingTree::Orientation::SideBySide)
        {
            const LONG divider = rect.left + static_cast<LONG>(std::lround((static_cast<double>(rect.right) - rect.left) * ratio));
            first.right = divider - firstGap;
            second.left = divider + secondGap;
        }
        else
        {
            const LONG divider = rect.top + static_cast<LONG>(std::lround((static_cast<double>(rect.bottom) - rect.top) * ratio));
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

    // Turn the target leaf into a split that holds the target window first and the new window second
    auto first = std::make_unique<Node>();
    first->window = targetLeaf->window;
    first->parent = targetLeaf;

    auto second = std::make_unique<Node>();
    second->window = window;
    second->parent = targetLeaf;

    const bool wide = (targetRect.right - targetRect.left) >= (targetRect.bottom - targetRect.top);
    targetLeaf->orientation = wide ? Orientation::SideBySide : Orientation::Stacked;
    targetLeaf->ratio = 0.5;
    targetLeaf->window = nullptr;
    targetLeaf->first = std::move(first);
    targetLeaf->second = std::move(second);

    m_insertionOrder.push_back(window);
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

TilingTree::WindowRects TilingTree::CalculateRects(const RECT& area, int spacing) const
{
    WindowRects result;
    if (m_root)
    {
        const RECT inner{ area.left + spacing, area.top + spacing, area.right - spacing, area.bottom - spacing };
        Collect(m_root.get(), inner, spacing, result);
    }

    return result;
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

void TilingTree::Collect(const Node* node, const RECT& rect, int spacing, WindowRects& result)
{
    if (node->IsLeaf())
    {
        result.emplace_back(node->window, rect);
        return;
    }

    const auto [first, second] = SplitRect(rect, node->orientation, node->ratio, spacing);
    Collect(node->first.get(), first, spacing, result);
    Collect(node->second.get(), second, spacing, result);
}
