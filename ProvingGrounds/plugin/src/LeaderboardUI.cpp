#include "LeaderboardUI.h"
#include "LeaderboardStore.h"
#include "LeaderboardLayout.h"
#include "NativeUI.h"
#include "PGLog.h"
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/util/hand.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#pragma warning(pop)

namespace
{
    MyGUI::Window* g_window = NULL;
    MyGUI::EditBox* g_subtitle = NULL;
    MyGUI::EditBox* g_empty = NULL;
    MyGUI::ScrollView* g_scroll = NULL;
    MyGUI::Button* g_close = NULL;
    MyGUI::TextBox* g_champions = NULL;
    MyGUI::Widget* g_championRails[2] = {};
    MyGUI::Widget* g_headerUnderline = NULL;
    MyGUI::TextBox* g_headers[LeaderboardLayout::ColumnCount] = {};
    MyGUI::IntSize g_clientSize;
    MyGUI::IntSize g_screenSize;
    int g_fontHeight = 0;
    int g_contentHeight = 0;

    struct RowWidgets
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* portrait;
        MyGUI::TextBox* placeholder;
        MyGUI::TextBox* cells[LeaderboardLayout::ColumnCount];
        MyGUI::EditBox* details;
        MyGUI::Widget* portraitBorder[4];
        MyGUI::Widget* cardBorder[4];
        MyGUI::Widget* separator;
        bool portraitAvailable;
    };
    std::vector<RowWidgets> g_rows;

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int BodyHeight()
    {
        return Max(1, Max(g_headers[0]->getFontHeight(), g_headers[0]->getTextSize().height));
    }

    void OnCloseClicked(MyGUI::Widget*) { LeaderboardUI::Close(); }
    void OnWindowButtonPressed(MyGUI::Widget*, const std::string& name)
    {
        if (name == "close" || name == "Close") LeaderboardUI::Close();
    }

    void ClearRows()
    {
        for (size_t i = 0; i < g_rows.size(); ++i)
            MyGUI::Gui::getInstance().destroyWidget(g_rows[i].root);
        g_rows.clear();
        g_contentHeight = 0;
    }

    void AbandonWindow()
    {
        // Window destruction also owns a partially constructed row not yet in g_rows.
        if (g_window) MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_subtitle = g_empty = NULL;
        g_scroll = NULL;
        g_close = NULL;
        g_champions = NULL;
        g_championRails[0] = g_championRails[1] = NULL;
        g_headerUnderline = NULL;
        for (int i = 0; i < LeaderboardLayout::ColumnCount; ++i) g_headers[i] = NULL;
        g_rows.clear();
        g_contentHeight = 0;
        g_clientSize = MyGUI::IntSize();
        g_screenSize = MyGUI::IntSize();
    }

    void OnLeaderboardWheel(MyGUI::Widget*, int relative)
    {
        if (!g_scroll || relative == 0) return;
        // Move the native canvas so scrollbar and content stay in sync.
        // Do not also apply an independent manual offset to each row.
        const int step = BodyHeight() * 3;
        const int offset = LeaderboardLayout::ClampOffset(
            -g_scroll->getViewOffset().top + (relative < 0 ? step : -step),
            g_contentHeight, g_scroll->getViewCoord().height);
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -offset));
    }

    void BindWheel(MyGUI::Widget* widget)
    {
        // Native wrapped labels have a mouse-pickable TextBox client. Its
        // EditBox owner handles that client's wheel without forwarding the
        // event to the root, so subscribe on the actual hit surface as well.
        while (widget)
        {
            widget->setNeedMouseFocus(true);
            widget->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }

    bool BindPortrait(MyGUI::ImageBox* image, Character* character)
    {
        PortraitManager* manager = PortraitManager::getInstance();
        if (!character || !character->isValid() || !manager)
        {
            image->setVisible(false);
            return false;
        }
        manager->setImageWidget(character->getHandle(), image, true);
        image->setVisible(true);
        return true;
    }

    int WrappedHeight(MyGUI::TextBox* label, int x, int y, int width)
    {
        // Final width must be set before measuring the native static EditBox.
        label->setCoord(x, y, Max(1, width), BodyHeight());
        const int height = Max(BodyHeight(), label->getTextSize().height);
        label->setSize(Max(1, width), height);
        return height;
    }

    MyGUI::Colour RankColour(int rank)
    {
        if (rank == 1) return MyGUI::Colour(.82f, .63f, .26f);
        if (rank == 2) return MyGUI::Colour(.66f, .69f, .72f);
        if (rank == 3) return MyGUI::Colour(.62f, .37f, .20f);
        return g_headers[0]->getTextColour();
    }

    MyGUI::Widget* OptionalAccent(
        MyGUI::Widget* parent,
        const MyGUI::IntCoord& coord,
        const std::string& name,
        float alpha)
    {
        try
        {
            MyGUI::Widget* accent = parent->createWidget<MyGUI::Widget>(
                "WhiteSkin", coord, MyGUI::Align::Default, name);
            accent->setAlpha(alpha);
            BindWheel(accent);
            return accent;
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: leaderboard accent skin unavailable");
            return NULL;
        }
    }

    void SetAccent(
        MyGUI::Widget* accent,
        const MyGUI::IntCoord& coord,
        const MyGUI::Colour& colour,
        bool visible)
    {
        if (!accent)
            return;
        accent->setCoord(coord);
        accent->setColour(colour);
        accent->setVisible(visible);
    }

    void LayoutPortrait(
        RowWidgets& row,
        int x,
        int y,
        int size,
        bool show,
        const MyGUI::Colour& colour,
        int edge)
    {
        row.portrait->setCoord(x, y, size, size);
        row.portrait->setVisible(show && row.portraitAvailable);
        row.placeholder->setCoord(x, y, size, size);
        row.placeholder->setVisible(show && !row.portraitAvailable);
        const MyGUI::IntCoord borders[4] = {
            MyGUI::IntCoord(x, y, edge, size),
            MyGUI::IntCoord(x, y, size, edge),
            MyGUI::IntCoord(x, y + size - edge, size, edge),
            MyGUI::IntCoord(x + size - edge, y, edge, size)
        };
        for (int i = 0; i < 4; ++i)
        {
            if (!row.portraitBorder[i])
                continue;
            row.portraitBorder[i]->setCoord(borders[i]);
            row.portraitBorder[i]->setColour(colour);
            row.portraitBorder[i]->setVisible(show);
        }
    }

    int LayoutFeatured(
        RowWidgets& row,
        int rank,
        int left,
        int top,
        int width,
        int portraitSize,
        int gap,
        int body,
        int edge)
    {
        row.root->setVisible(true);
        for (int c = 0; c < LeaderboardLayout::ColumnCount; ++c)
            row.cells[c]->setVisible(
                c == LeaderboardLayout::Rank ||
                c == LeaderboardLayout::Fighter);
        row.details->setVisible(true);
        row.cells[LeaderboardLayout::Rank]->setTextAlign(
            MyGUI::Align::Center | MyGUI::Align::VCenter);
        row.cells[LeaderboardLayout::Fighter]->setTextAlign(
            MyGUI::Align::Center | MyGUI::Align::VCenter);
        row.details->setTextAlign(
            MyGUI::Align::Center | MyGUI::Align::Top);

        row.cells[LeaderboardLayout::Rank]->setCoord(gap, gap,
            Max(1, width - 2 * gap), body);
        const int portraitTop = body + 2 * gap;
        LayoutPortrait(row, Max(0, (width - portraitSize) / 2),
            portraitTop, portraitSize, true, RankColour(rank), edge);
        const int nameTop = portraitTop + portraitSize + gap;
        const int nameHeight = WrappedHeight(
            row.cells[LeaderboardLayout::Fighter], gap, nameTop,
            Max(1, width - 2 * gap));
        const int detailsTop = nameTop + nameHeight + gap;
        const int detailsHeight = WrappedHeight(
            row.details, gap, detailsTop, Max(1, width - 2 * gap));
        const int height = detailsTop + detailsHeight + 2 * gap;
        row.root->setCoord(left, top, width, height);
        const MyGUI::Colour colour = RankColour(rank);
        SetAccent(row.cardBorder[0], MyGUI::IntCoord(0, 0, edge, height), colour, true);
        SetAccent(row.cardBorder[1], MyGUI::IntCoord(0, 0, width, edge), colour, true);
        SetAccent(row.cardBorder[2], MyGUI::IntCoord(0, height - edge, width, edge), colour, true);
        SetAccent(row.cardBorder[3], MyGUI::IntCoord(width - edge, 0, edge, height), colour, true);
        if (row.separator) row.separator->setVisible(false);
        return height;
    }

    void LayoutRows()
    {
        using namespace LeaderboardLayout;
        const int oldOffset = -g_scroll->getViewOffset().top;
        const int gap = NativeUI::Spacing(g_headers[0]);
        const int body = BodyHeight();
        const CeremonialMetrics ceremony = FitCeremonialMetrics(body, gap);
        const int rowPortrait = ceremony.rowPortrait;
        const int rowGap = ceremony.rowGap;
        // Scrollbar appearance can change the viewport width; use its actual
        // geometry, including a second pass after the canvas height is set.
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_scroll->getViewCoord();
            const int width = Max(1, view.width - 2 * gap);
            const int featuredCount = Min(3, static_cast<int>(g_rows.size()));
            int y = gap;
            g_champions->setVisible(featuredCount > 0);
            if (featuredCount > 0)
            {
                const TitleRails rails = FitTitleRails(
                    width, gap, g_champions->getTextSize().width + 4 * gap);
                g_champions->setCoord(gap + rails.titleLeft, y,
                    rails.titleWidth, body);
                const int railTop = y + (body - ceremony.edge) / 2;
                SetAccent(g_championRails[0],
                    MyGUI::IntCoord(gap, railTop,
                        rails.leftWidth, ceremony.edge),
                    RankColour(1), rails.leftWidth > 0);
                SetAccent(g_championRails[1],
                    MyGUI::IntCoord(gap + rails.rightLeft, railTop,
                        rails.rightWidth, ceremony.edge),
                    RankColour(1), rails.rightWidth > 0);
                y += body + gap;
                const Podium podium = FitPodium(
                    width, gap, featuredCount,
                    ceremony.championPortrait, ceremony.otherPortrait);
                int heights[3] = {};
                int maximumHeight = 0;
                if (podium.stacked)
                {
                    for (int p = 0; p < podium.count; ++p)
                    {
                        const PodiumSlot& slot = podium.slots[p];
                        RowWidgets& row = g_rows[slot.sourceIndex];
                        heights[p] = LayoutFeatured(row, slot.rank,
                            gap + slot.left, y, slot.width,
                            slot.portrait, gap, body, ceremony.edge);
                        y += heights[p] + gap;
                    }
                }
                else
                {
                    for (int p = 0; p < podium.count; ++p)
                    {
                        const PodiumSlot& slot = podium.slots[p];
                        RowWidgets& row = g_rows[slot.sourceIndex];
                        heights[p] = LayoutFeatured(row, slot.rank,
                            gap + slot.left, y, slot.width,
                            slot.portrait, gap, body, ceremony.edge);
                        maximumHeight = Max(maximumHeight, heights[p]);
                    }
                    for (int p = 0; p < podium.count; ++p)
                    {
                        const PodiumSlot& slot = podium.slots[p];
                        RowWidgets& row = g_rows[slot.sourceIndex];
                        row.root->setPosition(
                            gap + slot.left,
                            y + maximumHeight - heights[p]);
                    }
                    y += maximumHeight + gap;
                }
            }
            else
            {
                if (g_championRails[0]) g_championRails[0]->setVisible(false);
                if (g_championRails[1]) g_championRails[1]->setVisible(false);
            }

            int minimum[ColumnCount];
            for (int c = 0; c < ColumnCount; ++c)
            {
                minimum[c] = g_headers[c]->getTextSize().width + gap;
                if (c == Fighter) continue;
                for (size_t r = 3; r < g_rows.size(); ++r)
                    minimum[c] = Max(minimum[c], g_rows[r].cells[c]->getTextSize().width + gap);
            }
            minimum[Fighter] = Max(minimum[Fighter], rowPortrait + gap + body * 8);
            const Columns columns = FitColumns(width, gap, minimum);
            const bool hasRows = g_rows.size() > 3;
            for (int c = 0; c < ColumnCount; ++c)
            {
                g_headers[c]->setVisible(hasRows && !columns.stacked);
                if (hasRows && !columns.stacked)
                    g_headers[c]->setCoord(gap + columns.left[c], y,
                        columns.width[c], body);
            }
            if (hasRows && columns.stacked)
            {
                g_headers[Fighter]->setVisible(true);
                g_headers[Fighter]->setCoord(gap, y, width, body);
            }
            SetAccent(g_headerUnderline,
                MyGUI::IntCoord(gap, y + body, width, ceremony.edge),
                g_headers[0]->getTextColour(), hasRows);
            if (hasRows) y += body + ceremony.edge + rowGap;

            for (size_t r = 3; r < g_rows.size(); ++r)
            {
                RowWidgets& row = g_rows[r];
                row.root->setVisible(true);
                row.root->setCoord(gap, y, width, body);
                row.details->setVisible(columns.stacked);
                for (int c = 0; c < ColumnCount; ++c)
                {
                    row.cells[c]->setVisible(!columns.stacked || c == Rank || c == Fighter);
                    row.cells[c]->setTextAlign(
                        MyGUI::Align::Left | MyGUI::Align::VCenter);
                }
                int height;
                if (columns.stacked)
                {
                    const bool showImage = width > rowPortrait + gap + body * 4;
                    const int nameX = showImage ? rowPortrait + gap : 0;
                    const int nameY = showImage ? rowGap : body + 2 * rowGap;
                    row.cells[Rank]->setCoord(0, rowGap,
                        showImage ? rowPortrait : width, body);
                    LayoutPortrait(row, 0, body + 2 * rowGap,
                        rowPortrait, showImage, g_headers[0]->getTextColour(),
                        ceremony.edge);
                    const int nameHeight = WrappedHeight(row.cells[Fighter], nameX, nameY, width - nameX);
                    const int topHeight = Max(nameY + nameHeight,
                        showImage ? body + 2 * rowGap + rowPortrait : 0);
                    height = topHeight + rowGap + WrappedHeight(row.details, 0,
                        topHeight + rowGap, width) + rowGap;
                }
                else
                {
                    const int nameX = columns.left[Fighter] + rowPortrait + gap;
                    const int nameHeight = WrappedHeight(row.cells[Fighter], nameX, rowGap,
                        columns.width[Fighter] - rowPortrait - gap);
                    height = Max(nameHeight, rowPortrait) + 2 * rowGap;
                    LayoutPortrait(row, columns.left[Fighter], rowGap,
                        rowPortrait, true, g_headers[0]->getTextColour(),
                        ceremony.edge);
                    for (int c = 0; c < ColumnCount; ++c)
                        if (c != Fighter)
                            row.cells[c]->setCoord(columns.left[c], rowGap,
                                columns.width[c], height - 2 * rowGap);
                }
                row.root->setSize(width, height);
                for (int edge = 0; edge < 4; ++edge)
                    if (row.cardBorder[edge]) row.cardBorder[edge]->setVisible(false);
                SetAccent(row.separator,
                    MyGUI::IntCoord(0, height - ceremony.edge,
                        width, ceremony.edge),
                    g_headers[0]->getTextColour(), true);
                y += height + rowGap;
            }
            g_contentHeight = y;
            g_scroll->setCanvasSize(Max(1, view.width), Max(y, view.height));
            if (g_scroll->getViewCoord().width == view.width) break;
        }
        const int offset = LeaderboardLayout::ClampOffset(oldOffset,
            g_contentHeight, g_scroll->getViewCoord().height);
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -offset));
    }

    void FitWindowToScreen()
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render) return;
        const MyGUI::IntSize size = render->getViewSize();
        if (size.width <= 0 || size.height <= 0 || size == g_screenSize) return;
        g_screenSize = size;
        const int width = size.width * 88 / 100;
        const int height = size.height * 82 / 100;
        g_window->setCoord((size.width - width) / 2, (size.height - height) / 2, width, height);
    }

    void LayoutWindow()
    {
        MyGUI::Widget* client = g_window->getClientWidget();
        const MyGUI::IntSize size = client->getSize();
        const int gap = NativeUI::Spacing(g_headers[0]);
        const int width = Max(1, size.width - 2 * gap);
        const int body = BodyHeight();
        // A long save key scrolls inside a native text panel rather than pushing
        // the standings or close button beyond the client. Include skin insets.
        g_subtitle->setCoord(gap, gap, width, Max(100, body * 4));
        const int contextInsets = Max(0, g_subtitle->getHeight() - g_subtitle->getTextRegion().height);
        const int contextHeight = g_subtitle->getTextSize().height + contextInsets;
        const LeaderboardLayout::Vertical vertical = LeaderboardLayout::FitVertical(
            size.height, gap, body, contextHeight, NativeUI::RowHeight(g_close, 0));
        g_subtitle->setSize(width, vertical.contextHeight);
        g_subtitle->setVisible(vertical.contextHeight > 0);
        const int scrollTop = vertical.headingTop;
        const int scrollHeight = Max(0, vertical.closeTop - gap - scrollTop);
        g_scroll->setCoord(gap, scrollTop, width, scrollHeight);
        g_scroll->setVisible(!g_rows.empty() && scrollHeight > 0);
        const int closeWidth = Max(g_close->getTextSize().width + 4 * gap, body * 6);
        g_close->setCoord(Max(gap, (size.width - closeWidth) / 2), vertical.closeTop,
            closeWidth < width ? closeWidth : width, vertical.closeHeight);
        const int emptyHeight = WrappedHeight(g_empty, 2 * gap,
            scrollTop, Max(1, width - 2 * gap));
        g_empty->setSize(Max(1, width - 2 * gap),
            emptyHeight < scrollHeight ? emptyHeight : scrollHeight);
        g_empty->setVisible(g_rows.empty() && scrollHeight > 0);
        LayoutRows();
        g_clientSize = size;
        g_fontHeight = body;
    }

    bool EnsureWindow()
    {
        if (g_window) return true;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return false;
        const char* resource = "Kenshi_WindowCX";
        try
        {
            g_window = gui->createWidgetReal<MyGUI::Window>(resource,
                .06f, .09f, .88f, .82f, MyGUI::Align::Center,
                "Window", "ProvingGroundsLeaderboardWindow");
            g_window->setCaption("Proving Grounds - Leaderboard");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client) throw std::runtime_error("window has no client widget");
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            resource = "Kenshi_WordWrap";
            g_subtitle = client->createWidget<MyGUI::EditBox>(resource,
                MyGUI::IntCoord(0, 0, 100, 100), MyGUI::Align::Default, "PG_LeaderboardSubtitle");
            g_subtitle->setCaption("Arena rating");
            resource = "Kenshi_WordWrapEmpty";
            g_empty = NativeUI::WrappedLabel(client, initial, "PG_LeaderboardEmpty",
                "No rated fighters yet.\nWin arena matches to climb the board.");
            g_empty->setNeedMouseFocus(false);
            resource = "Kenshi_ScrollView";
            g_scroll = NativeUI::Scroll(client,
                MyGUI::IntCoord(0, 0, 300, 200), "PG_LeaderboardScroll");
            resource = "Kenshi_TextboxStandardText";
            g_champions = NativeUI::Label(g_scroll, initial,
                "PG_LeaderboardChampions", "Champions of the Pit");
            g_champions->setTextAlign(
                MyGUI::Align::Center | MyGUI::Align::VCenter);
            g_champions->setTextColour(RankColour(1));
            BindWheel(g_champions);
            g_championRails[0] = OptionalAccent(g_scroll, initial,
                "PG_LeaderboardChampionRailLeft", .42f);
            g_championRails[1] = OptionalAccent(g_scroll, initial,
                "PG_LeaderboardChampionRailRight", .42f);
            const char* headings[] = {"Rank", "Fighter", "Rating", "Record", "Matches"};
            for (int c = 0; c < LeaderboardLayout::ColumnCount; ++c)
            {
                g_headers[c] = NativeUI::Label(g_scroll, initial,
                    std::string("PG_LeaderboardHeader_") + headings[c], headings[c]);
                g_headers[c]->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
                BindWheel(g_headers[c]);
            }
            g_headerUnderline = OptionalAccent(g_scroll, initial,
                "PG_LeaderboardHeaderUnderline", .25f);
            resource = "Kenshi_Button1";
            g_close = NativeUI::Button(client, initial, "PG_LeaderboardClose", "Close");
            g_close->eventMouseButtonClick += MyGUI::newDelegate(OnCloseClicked);
            FitWindowToScreen();
            return true;
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: native leaderboard creation failed at ") +
                resource + ": " + error.what());
        }
        catch (...)
        {
            PGLog::Error(std::string("Proving Grounds: native leaderboard creation failed at ") + resource);
        }
        AbandonWindow();
        return false;
    }

    void RebuildRows(
        const std::vector<LeaderboardStore::Record>& standings,
        LeaderboardData::Kind kind,
        Building* source)
    {
        ClearRows();
        g_scroll->setViewOffset(MyGUI::IntPoint(0, 0));
        const MyGUI::IntCoord initial(0, 0, 100, 24);
        for (size_t i = 0; i < standings.size(); ++i)
        {
            const LeaderboardStore::Record& record = standings[i];
            char index[32], rank[32], rating[32], result[64], matches[32], details[160];
            sprintf_s(index, "PG_Lb_%u", static_cast<unsigned>(i));
            sprintf_s(rank, "%u", static_cast<unsigned>(i + 1));
            sprintf_s(rating, "%d", static_cast<int>(record.mmr + .5f));
            sprintf_s(result, "%d - %d", record.wins, record.losses);
            sprintf_s(matches, "%d", record.matches);
            sprintf_s(details, "Rating %s  \xE2\x80\xA2  %s  \xE2\x80\xA2  %s %s",
                rating, result, matches, record.matches == 1 ? "bout" : "bouts");
            const std::string prefix(index);
            RowWidgets row = {};
            row.root = g_scroll->createWidget<MyGUI::Widget>(
                "PanelEmpty", initial, MyGUI::Align::Default, prefix);
            BindWheel(row.root);
            row.portrait = row.root->createWidget<MyGUI::ImageBox>(
                "ImageBox", initial, MyGUI::Align::Default, prefix + "_Portrait");
            BindWheel(row.portrait);
            const std::string placeholder = record.name.empty()
                ? "?" : record.name.substr(0, 1);
            row.placeholder = NativeUI::Label(row.root, initial,
                prefix + "_PortraitPlaceholder", placeholder);
            row.placeholder->setTextAlign(
                MyGUI::Align::Center | MyGUI::Align::VCenter);
            BindWheel(row.placeholder);
            for (int edge = 0; edge < 4; ++edge)
                row.portraitBorder[edge] = OptionalAccent(
                    row.root, initial,
                    prefix + "_PortraitBorder" +
                        static_cast<char>('0' + edge), .82f);
            row.cells[LeaderboardLayout::Rank] = NativeUI::Label(row.root, initial, prefix + "_Rank", rank);
            row.cells[LeaderboardLayout::Fighter] = NativeUI::WrappedLabel(row.root, initial, prefix + "_Name", record.name);
            row.cells[LeaderboardLayout::Rating] = NativeUI::Label(row.root, initial, prefix + "_Rating", rating);
            row.cells[LeaderboardLayout::Record] = NativeUI::Label(row.root, initial, prefix + "_Record", result);
            row.cells[LeaderboardLayout::Matches] = NativeUI::Label(row.root, initial, prefix + "_Matches", matches);
            for (int c = 0; c < LeaderboardLayout::ColumnCount; ++c)
            {
                row.cells[c]->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
                BindWheel(row.cells[c]);
            }
            row.details = NativeUI::WrappedLabel(row.root, initial, prefix + "_Details", details);
            BindWheel(row.details);
            for (int edge = 0; edge < 4; ++edge)
                row.cardBorder[edge] = OptionalAccent(
                    row.root, initial,
                    prefix + "_CardBorder" +
                        static_cast<char>('0' + edge), .28f);
            row.separator = OptionalAccent(row.root, initial,
                prefix + "_Separator", .18f);
            if (i < 3)
                row.cells[LeaderboardLayout::Rank]->setTextColour(
                    RankColour(static_cast<int>(i + 1)));
            row.portraitAvailable = BindPortrait(
                row.portrait,
                LeaderboardStore::FindRatedCharacter(
                    kind, record.id, source));
            g_rows.push_back(row);
        }
    }

    bool RefreshContent(LeaderboardData::Kind kind, Building* source)
    {
        if (!EnsureWindow()) return false;
        try
        {
            std::vector<LeaderboardStore::Record> standings;
            LeaderboardStore::GetStandings(kind, standings);
            const bool town = kind == LeaderboardData::Town;
            g_window->setCaption(town
                ? "Proving Grounds - Town Leaderboard"
                : "Proving Grounds - Player Leaderboard");
            g_subtitle->setCaption(LeaderboardStore::HasActiveSave()
                ? std::string(town
                    ? "Scratch circuit standings - "
                    : "Player arena standings - ") +
                    LeaderboardStore::GetActiveSaveKey()
                : std::string(town
                    ? "Scratch circuit standings - no active save"
                    : "Player arena standings - no active save"));
            g_empty->setCaption(town
                ? "No town bouts recorded yet.\nComplete a town arena bout to open the standings."
                : "No rated player fighters yet.\nComplete a player-v-player arena match to open the standings.");
            RebuildRows(standings, kind, source);
            g_empty->setVisible(standings.empty());
            g_scroll->setVisible(!standings.empty());
            FitWindowToScreen();
            LayoutWindow();
            return true;
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: native leaderboard content failed ") +
                "(PanelEmpty/ImageBox/Kenshi_TextboxStandardText/Kenshi_WordWrapEmpty): " + error.what());
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native leaderboard content failed (PanelEmpty/ImageBox/Kenshi_TextboxStandardText/Kenshi_WordWrapEmpty)");
        }
        AbandonWindow();
        return false;
    }
}

namespace LeaderboardUI
{
    void Show(LeaderboardData::Kind kind, Building* source)
    {
        if (!RefreshContent(kind, source))
        {
            PGLog::Error("Proving Grounds: Leaderboard UI could not create window");
            return;
        }
        g_window->setVisible(true);
        PGLog::Debug("Proving Grounds: native leaderboard UI shown");
    }

    void Close() { if (g_window) g_window->setVisible(false); }
    bool IsVisible() { return g_window && g_window->getVisible(); }

    void Tick()
    {
        if (!IsVisible()) return;
        try
        {
            FitWindowToScreen();
            // Standings remain event-driven; resize never rebuilds or rebinds rows.
            if (g_window->getClientWidget()->getSize() != g_clientSize || BodyHeight() != g_fontHeight)
                LayoutWindow();
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native leaderboard resize failed");
            AbandonWindow();
        }
    }
}
