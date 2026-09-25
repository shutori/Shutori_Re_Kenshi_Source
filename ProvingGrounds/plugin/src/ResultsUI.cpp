#include "ResultsUI.h"
#include "SparStats.h"
#include "NativeUI.h"
#include "ResultsLayout.h"
#include "PGLog.h"
#include <Windows.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

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
    bool g_pending = false;
    float g_delaySec = 1.0f;
    float g_elapsedSec = 0.0f;
    DWORD g_lastTick = 0;
    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_measure = NULL;
    MyGUI::EditBox* g_header = NULL;
    MyGUI::EditBox* g_context = NULL;
    MyGUI::EditBox* g_empty = NULL;
    MyGUI::Button* g_closeBtn = NULL;
    MyGUI::ScrollView* g_scroll = NULL;
    MyGUI::Widget* g_headerRails[2] = {};
    MyGUI::IntSize g_clientSize;
    MyGUI::IntSize g_screenSize;
    int g_fontHeight = 0;
    int g_contentHeight = 0;

    struct PodiumSlot
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* portrait;
        MyGUI::TextBox* placeholder;
        MyGUI::TextBox* rankLabel;
        MyGUI::EditBox* nameLabel;
        MyGUI::EditBox* statsLabel;
        MyGUI::Widget* portraitBorder[4];
        MyGUI::Widget* cardBorder[4];
        bool portraitAvailable;
        int place;
    };
    PodiumSlot g_slots[3] = {};

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int BodyHeight() { return Max(1, Max(g_measure->getFontHeight(), g_measure->getTextSize().height)); }
    void LayoutWindow();
    void OnCloseClicked(MyGUI::Widget*) { ResultsUI::Close(); }
    void OnWindowButtonPressed(MyGUI::Widget*, const std::string& name)
    {
        if (name == "close" || name == "Close") ResultsUI::Close();
    }
    void OnWheel(MyGUI::Widget*, int relative)
    {
        if (!g_scroll || !relative) return;
        const int maximum = Max(0, g_contentHeight - g_scroll->getViewCoord().height);
        const int next = -g_scroll->getViewOffset().top + (relative < 0 ? 3 : -3) * BodyHeight();
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -Min(maximum, Max(0, next))));
    }
    void BindWheel(MyGUI::Widget* widget)
    {
        while (widget)
        {
            widget->setNeedMouseFocus(true);
            widget->eventMouseWheel += MyGUI::newDelegate(OnWheel);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }
    int WrappedHeight(MyGUI::TextBox* label, int x, int y, int width)
    {
        label->setCoord(x, y, Max(1, width), BodyHeight());
        const int insets = Max(0, label->getHeight() - label->getTextRegion().height);
        const int height = Max(BodyHeight(), label->getTextSize().height + insets);
        label->setSize(Max(1, width), height);
        return height;
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
    MyGUI::Colour RankColour(int place)
    {
        if (place == 1) return MyGUI::Colour(.82f, .63f, .26f);
        if (place == 2) return MyGUI::Colour(.66f, .69f, .72f);
        if (place == 3) return MyGUI::Colour(.62f, .37f, .20f);
        return g_measure->getTextColour();
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
            PGLog::Error("Proving Grounds: results accent skin unavailable");
            return NULL;
        }
    }
    void SetAccent(
        MyGUI::Widget* accent,
        const MyGUI::IntCoord& coord,
        const MyGUI::Colour& colour,
        bool visible)
    {
        if (!accent) return;
        accent->setCoord(coord);
        accent->setColour(colour);
        accent->setVisible(visible);
    }
    void LayoutPortrait(
        PodiumSlot& slot,
        int x,
        int y,
        int size,
        const MyGUI::Colour& colour,
        int edge)
    {
        slot.portrait->setCoord(x, y, size, size);
        slot.portrait->setVisible(slot.portraitAvailable);
        slot.placeholder->setCoord(x, y, size, size);
        slot.placeholder->setVisible(!slot.portraitAvailable);
        const MyGUI::IntCoord borders[4] = {
            MyGUI::IntCoord(x, y, edge, size),
            MyGUI::IntCoord(x, y, size, edge),
            MyGUI::IntCoord(x, y + size - edge, size, edge),
            MyGUI::IntCoord(x + size - edge, y, edge, size)
        };
        for (int i = 0; i < 4; ++i)
            SetAccent(slot.portraitBorder[i], borders[i], colour, true);
    }
    int LayoutResultCard(
        PodiumSlot& slot,
        int left,
        int top,
        int width,
        int portrait,
        int gap,
        int body,
        int edge)
    {
        slot.root->setVisible(true);
        slot.rankLabel->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
        slot.nameLabel->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
        slot.statsLabel->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
        slot.rankLabel->setCoord(gap, gap, Max(1, width - 2 * gap), body);
        const int portraitTop = body + 2 * gap;
        LayoutPortrait(slot, Max(0, (width - portrait) / 2), portraitTop,
            portrait, RankColour(slot.place), edge);
        const int nameTop = portraitTop + portrait + gap;
        const int innerWidth = Max(1, width - 2 * gap);
        const int nameHeight = WrappedHeight(
            slot.nameLabel, gap, nameTop, innerWidth);
        const int statsTop = nameTop + nameHeight + gap;
        const int statsHeight = WrappedHeight(
            slot.statsLabel, gap, statsTop, innerWidth);
        const int height = statsTop + statsHeight + 2 * gap;
        slot.root->setCoord(left, top, width, height);
        const MyGUI::Colour colour = RankColour(slot.place);
        SetAccent(slot.cardBorder[0], MyGUI::IntCoord(0, 0, edge, height), colour, true);
        SetAccent(slot.cardBorder[1], MyGUI::IntCoord(0, 0, width, edge), colour, true);
        SetAccent(slot.cardBorder[2], MyGUI::IntCoord(0, height - edge, width, edge), colour, true);
        SetAccent(slot.cardBorder[3], MyGUI::IntCoord(width - edge, 0, edge, height), colour, true);
        return height;
    }
    void SetSlotVisible(PodiumSlot& slot, bool visible)
    {
        slot.root->setVisible(visible);
    }
    void AbandonWindow()
    {
        if (g_window) MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_measure = NULL;
        g_header = g_context = g_empty = NULL;
        g_closeBtn = NULL;
        g_scroll = NULL;
        g_headerRails[0] = g_headerRails[1] = NULL;
        for (int i = 0; i < 3; ++i) g_slots[i] = PodiumSlot();
        g_clientSize = g_screenSize = MyGUI::IntSize();
    }
    int VisibleResultCount()
    {
        int count = 0;
        for (int i = 0; i < 3; ++i)
            if (g_slots[i].root->getVisible()) ++count;
        return count;
    }
    void FitWindowToContent(bool force = false)
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render) return;
        const MyGUI::IntSize screen = render->getViewSize();
        if (screen.width <= 0 || screen.height <= 0) return;
        if (!force && screen == g_screenSize && BodyHeight() == g_fontHeight) return;
        g_screenSize = screen;

        const int gap = NativeUI::Spacing(g_measure);
        const int body = BodyHeight();
        const int count = VisibleResultCount();
        const LeaderboardLayout::CeremonialMetrics metrics =
            LeaderboardLayout::FitCeremonialMetrics(body, gap);
        const int naturalCell = metrics.championPortrait + 20 * gap;
        int contentWidth = count > 0
            ? count * naturalCell + (count - 1) * gap
            : g_empty->getTextSize().width + 4 * gap;
        contentWidth = Max(contentWidth, g_header->getTextSize().width + 8 * gap);
        contentWidth = Max(contentWidth, g_context->getTextSize().width + 4 * gap);
        contentWidth = Max(contentWidth, g_closeBtn->getTextSize().width + 4 * gap);

        const MyGUI::IntSize outerBefore = g_window->getSize();
        const MyGUI::IntSize clientBefore = g_window->getClientWidget()->getSize();
        const int chromeWidth = Max(0, outerBefore.width - clientBefore.width);
        const int chromeHeight = Max(0, outerBefore.height - clientBefore.height);
        const int scrollInsetWidth = Max(0,
            g_scroll->getWidth() - g_scroll->getViewCoord().width);
        const int maximumWidth = screen.width * 92 / 100;
        const int maximumHeight = screen.height * 90 / 100;
        const int desiredClientWidth = contentWidth + 4 * gap + scrollInsetWidth;
        const int outerWidth = Min(maximumWidth,
            Max(320, desiredClientWidth + chromeWidth));

        // Use the screen-height cap for the measuring pass, then collapse the
        // window around the actual canvas and persistent close button.
        g_window->setCoord((screen.width - outerWidth) / 2,
            (screen.height - maximumHeight) / 2, outerWidth, maximumHeight);
        LayoutWindow();
        const int scrollInsetHeight = Max(0,
            g_scroll->getHeight() - g_scroll->getViewCoord().height);
        const int closeHeight = NativeUI::RowHeight(g_closeBtn, 0);
        const int desiredClientHeight = g_contentHeight + scrollInsetHeight +
            closeHeight + 3 * gap;
        const int outerHeight = Min(maximumHeight,
            Max(180, desiredClientHeight + chromeHeight));
        g_window->setCoord((screen.width - outerWidth) / 2,
            (screen.height - outerHeight) / 2, outerWidth, outerHeight);
    }
    void LayoutWindow()
    {
        const MyGUI::IntSize size = g_window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(g_measure);
        const int body = BodyHeight();
        const int width = Max(1, size.width - 2 * gap);
        const int closeHeight = NativeUI::RowHeight(g_closeBtn, 0);
        const int closeTop = Max(gap, size.height - gap - closeHeight);
        const int closeWidth = Min(width, g_closeBtn->getTextSize().width + 4 * gap);
        g_closeBtn->setCoord((size.width - closeWidth) / 2, closeTop, closeWidth, closeHeight);
        // The outcome, context and three existing entries share a single canvas;
        // long snapshots scroll without displacing the persistent close action.
        g_scroll->setCoord(gap, gap, width, Max(1, closeTop - 2 * gap));
        const int oldOffset = -g_scroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_scroll->getViewCoord();
            const int contentWidth = Max(1, view.width - 2 * gap);
            int resultCount = 0;
            for (int i = 0; i < 3; ++i)
                if (g_slots[i].root->getVisible()) ++resultCount;
            const ResultsLayout::Ceremony ceremony =
                ResultsLayout::FitCeremony(
                    contentWidth, gap, body, resultCount,
                    g_measure->getTextSize().width + 4 * gap);
            int y = gap;
            const int headerHeight = WrappedHeight(
                g_header, gap + ceremony.title.titleLeft, y,
                ceremony.title.titleWidth);
            const int railTop = y + (headerHeight - ceremony.metrics.edge) / 2;
            SetAccent(g_headerRails[0],
                MyGUI::IntCoord(gap, railTop,
                    ceremony.title.leftWidth, ceremony.metrics.edge),
                RankColour(1), ceremony.title.leftWidth > 0);
            SetAccent(g_headerRails[1],
                MyGUI::IntCoord(gap + ceremony.title.rightLeft, railTop,
                    ceremony.title.rightWidth, ceremony.metrics.edge),
                RankColour(1), ceremony.title.rightWidth > 0);
            y += headerHeight + gap;
            y += WrappedHeight(g_context, gap, y, contentWidth) + 2 * gap;
            if (resultCount > 0)
            {
                int heights[3] = {};
                int maximumHeight = 0;
                if (ceremony.podium.stacked)
                {
                    for (int p = 0; p < ceremony.podium.count; ++p)
                    {
                        const LeaderboardLayout::PodiumSlot& geometry =
                            ceremony.podium.slots[p];
                        PodiumSlot& slot = g_slots[geometry.sourceIndex];
                        heights[p] = LayoutResultCard(
                            slot, gap + geometry.left, y,
                            geometry.width, geometry.portrait,
                            gap, body, ceremony.metrics.edge);
                        y += heights[p] + gap;
                    }
                }
                else
                {
                    for (int p = 0; p < ceremony.podium.count; ++p)
                    {
                        const LeaderboardLayout::PodiumSlot& geometry =
                            ceremony.podium.slots[p];
                        PodiumSlot& slot = g_slots[geometry.sourceIndex];
                        heights[p] = LayoutResultCard(
                            slot, gap + geometry.left, y,
                            geometry.width, geometry.portrait,
                            gap, body, ceremony.metrics.edge);
                        maximumHeight = Max(maximumHeight, heights[p]);
                    }
                    for (int p = 0; p < ceremony.podium.count; ++p)
                    {
                        const LeaderboardLayout::PodiumSlot& geometry =
                            ceremony.podium.slots[p];
                        g_slots[geometry.sourceIndex].root->setPosition(
                            gap + geometry.left,
                            y + maximumHeight - heights[p]);
                    }
                    y += maximumHeight + gap;
                }
            }
            if (g_empty->getVisible()) y += WrappedHeight(g_empty, gap, y, contentWidth) + gap;
            g_contentHeight = y;
            g_scroll->setCanvasSize(Max(1, view.width), Max(y, view.height));
            if (g_scroll->getViewCoord().width == view.width) break;
        }
        const int maximum = Max(0, g_contentHeight - g_scroll->getViewCoord().height);
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -Min(maximum, Max(0, oldOffset))));
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
                .08f, .09f, .84f, .82f, MyGUI::Align::Center,
                "Window", "ProvingGroundsResultsWindow");
            g_window->setCaption("Proving Grounds - Results");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client) throw std::runtime_error("window has no client widget");
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            resource = "Kenshi_TextboxStandardText";
            g_measure = NativeUI::Label(client, initial, "PG_ResultsMeasure", "Match complete");
            g_measure->setVisible(false);
            resource = "Kenshi_ScrollView";
            g_scroll = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 200), "PG_ResultsScroll");
            resource = "Kenshi_WordWrapEmpty";
            g_header = NativeUI::WrappedLabel(g_scroll, initial, "PG_ResultsHeader", "");
            g_context = NativeUI::WrappedLabel(g_scroll, initial, "PG_ResultsContext", "");
            g_empty = NativeUI::WrappedLabel(g_scroll, initial, "PG_ResultsEmpty",
                "No victor\nThe match ended without a podium.");
            g_header->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
            g_header->setTextColour(RankColour(1));
            g_context->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
            g_empty->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
            BindWheel(g_header);
            BindWheel(g_context);
            BindWheel(g_empty);
            g_headerRails[0] = OptionalAccent(g_scroll, initial,
                "PG_ResultsHeaderRailLeft", .42f);
            g_headerRails[1] = OptionalAccent(g_scroll, initial,
                "PG_ResultsHeaderRailRight", .42f);
            for (int i = 0; i < 3; ++i)
            {
                char name[64];
                sprintf_s(name, "PG_ResultsSlot_%d", i);
                const std::string prefix(name);
                PodiumSlot& slot = g_slots[i];
                resource = "PanelEmpty";
                slot.root = g_scroll->createWidget<MyGUI::Widget>(resource, initial, MyGUI::Align::Default, prefix);
                resource = "ImageBox";
                slot.portrait = slot.root->createWidget<MyGUI::ImageBox>(resource, initial,
                    MyGUI::Align::Default, prefix + "_Portrait");
                slot.portrait->setVisible(false);
                resource = "Kenshi_TextboxStandardText";
                slot.placeholder = NativeUI::Label(
                    slot.root, initial, prefix + "_PortraitPlaceholder", "?");
                slot.placeholder->setTextAlign(
                    MyGUI::Align::Center | MyGUI::Align::VCenter);
                slot.rankLabel = NativeUI::Label(slot.root, initial, prefix + "_Rank", "");
                resource = "Kenshi_WordWrapEmpty";
                slot.nameLabel = NativeUI::WrappedLabel(slot.root, initial, prefix + "_Name", "");
                slot.statsLabel = NativeUI::WrappedLabel(slot.root, initial, prefix + "_Stats", "");
                BindWheel(slot.root);
                BindWheel(slot.portrait);
                BindWheel(slot.placeholder);
                BindWheel(slot.rankLabel);
                BindWheel(slot.nameLabel);
                BindWheel(slot.statsLabel);
                for (int edge = 0; edge < 4; ++edge)
                {
                    slot.portraitBorder[edge] = OptionalAccent(
                        slot.root, initial,
                        prefix + "_PortraitBorder" +
                            static_cast<char>('0' + edge), .82f);
                    slot.cardBorder[edge] = OptionalAccent(
                        slot.root, initial,
                        prefix + "_CardBorder" +
                            static_cast<char>('0' + edge), .28f);
                }
            }
            resource = "Kenshi_Button1";
            g_closeBtn = NativeUI::Button(client, initial, "PG_ResultsClose", "Close results");
            g_closeBtn->eventMouseButtonClick += MyGUI::newDelegate(OnCloseClicked);
            return true;
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: native results creation failed at ") + resource + ": " + error.what());
        }
        catch (...)
        {
            PGLog::Error(std::string("Proving Grounds: native results creation failed at ") + resource);
        }
        AbandonWindow();
        return false;
    }

    const char* PlaceCaption(MatchRules::MatchMode mode, int place)
    {
        if (mode == MatchRules::ModeTeamAvB ||
            mode == MatchRules::ModeTeams1v1)
        {
            if (place == 1)
                return "MVP";
            if (place == 2)
                return "MVP #2";
            if (place == 3)
                return "MVP #3";
            return "";
        }

        if (place == 1)
            return "VICTOR";
        if (place == 2)
            return "SECOND PLACE";
        if (place == 3)
            return "THIRD PLACE";
        return "";
    }

    const char* ModeCaption(MatchRules::MatchMode mode)
    {
        if (mode == MatchRules::ModeTeamAvB)
            return "Team A vs Team B";
        if (mode == MatchRules::ModeTeams1v1)
            return "Teams 1v1";
        return "Last man standing";
    }

    const char* TeamCaption(MatchRules::MatchTeam team)
    {
        if (team == MatchRules::TeamA)
            return "Team A";
        if (team == MatchRules::TeamB)
            return "Team B";
        return "";
    }

    void FillSlot(
        int slotIndex,
        const SparPodium::PodiumEntry& entry,
        MatchRules::MatchMode mode,
        Character* character)
    {
        PodiumSlot& slot = g_slots[slotIndex];
        SetSlotVisible(slot, true);
        slot.place = entry.place;

        slot.rankLabel->setCaption(PlaceCaption(mode, entry.place));
        slot.rankLabel->setTextColour(RankColour(entry.place));

        char nameLine[96];
        const char* team = TeamCaption(entry.fighter.team);
        if (team[0])
            sprintf_s(nameLine, "%s  -  %s", entry.fighter.name, team);
        else
            sprintf_s(nameLine, "%s", entry.fighter.name);
        slot.nameLabel->setCaption(nameLine);
        slot.placeholder->setCaption(
            ResultsLayout::PortraitInitialUtf8(entry.fighter.name));

        char statsLine[256];
        if (entry.fighter.ratingUpdated)
        {
            sprintf_s(
                statsLine,
                "DEALT %.0f  \xE2\x80\xA2  TAKEN %.0f  \xE2\x80\xA2  MITIGATED %.0f\n"
                "HITS %d  \xE2\x80\xA2  BLOCKS %d  \xE2\x80\xA2  MISSES %d  \xE2\x80\xA2  DODGES %d\n"
                "RATING %.1f (%+.1f)  \xE2\x80\xA2  MARKS %d (+%d)",
                entry.fighter.damageDealt,
                entry.fighter.damageTaken,
                entry.fighter.damageMitigated,
                entry.fighter.hitsLanded,
                entry.fighter.blocks,
                entry.fighter.misses,
                entry.fighter.dodges,
                entry.fighter.ratingAfter,
                entry.fighter.ratingDelta,
                entry.fighter.marksAfter,
                entry.fighter.marksEarned);
        }
        else
        {
            sprintf_s(
                statsLine,
                "DEALT %.0f  \xE2\x80\xA2  TAKEN %.0f  \xE2\x80\xA2  MITIGATED %.0f\n"
                "HITS %d  \xE2\x80\xA2  BLOCKS %d  \xE2\x80\xA2  MISSES %d  \xE2\x80\xA2  DODGES %d",
                entry.fighter.damageDealt,
                entry.fighter.damageTaken,
                entry.fighter.damageMitigated,
                entry.fighter.hitsLanded,
                entry.fighter.blocks,
                entry.fighter.misses,
                entry.fighter.dodges);
        }
        slot.statsLabel->setCaption(statsLine);

        slot.portraitAvailable = BindPortrait(slot.portrait, character);
    }

    void ShowFromSnapshot(const SparPodium::Snapshot& snapshot)
    {
        if (!EnsureWindow()) return;
        if (!g_window || !g_header || !g_context)
        {
            PGLog::Debug("Proving Grounds: results UI missing widgets");
            return;
        }

        g_header->setCaption(snapshot.header[0] ? snapshot.header : "Match ended");
        g_measure->setCaption(g_header->getCaption());

        char context[128];
        sprintf_s(
            context,
            "%s  -  %d fighter%s",
            ModeCaption(snapshot.mode),
            snapshot.fighterCount,
            snapshot.fighterCount == 1 ? "" : "s");
        g_context->setCaption(context);

        for (int i = 0; i < 3; ++i)
            SetSlotVisible(g_slots[i], false);

        for (int i = 0; i < snapshot.podiumCount && i < 3; ++i)
            FillSlot(
                i,
                snapshot.podium[i],
                snapshot.mode,
                SparStats::GetPodiumCharacter(i));

        if (g_empty)
            g_empty->setVisible(snapshot.podiumCount == 0);

        g_scroll->setViewOffset(MyGUI::IntPoint(0, 0));
        FitWindowToContent(true);
        LayoutWindow();
        g_window->setVisible(true);
        PGLog::Debug("Proving Grounds: results podium shown");
    }
}

namespace ResultsUI
{
    void ScheduleShow(float delaySec)
    {
        if (!SparStats::HasSnapshot())
            return;
        g_pending = true;
        g_delaySec = delaySec > 0.0f ? delaySec : 1.0f;
        g_elapsedSec = 0.0f;
        g_lastTick = GetTickCount();
        PGLog::Debug("Proving Grounds: results UI scheduled");
    }

    void Cancel()
    {
        g_pending = false;
        g_elapsedSec = 0.0f;
    }

    void Close()
    {
        Cancel();
        if (g_window)
            g_window->setVisible(false);
    }

    bool IsVisible()
    {
        return g_window && g_window->getVisible();
    }

    void Tick()
    {
        if (IsVisible())
        {
            try
            {
                FitWindowToContent();
                if (g_window->getClientWidget()->getSize() != g_clientSize || BodyHeight() != g_fontHeight)
                    LayoutWindow();
            }
            catch (...)
            {
                PGLog::Error("Proving Grounds: native results resize failed");
                AbandonWindow();
            }
        }
        if (!g_pending)
            return;

        const DWORD now = GetTickCount();
        const float deltaSec = static_cast<float>(now - g_lastTick) / 1000.0f;
        g_lastTick = now;
        g_elapsedSec += deltaSec;

        if (g_elapsedSec < g_delaySec)
            return;

        g_pending = false;
        if (SparStats::HasSnapshot())
        {
            try { ShowFromSnapshot(SparStats::GetSnapshot()); }
            catch (...)
            {
                PGLog::Error("Proving Grounds: native results content failed (ImageBox/Kenshi_WordWrapEmpty)");
                AbandonWindow();
            }
        }
    }
}
