#include "LeaderboardUI.h"
#include "LeaderboardStore.h"

#include <Debug.h>

#include <Windows.h>
#include <cstdio>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/util/hand.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Colour.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#include <ogre/OgreResourceGroupManager.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    const MyGUI::Colour kBgDark(31.f / 255.f, 23.f / 255.f, 16.f / 255.f, 1.f);
    const MyGUI::Colour kTextPrimary(25.f / 255.f, 16.f / 255.f, 10.f / 255.f, 1.f);
    const MyGUI::Colour kHeading(35.f / 255.f, 20.f / 255.f, 10.f / 255.f, 1.f);
    const MyGUI::Colour kMuted(72.f / 255.f, 48.f / 255.f, 28.f / 255.f, 1.f);
    const MyGUI::Colour kRank(91.f / 255.f, 48.f / 255.f, 19.f / 255.f, 1.f);
    const MyGUI::Colour kGold(126.f / 255.f, 75.f / 255.f, 20.f / 255.f, 1.f);
    const MyGUI::Colour kSilver(69.f / 255.f, 66.f / 255.f, 59.f / 255.f, 1.f);
    const MyGUI::Colour kBronze(113.f / 255.f, 56.f / 255.f, 31.f / 255.f, 1.f);
    const MyGUI::Colour kChromeText(221.f / 255.f, 201.f / 255.f, 166.f / 255.f, 1.f);
    const MyGUI::Colour kChromeHover(238.f / 255.f, 185.f / 255.f, 86.f / 255.f, 1.f);

    static const char* kBgTexture = "leaderboard_ui.png";
    static const char* kTopTexture = "leaderboard_top.png";
    static const char* kGuiResourceGroup = "GUI";
    // Match the horizontal row guides baked into leaderboard_ui.png. Keeping
    // this separate from portrait size leaves breathing room between fighters.
    static const int kRowPixelHeight = 59;
    static const int kPortraitPixelSize = 48;
    static const int kRowTextTop = 7;
    static const int kRowTextHeight = 40;

    MyGUI::Window* g_window = NULL;
    MyGUI::ImageBox* g_bg = NULL;
    MyGUI::ImageBox* g_top = NULL;
    MyGUI::TextBox* g_subtitle = NULL;
    MyGUI::TextBox* g_empty = NULL;
    MyGUI::ScrollView* g_scroll = NULL;
    MyGUI::TextBox* g_closeBtn = NULL;
    int g_scrollOffset = 0;
    bool g_resourcesReady = false;

    struct RowWidgets
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* portrait;
        MyGUI::TextBox* rank;
        MyGUI::TextBox* name;
        MyGUI::TextBox* rating;
        MyGUI::TextBox* record;
        MyGUI::TextBox* matches;
    };

    std::vector<RowWidgets> g_rows;

    void TintWidget(MyGUI::Widget* widget, const MyGUI::Colour& colour)
    {
        if (widget)
            widget->setColour(colour);
    }

    std::string GetPluginModDirectory()
    {
        HMODULE module = NULL;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)&GetPluginModDirectory,
                &module) ||
            !module)
        {
            return std::string();
        }

        char path[MAX_PATH];
        const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return std::string();

        std::string fullPath(path, path + length);
        const size_t slash = fullPath.find_last_of("\\/");
        if (slash == std::string::npos)
            return std::string();
        return fullPath.substr(0, slash);
    }

    bool EnsureResources()
    {
        if (g_resourcesReady)
            return true;

        const std::string modDirectory = GetPluginModDirectory();
        if (modDirectory.empty())
        {
            ErrorLog("Proving Grounds: could not resolve Leaderboard UI image directory");
            return false;
        }

        const std::string imagesDirectory = modDirectory + "\\gui\\images";
        try
        {
            Ogre::ResourceGroupManager& resources =
                Ogre::ResourceGroupManager::getSingleton();
            const bool hasBackground =
                resources.resourceExists(kGuiResourceGroup, kBgTexture);
            const bool hasTop =
                resources.resourceExists(kGuiResourceGroup, kTopTexture);
            if (!hasBackground || !hasTop)
            {
                resources.addResourceLocation(
                    imagesDirectory, "FileSystem", kGuiResourceGroup, false);
                resources.initialiseResourceGroup(kGuiResourceGroup);
            }
            g_resourcesReady = true;
            return true;
        }
        catch (...)
        {
            ErrorLog("Proving Grounds: Leaderboard UI resource setup failed");
            return false;
        }
    }

    void ApplyTextures()
    {
        if (!EnsureResources())
            return;
        if (g_bg)
        {
            g_bg->setImageTexture(kBgTexture);
            g_bg->setNeedMouseFocus(false);
            g_bg->setVisible(true);
        }
        if (g_top)
        {
            g_top->setImageTexture(kTopTexture);
            g_top->setNeedMouseFocus(false);
            g_top->setVisible(true);
        }
    }

    void InstallTopBar(MyGUI::Window* window)
    {
        if (!window || g_top)
            return;

        MyGUI::TextBox* caption = window->getCaptionWidget();
        MyGUI::Widget* header = caption ? caption->getParent() : NULL;
        if (!header)
            header = window;

        MyGUI::Widget* closeButton = NULL;
        MyGUI::Widget* searchRoot = caption ? caption->getParent() : window;
        if (searchRoot)
        {
            for (size_t i = 0; i < searchRoot->getChildCount(); ++i)
            {
                MyGUI::Widget* child = searchRoot->getChildAt(i);
                if (child && child->getUserString("Event") == "close")
                {
                    closeButton = child;
                    break;
                }
            }
        }

        const int barHeight = header == static_cast<MyGUI::Widget*>(window)
            ? 38
            : header->getHeight();
        const int barWidth =
            header->getWidth() > 0 ? header->getWidth() : window->getWidth();

        g_top = header->createWidget<MyGUI::ImageBox>(
            "ImageBox",
            0,
            0,
            barWidth,
            barHeight > 0 ? barHeight : 38,
            MyGUI::Align::HStretch | MyGUI::Align::Top,
            "PG_LeaderboardTop");
        ApplyTextures();

        if (caption && caption->getParent() == header)
        {
            const MyGUI::IntCoord coord = caption->getCoord();
            const MyGUI::Align align = caption->getAlign();
            caption->detachFromWidget();
            caption->attachToWidget(header);
            caption->setCoord(coord);
            caption->setAlign(align);
            caption->setFontHeight(18);
            caption->setTextShadow(true);
            caption->setTextShadowColour(MyGUI::Colour::Black);
            TintWidget(caption, kChromeText);
        }
        if (closeButton && closeButton->getParent() == header)
        {
            const MyGUI::IntCoord coord = closeButton->getCoord();
            const MyGUI::Align align = closeButton->getAlign();
            closeButton->detachFromWidget();
            closeButton->attachToWidget(header);
            closeButton->setCoord(coord);
            closeButton->setAlign(align);
        }
    }

    MyGUI::TextBox* MakeLabel(
        MyGUI::Widget* parent,
        float x,
        float y,
        float width,
        float height,
        const char* name,
        const char* caption)
    {
        MyGUI::TextBox* label = parent->createWidgetReal<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText",
            x,
            y,
            width,
            height,
            MyGUI::Align::Default,
            name);
        label->setCaption(caption);
        label->setFontHeight(16);
        label->setTextColour(kTextPrimary);
        label->setTextShadow(false);
        TintWidget(label, kTextPrimary);
        return label;
    }

    MyGUI::TextBox* MakeButton(
        MyGUI::Widget* parent,
        float x,
        float y,
        float width,
        float height,
        const char* name,
        const char* caption)
    {
        MyGUI::TextBox* button = parent->createWidgetReal<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText",
            x,
            y,
            width,
            height,
            MyGUI::Align::Default,
            name);
        button->setCaption(caption);
        button->setFontHeight(20);
        button->setTextAlign(MyGUI::Align::Center);
        button->setTextColour(kChromeText);
        button->setTextShadow(true);
        button->setTextShadowColour(MyGUI::Colour::Black);
        button->setNeedMouseFocus(true);
        TintWidget(button, kChromeText);
        return button;
    }

    void OnCloseHover(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        MyGUI::TextBox* label = static_cast<MyGUI::TextBox*>(sender);
        label->setTextColour(kChromeHover);
        TintWidget(label, kChromeHover);
    }

    void OnCloseLeave(MyGUI::Widget* sender, MyGUI::Widget* /*newFocus*/)
    {
        MyGUI::TextBox* label = static_cast<MyGUI::TextBox*>(sender);
        label->setTextColour(kChromeText);
        TintWidget(label, kChromeText);
    }

    void OnCloseClicked(MyGUI::Widget* /*sender*/)
    {
        LeaderboardUI::Close();
    }

    void OnWindowButtonPressed(MyGUI::Widget* /*sender*/, const std::string& name)
    {
        if (name == "close" || name == "Close")
            LeaderboardUI::Close();
    }

    void ClearRows()
    {
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].root)
                MyGUI::Gui::getInstance().destroyWidget(g_rows[i].root);
        }
        g_rows.clear();
    }

    void ScrollRowsBy(int pixels)
    {
        if (!g_scroll)
            return;

        const MyGUI::IntCoord view = g_scroll->getViewCoord();
        const int contentHeight =
            static_cast<int>(g_rows.size()) * kRowPixelHeight + 8;
        const int maxOffset =
            contentHeight > view.height ? contentHeight - view.height : 0;

        g_scrollOffset -= pixels;
        if (g_scrollOffset < 0)
            g_scrollOffset = 0;
        else if (g_scrollOffset > maxOffset)
            g_scrollOffset = maxOffset;

        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].root)
                g_rows[i].root->setPosition(
                    4,
                    4 + static_cast<int>(i) * kRowPixelHeight -
                        g_scrollOffset);
        }
    }

    void OnLeaderboardWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        // Match ArenaUI's manual scrolling: Kenshi's bundled MyGUI does not
        // reliably move dynamically-created ScrollView children itself.
        ScrollRowsBy(rel < 0 ? -kRowPixelHeight : kRowPixelHeight);
    }

    void BindPortrait(MyGUI::ImageBox* image, Character* character)
    {
        if (!image)
            return;
        if (!character || !character->isValid())
        {
            image->setVisible(false);
            return;
        }

        PortraitManager* portraitManager = PortraitManager::getInstance();
        if (portraitManager)
            portraitManager->setImageWidget(character->getHandle(), image, true);
        image->setVisible(true);
    }

    void EnsureWindow()
    {
        if (g_window)
            return;

        MyGUI::Gui* guiInstance = MyGUI::Gui::getInstancePtr();
        if (!guiInstance)
            return;

        EnsureResources();

        g_window = guiInstance->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX",
            0.22f,
            0.12f,
            0.56f,
            0.68f,
            MyGUI::Align::Center,
            "Window",
            "ProvingGroundsLeaderboardWindow");
        g_window->setCaption("Proving Grounds - Leaderboard");
        g_window->setVisible(false);
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
        TintWidget(g_window, kBgDark);
        InstallTopBar(g_window);

        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client)
            return;

        g_bg = client->createWidgetReal<MyGUI::ImageBox>(
            "ImageBox",
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            MyGUI::Align::Default,
            "PG_LeaderboardBackground");
        g_bg->setNeedMouseFocus(false);
        ApplyTextures();

        MyGUI::TextBox* title = MakeLabel(
            client,
            0.16f,
            0.065f,
            0.68f,
            0.042f,
            "PG_LeaderboardTitle",
            "ARENA STANDINGS");
        title->setFontHeight(22);
        title->setTextAlign(MyGUI::Align::Center);
        TintWidget(title, kHeading);

        g_subtitle = MakeLabel(
            client,
            0.16f,
            0.108f,
            0.68f,
            0.034f,
            "PG_LeaderboardSubtitle",
            "ARENA RATING");
        g_subtitle->setFontHeight(16);
        g_subtitle->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_subtitle, kMuted);

        MyGUI::TextBox* rankHeader = MakeLabel(
            client,
            0.14f,
            0.158f,
            0.09f,
            0.026f,
            "PG_LeaderboardRankHeader",
            "RANK");
        rankHeader->setFontHeight(16);
        rankHeader->setTextAlign(MyGUI::Align::Center);
        TintWidget(rankHeader, kMuted);

        MyGUI::TextBox* fighterHeader = MakeLabel(
            client,
            0.285f,
            0.158f,
            0.285f,
            0.026f,
            "PG_LeaderboardFighterHeader",
            "FIGHTER");
        fighterHeader->setFontHeight(16);
        TintWidget(fighterHeader, kMuted);

        MyGUI::TextBox* ratingHeader = MakeLabel(
            client,
            0.585f,
            0.158f,
            0.09f,
            0.026f,
            "PG_LeaderboardRatingHeader",
            "RATING");
        ratingHeader->setFontHeight(16);
        ratingHeader->setTextAlign(MyGUI::Align::Center);
        TintWidget(ratingHeader, kMuted);

        MyGUI::TextBox* recordHeader = MakeLabel(
            client,
            0.68f,
            0.158f,
            0.09f,
            0.026f,
            "PG_LeaderboardRecordHeader",
            "RECORD");
        recordHeader->setFontHeight(16);
        recordHeader->setTextAlign(MyGUI::Align::Center);
        TintWidget(recordHeader, kMuted);

        MyGUI::TextBox* matchesHeader = MakeLabel(
            client,
            0.774f,
            0.158f,
            0.072f,
            0.026f,
            "PG_LeaderboardMatchesHeader",
            "MATCHES");
        matchesHeader->setFontHeight(16);
        matchesHeader->setTextAlign(MyGUI::Align::Center);
        TintWidget(matchesHeader, kMuted);

        g_scroll = client->createWidgetReal<MyGUI::ScrollView>(
            "ScrollView",
            0.14f,
            0.195f,
            0.72f,
            0.61f,
            MyGUI::Align::Default,
            "PG_LeaderboardScroll");
        g_scroll->setVisibleHScroll(false);
        g_scroll->setVisibleVScroll(true);
        g_scroll->setCanvasAlign(MyGUI::Align::Default);
        g_scroll->setCanvasSize(400, kRowPixelHeight);
        g_scroll->setNeedMouseFocus(true);
        g_scroll->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

        g_empty = MakeLabel(
            client,
            0.18f,
            0.40f,
            0.76f,
            0.16f,
            "PG_LeaderboardEmpty",
            "No rated fighters yet.\nWin arena matches to climb the board.");
        g_empty->setFontHeight(17);
        g_empty->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_empty, kMuted);
        g_empty->setVisible(false);

        g_closeBtn = MakeButton(
            client,
            0.355f,
            0.848f,
            0.29f,
            0.07f,
            "PG_LeaderboardClose",
            "CLOSE");
        g_closeBtn->eventMouseButtonClick += MyGUI::newDelegate(OnCloseClicked);
        g_closeBtn->eventMouseSetFocus += MyGUI::newDelegate(OnCloseHover);
        g_closeBtn->eventMouseLostFocus += MyGUI::newDelegate(OnCloseLeave);
    }

    void RebuildRows(const std::vector<LeaderboardStore::Record>& standings)
    {
        ClearRows();
        if (!g_scroll)
            return;

        const int count = static_cast<int>(standings.size());
        const MyGUI::IntCoord view = g_scroll->getViewCoord();
        const int canvasW = view.width > 120 ? view.width : 640;
        const int canvasH = (count > 0 ? count : 1) * kRowPixelHeight + 8;
        const int maxOffset = canvasH > view.height ? canvasH - view.height : 0;
        if (g_scrollOffset < 0)
            g_scrollOffset = 0;
        else if (g_scrollOffset > maxOffset)
            g_scrollOffset = maxOffset;

        g_scroll->setViewOffset(MyGUI::IntPoint(0, 0));
        g_scroll->setCanvasSize(canvasW, view.height);

        for (int i = 0; i < count; ++i)
        {
            const LeaderboardStore::Record& rec = standings[i];
            char widgetName[64];

            sprintf_s(widgetName, "PG_LbRow_%d", i);
            MyGUI::Widget* root = g_scroll->createWidget<MyGUI::Widget>(
                "Widget",
                4,
                4 + i * kRowPixelHeight - g_scrollOffset,
                canvasW - 12,
                kRowPixelHeight - 4,
                MyGUI::Align::Default,
                widgetName);
            root->setNeedMouseFocus(true);
            root->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

            RowWidgets row;
            row.root = root;

            sprintf_s(widgetName, "PG_LbPortrait_%d", i);
            row.portrait = root->createWidget<MyGUI::ImageBox>(
                "ImageBox",
                canvasW * 18 / 100,
                3,
                kPortraitPixelSize,
                kPortraitPixelSize,
                MyGUI::Align::Default,
                widgetName);
            row.portrait->setVisible(false);
            row.portrait->setNeedMouseFocus(true);
            row.portrait->eventMouseWheel +=
                MyGUI::newDelegate(OnLeaderboardWheel);

            sprintf_s(widgetName, "PG_LbRank_%d", i);
            row.rank = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                4,
                kRowTextTop,
                canvasW * 12 / 100,
                kRowTextHeight,
                MyGUI::Align::Default,
                widgetName);
            char rankText[16];
            sprintf_s(rankText, "%d", i + 1);
            row.rank->setCaption(rankText);
            row.rank->setFontHeight(20);
            row.rank->setTextAlign(MyGUI::Align::Center);
            row.rank->setNeedMouseFocus(true);
            row.rank->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);
            const MyGUI::Colour rankColour =
                i == 0 ? kGold : (i == 1 ? kSilver : (i == 2 ? kBronze : kRank));
            row.rank->setTextColour(rankColour);
            TintWidget(row.rank, rankColour);

            sprintf_s(widgetName, "PG_LbName_%d", i);
            row.name = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                canvasW * 26 / 100,
                kRowTextTop,
                canvasW * 30 / 100,
                kRowTextHeight,
                MyGUI::Align::Default,
                widgetName);
            row.name->setCaption(rec.name.c_str());
            row.name->setFontHeight(19);
            row.name->setTextAlign(MyGUI::Align::VCenter | MyGUI::Align::Left);
            row.name->setTextColour(kTextPrimary);
            TintWidget(row.name, kTextPrimary);
            row.name->setNeedMouseFocus(true);
            row.name->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

            sprintf_s(widgetName, "PG_LbRating_%d", i);
            row.rating = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                canvasW * 62 / 100,
                kRowTextTop,
                canvasW * 12 / 100,
                kRowTextHeight,
                MyGUI::Align::Default,
                widgetName);
            char ratingText[24];
            sprintf_s(ratingText, "%d", static_cast<int>(rec.mmr + 0.5f));
            row.rating->setCaption(ratingText);
            row.rating->setFontHeight(18);
            row.rating->setTextAlign(MyGUI::Align::Center);
            row.rating->setTextColour(kTextPrimary);
            TintWidget(row.rating, kTextPrimary);
            row.rating->setNeedMouseFocus(true);
            row.rating->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

            sprintf_s(widgetName, "PG_LbRecord_%d", i);
            row.record = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                canvasW * 75 / 100,
                kRowTextTop,
                canvasW * 12 / 100,
                kRowTextHeight,
                MyGUI::Align::Default,
                widgetName);
            char recordText[32];
            sprintf_s(recordText, "%d - %d", rec.wins, rec.losses);
            row.record->setCaption(recordText);
            row.record->setFontHeight(18);
            row.record->setTextAlign(MyGUI::Align::Center);
            row.record->setTextColour(kMuted);
            TintWidget(row.record, kMuted);
            row.record->setNeedMouseFocus(true);
            row.record->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

            sprintf_s(widgetName, "PG_LbMatches_%d", i);
            row.matches = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                canvasW * 88 / 100,
                kRowTextTop,
                canvasW * 10 / 100,
                kRowTextHeight,
                MyGUI::Align::Default,
                widgetName);
            char matchesText[24];
            sprintf_s(matchesText, "%d", rec.matches);
            row.matches->setCaption(matchesText);
            row.matches->setFontHeight(18);
            row.matches->setTextAlign(MyGUI::Align::Center);
            row.matches->setTextColour(kMuted);
            TintWidget(row.matches, kMuted);
            row.matches->setNeedMouseFocus(true);
            row.matches->eventMouseWheel += MyGUI::newDelegate(OnLeaderboardWheel);

            BindPortrait(row.portrait, LeaderboardStore::FindRatedCharacter(rec.id));
            g_rows.push_back(row);
        }

    }

    void RefreshContent()
    {
        EnsureWindow();
        if (!g_window)
            return;

        std::vector<LeaderboardStore::Record> standings;
        LeaderboardStore::GetStandings(standings);
        g_scrollOffset = 0;

        if (g_subtitle)
        {
            if (LeaderboardStore::HasActiveSave())
            {
                const std::string& key = LeaderboardStore::GetActiveSaveKey();
                g_subtitle->setCaption(("ARENA RATING  -  " + key).c_str());
            }
            else
            {
                g_subtitle->setCaption("ARENA RATING  -  NO ACTIVE SAVE");
            }
        }

        RebuildRows(standings);
        if (g_empty)
            g_empty->setVisible(standings.empty());
        if (g_scroll)
            g_scroll->setVisible(!standings.empty());

        ApplyTextures();
    }
}

namespace LeaderboardUI
{
    void Show()
    {
        RefreshContent();
        if (!g_window)
        {
            ErrorLog("Proving Grounds: Leaderboard UI could not create window");
            return;
        }
        g_window->setVisible(true);
        DebugLog("Proving Grounds: leaderboard UI shown");
    }

    void Close()
    {
        if (g_window)
            g_window->setVisible(false);
    }

    bool IsVisible()
    {
        return g_window && g_window->getVisible();
    }

    void Tick()
    {
        // Reserved for future refresh / input; panel is event-driven.
    }
}
