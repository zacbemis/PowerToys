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

        TEST_METHOD (SideAtPicksNearestEdgeByDiagonals)
        {
            const RECT tile{ 100, 100, 500, 300 };
            Assert::IsTrue(TilingTree::Side::Left == TilingTree::SideAt(tile, POINT{ 120, 200 }));
            Assert::IsTrue(TilingTree::Side::Right == TilingTree::SideAt(tile, POINT{ 480, 200 }));
            Assert::IsTrue(TilingTree::Side::Top == TilingTree::SideAt(tile, POINT{ 300, 110 }));
            Assert::IsTrue(TilingTree::Side::Bottom == TilingTree::SideAt(tile, POINT{ 300, 290 }));

            // Measured relative to the tile's size: in a wide tile, a point near the top middle is still on top
            Assert::IsTrue(TilingTree::Side::Top == TilingTree::SideAt(tile, POINT{ 200, 140 }));
            Assert::IsTrue(TilingTree::Side::Left == TilingTree::SideAt(tile, POINT{ 140, 180 }));
        }

        TEST_METHOD (DropRectIsHalfOfTileOnThatSide)
        {
            const RECT tile{ 0, 0, 1000, 600 };
            CustomAssert::AreEqual(RECT{ 0, 0, 500, 600 }, TilingTree::DropRect(tile, TilingTree::Side::Left, 0));
            CustomAssert::AreEqual(RECT{ 500, 0, 1000, 600 }, TilingTree::DropRect(tile, TilingTree::Side::Right, 0));
            CustomAssert::AreEqual(RECT{ 0, 0, 1000, 300 }, TilingTree::DropRect(tile, TilingTree::Side::Top, 0));
            CustomAssert::AreEqual(RECT{ 0, 300, 1000, 600 }, TilingTree::DropRect(tile, TilingTree::Side::Bottom, 0));
        }

        TEST_METHOD (InsertBesideEachSide)
        {
            const std::pair<TilingTree::Side, RECT> cases[] = {
                { TilingTree::Side::Left, RECT{ 0, 0, 960, 1080 } },
                { TilingTree::Side::Right, RECT{ 960, 0, 1920, 1080 } },
                { TilingTree::Side::Top, RECT{ 0, 0, 1920, 540 } },
                { TilingTree::Side::Bottom, RECT{ 0, 540, 1920, 1080 } },
            };

            for (const auto& [side, expected] : cases)
            {
                TilingTree tree;
                tree.Insert(m_first, nullptr, m_landscape, 0);
                Assert::IsTrue(tree.InsertBeside(m_second, m_first, side));

                const auto rects = tree.CalculateRects(m_landscape, 0);
                CustomAssert::AreEqual(expected, RectOf(rects, m_second));
                CustomAssert::AreEqual(TilingTree::DropRect(m_landscape, side, 0), RectOf(rects, m_second));
            }
        }

        TEST_METHOD (InsertBesideInEmptyTreeFillsArea)
        {
            TilingTree tree;
            Assert::IsTrue(tree.InsertBeside(m_first, nullptr, TilingTree::Side::Right));
            CustomAssert::AreEqual(m_landscape, RectOf(tree.CalculateRects(m_landscape, 0), m_first));
            Assert::IsTrue(m_first == tree.NewestWindow());
        }

        TEST_METHOD (InsertBesideUnknownTargetOrTiledWindowFails)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            Assert::IsFalse(tree.InsertBeside(m_second, FakeWindow(42), TilingTree::Side::Left));
            Assert::IsFalse(tree.InsertBeside(m_first, m_first, TilingTree::Side::Left));
            Assert::IsFalse(tree.InsertBeside(nullptr, m_first, TilingTree::Side::Left));
            Assert::AreEqual(size_t{ 1 }, tree.Size());
        }

        TEST_METHOD (MoveBesideTakesWindowOutOfItsTile)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            // m_first leaves the left half, so the right column fills the screen, then splits m_third's tile
            Assert::IsTrue(tree.MoveBeside(m_first, m_third, TilingTree::Side::Right));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 0, 540, 960, 1080 }, RectOf(rects, m_third));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_first));
        }

        TEST_METHOD (MoveBesideSiblingSwapsSides)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsTrue(tree.MoveBeside(m_first, m_second, TilingTree::Side::Right));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 1080 }, RectOf(rects, m_first));
        }

        TEST_METHOD (MoveBesideItselfOrUnknownWindowFails)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsFalse(tree.MoveBeside(m_first, m_first, TilingTree::Side::Right));
            Assert::IsFalse(tree.MoveBeside(m_third, m_first, TilingTree::Side::Right));
            Assert::IsFalse(tree.MoveBeside(m_first, m_third, TilingTree::Side::Right));
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(tree.CalculateRects(m_landscape, 0), m_first));
        }

        TEST_METHOD (SwapTowardTradesPlacesWithEverythingAcross)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            // m_first on the left, m_second and m_third stacked on the right: both move left together
            Assert::IsTrue(tree.SwapToward(m_first, TilingTree::Side::Right));

            auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 0, 540, 960, 1080 }, RectOf(rects, m_third));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 1080 }, RectOf(rects, m_first));

            Assert::IsTrue(tree.SwapToward(m_first, TilingTree::Side::Left));

            rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_third));
        }

        TEST_METHOD (SwapTowardFromInsideSplitTakesOnlyWindowsTile)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            Assert::IsTrue(tree.SwapToward(m_third, TilingTree::Side::Top));

            auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_third));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_second));

            // Across the left divider is m_first alone, which takes m_third's quarter
            Assert::IsTrue(tree.SwapToward(m_third, TilingTree::Side::Left));

            rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(rects, m_third));
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 540 }, RectOf(rects, m_first));
            CustomAssert::AreEqual(RECT{ 960, 540, 1920, 1080 }, RectOf(rects, m_second));
        }

        TEST_METHOD (SwapTowardKeepsTileSizes)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.ResizeTile(m_first, RECT{ 0, 0, 600, 1080 }, m_landscape, 0);

            Assert::IsTrue(tree.SwapToward(m_first, TilingTree::Side::Right));

            const auto rects = tree.CalculateRects(m_landscape, 0);
            CustomAssert::AreEqual(RECT{ 0, 0, 600, 1080 }, RectOf(rects, m_second));
            CustomAssert::AreEqual(RECT{ 600, 0, 1920, 1080 }, RectOf(rects, m_first));
        }

        TEST_METHOD (SwapTowardEdgeOrUnknownWindowFails)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            Assert::IsFalse(tree.SwapToward(m_first, TilingTree::Side::Right));

            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);
            Assert::IsFalse(tree.SwapToward(m_first, TilingTree::Side::Left));
            Assert::IsFalse(tree.SwapToward(m_first, TilingTree::Side::Top));
            Assert::IsFalse(tree.SwapToward(m_second, TilingTree::Side::Right));
            Assert::IsFalse(tree.SwapToward(m_third, TilingTree::Side::Bottom));
            Assert::IsFalse(tree.SwapToward(m_fourth, TilingTree::Side::Left));

            CustomAssert::AreEqual(RECT{ 0, 0, 960, 1080 }, RectOf(tree.CalculateRects(m_landscape, 0), m_first));
        }

        TEST_METHOD (ReplaceWindowTakesOverTile)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsTrue(tree.ReplaceWindow(m_second, m_fourth));

            Assert::IsFalse(tree.Contains(m_second));
            Assert::AreEqual(size_t{ 2 }, tree.Size());
            Assert::IsTrue(m_fourth == tree.NewestWindow());
            CustomAssert::AreEqual(RECT{ 960, 0, 1920, 1080 }, RectOf(tree.CalculateRects(m_landscape, 0), m_fourth));
        }

        TEST_METHOD (ReplaceWindowWithTiledOrUnknownWindowFails)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);

            Assert::IsFalse(tree.ReplaceWindow(m_first, m_second));
            Assert::IsFalse(tree.ReplaceWindow(m_first, nullptr));
            Assert::IsFalse(tree.ReplaceWindow(m_third, m_fourth));
            Assert::IsTrue(tree.Contains(m_first));
        }

        TEST_METHOD (TileAtEdgePicksNearestTileAlongThatEdge)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);
            const auto rects = tree.CalculateRects(m_landscape, 0);

            Assert::IsTrue(m_second == TilingTree::TileAtEdge(rects, TilingTree::Side::Right, POINT{ -500, 100 }));
            Assert::IsTrue(m_third == TilingTree::TileAtEdge(rects, TilingTree::Side::Right, POINT{ -500, 800 }));
            Assert::IsTrue(m_third == TilingTree::TileAtEdge(rects, TilingTree::Side::Right, POINT{ -500, 5000 }));
            Assert::IsTrue(m_first == TilingTree::TileAtEdge(rects, TilingTree::Side::Left, POINT{ 3000, 800 }));
            Assert::IsTrue(m_second == TilingTree::TileAtEdge(rects, TilingTree::Side::Top, POINT{ 1500, 2000 }));
            Assert::IsTrue(m_first == TilingTree::TileAtEdge(rects, TilingTree::Side::Bottom, POINT{ 100, -900 }));
            Assert::IsNull(TilingTree::TileAtEdge({}, TilingTree::Side::Left, POINT{}));
        }

        TEST_METHOD (CalculateRectsWithoutWindowMatchesRemovingIt)
        {
            TilingTree tree;
            tree.Insert(m_first, nullptr, m_landscape, 0);
            tree.Insert(m_second, m_first, m_landscape, 0);
            tree.Insert(m_third, m_second, m_landscape, 0);

            for (HWND window : { m_first, m_second, m_third })
            {
                const auto without = tree.CalculateRects(m_landscape, 0, window);

                TilingTree removed;
                removed.Insert(m_first, nullptr, m_landscape, 0);
                removed.Insert(m_second, m_first, m_landscape, 0);
                removed.Insert(m_third, m_second, m_landscape, 0);
                removed.Remove(window);
                const auto expected = removed.CalculateRects(m_landscape, 0);

                Assert::AreEqual(expected.size(), without.size());
                for (const auto& [tileWindow, rect] : expected)
                {
                    CustomAssert::AreEqual(rect, RectOf(without, tileWindow));
                }
            }

            TilingTree single;
            single.Insert(m_first, nullptr, m_landscape, 0);
            Assert::IsTrue(single.CalculateRects(m_landscape, 0, m_first).empty());
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
