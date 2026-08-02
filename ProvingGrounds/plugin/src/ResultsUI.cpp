#include "ResultsUI.h"
#include "SparStats.h"

#include <Debug.h>

#include <Windows.h>
#include <cstdio>
#include <string>

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
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#include <ogre/OgreResourceGroupManager.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    const MyGUI::Colour kBgDark(26.f / 255.f, 22.f / 255.f, 18.f / 255.f, 1.f);
    const MyGUI::Colour kTextPrimary(232.f / 255.f, 220.f / 255.f, 200.f / 255.f, 1.f);
    const MyGUI::Colour kBrass(196.f / 255.f, 165.f / 255.f, 116.f / 255.f, 1.f);
    const MyGUI::Colour kMuted(154.f / 255.f, 139.f / 255.f, 114.f / 255.f, 1.f);
    const MyGUI::Colour kAmber(222.f / 255.f, 169.f / 255.f, 76.f / 255.f, 1.f);
    const MyGUI::Colour kSilver(181.f / 255.f, 177.f / 255.f, 166.f / 255.f, 1.f);
    const MyGUI::Colour kBronze(169.f / 255.f, 116.f / 255.f, 70.f / 255.f, 1.f);
    const MyGUI::Colour kChromeHover(238.f / 255.f, 185.f / 255.f, 86.f / 255.f, 1.f);

    bool g_pending = false;
    float g_delaySec = 0.8f;
    float g_elapsedSec = 0.0f;
    DWORD g_lastTick = 0;

    MyGUI::Window* g_window = NULL;
    MyGUI::ImageBox* g_resultsBg = NULL;
    MyGUI::ImageBox* g_resultsTop = NULL;
    MyGUI::TextBox* g_header = NULL;
    MyGUI::TextBox* g_context = NULL;
    MyGUI::TextBox* g_empty = NULL;
    MyGUI::TextBox* g_closeBtn = NULL;
    bool g_resultsResourcesReady = false;

    static const char* kResultsTexture = "results_ui.png";
    static const char* kResultsTopTexture = "arena_top.png";
    static const char* kGuiResourceGroup = "GUI";

    struct PodiumSlot
    {
        MyGUI::Button* surface;
        MyGUI::ImageBox* portrait;
        MyGUI::TextBox* rankLabel;
        MyGUI::TextBox* nameLabel;
        MyGUI::TextBox* statsLabel;
    };

    // Visual order: 0 = victor, 1 = second, 2 = third.
    PodiumSlot g_slots[3];

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

    bool EnsureResultsResources()
    {
        if (g_resultsResourcesReady)
            return true;

        const std::string modDirectory = GetPluginModDirectory();
        if (modDirectory.empty())
        {
            ErrorLog("Proving Grounds: could not resolve Results UI image directory");
            return false;
        }

        const std::string imagesDirectory = modDirectory + "\\gui\\images";
        try
        {
            Ogre::ResourceGroupManager& resources =
                Ogre::ResourceGroupManager::getSingleton();
            const bool hasBackground =
                resources.resourceExists(kGuiResourceGroup, kResultsTexture);
            const bool hasTop =
                resources.resourceExists(kGuiResourceGroup, kResultsTopTexture);
            if (!hasBackground || !hasTop)
            {
                resources.addResourceLocation(
                    imagesDirectory,
                    "FileSystem",
                    kGuiResourceGroup,
                    false);
                DebugLog(
                    ("Proving Grounds: registered Results UI images at " +
                        imagesDirectory).c_str());
            }
            g_resultsResourcesReady = true;
            return true;
        }
        catch (...)
        {
            ErrorLog("Proving Grounds: failed to register Results UI image resources");
            return false;
        }
    }

    void ApplyResultsTexture()
    {
        if (!EnsureResultsResources())
            return;
        if (g_resultsBg)
        {
            g_resultsBg->setImageTexture(kResultsTexture);
            g_resultsBg->setNeedMouseFocus(false);
            g_resultsBg->setVisible(true);
        }
        if (g_resultsTop)
        {
            g_resultsTop->setImageTexture(kResultsTopTexture);
            g_resultsTop->setNeedMouseFocus(false);
            g_resultsTop->setVisible(true);
        }
    }

    void InstallResultsTopBar(MyGUI::Window* window)
    {
        if (!window || g_resultsTop)
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

        g_resultsTop = header->createWidget<MyGUI::ImageBox>(
            "ImageBox",
            0,
            0,
            barWidth,
            barHeight > 0 ? barHeight : 38,
            MyGUI::Align::HStretch | MyGUI::Align::Top,
            "PG_ResultsTop");
        ApplyResultsTexture();

        if (caption && caption->getParent() == header)
        {
            const MyGUI::IntCoord coord = caption->getCoord();
            const MyGUI::Align align = caption->getAlign();
            caption->detachFromWidget();
            caption->attachToWidget(header);
            caption->setCoord(coord);
            caption->setAlign(align);
            TintWidget(caption, kBrass);
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

    void OnCloseClicked(MyGUI::Widget* /*sender*/)
    {
        ResultsUI::Close();
    }

    void OnWindowButtonPressed(MyGUI::Widget* /*sender*/, const std::string& name)
    {
        if (name == "close" || name == "Close")
            ResultsUI::Close();
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
        label->setTextShadow(true);
        label->setTextShadowColour(MyGUI::Colour::Black);
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
        button->setTextColour(kTextPrimary);
        button->setTextShadow(true);
        button->setTextShadowColour(MyGUI::Colour::Black);
        button->setNeedMouseFocus(true);
        TintWidget(button, kTextPrimary);
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
        label->setTextColour(kTextPrimary);
        TintWidget(label, kTextPrimary);
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

    void MakeSlot(
        MyGUI::Widget* client,
        PodiumSlot& slot,
        float x,
        float y,
        float width,
        float height,
        bool victor,
        const char* baseName)
    {
        char name[64];
        const float contentX = victor ? 0.30f : 0.36f;

        // Card recesses and borders are baked into the results texture.
        slot.surface = NULL;

        sprintf_s(name, "%s_Portrait", baseName);
        slot.portrait = client->createWidgetReal<MyGUI::ImageBox>(
            "ImageBox",
            x + width * (victor ? 0.055f : 0.06f),
            y + height * (victor ? 0.16f : 0.20f),
            width * (victor ? 0.20f : 0.25f),
            height * (victor ? 0.68f : 0.60f),
            MyGUI::Align::Default,
            name);
        slot.portrait->setVisible(false);

        sprintf_s(name, "%s_Rank", baseName);
        slot.rankLabel = MakeLabel(
            client,
            x + width * contentX,
            y + height * 0.12f,
            width * (0.94f - contentX),
            height * 0.18f,
            name,
            "");
        slot.rankLabel->setFontHeight(victor ? 18 : 17);

        sprintf_s(name, "%s_Name", baseName);
        slot.nameLabel = MakeLabel(
            client,
            x + width * contentX,
            y + height * 0.31f,
            width * (0.94f - contentX),
            height * 0.24f,
            name,
            "");
        slot.nameLabel->setFontHeight(victor ? 22 : 18);

        sprintf_s(name, "%s_Stats", baseName);
        slot.statsLabel = MakeLabel(
            client,
            x + width * contentX,
            y + height * 0.57f,
            width * (0.94f - contentX),
            height * 0.36f,
            name,
            "");
        slot.statsLabel->setFontHeight(victor ? 17 : 16);
        TintWidget(slot.statsLabel, kMuted);
    }

    void SetSlotVisible(PodiumSlot& slot, bool visible)
    {
        if (slot.surface)
            slot.surface->setVisible(visible);
        if (slot.portrait)
            slot.portrait->setVisible(visible);
        if (slot.rankLabel)
            slot.rankLabel->setVisible(visible);
        if (slot.nameLabel)
            slot.nameLabel->setVisible(visible);
        if (slot.statsLabel)
            slot.statsLabel->setVisible(visible);
    }

    void EnsureWindow()
    {
        if (g_window)
            return;

        MyGUI::Gui* guiInstance = MyGUI::Gui::getInstancePtr();
        if (!guiInstance)
            return;

        g_window = guiInstance->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX",
            0.22f,
            0.13f,
            0.56f,
            0.68f,
            MyGUI::Align::Center,
            "Window",
            "ProvingGroundsResultsWindow");
        g_window->setCaption("Proving Grounds - Results");
        g_window->setVisible(false);
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
        TintWidget(g_window, kBgDark);
        InstallResultsTopBar(g_window);

        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client)
            return;

        g_resultsBg = client->createWidgetReal<MyGUI::ImageBox>(
            "ImageBox",
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            MyGUI::Align::Default,
            "PG_ResultsBackground");
        ApplyResultsTexture();

        MyGUI::TextBox* title = MakeLabel(
            client,
            0.08f,
            0.012f,
            0.84f,
            0.045f,
            "PG_ResultsTitle",
            "MATCH COMPLETE");
        title->setFontHeight(16);
        title->setTextAlign(MyGUI::Align::Center);
        TintWidget(title, kBrass);

        g_header = MakeLabel(
            client,
            0.08f,
            0.052f,
            0.84f,
            0.068f,
            "PG_ResultsHeader",
            "");
        g_header->setFontHeight(26);
        g_header->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_header, kAmber);

        g_context = MakeLabel(
            client,
            0.08f,
            0.116f,
            0.84f,
            0.04f,
            "PG_ResultsContext",
            "");
        g_context->setFontHeight(18);
        g_context->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_context, kMuted);

        MakeSlot(
            client, g_slots[0], 0.12f, 0.175f, 0.76f, 0.27f, true, "PG_Results1st");
        MakeSlot(
            client, g_slots[1], 0.12f, 0.455f, 0.365f, 0.23f, false, "PG_Results2nd");
        MakeSlot(
            client, g_slots[2], 0.515f, 0.455f, 0.365f, 0.23f, false, "PG_Results3rd");

        g_empty = MakeLabel(
            client,
            0.18f,
            0.30f,
            0.64f,
            0.25f,
            "PG_ResultsEmpty",
            "No victor\nThe match ended without a podium.");
        g_empty->setFontHeight(18);
        g_empty->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_empty, kMuted);
        g_empty->setVisible(false);

        g_closeBtn = MakeButton(
            client,
            0.345f,
            0.845f,
            0.31f,
            0.060f,
            "PG_ResultsClose",
            "CLOSE RESULTS");
        g_closeBtn->eventMouseButtonClick += MyGUI::newDelegate(OnCloseClicked);
        g_closeBtn->eventMouseSetFocus += MyGUI::newDelegate(OnCloseHover);
        g_closeBtn->eventMouseLostFocus += MyGUI::newDelegate(OnCloseLeave);
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

        const MyGUI::Colour rankColour =
            entry.place == 1 ? kAmber : (entry.place == 2 ? kSilver : kBronze);
        slot.rankLabel->setCaption(PlaceCaption(mode, entry.place));
        slot.rankLabel->setTextColour(rankColour);
        TintWidget(slot.rankLabel, rankColour);

        char nameLine[96];
        const char* team = TeamCaption(entry.fighter.team);
        if (team[0])
            sprintf_s(nameLine, "%s  -  %s", entry.fighter.name, team);
        else
            sprintf_s(nameLine, "%s", entry.fighter.name);
        slot.nameLabel->setCaption(nameLine);

        char statsLine[256];
        if (entry.fighter.ratingUpdated)
        {
            sprintf_s(
                statsLine,
                "DEALT  %.0f     TAKEN  %.0f     MITIGATED  %.0f\n"
                "HITS %d   BLOCKS %d   MISSES %d   DODGES %d\n"
                "RATING  %.1f  (%+.1f)",
                entry.fighter.damageDealt,
                entry.fighter.damageTaken,
                entry.fighter.damageMitigated,
                entry.fighter.hitsLanded,
                entry.fighter.blocks,
                entry.fighter.misses,
                entry.fighter.dodges,
                entry.fighter.ratingAfter,
                entry.fighter.ratingDelta);
        }
        else
        {
            sprintf_s(
                statsLine,
                "DEALT  %.0f     TAKEN  %.0f     MITIGATED  %.0f\n"
                "HITS %d   BLOCKS %d   MISSES %d   DODGES %d",
                entry.fighter.damageDealt,
                entry.fighter.damageTaken,
                entry.fighter.damageMitigated,
                entry.fighter.hitsLanded,
                entry.fighter.blocks,
                entry.fighter.misses,
                entry.fighter.dodges);
        }
        slot.statsLabel->setCaption(statsLine);

        BindPortrait(slot.portrait, character);
    }

    void ShowFromSnapshot(const SparPodium::Snapshot& snapshot)
    {
        EnsureWindow();
        if (!g_window || !g_header || !g_context)
        {
            DebugLog("Proving Grounds: results UI missing widgets");
            return;
        }

        g_header->setCaption(snapshot.header[0] ? snapshot.header : "Match ended");

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

        // Re-apply in case the first window creation preceded GUI resource setup.
        ApplyResultsTexture();
        g_window->setVisible(true);
        DebugLog("Proving Grounds: results podium shown");
    }
}

namespace ResultsUI
{
    void ScheduleShow(float delaySec)
    {
        if (!SparStats::HasSnapshot())
            return;
        g_pending = true;
        g_delaySec = delaySec > 0.0f ? delaySec : 0.8f;
        g_elapsedSec = 0.0f;
        g_lastTick = GetTickCount();
        DebugLog("Proving Grounds: results UI scheduled");
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
            ShowFromSnapshot(SparStats::GetSnapshot());
    }
}
