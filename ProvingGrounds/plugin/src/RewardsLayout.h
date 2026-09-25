#pragma once

namespace RewardsLayout
{
    inline int Min(int a, int b) { return a < b ? a : b; }
    inline int Max(int a, int b) { return a > b ? a : b; }

    inline bool ShouldStack(int contentWidth, int bodyHeight)
    {
        return contentWidth < Max(720, bodyHeight * 50);
    }

    inline int FighterWidth(int contentWidth, int bodyHeight)
    {
        const int minimum = Max(160, bodyHeight * 11);
        return Min(320, Max(minimum, contentWidth * 21 / 100));
    }

    inline int GridColumns(
        int width,
        int minimumCardWidth,
        int gap,
        int maximumColumns)
    {
        if (minimumCardWidth <= 0 || maximumColumns <= 0)
            return 1;
        const int columns = (width + Max(0, gap)) /
            (minimumCardWidth + Max(0, gap));
        return Min(maximumColumns, Max(1, columns));
    }

    inline int FillRowHeight(
        int viewHeight,
        int visibleRows,
        int gap,
        int minimumHeight)
    {
        if (visibleRows <= 0)
            return Max(1, minimumHeight);
        const int safeGap = Max(0, gap);
        const int usable = viewHeight - (visibleRows + 1) * safeGap;
        return Max(Max(1, minimumHeight), usable / visibleRows);
    }

    inline int RarityIndex(int offeredTier, bool complete)
    {
        if (complete)
            return 3;
        return Min(3, Max(0, offeredTier));
    }

    inline int CompactRowHeight(int roomyHeight, int minimumHeight)
    {
        return Max(Max(1, minimumHeight), roomyHeight / 2);
    }

    inline int FightSupplyColumns(int visibleSupplyCount)
    {
        return visibleSupplyCount > 1 ? 2 : 1;
    }
}
