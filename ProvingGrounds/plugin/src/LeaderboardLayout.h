#pragma once

// Geometry only: measured text enters from MyGUI, no game/widget ownership.
namespace LeaderboardLayout
{
    enum Column { Rank, Fighter, Rating, Record, Matches, ColumnCount };
    struct Columns
    {
        bool stacked;
        int left[ColumnCount];
        int width[ColumnCount];
        Columns() : stacked(false)
        {
            for (int i = 0; i < ColumnCount; ++i) left[i] = width[i] = 0;
        }
    };

    inline Columns FitColumns(int availableWidth, int gap, const int* minimum)
    {
        Columns result;
        int required = gap * (ColumnCount - 1);
        for (int i = 0; i < ColumnCount; ++i) required += minimum[i];
        result.stacked = availableWidth < required;
        if (result.stacked) return result;
        int x = 0;
        for (int i = 0; i < ColumnCount; ++i)
        {
            result.left[i] = x;
            result.width[i] = minimum[i];
            if (i == Fighter) result.width[i] += availableWidth - required;
            x += result.width[i] + gap;
        }
        return result;
    }

    inline int ClampOffset(int offset, int contentHeight, int viewHeight)
    {
        const int maximum = contentHeight > viewHeight ? contentHeight - viewHeight : 0;
        return offset < 0 ? 0 : offset > maximum ? maximum : offset;
    }

    struct Vertical
    {
        int contextHeight, headingTop, headingHeight, scrollTop, scrollHeight;
        int closeTop, closeHeight;
        Vertical() : contextHeight(0), headingTop(0), headingHeight(0),
            scrollTop(0), scrollHeight(0), closeTop(0), closeHeight(0) {}
    };

    inline Vertical FitVertical(int height, int gap, int body, int contextHeight, int closeHeight)
    {
        Vertical result;
        if (height <= 0) return result;
        result.closeHeight = closeHeight < height ? closeHeight : height;
        result.closeTop = height - gap - result.closeHeight;
        if (result.closeTop < 0) result.closeTop = 0;
        const int usable = result.closeTop > gap ? result.closeTop - gap : 0;
        result.contextHeight = contextHeight < usable / 4 ? contextHeight : usable / 4;
        result.headingTop = 2 * gap + result.contextHeight;
        if (result.headingTop > usable) result.headingTop = usable;
        const int headerRoom = usable - result.headingTop - gap;
        result.headingHeight = headerRoom > 0 ? (body < headerRoom ? body : headerRoom) : 0;
        result.scrollTop = result.headingTop + result.headingHeight + gap;
        if (result.scrollTop > usable) result.scrollTop = usable;
        result.scrollHeight = usable - result.scrollTop;
        return result;
    }

    struct CeremonialMetrics
    {
        int championPortrait, otherPortrait, rowPortrait, rowGap, edge;
        CeremonialMetrics() : championPortrait(0), otherPortrait(0),
            rowPortrait(0), rowGap(0), edge(0) {}
    };

    inline CeremonialMetrics FitCeremonialMetrics(int body, int gap)
    {
        CeremonialMetrics result;
        result.championPortrait = body * 5 > 96 ? body * 5 : 96;
        result.otherPortrait = body * 4 > 72 ? body * 4 : 72;
        result.rowPortrait = body * 2 > 40 ? body * 2 : 40;
        result.rowGap = gap / 2 > 2 ? gap / 2 : 2;
        result.edge = gap / 4 > 1 ? gap / 4 : 1;
        return result;
    }

    struct TitleRails
    {
        int titleLeft, titleWidth, leftWidth, rightLeft, rightWidth;
        TitleRails() : titleLeft(0), titleWidth(0), leftWidth(0),
            rightLeft(0), rightWidth(0) {}
    };

    inline TitleRails FitTitleRails(
        int availableWidth,
        int gap,
        int measuredTitleWidth)
    {
        TitleRails result;
        if (availableWidth <= 0)
            return result;
        const int maximumTitle = availableWidth > 2 * gap
            ? availableWidth - 2 * gap : availableWidth;
        result.titleWidth = measuredTitleWidth < maximumTitle
            ? measuredTitleWidth : maximumTitle;
        if (result.titleWidth < 0)
            result.titleWidth = 0;
        result.titleLeft = (availableWidth - result.titleWidth) / 2;
        const int railGap = 2 * gap;
        result.leftWidth = result.titleLeft > railGap
            ? result.titleLeft - railGap : 0;
        result.rightLeft = result.titleLeft + result.titleWidth + railGap;
        result.rightWidth = result.rightLeft < availableWidth
            ? availableWidth - result.rightLeft : 0;
        if (result.leftWidth == 0 || result.rightWidth == 0)
        {
            result.leftWidth = 0;
            result.rightWidth = 0;
        }
        return result;
    }

    struct PodiumSlot
    {
        int sourceIndex, rank, left, top, width, portrait;
        PodiumSlot() : sourceIndex(0), rank(0), left(0), top(0),
            width(0), portrait(0) {}
    };

    struct Podium
    {
        bool stacked;
        int count, contentHeight;
        PodiumSlot slots[3];
        Podium() : stacked(false), count(0), contentHeight(0) {}
    };

    inline Podium FitPodium(
        int availableWidth,
        int gap,
        int standingsCount,
        int championPortrait,
        int otherPortrait)
    {
        Podium result;
        result.count = standingsCount < 0 ? 0 :
            standingsCount > 3 ? 3 : standingsCount;
        if (result.count == 0 || availableWidth <= 0)
            return result;

        const int minimumCell = championPortrait + 8 * gap;
        const int required = result.count * minimumCell +
            (result.count - 1) * gap;
        result.stacked = availableWidth < required;

        if (result.stacked)
        {
            int top = 0;
            for (int i = 0; i < result.count; ++i)
            {
                PodiumSlot& slot = result.slots[i];
                slot.sourceIndex = i;
                slot.rank = i + 1;
                slot.left = 0;
                slot.top = top;
                slot.width = availableWidth;
                slot.portrait = i == 0 ? championPortrait : otherPortrait;
                top += slot.portrait + 8 * gap;
                if (i + 1 < result.count) top += gap;
            }
            result.contentHeight = top;
            return result;
        }

        const int maximumCell = championPortrait + 20 * gap;
        int cell = (availableWidth - (result.count - 1) * gap) /
            result.count;
        if (cell > maximumCell) cell = maximumCell;
        const int used = result.count * cell + (result.count - 1) * gap;
        int left = (availableWidth - used) / 2;
        const int sourceOrder[3] = { 1, 0, 2 };
        const int rankOrder[3] = { 2, 1, 3 };
        for (int i = 0; i < result.count; ++i)
        {
            PodiumSlot& slot = result.slots[i];
            slot.sourceIndex = result.count == 3 ? sourceOrder[i] : i;
            slot.rank = result.count == 3 ? rankOrder[i] : i + 1;
            slot.left = left;
            slot.top = 0;
            slot.width = cell;
            slot.portrait = slot.rank == 1
                ? championPortrait : otherPortrait;
            left += cell + gap;
        }
        result.contentHeight = championPortrait + 8 * gap;
        return result;
    }
}
