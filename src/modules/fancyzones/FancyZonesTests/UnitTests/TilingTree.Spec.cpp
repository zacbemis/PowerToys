#include "pch.h"
#include "FancyZonesLib\TilingTree.h"

#include "Util.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace FancyZonesUnitTests
{
    TEST_CLASS(TilingTreeUnitTests)
    {
        // TilingTree never dereferences window handles, so fake ones are enough
        static HWND FakeWindow(intptr_t id)
        {
            return reinterpret_cast<HWND>(id);
        }

        const HWND m_first = FakeWindow(1);
        const HWND m_second = FakeWindow(2);
        const HWND m_third = FakeWindow(3);
        const HWND m_fourth = FakeWindow(4);

        const RECT m_landscape{ 0, 0, 1920, 1080 };
        const RECT m_portrait{ 0, 0, 1080, 1920 };

        static RECT RectOf(const TilingTree::WindowRects& rects, HWND window)
        {
            const auto tile = std::find_if(rects.begin(), rects.end(), [window](const auto& item) { return item.first == window; });
            Assert::IsTrue(tile != rects.end(), L"Window not tiled");
            return tile->second;
        }

    public:
        TEST_METHOD (EmptyTreeHasNoTiles)
        {
            TilingTree tree;
            Assert::IsTrue(tree.Empty());
            Assert::AreEqual(size_t{ 0 }, tree.Size());
            Assert::IsTrue(tree.CalculateRects(m_landscape, 0).empty());
        }

        TEST_METHOD (FirstWindowFillsArea)
        {
            TilingTree tree;
            Assert::IsTrue(tree.Insert(m_first, nullptr, m_landscape, 0));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            Assert::AreEqual(size_t{ 1 }, rects.size());
            CustomAssert::AreEqual(m_landscape, RectOf(rects, m_first));
        }

        TEST_METHOD (FirstWindowIsInsetBySpacing)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 16);

            const RECT expected{ 16, 16, 1904, 1064 };
            CustomAssert::AreEqual(expected, RectOf(tree.CalculateRects(m_landscape, 16), m_first));
        }

        TEST_METHOD (SecondWindowSplitsLandscapeSideBySide)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 1080 }, RectOf(rects, m_second));
        }

        TEST_METHOD (SecondWindowSplitsPortraitStacked)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_portrait, 0);
            tree.Insert(m_second, m_first, m_portrait, 0);

            const auto rects = tree.CalculateRects(m_portrait, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 1080, 960 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 0, 960, 1080, 1920 }, RectOf(rects, m_second));
        }

        TEST_METHOD (ThirdWindowSplitsTargetTileByItsAspectRatio)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            // second tile is 960x1080, taller than wide, so it is split top/bottom
            tree.Insert(m_third, m_second, m_landscape, 0);

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (SpacingSeparatesNeighbors)
        {
            constexpr int spacing = 16;
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, spacing);
            tree.Insert(m_second, m_first, m_landscape, spacing);

            const auto rects = tree.CalculateRects(m_landscape, spacing);
            const RECT first = RectOf(rects, m_first);
            const RECT second = RectOf(rects, m_second);
            Assert::AreEqual(static_cast<LONG>(spacing), second.left - first.right);
            Assert::AreEqual(static_cast<LONG>(spacing), first.left);
            Assert::AreEqual(m_landscape.right - spacing, second.right);
        }

        TEST_METHOD (UnknownTargetSplitsLastInsertedWindow)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, FakeWindow(42), m_landscape, 0);

            // m_second was inserted last, so its tile is the one split
            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (InsertDuplicateOrNullFails)
        {
            TilingTree tree;
            Assert::IsFalse(tree.Insert(nullptr, nullptr, m_landscape, 0));
            Assert::IsTrue(tree.Insert(m_first, nullptr, m_landscape, 0));
            Assert::IsFalse(tree.Insert(m_first, nullptr, m_landscape, 0));
            Assert::AreEqual(size_t{ 1 }, tree.Size());
        }

        TEST_METHOD (WindowsAreInDepthFirstOrder)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_first, m_landscape, 0);

            const std::vector<HWND> expected{ m_first, m_third, m_second };
            Assert::IsTrue(expected == tree.Windows());
        }

        TEST_METHOD (RemoveLastWindowEmptiesTree)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            Assert::IsTrue(tree.Remove(m_first));
            Assert::IsTrue(tree.Empty());
            Assert::IsFalse(tree.Contains(m_first));
        }

        TEST_METHOD (RemoveUnknownWindowFails)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            Assert::IsFalse(tree.Remove(m_second));
            Assert::AreEqual(size_t{ 1 }, tree.Size());
        }

        TEST_METHOD (RemovedWindowsSiblingFillsItsSpace)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            Assert::IsTrue(tree.Remove(m_second));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            Assert::AreEqual(size_t{ 2 }, rects.size());
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (RemoveRootSplitChildKeepsSubtree)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            // removing the left half leaves the right subtree to fill the whole area
            Assert::IsTrue(tree.Remove(m_first));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 0, 540, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (InsertAfterRemovingLastInsertedUsesLastTile)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);
            tree.Remove(m_third);

            Assert::IsTrue(tree.Insert(m_fourth, nullptr, m_landscape, 0));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_fourth));
        }

        TEST_METHOD (NewestWindowTracksInsertionsAndRemovals)
        {
            TilingTree tree;
            Assert::IsNull(tree.NewestWindow());

            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_first, m_landscape, 0);
            Assert::IsTrue(m_third == tree.NewestWindow());

            tree.Remove(m_third);
            Assert::IsTrue(m_second == tree.NewestWindow());

            tree.Remove(m_first);
            Assert::IsTrue(m_second == tree.NewestWindow());
            Assert::AreEqual(size_t{ 1 }, tree.Size());
        }

        TEST_METHOD (ResizeTileKeepsMinimumTileExtent)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsTrue(tree.ResizeTile(m_first, RECT{ 0, 0, 5, 1080 }, m_landscape, 0));
            Assert::AreEqual(TilingTree::MinimumTileExtent, RectOf(tree.CalculateRects(m_landscape, 0), m_first).right);

            Assert::IsTrue(tree.ResizeTile(m_second, RECT{ 1915, 0, 1920, 1080 }, m_landscape, 0));
            Assert::AreEqual(1920 - TilingTree::MinimumTileExtent, RectOf(tree.CalculateRects(m_landscape, 0), m_second).left);
        }

        TEST_METHOD (ResizeTileToItsOwnRectChangesNothing)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            const RECT first = RectOf(tree.CalculateRects(m_landscape, 0), m_first);
            Assert::IsFalse(tree.ResizeTile(m_first, first, m_landscape, 0));
        }

        TEST_METHOD (DraggingRightEdgeMovesDividerToItsRight)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsTrue(tree.ResizeTile(m_first, RECT{ 0, 0, 1100, 1080 }, m_landscape, 0));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 1100, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 1100, 0, 1920, 1080 }, RectOf(rects, m_second));
        }

        TEST_METHOD (DraggingLeftEdgeMovesDividerToItsLeft)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            // m_third is bottom right, its left edge is the root divider
            Assert::IsTrue(tree.ResizeTile(m_third, RECT{ 800, 540, 1920, 1080 }, m_landscape, 0));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 800, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 800, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 800, 540, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (DraggingCornerMovesBothDividers)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            // Top left corner of m_third moves both the root divider and the one between m_second and m_third
            Assert::IsTrue(tree.ResizeTile(m_third, RECT{ 1000, 400, 1920, 1080 }, m_landscape, 0));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 1000, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 1000, 0, 1920, 400 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 1000, 400, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (DraggingOuterEdgeChangesNothing)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsFalse(tree.ResizeTile(m_first, RECT{ 100, 0, 960, 1080 }, m_landscape, 0));
            Assert::IsFalse(tree.ResizeTile(m_second, RECT{ 960, 0, 1920, 900 }, m_landscape, 0));
            Assert::IsFalse(tree.ResizeTile(FakeWindow(42), RECT{ 0, 0, 10, 10 }, m_landscape, 0));
            Assert::AreEqual(960L, RectOf(tree.CalculateRects(m_landscape, 0), m_first).right);
        }

        TEST_METHOD (ResizeTileAccountsForSpacing)
        {
            constexpr int spacing = 16;
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, spacing);
            tree.Insert(m_second, m_first, m_landscape, spacing);

            const RECT first = RectOf(tree.CalculateRects(m_landscape, spacing), m_first);
            Assert::IsTrue(tree.ResizeTile(m_first, RECT{ first.left, first.top, 1100, first.bottom }, m_landscape, spacing));

            const auto rects = tree.CalculateRects(m_landscape, spacing);
            Assert::AreEqual(1100L, RectOf(rects, m_first).right);
            Assert::AreEqual(1100L + spacing, RectOf(rects, m_second).left);
        }

        TEST_METHOD (ClearRemovesAllWindows)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Clear();

            Assert::IsTrue(tree.Empty());
            Assert::IsTrue(tree.Insert(m_third, nullptr, m_landscape, 0));
            CustomAssert::AreEqual(m_landscape, RectOf(tree.CalculateRects(m_landscape, 0), m_third));
        }
    };
}
