#pragma once

namespace TownCardLayout
{
    struct Grid {
        int columns, width, gap;
        int Column(int index) const { return index % columns; }
        int Row(int index) const { return index / columns; }
        int Left(int index) const { return Column(index) * (width + gap); }
        int ContentWidth(int count) const { return count > 0 ? count * width + (count - 1) * gap : 0; }
    };
    inline Grid Fit(int available, int minimum, int spacing) {
        if (available < 1) available = 1;
        if (minimum < 1) minimum = 1;
        if (spacing < 0) spacing = 0;
        int columns = (available + spacing) / (minimum + spacing);
        if (columns < 1) columns = 1;
        if (columns > 5) columns = 5;
        Grid grid = {columns, (available - (columns - 1) * spacing) / columns, spacing};
        return grid;
    }
    inline Grid SingleRow(int available, int minimum, int spacing, int count) {
        if (spacing < 0) spacing = 0;
        if (count < 1) count = 1;
        const Grid fitted = Fit(available, minimum, spacing);
        Grid grid = {count, fitted.width, spacing};
        return grid;
    }
    inline int ClampViewOffset(int offset, int content, int view) {
        const int maximum = content > view ? content - view : 0;
        int position = offset < 0 ? -offset : 0;
        if (position > maximum) position = maximum;
        return -position;
    }
}
