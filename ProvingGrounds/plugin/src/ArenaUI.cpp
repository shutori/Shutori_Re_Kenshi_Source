#include "ArenaUI.h"
#include "ArenaIngress.h"
#include "ContextMenuHooks.h"
#include "DebugMenu.h"
#include "KOWatcher.h"
#include "LeaderboardUI.h"
#include "LeaderboardStore.h"
#include "MatchRules.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "SparSession.h"
#include "SquadUtil.h"

#include <Debug.h>
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Globals.h>
#include <kenshi/InputHandler.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/gui/TitleScreen.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Colour.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <ogre/OgreResourceGroupManager.h>
#include <ogre/OgreVector3.h>

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    enum UiMode { UiAvB, UiTeams1v1, UiLastStanding };
    enum AssignDest { DestTeamA, DestTeamB };
    enum ListKind { ListRoster, ListTeamA, ListTeamB, ListPool };
    enum RosterFilter { FilterAll, FilterReady, FilterAssigned, FilterUnavailable };

    struct PortraitRow
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* image;
        MyGUI::Button* handlerFrame;
        MyGUI::ImageBox* handlerImage;
        MyGUI::TextBox* label;
        MyGUI::TextBox* handlerLabel;
        Character* character;
        ListKind kind;
    };

    struct HandlerAssignment
    {
        Character* prisoner;
        Character* handler;
    };

    struct SetupPreset
    {
        bool valid;
        UiMode mode;
        AssignDest destination;
        ArenaIngress::LocationMode location;
        bool koElimination;
        std::vector<Character*> teamA;
        std::vector<Character*> teamB;
        std::vector<Character*> pool;
    };

    struct BalanceCandidate
    {
        Character* fighter;
        float score;
    };

    bool BetterBalanceCandidate(const BalanceCandidate& a, const BalanceCandidate& b)
    {
        if (a.score != b.score)
            return a.score > b.score;
        if (!a.fighter || !b.fighter)
            return a.fighter != NULL;
        return a.fighter->getName() < b.fighter->getName();
    }

    TitleScreen* (*TitleScreen_orig)(TitleScreen*) = NULL;
    void (*ForgottenGUI_update_orig)(ForgottenGUI*) = NULL;

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_titleEyebrow = NULL;
    MyGUI::TextBox* g_titleHero = NULL;

    MyGUI::ScrollView* g_rosterScroll = NULL;
    MyGUI::ScrollView* g_teamAScroll = NULL;
    MyGUI::ScrollView* g_teamBScroll = NULL;
    MyGUI::ScrollView* g_poolScroll = NULL;
    MyGUI::Button* g_rosterScrollUp = NULL;
    MyGUI::Button* g_rosterScrollDown = NULL;
    MyGUI::Button* g_teamAScrollUp = NULL;
    MyGUI::Button* g_teamAScrollDown = NULL;
    MyGUI::Button* g_teamBScrollUp = NULL;
    MyGUI::Button* g_teamBScrollDown = NULL;
    MyGUI::Button* g_poolScrollUp = NULL;
    MyGUI::Button* g_poolScrollDown = NULL;
    MyGUI::EditBox* g_rosterSearch = NULL;
    MyGUI::Button* g_rosterSearchSurface = NULL;
    MyGUI::TextBox* g_rosterCount = NULL;
    MyGUI::Button* g_filterAll = NULL;
    MyGUI::Button* g_filterReady = NULL;
    MyGUI::Button* g_filterAssigned = NULL;
    MyGUI::Button* g_filterUnavailable = NULL;

    MyGUI::Button* g_modeAvB = NULL;
    MyGUI::Button* g_modeTeams1v1 = NULL;
    MyGUI::Button* g_modeLast = NULL;
    MyGUI::Button* g_locArena = NULL;
    MyGUI::Button* g_locBanner = NULL;
    MyGUI::Button* g_koEliminationButton = NULL;
    MyGUI::TextBox* g_lblTeamA = NULL;
    MyGUI::TextBox* g_lblTeamB = NULL;
    MyGUI::TextBox* g_lblPool = NULL;
    MyGUI::TextBox* g_lblDest = NULL;
    MyGUI::Button* g_btnDestA = NULL;
    MyGUI::Button* g_btnDestB = NULL;
    MyGUI::Button* g_btnRemove = NULL;
    MyGUI::Button* g_btnAutoBalance = NULL;
    MyGUI::Button* g_btnBalanceSkill = NULL;
    MyGUI::Button* g_btnBalanceRating = NULL;
    MyGUI::Button* g_btnClearTeams = NULL;
    MyGUI::Button* g_btnSwapTeams = NULL;
    MyGUI::Button* g_presetButtons[3] = { NULL, NULL, NULL };
    MyGUI::Button* g_teamASurface = NULL;
    MyGUI::Button* g_teamBSurface = NULL;
    MyGUI::Button* g_poolSurface = NULL;

    MyGUI::ImageBox* g_previewImage = NULL;
    MyGUI::TextBox* g_previewName = NULL;
    MyGUI::TextBox* g_previewMeta = NULL;
    MyGUI::TextBox* g_previewStats = NULL;
    MyGUI::TextBox* g_summary = NULL;
    MyGUI::Button* g_startButton = NULL;
    MyGUI::Button* g_stopButton = NULL;
    MyGUI::TextBox* g_status = NULL;
    MyGUI::Button* g_rosterDrawerButton = NULL;
    MyGUI::Button* g_settingsDrawerButton = NULL;
    std::vector<MyGUI::Widget*> g_rosterColumnWidgets;
    std::vector<MyGUI::Widget*> g_settingsColumnWidgets;

    std::vector<Character*> g_squadCache;
    std::vector<Character*> g_rosterCache;
    std::vector<Character*> g_filteredRoster;
    std::vector<Character*> g_teamA;
    std::vector<Character*> g_teamB;
    std::vector<Character*> g_pool;
    std::vector<PortraitRow> g_rosterRows;
    std::vector<PortraitRow> g_teamARows;
    std::vector<PortraitRow> g_teamBRows;
    std::vector<PortraitRow> g_poolRows;
    std::vector<HandlerAssignment> g_handlerAssignments;
    SetupPreset g_setupPresets[3] = {};
    int g_listScrollOffset[4] = { 0, 0, 0, 0 };

    UiMode g_uiMode = UiAvB;
    bool g_koEliminationEnabled = false;
    bool g_balanceMenuOpen = false;

    bool IsTeamUiMode()
    {
        return g_uiMode == UiAvB || g_uiMode == UiTeams1v1;
    }
    AssignDest g_dest = DestTeamA;
    RosterFilter g_rosterFilter = FilterAll;
    Character* g_previewChar = NULL;
    Character* g_dragCharacter = NULL;
    bool g_rowDragActive = false;
    std::string g_rosterQuery;
    bool g_searchPlaceholderActive = true;
    bool g_f8WasDown = false;
    bool g_escWasDown = false;
    bool g_compactMode = false;
    bool g_rosterDrawerOpen = false;
    bool g_settingsDrawerOpen = false;

    // F8 debug routing. Keep only one enabled; debug menu wins if both are true.
    static const bool kF8OpensDebugMenu = true;
    static const bool kF8OpensArenaUi = false;

    // Kenshi MyGUI loads textures from resource group "GUI".
    // Paths under mods/<mod>/gui/... are auto-added to GUI (same as Character Inspector).
    static const char* kArenaBgTexture = "arena_ui.png";
    static const char* kArenaTopTexture = "arena_top.png";
    static const char* kArenaUiResourceGroup = "GUI";
    bool g_arenaUiResourcesReady = false;
    MyGUI::ImageBox* g_arenaBg = NULL;
    MyGUI::ImageBox* g_arenaTop = NULL;

    const int kRowH = 56;
    const int kPortraitSize = 48;
    const int kHandlerPortraitSize = 22;
    const int kHandlerBadgeSize = 26;
    const float kMaxFighterDistanceFromRegistry = 2500.0f;

    const MyGUI::Colour kBgDark(26.f / 255.f, 22.f / 255.f, 18.f / 255.f, 1.f);
    const MyGUI::Colour kTextPrimary(232.f / 255.f, 220.f / 255.f, 200.f / 255.f, 1.f);
    const MyGUI::Colour kBrass(196.f / 255.f, 165.f / 255.f, 116.f / 255.f, 1.f);
    const MyGUI::Colour kCta(107.f / 255.f, 58.f / 255.f, 30.f / 255.f, 1.f);
    const MyGUI::Colour kTeamA(122.f / 255.f, 59.f / 255.f, 46.f / 255.f, 1.f);
    const MyGUI::Colour kTeamB(59.f / 255.f, 90.f / 255.f, 122.f / 255.f, 1.f);
    const MyGUI::Colour kMuted(154.f / 255.f, 139.f / 255.f, 114.f / 255.f, 1.f);
    const MyGUI::Colour kAmber(222.f / 255.f, 169.f / 255.f, 76.f / 255.f, 1.f);
    const MyGUI::Colour kDanger(184.f / 255.f, 72.f / 255.f, 58.f / 255.f, 1.f);
    const MyGUI::Colour kReady(112.f / 255.f, 156.f / 255.f, 101.f / 255.f, 1.f);
    const MyGUI::Colour kDisabled(104.f / 255.f, 98.f / 255.f, 88.f / 255.f, 1.f);

    std::string GetPluginModDirectory()
    {
        HMODULE module = NULL;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)&GetPluginModDirectory,
                &module) ||
            !module)
        {
            return std::string();
        }

        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(module, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return std::string();

        std::string full(path, path + len);
        size_t slash = full.find_last_of("\\/");
        if (slash == std::string::npos)
            return std::string();
        return full.substr(0, slash);
    }

    bool EnsureArenaUiResources()
    {
        if (g_arenaUiResourcesReady)
            return true;

        std::string modDir = GetPluginModDirectory();
        if (modDir.empty())
        {
            ErrorLog("Proving Grounds: could not resolve mod directory for UI images");
            return false;
        }

        // Match Character Inspector: assets live under gui/ so Kenshi maps them into GUI.
        std::string imagesDir = modDir + "\\gui\\images";
        try
        {
            Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
            const bool hasBg = rgm.resourceExists(kArenaUiResourceGroup, kArenaBgTexture);
            const bool hasTop = rgm.resourceExists(kArenaUiResourceGroup, kArenaTopTexture);
            if (!hasBg || !hasTop)
            {
                rgm.addResourceLocation(imagesDir, "FileSystem", kArenaUiResourceGroup, false);
                DebugLog(("Proving Grounds: registered GUI images at " + imagesDir).c_str());
            }
            else
            {
                DebugLog("Proving Grounds: Arena UI textures already in GUI resource group");
            }
            g_arenaUiResourcesReady = true;
            return true;
        }
        catch (...)
        {
            ErrorLog("Proving Grounds: failed to register Arena UI image resources");
            return false;
        }
    }

    void TintWidget(MyGUI::Widget* w, const MyGUI::Colour& c);

    void ApplyGuiTexture(MyGUI::ImageBox* image, const char* textureName)
    {
        if (!image || !textureName)
            return;
        if (!EnsureArenaUiResources())
        {
            ErrorLog("Proving Grounds: UI texture skipped (resources unavailable)");
            return;
        }
        image->setImageTexture(textureName);
        image->setVisible(true);
        image->setNeedMouseFocus(false);
    }

    void ApplyArenaChrome()
    {
        ApplyGuiTexture(g_arenaBg, kArenaBgTexture);
        ApplyGuiTexture(g_arenaTop, kArenaTopTexture);
    }

    void InstallArenaTopBar(MyGUI::Window* window)
    {
        if (!window || g_arenaTop)
            return;

        // Kenshi_WindowCX header: use Window API — findWidget("Caption") is unreliable.
        MyGUI::TextBox* caption = window->getCaptionWidget();
        MyGUI::Widget* header = caption ? caption->getParent() : NULL;
        if (!header)
        {
            // Fallback: paint top bar on the window itself (above client).
            header = window;
            DebugLog("Proving Grounds: no caption parent — top bar on window root");
        }

        MyGUI::Widget* closeBtn = NULL;
        MyGUI::Widget* searchRoot = caption ? caption->getParent() : window;
        if (searchRoot)
        {
            for (size_t i = 0; i < searchRoot->getChildCount(); ++i)
            {
                MyGUI::Widget* child = searchRoot->getChildAt(i);
                if (child && child->getUserString("Event") == "close")
                {
                    closeBtn = child;
                    break;
                }
            }
        }

        const int barH = (header == static_cast<MyGUI::Widget*>(window))
            ? 38
            : header->getHeight();
        const int barW = header->getWidth() > 0 ? header->getWidth() : window->getWidth();

        g_arenaTop = header->createWidget<MyGUI::ImageBox>(
            "ImageBox",
            0,
            0,
            barW,
            barH > 0 ? barH : 38,
            MyGUI::Align::HStretch | MyGUI::Align::Top,
            "PG_ArenaTop");
        ApplyGuiTexture(g_arenaTop, kArenaTopTexture);

        // Re-attach caption + close so they draw above the ImageBox.
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
        if (closeBtn && closeBtn->getParent() == header)
        {
            const MyGUI::IntCoord coord = closeBtn->getCoord();
            const MyGUI::Align align = closeBtn->getAlign();
            closeBtn->detachFromWidget();
            closeBtn->attachToWidget(header);
            closeBtn->setCoord(coord);
            closeBtn->setAlign(align);
        }

        DebugLog("Proving Grounds: Arena top bar installed");
    }

    void TintWidget(MyGUI::Widget* w, const MyGUI::Colour& c)
    {
        if (w)
            w->setColour(c);
    }

    bool IsTooFarFromRegistry(Character* c)
    {
        if (!c || !c->isValid())
            return false;

        Building* registry = ArenaIngress::GetBoundRegistry();
        if (!registry || !registry->isValid())
            return false;

        const Ogre::Vector3 fighterPos = c->getPosition();
        const Ogre::Vector3 registryPos = registry->getPosition();
        const float dx = fighterPos.x - registryPos.x;
        const float dz = fighterPos.z - registryPos.z;
        const float maxDistSq =
            kMaxFighterDistanceFromRegistry * kMaxFighterDistanceFromRegistry;
        return (dx * dx) + (dz * dz) > maxDistSq;
    }

    void AppendUnavailableReason(std::string& reasons, const char* reason)
    {
        if (!reasons.empty())
            reasons += ", ";
        reasons += reason;
    }

    bool VectorContains(const std::vector<Character*>& list, Character* c)
    {
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] == c)
                return true;
        }
        return false;
    }

    bool IsInMatchRoster(Character* c)
    {
        return VectorContains(g_teamA, c) ||
            VectorContains(g_teamB, c) ||
            VectorContains(g_pool, c);
    }

    std::string FighterUnavailableReason(Character* c)
    {
        if (!c || !c->isValid())
            return "Invalid";

        std::string reasons;
        if (c->isDead())
            AppendUnavailableReason(reasons, "Dead");
        else if (c->isUnconcious())
            AppendUnavailableReason(reasons, "Unconscious");
        if (IsTooFarFromRegistry(c))
            AppendUnavailableReason(reasons, "Too far away");
        if (PrisonerUtil::IsRosterPrisoner(c) && !PrisonerUtil::FindCageForOccupant(c))
        {
            const bool releasedForMatch =
                PrisonerUtil::MatchIncludesPrisoner() && IsInMatchRoster(c);
            if (!releasedForMatch)
                AppendUnavailableReason(reasons, "No cage");
        }
        return reasons;
    }

    bool IsAssignable(Character* c)
    {
        return FighterUnavailableReason(c).empty();
    }

    void RemoveFromAll(Character* c)
    {
        if (!c)
            return;

        for (size_t i = 0; i < g_teamA.size(); )
        {
            if (g_teamA[i] == c)
                g_teamA.erase(g_teamA.begin() + i);
            else
                ++i;
        }
        for (size_t i = 0; i < g_teamB.size(); )
        {
            if (g_teamB[i] == c)
                g_teamB.erase(g_teamB.begin() + i);
            else
                ++i;
        }
        for (size_t i = 0; i < g_pool.size(); )
        {
            if (g_pool[i] == c)
                g_pool.erase(g_pool.begin() + i);
            else
                ++i;
        }

        for (size_t i = 0; i < g_handlerAssignments.size(); )
        {
            if (g_handlerAssignments[i].prisoner == c ||
                g_handlerAssignments[i].handler == c)
            {
                g_handlerAssignments.erase(g_handlerAssignments.begin() + i);
            }
            else
                ++i;
        }
    }

    bool IsAssigned(Character* c)
    {
        return VectorContains(g_teamA, c) || VectorContains(g_teamB, c) || VectorContains(g_pool, c);
    }

    std::string RosterLabel(Character* c)
    {
        if (!c)
            return "";
        std::string name = c->getName();
        const std::string reason = FighterUnavailableReason(c);
        if (!reason.empty())
            return "[" + reason + "] " + name;
        if (IsAssigned(c))
            return name + " *";
        return name;
    }

    const char* FighterAssignment(Character* c)
    {
        if (VectorContains(g_teamA, c))
            return "Team A";
        if (VectorContains(g_teamB, c))
            return "Team B";
        if (VectorContains(g_pool, c))
            return "Fighter pool";
        return IsAssignable(c) ? "Unassigned" : "Unavailable";
    }

    std::string FighterDetailLabel(Character* c)
    {
        const std::string reason = FighterUnavailableReason(c);
        const char* assignment = FighterAssignment(c);
        if (reason.empty())
        {
            if (PrisonerUtil::IsRosterPrisoner(c))
            {
                std::string detail = assignment;
                detail += " · Prisoner";
                return detail;
            }
            return assignment;
        }

        std::string detail;
        if (IsAssigned(c))
        {
            detail = assignment;
            if (PrisonerUtil::IsRosterPrisoner(c))
                detail += " · Prisoner";
            detail += " - ";
        }
        else
        {
            detail = "Unavailable - ";
        }
        detail += reason;
        return detail;
    }

    void RefreshRosterCaches()
    {
        SquadUtil::CollectPlayerSquad(g_squadCache);
        g_rosterCache = g_squadCache;
        Building* registry = ArenaIngress::GetBoundRegistry();
        if (registry)
        {
            std::vector<Character*> prisoners;
            PrisonerUtil::CollectNearbyCagePrisoners(registry, prisoners);
            for (size_t i = 0; i < prisoners.size(); ++i)
            {
                Character* p = prisoners[i];
                if (!p || VectorContains(g_rosterCache, p))
                    continue;
                g_rosterCache.push_back(p);
            }
        }

        if (ArenaIngress::IsPending() || PrisonerUtil::MatchIncludesPrisoner())
        {
            const std::vector<Character*>& matchPrisoners =
                PrisonerUtil::GetMatchPrisoners();
            for (size_t i = 0; i < matchPrisoners.size(); ++i)
            {
                Character* p = matchPrisoners[i];
                if (!p || VectorContains(g_rosterCache, p))
                    continue;
                g_rosterCache.push_back(p);
            }
        }
    }

    void CollectMatchFighters(std::vector<Character*>& out)
    {
        out.clear();
        if (IsTeamUiMode())
        {
            out.insert(out.end(), g_teamA.begin(), g_teamA.end());
            out.insert(out.end(), g_teamB.begin(), g_teamB.end());
        }
        else
        {
            out.insert(out.end(), g_pool.begin(), g_pool.end());
        }
    }

    int CountPrisonersInMatch()
    {
        std::vector<Character*> match;
        CollectMatchFighters(match);
        int n = 0;
        for (size_t i = 0; i < match.size(); ++i)
        {
            if (PrisonerUtil::IsRosterPrisoner(match[i]))
                ++n;
        }
        return n;
    }

    int CountAvailableHandlers()
    {
        std::vector<Character*> match;
        CollectMatchFighters(match);
        int n = 0;
        for (size_t i = 0; i < g_squadCache.size(); ++i)
        {
            Character* c = g_squadCache[i];
            if (!IsAssignable(c))
                continue;
            if (VectorContains(match, c))
                continue;
            ++n;
        }
        return n;
    }

    std::string LowerText(const std::string& value)
    {
        std::string result(value);
        for (size_t i = 0; i < result.size(); ++i)
            result[i] = static_cast<char>(tolower(static_cast<unsigned char>(result[i])));
        return result;
    }

    void RefreshRowStates();

    void SetPreview(Character* c)
    {
        g_previewChar = c;
        if (g_previewName)
        {
            if (c && c->isValid())
                g_previewName->setCaption(c->getName());
            else
                g_previewName->setCaption("No fighter selected");
        }

        if (g_previewMeta)
        {
            if (c && c->isValid())
                g_previewMeta->setCaption(FighterDetailLabel(c));
            else
                g_previewMeta->setCaption("Select a roster or team row");
        }

        if (g_previewStats)
        {
            if (c && c->isValid() && c->getStats())
            {
                CharStats* stats = c->getStats();
                char statBuf[192];
                sprintf_s(
                    statBuf,
                    "Combat %.0f   Toughness %.0f\nStrength %.0f   Dexterity %.0f",
                    stats->getOverallSkillLevel_0_100(),
                    stats->toughness(),
                    stats->strengthActual(),
                    stats->dexterityActual());
                g_previewStats->setCaption(statBuf);
            }
            else
            {
                g_previewStats->setCaption(
                    "Choose a fighter to review readiness,\nassignment, and combat stats.");
            }
        }

        if (!g_previewImage)
            return;

        if (c && c->isValid())
        {
            PortraitManager* pm = PortraitManager::getInstance();
            if (pm)
                pm->setImageWidget(c->getHandle(), g_previewImage, true);
            g_previewImage->setVisible(true);
        }
        else
        {
            g_previewImage->setVisible(false);
        }

        RefreshRowStates();
    }

    void ClearScrollChildren(MyGUI::ScrollView* scroll)
    {
        if (!scroll)
            return;
        while (scroll->getChildCount() > 0)
            MyGUI::Gui::getInstance().destroyWidget(scroll->getChildAt(0));
    }

    void BindPortrait(MyGUI::ImageBox* image, Character* c)
    {
        if (!image || !c || !c->isValid())
            return;
        PortraitManager* pm = PortraitManager::getInstance();
        if (pm)
            pm->setImageWidget(c->getHandle(), image, true);
    }

    void OnPortraitRowClick(MyGUI::Widget* sender);
    void OnPortraitRowDoubleClick(MyGUI::Widget* sender);
    void OnPortraitRowHover(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
    void OnPortraitRowLeave(MyGUI::Widget* sender, MyGUI::Widget* newFocus);
    void OnPortraitRowWheel(MyGUI::Widget* sender, int rel);
    void OnPortraitRowDrag(
        MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton button);
    void OnPortraitRowRelease(
        MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton button);

    Character* FindHandlerAssignment(Character* prisoner);
    void EnsureHandlerAssignments();
    void BuildOrderedHandlers(
        const std::vector<Character*>& prisonerFighters,
        std::vector<Character*>& outHandlers);
    PortraitRow* FindPortraitRowAtPoint(const MyGUI::IntPoint& point);
    PortraitRow* FindAssignedPrisonerRowAtPoint(const MyGUI::IntPoint& point);
    Character* FindAssignedPrisonerAtPoint(const MyGUI::IntPoint& point);
    bool IsHandlerCandidate(Character* c);
    void SetHandlerAssignment(Character* prisoner, Character* handler);

    PortraitRow MakeRow(MyGUI::ScrollView* scroll, Character* c, ListKind kind, int index, int width)
    {
        PortraitRow row;
        row.root = NULL;
        row.image = NULL;
        row.handlerFrame = NULL;
        row.handlerImage = NULL;
        row.label = NULL;
        row.handlerLabel = NULL;
        row.character = c;
        row.kind = kind;

        if (!scroll || !c)
            return row;

        char nameBuf[64];
        sprintf_s(nameBuf, "PG_Row_%d_%d", static_cast<int>(kind), index);

        row.root = scroll->createWidget<MyGUI::Button>(
            "Kenshi_Button1",
            0,
            index * kRowH - g_listScrollOffset[static_cast<int>(kind)],
            width,
            kRowH - 4,
            MyGUI::Align::Default,
            nameBuf);
        if (!row.root)
            return row;

        row.root->setNeedMouseFocus(true);
        row.root->eventMouseButtonClick += MyGUI::newDelegate(OnPortraitRowClick);
        row.root->eventMouseButtonDoubleClick += MyGUI::newDelegate(OnPortraitRowDoubleClick);
        row.root->eventMouseSetFocus += MyGUI::newDelegate(OnPortraitRowHover);
        row.root->eventMouseLostFocus += MyGUI::newDelegate(OnPortraitRowLeave);
        row.root->eventMouseWheel += MyGUI::newDelegate(OnPortraitRowWheel);
        row.root->eventMouseDrag += MyGUI::newDelegate(OnPortraitRowDrag);
        row.root->eventMouseButtonReleased += MyGUI::newDelegate(OnPortraitRowRelease);

        sprintf_s(nameBuf, "PG_Img_%d_%d", static_cast<int>(kind), index);
        row.image = row.root->createWidget<MyGUI::ImageBox>(
            "ImageBox", 5, 3, kPortraitSize, kPortraitSize, MyGUI::Align::Default, nameBuf);
        if (row.image)
        {
            // Keep focus on the row while crossing its portrait and text.
            // Child focus transitions were causing the hover tint to flicker.
            row.image->setNeedMouseFocus(false);
            BindPortrait(row.image, c);
        }

        Character* assignedHandler = NULL;
        if (kind != ListRoster &&
            PrisonerUtil::IsRosterPrisoner(c) &&
            !c->isUnconcious())
        {
            assignedHandler = FindHandlerAssignment(c);
            if (assignedHandler)
            {
                // Keep the handler visually separate on the right edge so the
                // prisoner's portrait remains the clear primary identity.
                sprintf_s(nameBuf, "PG_HFrame_%d_%d", static_cast<int>(kind), index);
                row.handlerFrame = row.root->createWidget<MyGUI::Button>(
                    "Kenshi_Button1",
                    width - kHandlerBadgeSize - 6,
                    (kRowH - 4 - kHandlerBadgeSize) / 2,
                    kHandlerBadgeSize,
                    kHandlerBadgeSize,
                    MyGUI::Align::Default,
                    nameBuf);
                if (row.handlerFrame)
                {
                    row.handlerFrame->setNeedMouseFocus(false);
                    TintWidget(row.handlerFrame, kAmber);
                }

                sprintf_s(nameBuf, "PG_HImg_%d_%d", static_cast<int>(kind), index);
                MyGUI::Widget* handlerParent = row.handlerFrame ?
                    static_cast<MyGUI::Widget*>(row.handlerFrame) : row.root;
                row.handlerImage = handlerParent->createWidget<MyGUI::ImageBox>(
                    "ImageBox",
                    row.handlerFrame ? 2 : (width - kHandlerPortraitSize - 8),
                    row.handlerFrame ? 2 : ((kRowH - 4 - kHandlerPortraitSize) / 2),
                    kHandlerPortraitSize,
                    kHandlerPortraitSize,
                    MyGUI::Align::Default,
                    nameBuf);
                if (row.handlerImage)
                {
                    row.handlerImage->setNeedMouseFocus(false);
                    BindPortrait(row.handlerImage, assignedHandler);
                }
            }
        }

        const int labelX = 12 + kPortraitSize;
        const int labelRightPadding = assignedHandler ? kHandlerBadgeSize + 12 : 6;
        sprintf_s(nameBuf, "PG_Lbl_%d_%d", static_cast<int>(kind), index);
        row.label = row.root->createWidget<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText",
            labelX,
            5,
            width - (labelX + labelRightPadding),
            assignedHandler ? 22 : 40,
            MyGUI::Align::Default,
            nameBuf);
        if (row.label)
        {
            row.label->setNeedMouseFocus(false);
            row.label->setTextAlign(MyGUI::Align::VCenter | MyGUI::Align::Left);
            TintWidget(row.label, IsAssignable(c) ? kTextPrimary : kMuted);
            if (kind == ListRoster)
            {
                std::string caption = c->getName();
                if (IsAssigned(c) || !IsAssignable(c))
                {
                    caption += "\n";
                    caption += FighterDetailLabel(c);
                }
                row.label->setCaption(caption);
            }
            else
            {
                std::string caption = c->getName();
                const std::string reason = FighterUnavailableReason(c);
                if (!reason.empty())
                {
                    caption += "\nUnavailable - ";
                    caption += reason;
                }
                row.label->setCaption(caption);
            }
        }

        if (assignedHandler)
        {
            sprintf_s(nameBuf, "PG_HLbl_%d_%d", static_cast<int>(kind), index);
            row.handlerLabel = row.root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                labelX,
                26,
                width - (labelX + labelRightPadding),
                20,
                MyGUI::Align::Default,
                nameBuf);
            if (row.handlerLabel)
            {
                std::string caption("Handler: ");
                caption += assignedHandler->getName();
                row.handlerLabel->setCaption(caption);
                row.handlerLabel->setFontHeight(12);
                row.handlerLabel->setTextAlign(MyGUI::Align::VCenter | MyGUI::Align::Left);
                row.handlerLabel->setNeedMouseFocus(false);
                TintWidget(row.handlerLabel, kAmber);
            }

        }
        // Encode kind+index for click lookup (VS2010-safe).
        char userBuf[32];
        sprintf_s(userBuf, "%d:%d", static_cast<int>(kind), index);
        row.root->setUserString("pg", userBuf);
        if (row.image)
            row.image->setUserString("pg", userBuf);
        if (row.label)
            row.label->setUserString("pg", userBuf);

        TintWidget(row.root, c == g_previewChar ? kAmber :
            (IsAssignable(c) ? kMuted : kDisabled));

        return row;
    }

    void RebuildList(
        MyGUI::ScrollView* scroll,
        std::vector<PortraitRow>& rows,
        const std::vector<Character*>& chars,
        ListKind kind)
    {
        ClearScrollChildren(scroll);
        rows.clear();
        if (!scroll)
            return;

        MyGUI::IntCoord view = scroll->getViewCoord();
        int width = view.width;
        if (width < 80)
            width = 180;

        const int canvasH = static_cast<int>(chars.size()) * kRowH;
        const int maxOffset = canvasH > view.height ? canvasH - view.height : 0;
        int& logicalOffset = g_listScrollOffset[static_cast<int>(kind)];
        if (logicalOffset < 0)
            logicalOffset = 0;
        else if (logicalOffset > maxOffset)
            logicalOffset = maxOffset;

        // Kenshi's bundled MyGUI does not reliably apply ScrollView view offsets
        // to dynamically-created children. Keep a fixed clipped canvas and move
        // the row widgets ourselves using logicalOffset.
        scroll->setViewOffset(MyGUI::IntPoint(0, 0));
        scroll->setCanvasSize(width, view.height);
        scroll->setVisibleHScroll(false);
        scroll->setVisibleVScroll(true);

        if (chars.empty())
        {
            const char* emptyText = "No fighters match this filter.";
            MyGUI::Colour emptyColour = kMuted;
            if (kind == ListTeamA)
            {
                emptyText = "Drop fighters here";
                emptyColour = kTeamA;
            }
            else if (kind == ListTeamB)
            {
                emptyText = "Drop fighters here";
                emptyColour = kTeamB;
            }
            else if (kind == ListPool)
            {
                emptyText = "Drop fighters here";
                emptyColour = kReady;
            }

            MyGUI::TextBox* empty = scroll->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText",
                12,
                view.height / 2 - 34,
                width - 24,
                68,
                MyGUI::Align::Default,
                "PG_EmptyList");
            empty->setCaption(emptyText);
            empty->setFontHeight(16);
            empty->setTextAlign(MyGUI::Align::Center);
            empty->setNeedMouseFocus(false);
            TintWidget(empty, emptyColour);
        }

        for (size_t i = 0; i < chars.size(); ++i)
        {
            if (!chars[i])
                continue;
            rows.push_back(MakeRow(
                scroll, chars[i], kind, static_cast<int>(rows.size()), width));
        }
    }

    Character* FindRowCharacter(ListKind kind, int index)
    {
        const std::vector<PortraitRow>* rows = NULL;
        if (kind == ListRoster)
            rows = &g_rosterRows;
        else if (kind == ListTeamA)
            rows = &g_teamARows;
        else if (kind == ListTeamB)
            rows = &g_teamBRows;
        else
            rows = &g_poolRows;

        if (!rows || index < 0 || index >= static_cast<int>(rows->size()))
            return NULL;
        return (*rows)[static_cast<size_t>(index)].character;
    }

    bool DecodeRow(MyGUI::Widget* sender, ListKind& kind, int& index)
    {
        if (!sender)
            return false;
        const std::string& tag = sender->getUserString("pg");
        int kindInt = 0;
        index = 0;
        if (tag.empty() || sscanf_s(tag.c_str(), "%d:%d", &kindInt, &index) != 2)
            return false;
        kind = static_cast<ListKind>(kindInt);
        return true;
    }

    PortraitRow* FindPortraitRow(ListKind kind, int index)
    {
        std::vector<PortraitRow>* rows = NULL;
        if (kind == ListRoster)
            rows = &g_rosterRows;
        else if (kind == ListTeamA)
            rows = &g_teamARows;
        else if (kind == ListTeamB)
            rows = &g_teamBRows;
        else
            rows = &g_poolRows;

        if (!rows || index < 0 || index >= static_cast<int>(rows->size()))
            return NULL;
        return &(*rows)[static_cast<size_t>(index)];
    }

    bool PointInside(MyGUI::Widget* widget, const MyGUI::IntPoint& point)
    {
        if (!widget || !widget->getVisible())
            return false;
        const MyGUI::IntCoord coord = widget->getAbsoluteCoord();
        return point.left >= coord.left && point.left < coord.right() &&
            point.top >= coord.top && point.top < coord.bottom();
    }

    Character* FindHandlerAssignment(Character* prisoner)
    {
        if (!prisoner)
            return NULL;
        for (size_t i = 0; i < g_handlerAssignments.size(); ++i)
        {
            if (g_handlerAssignments[i].prisoner == prisoner)
                return g_handlerAssignments[i].handler;
        }
        return NULL;
    }

    bool HandlerClaimed(Character* handler)
    {
        if (!handler)
            return false;
        for (size_t i = 0; i < g_handlerAssignments.size(); ++i)
        {
            if (g_handlerAssignments[i].handler == handler)
                return true;
        }
        return false;
    }

    bool IsHandlerCandidate(Character* c)
    {
        if (!IsAssignable(c))
            return false;
        if (PrisonerUtil::IsRosterPrisoner(c))
            return false;
        if (IsAssigned(c))
            return false;
        return true;
    }

    void SetHandlerAssignment(Character* prisoner, Character* handler)
    {
        if (!prisoner || !handler)
            return;

        for (size_t i = 0; i < g_handlerAssignments.size(); )
        {
            if (g_handlerAssignments[i].handler == handler ||
                g_handlerAssignments[i].prisoner == prisoner)
            {
                g_handlerAssignments.erase(g_handlerAssignments.begin() + i);
            }
            else
                ++i;
        }

        HandlerAssignment assignment;
        assignment.prisoner = prisoner;
        assignment.handler = handler;
        g_handlerAssignments.push_back(assignment);
    }

    void PruneHandlerAssignments()
    {
        for (size_t i = 0; i < g_handlerAssignments.size(); )
        {
            Character* prisoner = g_handlerAssignments[i].prisoner;
            Character* handler = g_handlerAssignments[i].handler;
            const bool keep =
                prisoner &&
                PrisonerUtil::IsRosterPrisoner(prisoner) &&
                IsAssigned(prisoner) &&
                handler &&
                handler->isValid() &&
                !handler->isDead() &&
                !IsAssigned(handler) &&
                !PrisonerUtil::IsRosterPrisoner(handler);
            if (!keep)
                g_handlerAssignments.erase(g_handlerAssignments.begin() + i);
            else
                ++i;
        }
    }

    void EnsureHandlerAssignments()
    {
        PruneHandlerAssignments();

        std::vector<Character*> match;
        CollectMatchFighters(match);

        for (size_t i = 0; i < match.size(); ++i)
        {
            Character* prisoner = match[i];
            if (!PrisonerUtil::IsRosterPrisoner(prisoner))
                continue;
            if (FindHandlerAssignment(prisoner))
                continue;

            for (size_t s = 0; s < g_squadCache.size(); ++s)
            {
                Character* candidate = g_squadCache[s];
                if (!IsHandlerCandidate(candidate))
                    continue;
                if (HandlerClaimed(candidate))
                    continue;
                SetHandlerAssignment(prisoner, candidate);
                break;
            }
        }
    }

    void BuildOrderedHandlers(
        const std::vector<Character*>& prisonerFighters,
        std::vector<Character*>& outHandlers)
    {
        EnsureHandlerAssignments();
        outHandlers.clear();

        for (size_t i = 0; i < prisonerFighters.size(); ++i)
        {
            Character* assigned = FindHandlerAssignment(prisonerFighters[i]);
            if (!assigned)
            {
                for (size_t s = 0; s < g_squadCache.size(); ++s)
                {
                    Character* candidate = g_squadCache[s];
                    if (!IsHandlerCandidate(candidate))
                        continue;
                    bool used = false;
                    for (size_t h = 0; h < outHandlers.size(); ++h)
                    {
                        if (outHandlers[h] == candidate)
                        {
                            used = true;
                            break;
                        }
                    }
                    if (used || HandlerClaimed(candidate))
                        continue;
                    SetHandlerAssignment(prisonerFighters[i], candidate);
                    assigned = candidate;
                    break;
                }
            }
            outHandlers.push_back(assigned);
        }
    }

    PortraitRow* FindPortraitRowAtPoint(const MyGUI::IntPoint& point)
    {
        MyGUI::ScrollView* scrolls[4] = {
            g_rosterScroll, g_teamAScroll, g_teamBScroll, g_poolScroll
        };
        std::vector<PortraitRow>* lists[4] = {
            &g_rosterRows, &g_teamARows, &g_teamBRows, &g_poolRows
        };

        for (int li = 0; li < 4; ++li)
        {
            if (!PointInside(scrolls[li], point))
                continue;
            std::vector<PortraitRow>& rows = *lists[li];
            for (size_t i = 0; i < rows.size(); ++i)
            {
                PortraitRow& row = rows[i];
                if (row.root && row.character && PointInside(row.root, point))
                    return &row;
            }
        }

        return NULL;
    }

    PortraitRow* FindAssignedPrisonerRowAtPoint(const MyGUI::IntPoint& point)
    {
        PortraitRow* row = FindPortraitRowAtPoint(point);
        if (!row || row->kind == ListRoster)
            return NULL;
        if (!PrisonerUtil::IsRosterPrisoner(row->character))
            return NULL;
        return row;
    }

    Character* FindAssignedPrisonerAtPoint(const MyGUI::IntPoint& point)
    {
        PortraitRow* row = FindAssignedPrisonerRowAtPoint(point);
        return row ? row->character : NULL;
    }

    void ApplyRowState(PortraitRow& row)
    {
        if (!row.character || !row.root)
            return;

        const bool selected = row.character == g_previewChar;
        const bool assignable = IsAssignable(row.character);
        MyGUI::Colour rowColour = kMuted;
        if (VectorContains(g_teamA, row.character))
            rowColour = kTeamA;
        else if (VectorContains(g_teamB, row.character))
            rowColour = kTeamB;
        else if (VectorContains(g_pool, row.character))
            rowColour = kReady;
        else if (!assignable)
            rowColour = kDisabled;
        TintWidget(row.root, selected ? kAmber : rowColour);
        if (row.label)
            TintWidget(row.label, selected ? kAmber : (assignable ? kTextPrimary : kDisabled));
    }

    void RefreshRowStates()
    {
        size_t i = 0;
        for (i = 0; i < g_rosterRows.size(); ++i)
            ApplyRowState(g_rosterRows[i]);
        for (i = 0; i < g_teamARows.size(); ++i)
            ApplyRowState(g_teamARows[i]);
        for (i = 0; i < g_teamBRows.size(); ++i)
            ApplyRowState(g_teamBRows[i]);
        for (i = 0; i < g_poolRows.size(); ++i)
            ApplyRowState(g_poolRows[i]);
    }

    void RefreshAssignmentLists();
    void RefreshRosterList();
    void RefreshSummary();
    void RefreshStatusAndButtons();
    void RefreshCompactDrawers();
    void UpdateDestHighlight();
    void OnWindowButtonPressed(MyGUI::Widget* sender, const std::string& name);

    void AssignCharacter(Character* c)
    {
        if (!IsAssignable(c) || SparSession::IsActive() || ArenaIngress::IsPending())
            return;

        if (IsTeamUiMode())
        {
            RemoveFromAll(c);
            if (g_dest == DestTeamA)
                g_teamA.push_back(c);
            else
                g_teamB.push_back(c);
        }
        else
        {
            if (VectorContains(g_pool, c))
            {
                SetPreview(c);
                return;
            }
            RemoveFromAll(c);
            g_pool.push_back(c);
        }

        SetPreview(c);
        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void RemoveCharacter(Character* c)
    {
        if (!c || SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        RemoveFromAll(c);
        SetPreview(c);
        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnPortraitRowClick(MyGUI::Widget* sender)
    {
        if (!sender)
            return;

        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;
        Character* c = FindRowCharacter(kind, index);
        if (!c)
            return;

        SetPreview(c);
        RefreshStatusAndButtons();
        // Single click selects; double click or explicit actions assign.
    }

    void OnPortraitRowDoubleClick(MyGUI::Widget* sender)
    {
        if (!sender || SparSession::IsActive() || ArenaIngress::IsPending())
            return;

        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index) || kind != ListRoster)
            return;
        Character* c = FindRowCharacter(kind, index);
        if (IsAssignable(c))
            AssignCharacter(c);
    }

    void OnPortraitRowHover(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;
        PortraitRow* row = FindPortraitRow(kind, index);
        if (row && row->root)
            TintWidget(row->root, kAmber);
    }

    void OnPortraitRowLeave(MyGUI::Widget* sender, MyGUI::Widget* /*newFocus*/)
    {
        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;
        PortraitRow* row = FindPortraitRow(kind, index);
        if (row)
            ApplyRowState(*row);
    }

    MyGUI::ScrollView* ScrollForListKind(ListKind kind)
    {
        if (kind == ListRoster)
            return g_rosterScroll;
        if (kind == ListTeamA)
            return g_teamAScroll;
        if (kind == ListTeamB)
            return g_teamBScroll;
        return g_poolScroll;
    }

    std::vector<PortraitRow>* RowsForListKind(ListKind kind)
    {
        if (kind == ListRoster)
            return &g_rosterRows;
        if (kind == ListTeamA)
            return &g_teamARows;
        if (kind == ListTeamB)
            return &g_teamBRows;
        return &g_poolRows;
    }

    ListKind ListKindForScroll(MyGUI::ScrollView* scroll)
    {
        if (scroll == g_rosterScroll)
            return ListRoster;
        if (scroll == g_teamAScroll)
            return ListTeamA;
        if (scroll == g_teamBScroll)
            return ListTeamB;
        return ListPool;
    }

    void ScrollListBy(MyGUI::ScrollView* scroll, int pixels)
    {
        if (!scroll)
            return;
        const ListKind kind = ListKindForScroll(scroll);
        std::vector<PortraitRow>* rows = RowsForListKind(kind);
        if (!rows)
            return;

        const MyGUI::IntCoord view = scroll->getViewCoord();
        const int contentHeight = static_cast<int>(rows->size()) * kRowH;
        const int maxOffset =
            contentHeight > view.height ? contentHeight - view.height : 0;
        int& offset = g_listScrollOffset[static_cast<int>(kind)];
        offset -= pixels;
        if (offset < 0)
            offset = 0;
        else if (offset > maxOffset)
            offset = maxOffset;

        for (size_t i = 0; i < rows->size(); ++i)
        {
            PortraitRow& row = (*rows)[i];
            if (row.root)
                row.root->setPosition(
                    0, static_cast<int>(i) * kRowH - offset);
        }
    }

    void UpdateScrollButtonsForKind(ListKind kind)
    {
        MyGUI::ScrollView* scroll = ScrollForListKind(kind);
        MyGUI::Button* up = NULL;
        MyGUI::Button* down = NULL;
        if (kind == ListRoster)
        {
            up = g_rosterScrollUp;
            down = g_rosterScrollDown;
        }
        else if (kind == ListTeamA)
        {
            up = g_teamAScrollUp;
            down = g_teamAScrollDown;
        }
        else if (kind == ListTeamB)
        {
            up = g_teamBScrollUp;
            down = g_teamBScrollDown;
        }
        else
        {
            up = g_poolScrollUp;
            down = g_poolScrollDown;
        }
        if (!scroll || !up || !down)
            return;

        std::vector<PortraitRow>* rows = RowsForListKind(kind);
        const MyGUI::IntCoord view = scroll->getViewCoord();
        const int contentHeight =
            rows ? static_cast<int>(rows->size()) * kRowH : 0;
        const int range = contentHeight > view.height ?
            contentHeight - view.height : 0;
        const int offset = g_listScrollOffset[static_cast<int>(kind)];
        const bool canUp = range > 0 && offset > 0;
        const bool canDown = range > 0 && offset < range;
        up->setEnabled(canUp);
        down->setEnabled(canDown);
        up->setTextColour(canUp ? kTextPrimary : kDisabled);
        down->setTextColour(canDown ? kTextPrimary : kDisabled);
        up->upLayerItem();
        down->upLayerItem();
    }

    void OnScrollButton(MyGUI::Widget* sender)
    {
        if (!sender)
            return;
        const std::string& tag = sender->getUserString("scroll");
        int kindInt = 0;
        int direction = 0;
        if (tag.empty() ||
            sscanf_s(tag.c_str(), "%d:%d", &kindInt, &direction) != 2)
            return;
        ScrollListBy(
            ScrollForListKind(static_cast<ListKind>(kindInt)),
            direction < 0 ? -kRowH : kRowH);
        UpdateScrollButtonsForKind(static_cast<ListKind>(kindInt));
    }

    void OnPortraitRowWheel(MyGUI::Widget* sender, int rel)
    {
        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;

        MyGUI::ScrollView* scroll = ScrollForListKind(kind);
        if (!scroll)
            return;

        // Interactive row children consume MyGUI's wheel event instead of
        // bubbling it to ScrollView's real client, so forward the same 50 px step.
        ScrollListBy(scroll, (rel < 0) ? -50 : 50);
        UpdateScrollButtonsForKind(kind);
    }

    void ResetDropZoneVisuals()
    {
        TintWidget(g_teamASurface, kTeamA);
        TintWidget(g_teamBSurface, kTeamB);
        TintWidget(g_poolSurface, kReady);
    }

    void ApplyDragHighlights(Character* c, const MyGUI::IntPoint& point)
    {
        const bool valid = IsAssignable(c) &&
            !SparSession::IsActive() && !ArenaIngress::IsPending() &&
            !PrisonerUtil::IsReturning();

        ResetDropZoneVisuals();
        RefreshRowStates();
        PortraitRow* prisonerTarget = FindAssignedPrisonerRowAtPoint(point);
        if (prisonerTarget)
        {
            const bool validHandler = valid && IsHandlerCandidate(c);
            TintWidget(prisonerTarget->root, validHandler ? kAmber : kDanger);
            if (prisonerTarget->handlerLabel)
                TintWidget(
                    prisonerTarget->handlerLabel, validHandler ? kAmber : kDanger);
            return;
        }

        if (IsTeamUiMode() && PointInside(g_teamAScroll, point))
            TintWidget(g_teamASurface, valid ? kAmber : kDanger);
        else if (IsTeamUiMode() && PointInside(g_teamBScroll, point))
            TintWidget(g_teamBSurface, valid ? kAmber : kDanger);
        else if (!IsTeamUiMode() && PointInside(g_poolScroll, point))
            TintWidget(g_poolSurface, valid ? kAmber : kDanger);
    }

    void RefreshPointerHighlights()
    {
        const MyGUI::IntPoint point =
            MyGUI::InputManager::getInstance().getMousePositionByLayer();
        const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (g_rowDragActive && !leftDown)
        {
            g_rowDragActive = false;
            g_dragCharacter = NULL;
            ResetDropZoneVisuals();
        }

        if (g_rowDragActive && g_dragCharacter)
        {
            ApplyDragHighlights(g_dragCharacter, point);
            return;
        }

        RefreshRowStates();
        PortraitRow* hovered = FindPortraitRowAtPoint(point);
        if (hovered && hovered->root)
            TintWidget(hovered->root, kAmber);
    }

    void OnPortraitRowDrag(
        MyGUI::Widget* sender, int /*left*/, int /*top*/, MyGUI::MouseButton button)
    {
        if (!sender || button != MyGUI::MouseButton::Left)
            return;

        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;
        Character* c = FindRowCharacter(kind, index);
        if (!c)
            return;
        g_dragCharacter = c;
        g_rowDragActive = true;
        const MyGUI::IntPoint point =
            MyGUI::InputManager::getInstance().getMousePositionByLayer();
        ApplyDragHighlights(c, point);
    }

    void OnPortraitRowRelease(
        MyGUI::Widget* sender, int /*left*/, int /*top*/, MyGUI::MouseButton button)
    {
        ResetDropZoneVisuals();
        g_rowDragActive = false;
        g_dragCharacter = NULL;
        RefreshRowStates();
        if (!sender || SparSession::IsActive() || ArenaIngress::IsPending() ||
            PrisonerUtil::IsReturning())
            return;

        ListKind kind = ListRoster;
        int index = 0;
        if (!DecodeRow(sender, kind, index))
            return;
        Character* c = FindRowCharacter(kind, index);

        if (button == MyGUI::MouseButton::Right)
        {
            if (IsAssigned(c))
                RemoveCharacter(c);
            return;
        }

        if (button != MyGUI::MouseButton::Left)
            return;
        if (!IsAssignable(c))
            return;

        const MyGUI::IntPoint point =
            MyGUI::InputManager::getInstance().getMousePositionByLayer();

        // Drag available squad member onto an assigned prisoner to set handler.
        Character* dropPrisoner = FindAssignedPrisonerAtPoint(point);
        if (dropPrisoner && IsHandlerCandidate(c))
        {
            SetHandlerAssignment(dropPrisoner, c);
            SetPreview(dropPrisoner);
            RefreshAssignmentLists();
            RefreshStatusAndButtons();
            return;
        }

        if (IsTeamUiMode() && PointInside(g_teamAScroll, point))
        {
            g_dest = DestTeamA;
            AssignCharacter(c);
        }
        else if (IsTeamUiMode() && PointInside(g_teamBScroll, point))
        {
            g_dest = DestTeamB;
            AssignCharacter(c);
        }
        else if (!IsTeamUiMode() && PointInside(g_poolScroll, point))
        {
            AssignCharacter(c);
        }
        else if (kind != ListRoster && PointInside(g_rosterScroll, point))
        {
            RemoveCharacter(c);
        }
    }

    void UpdateDestHighlight();
    void UpdateLocationHighlight();
    void UpdateKoEliminationButton();
    void RefreshStatusAndButtons();

    void UpdateBalanceMenuVisibility()
    {
        if (g_btnAutoBalance)
            g_btnAutoBalance->setCaption(g_balanceMenuOpen ? "Back" : "Balance");
        if (g_btnBalanceSkill)
            g_btnBalanceSkill->setVisible(g_balanceMenuOpen);
        if (g_btnBalanceRating)
            g_btnBalanceRating->setVisible(g_balanceMenuOpen);
        if (g_btnSwapTeams)
            g_btnSwapTeams->setVisible(!g_balanceMenuOpen && IsTeamUiMode());
        if (g_btnClearTeams)
            g_btnClearTeams->setVisible(!g_balanceMenuOpen);
    }

    void UpdateModeVisibility()
    {
        const bool teamMode = IsTeamUiMode();
        if (g_teamAScroll)
            g_teamAScroll->setVisible(teamMode);
        if (g_teamBScroll)
            g_teamBScroll->setVisible(teamMode);
        if (g_teamASurface)
            g_teamASurface->setVisible(teamMode);
        if (g_teamBSurface)
            g_teamBSurface->setVisible(teamMode);
        if (g_teamAScrollUp)
            g_teamAScrollUp->setVisible(teamMode);
        if (g_teamAScrollDown)
            g_teamAScrollDown->setVisible(teamMode);
        if (g_teamBScrollUp)
            g_teamBScrollUp->setVisible(teamMode);
        if (g_teamBScrollDown)
            g_teamBScrollDown->setVisible(teamMode);
        if (g_lblTeamA)
            g_lblTeamA->setVisible(teamMode);
        if (g_lblTeamB)
            g_lblTeamB->setVisible(teamMode);
        if (g_btnDestA)
        {
            g_btnDestA->setVisible(true);
            g_btnDestA->setCaption(teamMode ? "Assign to Team A" : "Add Selected Fighter");
            if (!teamMode)
            {
                TintWidget(g_btnDestA, kReady);
                g_btnDestA->setTextColour(kReady);
            }
        }
        if (g_btnDestB)
            g_btnDestB->setVisible(teamMode);
        UpdateBalanceMenuVisibility();
        if (g_lblDest)
            g_lblDest->setVisible(false);

        const bool poolMode = !teamMode;
        if (g_poolScroll)
            g_poolScroll->setVisible(poolMode);
        if (g_poolSurface)
            g_poolSurface->setVisible(poolMode);
        if (g_poolScrollUp)
            g_poolScrollUp->setVisible(poolMode);
        if (g_poolScrollDown)
            g_poolScrollDown->setVisible(poolMode);
        if (g_lblPool)
            g_lblPool->setVisible(poolMode);

        if (g_btnRemove)
            g_btnRemove->setVisible(true);

        if (g_modeAvB)
        {
            TintWidget(g_modeAvB, (g_uiMode == UiAvB) ? kBrass : kMuted);
            g_modeAvB->setTextColour((g_uiMode == UiAvB) ? kAmber : kTextPrimary);
        }
        if (g_modeTeams1v1)
        {
            TintWidget(g_modeTeams1v1, (g_uiMode == UiTeams1v1) ? kBrass : kMuted);
            g_modeTeams1v1->setTextColour(
                (g_uiMode == UiTeams1v1) ? kAmber : kTextPrimary);
        }
        if (g_modeLast)
        {
            TintWidget(g_modeLast, (g_uiMode == UiLastStanding) ? kBrass : kMuted);
            g_modeLast->setTextColour(
                (g_uiMode == UiLastStanding) ? kAmber : kTextPrimary);
        }

        UpdateLocationHighlight();
        UpdateKoEliminationButton();
        UpdateDestHighlight();
    }

    void UpdateLocationHighlight()
    {
        const bool arena = (ArenaIngress::GetLocationMode() == ArenaIngress::LocationArena);
        if (g_locArena)
        {
            TintWidget(g_locArena, arena ? kBrass : kMuted);
            g_locArena->setTextColour(arena ? kAmber : kTextPrimary);
        }
        if (g_locBanner)
        {
            TintWidget(g_locBanner, arena ? kMuted : kBrass);
            g_locBanner->setTextColour(arena ? kTextPrimary : kAmber);
        }
    }

    void OnLocationArena(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending() || SparSession::IsActive())
            return;
        ArenaIngress::SetLocationMode(ArenaIngress::LocationArena);
        UpdateLocationHighlight();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnLocationBanner(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending() || SparSession::IsActive())
            return;
        ArenaIngress::SetLocationMode(ArenaIngress::LocationBanner);
        UpdateLocationHighlight();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void UpdateKoEliminationButton()
    {
        if (!g_koEliminationButton)
            return;

        if (g_uiMode == UiTeams1v1)
        {
            g_koEliminationButton->setCaption("KO Elimination: ALWAYS ON");
            TintWidget(g_koEliminationButton, kBrass);
            g_koEliminationButton->setTextColour(kAmber);
            return;
        }

        g_koEliminationButton->setCaption(
            g_koEliminationEnabled
                ? "KO Elimination: ON"
                : "KO Elimination: OFF");
        TintWidget(
            g_koEliminationButton,
            g_koEliminationEnabled ? kBrass : kMuted);
        g_koEliminationButton->setTextColour(
            g_koEliminationEnabled ? kAmber : kTextPrimary);
    }

    void OnKoEliminationToggle(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending() ||
            SparSession::IsActive() ||
            g_uiMode == UiTeams1v1)
        {
            return;
        }

        g_koEliminationEnabled = !g_koEliminationEnabled;
        SparSession::SetKoEliminationEnabled(g_koEliminationEnabled);
        UpdateKoEliminationButton();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void UpdateDestHighlight()
    {
        if (!IsTeamUiMode())
            return;

        if (g_btnDestA)
        {
            TintWidget(g_btnDestA, (g_dest == DestTeamA) ? kBrass : kMuted);
            g_btnDestA->setTextColour(
                (g_dest == DestTeamA) ? kAmber : kTextPrimary);
        }
        if (g_btnDestB)
        {
            TintWidget(g_btnDestB, (g_dest == DestTeamB) ? kBrass : kMuted);
            g_btnDestB->setTextColour(
                (g_dest == DestTeamB) ? kAmber : kTextPrimary);
        }
        if (g_lblDest)
        {
            g_lblDest->setCaption(g_dest == DestTeamA
                ? "Click roster → Team A"
                : "Click roster → Team B");
            TintWidget(g_lblDest, g_dest == DestTeamA ? kTeamA : kTeamB);
            g_lblDest->setCaption(g_dest == DestTeamA
                ? "Double-click assigns to Team A"
                : "Double-click assigns to Team B");
        }
        if (g_lblTeamA)
            TintWidget(g_lblTeamA, (g_dest == DestTeamA) ? kBrass : kTeamA);
        if (g_lblTeamB)
            TintWidget(g_lblTeamB, (g_dest == DestTeamB) ? kBrass : kTeamB);
    }

    void SwitchMode(UiMode newMode)
    {
        if (newMode == g_uiMode)
            return;
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;

        const bool wasTeam = IsTeamUiMode();
        const bool nowTeam = (newMode == UiAvB || newMode == UiTeams1v1);
        const bool nowLast = (newMode == UiLastStanding);
        const bool wasLast = (g_uiMode == UiLastStanding);

        if (wasTeam && nowLast)
        {
            for (size_t i = 0; i < g_teamA.size(); ++i)
            {
                if (!VectorContains(g_pool, g_teamA[i]))
                    g_pool.push_back(g_teamA[i]);
            }
            for (size_t i = 0; i < g_teamB.size(); ++i)
            {
                if (!VectorContains(g_pool, g_teamB[i]))
                    g_pool.push_back(g_teamB[i]);
            }
            g_teamA.clear();
            g_teamB.clear();
        }
        else if (wasLast && nowTeam)
        {
            g_pool.clear();
        }

        g_uiMode = newMode;
        UpdateModeVisibility();
    }

    void RefreshRosterList()
    {
        g_filteredRoster.clear();
        const std::string query = LowerText(g_rosterQuery);
        for (size_t i = 0; i < g_rosterCache.size(); ++i)
        {
            Character* c = g_rosterCache[i];
            if (!c)
                continue;
            bool filterMatch = true;
            if (g_rosterFilter == FilterReady)
                filterMatch = IsAssignable(c) && !IsAssigned(c);
            else if (g_rosterFilter == FilterAssigned)
                filterMatch = IsAssigned(c);
            else if (g_rosterFilter == FilterUnavailable)
                filterMatch = !IsAssignable(c);

            if (filterMatch &&
                (query.empty() || LowerText(c->getName()).find(query) != std::string::npos))
                g_filteredRoster.push_back(c);
        }
        RebuildList(g_rosterScroll, g_rosterRows, g_filteredRoster, ListRoster);

        if (g_rosterCount)
        {
            char countBuf[64];
            if (g_filteredRoster.size() == g_rosterCache.size())
                sprintf_s(countBuf, "%d fighters", static_cast<int>(g_rosterCache.size()));
            else
                sprintf_s(
                    countBuf,
                    "%d of %d fighters",
                    static_cast<int>(g_filteredRoster.size()),
                    static_cast<int>(g_rosterCache.size()));
            g_rosterCount->setCaption(countBuf);
        }
        UpdateScrollButtonsForKind(ListRoster);
    }

    void UpdateRosterFilterHighlight()
    {
        if (g_filterAll)
        {
            TintWidget(g_filterAll, g_rosterFilter == FilterAll ? kAmber : kMuted);
            g_filterAll->setTextColour(
                g_rosterFilter == FilterAll ? kAmber : kTextPrimary);
        }
        if (g_filterReady)
        {
            TintWidget(g_filterReady, g_rosterFilter == FilterReady ? kReady : kMuted);
            g_filterReady->setTextColour(
                g_rosterFilter == FilterReady ? kReady : kTextPrimary);
        }
        if (g_filterAssigned)
        {
            TintWidget(g_filterAssigned, g_rosterFilter == FilterAssigned ? kAmber : kMuted);
            g_filterAssigned->setTextColour(
                g_rosterFilter == FilterAssigned ? kAmber : kTextPrimary);
        }
        if (g_filterUnavailable)
        {
            TintWidget(
                g_filterUnavailable,
                g_rosterFilter == FilterUnavailable ? kDanger : kMuted);
            g_filterUnavailable->setTextColour(
                g_rosterFilter == FilterUnavailable ? kDanger : kTextPrimary);
        }
    }

    void SetRosterFilter(RosterFilter filter)
    {
        g_rosterFilter = filter;
        g_listScrollOffset[static_cast<int>(ListRoster)] = 0;
        UpdateRosterFilterHighlight();
        RefreshRosterList();
    }

    void OnFilterAll(MyGUI::Widget* /*sender*/) { SetRosterFilter(FilterAll); }
    void OnFilterReady(MyGUI::Widget* /*sender*/) { SetRosterFilter(FilterReady); }
    void OnFilterAssigned(MyGUI::Widget* /*sender*/) { SetRosterFilter(FilterAssigned); }
    void OnFilterUnavailable(MyGUI::Widget* /*sender*/) { SetRosterFilter(FilterUnavailable); }

    float AverageCombatSkill(const std::vector<Character*>& fighters)
    {
        float totalSkill = 0.0f;
        int validFighters = 0;
        for (size_t i = 0; i < fighters.size(); ++i)
        {
            Character* fighter = fighters[i];
            if (!fighter || !fighter->isValid() || !fighter->getStats())
                continue;

            totalSkill += fighter->getStats()->getOverallSkillLevel_0_100();
            ++validFighters;
        }

        return validFighters > 0 ? totalSkill / static_cast<float>(validFighters) : 0.0f;
    }

    void RefreshAssignmentLists()
    {
        EnsureHandlerAssignments();
        RebuildList(g_teamAScroll, g_teamARows, g_teamA, ListTeamA);
        RebuildList(g_teamBScroll, g_teamBRows, g_teamB, ListTeamB);
        RebuildList(g_poolScroll, g_poolRows, g_pool, ListPool);

        char buf[96];
        if (g_lblTeamA)
        {
            sprintf_s(
                buf,
                "Team A | %d fighter%s | ~%.0f combat",
                static_cast<int>(g_teamA.size()),
                g_teamA.size() == 1 ? "" : "s",
                AverageCombatSkill(g_teamA));
            g_lblTeamA->setCaption(buf);
        }
        if (g_lblTeamB)
        {
            sprintf_s(
                buf,
                "Team B | %d fighter%s | ~%.0f combat",
                static_cast<int>(g_teamB.size()),
                g_teamB.size() == 1 ? "" : "s",
                AverageCombatSkill(g_teamB));
            g_lblTeamB->setCaption(buf);
        }
        if (g_lblPool)
        {
            sprintf_s(buf, "Fighter pool  |  %d fighters", static_cast<int>(g_pool.size()));
            g_lblPool->setCaption(buf);
        }
        UpdateScrollButtonsForKind(ListTeamA);
        UpdateScrollButtonsForKind(ListTeamB);
        UpdateScrollButtonsForKind(ListPool);
    }

    void OnRosterSearchChanged(MyGUI::EditBox* sender)
    {
        if (!sender || g_searchPlaceholderActive)
            return;
        g_rosterQuery = sender->getOnlyText().asUTF8();
        g_listScrollOffset[static_cast<int>(ListRoster)] = 0;
        RefreshRosterList();
    }

    void OnRosterSearchFocus(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        if (!sender || !g_rosterSearch || !g_searchPlaceholderActive)
            return;
        g_searchPlaceholderActive = false;
        g_rosterSearch->setOnlyText("");
        g_rosterSearch->setTextColour(kTextPrimary);
        TintWidget(g_rosterSearchSurface, kAmber);
    }

    void OnRosterSearchMousePressed(
        MyGUI::Widget* sender, int /*left*/, int /*top*/, MyGUI::MouseButton button)
    {
        if (!sender || button != MyGUI::MouseButton::Left || !g_rosterSearch)
            return;
        OnRosterSearchFocus(g_rosterSearch, NULL);
        MyGUI::InputManager::getInstance().setKeyFocusWidget(g_rosterSearch);
    }

    void OnRosterSearchAccepted(MyGUI::EditBox* sender)
    {
        if (!sender)
            return;
        MyGUI::InputManager::getInstance().resetKeyFocusWidget(sender);
    }

    void OnRosterSearchBlur(MyGUI::Widget* sender, MyGUI::Widget* /*newFocus*/)
    {
        if (!sender || !g_rosterSearch || g_searchPlaceholderActive)
            return;
        if (!g_rosterSearch->getOnlyText().empty())
        {
            TintWidget(g_rosterSearchSurface, kMuted);
            return;
        }
        g_searchPlaceholderActive = true;
        g_rosterQuery.clear();
        g_listScrollOffset[static_cast<int>(ListRoster)] = 0;
        g_rosterSearch->setOnlyText("Search fighters...");
        g_rosterSearch->setTextColour(kMuted);
        TintWidget(g_rosterSearchSurface, kMuted);
        RefreshRosterList();
    }

    void RefreshRowLabel(PortraitRow& row)
    {
        if (!row.label || !row.character)
            return;

        std::string caption = row.character->getName();
        const std::string reason = FighterUnavailableReason(row.character);
        if (row.kind == ListRoster)
        {
            if (IsAssigned(row.character) || !reason.empty())
            {
                caption += "\n";
                caption += FighterDetailLabel(row.character);
            }
        }
        else if (!reason.empty())
        {
            caption += "\nUnavailable - ";
            caption += reason;
        }
        row.label->setCaption(caption);
        TintWidget(row.label, reason.empty() ? kTextPrimary : kMuted);
        ApplyRowState(row);
    }

    void RefreshAvailabilityLabels()
    {
        for (size_t i = 0; i < g_rosterRows.size(); ++i)
            RefreshRowLabel(g_rosterRows[i]);
        for (size_t i = 0; i < g_teamARows.size(); ++i)
            RefreshRowLabel(g_teamARows[i]);
        for (size_t i = 0; i < g_teamBRows.size(); ++i)
            RefreshRowLabel(g_teamBRows[i]);
        for (size_t i = 0; i < g_poolRows.size(); ++i)
            RefreshRowLabel(g_poolRows[i]);

        if (g_previewMeta && g_previewChar && g_previewChar->isValid())
            g_previewMeta->setCaption(FighterDetailLabel(g_previewChar));
    }

    void RefreshSummary()
    {
        if (!g_summary)
            return;

        const char* location =
            ArenaIngress::GetLocationMode() == ArenaIngress::LocationArena
                ? "Arena"
                : "Banner";
        char text[320];
        if (g_uiMode == UiAvB)
        {
            sprintf_s(
                text,
                "Teams   ·   A %d / B %d\n%s   ·   Team knockout",
                static_cast<int>(g_teamA.size()),
                static_cast<int>(g_teamB.size()),
                location);
        }
        else if (g_uiMode == UiTeams1v1)
        {
            sprintf_s(
                text,
                "Teams 1v1   ·   A %d / B %d\n%s   ·   Sequential · Winner stays",
                static_cast<int>(g_teamA.size()),
                static_cast<int>(g_teamB.size()),
                location);
        }
        else
        {
            sprintf_s(
                text,
                "Last man standing   ·   %d fighters\n%s   ·   Last conscious fighter wins",
                static_cast<int>(g_pool.size()),
                location);
        }

        g_summary->setCaption(text);
    }

    bool CanStart()
    {
        if (SparSession::IsActive())
            return false;
        if (ArenaIngress::IsPending())
            return false;
        if (PrisonerUtil::IsReturning())
            return false;
        if (!ArenaIngress::HasLocationSite())
            return false;

        if (IsTeamUiMode())
        {
            if (g_teamA.empty() || g_teamB.empty())
                return false;
            for (size_t i = 0; i < g_teamA.size(); ++i)
            {
                if (!IsAssignable(g_teamA[i]))
                    return false;
            }
            for (size_t i = 0; i < g_teamB.size(); ++i)
            {
                if (!IsAssignable(g_teamB[i]))
                    return false;
            }
            if (!PrisonerMatchLogic::HasEnoughHandlers(
                    CountPrisonersInMatch(), CountAvailableHandlers()))
                return false;
            return true;
        }

        if (g_pool.size() < 2)
            return false;
        for (size_t i = 0; i < g_pool.size(); ++i)
        {
            if (!IsAssignable(g_pool[i]))
                return false;
        }
        if (!PrisonerMatchLogic::HasEnoughHandlers(
                CountPrisonersInMatch(), CountAvailableHandlers()))
            return false;
        return true;
    }

    std::string FirstUnavailableAssignmentMessage(
        const std::vector<Character*>& fighters)
    {
        for (size_t i = 0; i < fighters.size(); ++i)
        {
            const std::string reason = FighterUnavailableReason(fighters[i]);
            if (reason.empty())
                continue;

            std::string message = fighters[i] && fighters[i]->isValid()
                ? fighters[i]->getName()
                : "Fighter";
            message += " unavailable: ";
            message += reason;
            return message;
        }
        return std::string();
    }

    std::string ValidationMessage()
    {
        if (ArenaIngress::IsPending())
            return "[...] Fighters are moving to the match location.";
        if (SparSession::IsActive())
            return "[LIVE] A spar is currently running.";
        if (!ArenaIngress::HasLocationSite())
            return ArenaIngress::GetMissingLocationMessage();
        if (IsTeamUiMode() && (g_teamA.empty() || g_teamB.empty()))
            return "Assign at least one fighter to each team.";
        if (!IsTeamUiMode() && g_pool.size() < 2)
            return "Assign at least two fighters.";

        std::string unavailable = FirstUnavailableAssignmentMessage(g_teamA);
        if (unavailable.empty())
            unavailable = FirstUnavailableAssignmentMessage(g_teamB);
        if (unavailable.empty())
            unavailable = FirstUnavailableAssignmentMessage(g_pool);
        if (!unavailable.empty())
            return unavailable;

        const int prisonersInMatch = CountPrisonersInMatch();
        const int availableHandlers = CountAvailableHandlers();
        if (!PrisonerMatchLogic::HasEnoughHandlers(prisonersInMatch, availableHandlers))
            return PrisonerMatchLogic::FormatHandlerShortage(
                prisonersInMatch, availableHandlers);

        return "Ready";
    }

    void RefreshStatusAndButtons()
    {
        const bool active = SparSession::IsActive();
        const bool pending = ArenaIngress::IsPending();
        const bool edit = !active && !pending;

        if (!active)
            RefreshAvailabilityLabels();

        if (g_status)
        {
            if (pending)
                g_status->setCaption(ArenaIngress::GetStatus().c_str());
            else if (active)
                g_status->setCaption(SparSession::GetStatus());
            else if (!ArenaIngress::HasLocationSite())
                g_status->setCaption(ArenaIngress::GetMissingLocationMessage());
            else if (g_previewChar && !IsAssignable(g_previewChar))
                g_status->setCaption("Selected fighter is KO/dead — cannot assign");
            else if (IsTeamUiMode())
                g_status->setCaption(g_dest == DestTeamA
                    ? "Click a face to send to Team A"
                    : "Click a face to send to Team B");
            else
                g_status->setCaption("Click a face to add to pool");
        }

        // Keep validation adjacent to the primary action; selection detail lives above.
        if (g_status && !active && !pending)
            g_status->setCaption(ValidationMessage());
        if (g_status)
            TintWidget(g_status, (!active && !pending && CanStart()) ? kReady :
                ((active || pending) ? kMuted : kDanger));

        if (g_startButton)
        {
            g_startButton->setVisible(!active && !pending);
            g_startButton->setEnabled(edit && CanStart());
            TintWidget(g_startButton, CanStart() ? kAmber : kDisabled);
            g_startButton->setTextColour(CanStart() ? kAmber : kDisabled);
        }
        if (g_stopButton)
        {
            g_stopButton->setVisible(active || pending);
            g_stopButton->setEnabled(active || pending);
            g_stopButton->setCaption(pending ? "Cancel Walk-in" : "Stop Spar");
            TintWidget(g_stopButton, kDanger);
            g_stopButton->setTextColour(kTextPrimary);
        }

        if (g_modeAvB)
            g_modeAvB->setEnabled(edit);
        if (g_modeTeams1v1)
            g_modeTeams1v1->setEnabled(edit);
        if (g_modeLast)
            g_modeLast->setEnabled(edit);
        if (g_locArena)
            g_locArena->setEnabled(edit);
        if (g_locBanner)
            g_locBanner->setEnabled(edit);
        if (g_koEliminationButton)
            g_koEliminationButton->setEnabled(edit && g_uiMode != UiTeams1v1);
        if (g_btnDestA)
            g_btnDestA->setEnabled(edit && g_previewChar && IsAssignable(g_previewChar));
        if (g_btnDestB)
            g_btnDestB->setEnabled(
                edit && IsTeamUiMode() && g_previewChar && IsAssignable(g_previewChar));
        if (g_btnRemove)
            g_btnRemove->setEnabled(edit && g_previewChar && IsAssigned(g_previewChar));
        if (g_btnAutoBalance)
            g_btnAutoBalance->setEnabled(edit);
        if (g_btnBalanceSkill)
            g_btnBalanceSkill->setEnabled(edit);
        if (g_btnBalanceRating)
            g_btnBalanceRating->setEnabled(edit);
        if (g_btnClearTeams)
            g_btnClearTeams->setEnabled(edit &&
                (!g_teamA.empty() || !g_teamB.empty() || !g_pool.empty()));
        if (g_btnSwapTeams)
            g_btnSwapTeams->setEnabled(edit && IsTeamUiMode() &&
                (!g_teamA.empty() || !g_teamB.empty()));
        for (int i = 0; i < 3; ++i)
        {
            if (g_presetButtons[i])
                g_presetButtons[i]->setEnabled(edit);
        }

        if (g_btnDestA)
            g_btnDestA->setTextColour(
                g_btnDestA->getEnabled() ? (IsTeamUiMode() &&
                    g_dest == DestTeamA ? kAmber : kTextPrimary) : kDisabled);
        if (g_btnDestB)
            g_btnDestB->setTextColour(
                g_btnDestB->getEnabled() ?
                    (g_dest == DestTeamB ? kAmber : kTextPrimary) : kDisabled);
        if (g_btnAutoBalance)
            g_btnAutoBalance->setTextColour(
                g_btnAutoBalance->getEnabled() ? kTextPrimary : kDisabled);
        if (g_btnBalanceSkill)
            g_btnBalanceSkill->setTextColour(
                g_btnBalanceSkill->getEnabled() ? kTextPrimary : kDisabled);
        if (g_btnBalanceRating)
            g_btnBalanceRating->setTextColour(
                g_btnBalanceRating->getEnabled() ? kTextPrimary : kDisabled);
        if (g_btnSwapTeams)
            g_btnSwapTeams->setTextColour(
                g_btnSwapTeams->getEnabled() ? kTextPrimary : kDisabled);
        if (g_btnClearTeams)
            g_btnClearTeams->setTextColour(
                g_btnClearTeams->getEnabled() ? kTextPrimary : kDisabled);
        if (g_btnRemove)
            g_btnRemove->setTextColour(
                g_btnRemove->getEnabled() ? kTextPrimary : kDisabled);
        RefreshCompactDrawers();
    }

    void RefreshAll()
    {
        RefreshRosterCaches();

        // Drop stale assignment pointers.
        for (size_t i = 0; i < g_teamA.size(); )
        {
            if (!VectorContains(g_rosterCache, g_teamA[i]))
                g_teamA.erase(g_teamA.begin() + i);
            else
                ++i;
        }
        for (size_t i = 0; i < g_teamB.size(); )
        {
            if (!VectorContains(g_rosterCache, g_teamB[i]))
                g_teamB.erase(g_teamB.begin() + i);
            else
                ++i;
        }
        for (size_t i = 0; i < g_pool.size(); )
        {
            if (!VectorContains(g_rosterCache, g_pool[i]))
                g_pool.erase(g_pool.begin() + i);
            else
                ++i;
        }

        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        if (g_previewChar && !VectorContains(g_rosterCache, g_previewChar))
            SetPreview(NULL);
        else
            SetPreview(g_previewChar);
        RefreshStatusAndButtons();
    }

    void OnModeAvB(MyGUI::Widget* /*sender*/)
    {
        SwitchMode(UiAvB);
        RefreshAssignmentLists();
        RefreshRosterList();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnModeTeams1v1(MyGUI::Widget* /*sender*/)
    {
        SwitchMode(UiTeams1v1);
        RefreshAssignmentLists();
        RefreshRosterList();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnModeLast(MyGUI::Widget* /*sender*/)
    {
        SwitchMode(UiLastStanding);
        RefreshAssignmentLists();
        RefreshRosterList();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnDestA(MyGUI::Widget* /*sender*/)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        g_dest = DestTeamA;
        UpdateDestHighlight();
        if (g_previewChar && IsAssignable(g_previewChar))
            AssignCharacter(g_previewChar);
        else
            RefreshStatusAndButtons();
    }

    void OnDestB(MyGUI::Widget* /*sender*/)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        g_dest = DestTeamB;
        UpdateDestHighlight();
        if (g_previewChar && IsAssignable(g_previewChar))
            AssignCharacter(g_previewChar);
        else
            RefreshStatusAndButtons();
    }

    void OnRemove(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending())
        {
            if (g_status)
                g_status->setCaption("Walk-in in progress — cannot change roster");
            return;
        }
        if (g_previewChar && IsAssigned(g_previewChar))
            RemoveCharacter(g_previewChar);
    }

    int PresetIndex(MyGUI::Widget* sender)
    {
        if (!sender)
            return -1;
        int index = -1;
        const std::string& value = sender->getUserString("preset");
        if (value.empty() || sscanf_s(value.c_str(), "%d", &index) != 1)
            return -1;
        return index >= 0 && index < 3 ? index : -1;
    }

    void RefreshPresetButtons()
    {
        for (int i = 0; i < 3; ++i)
        {
            if (!g_presetButtons[i])
                continue;
            char caption[32];
            sprintf_s(
                caption,
                "Preset %d%s",
                i + 1,
                g_setupPresets[i].valid ? " *" : "");
            g_presetButtons[i]->setCaption(caption);
            TintWidget(
                g_presetButtons[i],
                g_setupPresets[i].valid ? kBrass : kMuted);
        }
    }

    void SaveSetupPreset(int index)
    {
        if (index < 0 || index >= 3)
            return;
        SetupPreset& preset = g_setupPresets[index];
        preset.valid = true;
        preset.mode = g_uiMode;
        preset.destination = g_dest;
        preset.location = ArenaIngress::GetLocationMode();
        preset.koElimination = g_koEliminationEnabled;
        preset.teamA = g_teamA;
        preset.teamB = g_teamB;
        preset.pool = g_pool;
        RefreshPresetButtons();
    }

    void RemoveMissingPresetFighters(std::vector<Character*>& fighters)
    {
        for (size_t i = 0; i < fighters.size(); )
        {
            if (!VectorContains(g_rosterCache, fighters[i]))
                fighters.erase(fighters.begin() + i);
            else
                ++i;
        }
    }

    void LoadSetupPreset(int index)
    {
        if (index < 0 || index >= 3 || !g_setupPresets[index].valid)
            return;

        const SetupPreset& preset = g_setupPresets[index];
        g_uiMode = preset.mode;
        g_dest = preset.destination;
        g_koEliminationEnabled = preset.koElimination;
        g_teamA = preset.teamA;
        g_teamB = preset.teamB;
        g_pool = preset.pool;
        RemoveMissingPresetFighters(g_teamA);
        RemoveMissingPresetFighters(g_teamB);
        RemoveMissingPresetFighters(g_pool);
        ArenaIngress::SetLocationMode(preset.location);
        SparSession::SetKoEliminationEnabled(g_koEliminationEnabled);
        SetPreview(NULL);
        UpdateModeVisibility();
        UpdateLocationHighlight();
        UpdateKoEliminationButton();
        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnPresetButtonClick(MyGUI::Widget* /*sender*/)
    {
        // Preset actions use mouse-pressed so left/right can load/save.
    }

    void OnPresetMousePressed(
        MyGUI::Widget* sender,
        int /*left*/,
        int /*top*/,
        MyGUI::MouseButton button)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        const int index = PresetIndex(sender);
        if (index < 0)
            return;

        if (button == MyGUI::MouseButton::Right)
            SaveSetupPreset(index);
        else if (button == MyGUI::MouseButton::Left)
            LoadSetupPreset(index);
    }

    void OnBalanceMenu(MyGUI::Widget* /*sender*/)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        g_balanceMenuOpen = !g_balanceMenuOpen;
        UpdateBalanceMenuVisibility();
    }

    void BalanceTeams(bool byRating)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;

        std::vector<Character*> selected;
        if (IsTeamUiMode())
        {
            selected.insert(selected.end(), g_teamA.begin(), g_teamA.end());
            selected.insert(selected.end(), g_teamB.begin(), g_teamB.end());
        }
        else
        {
            selected = g_pool;
        }

        // With no selection, either balance method doubles as a quick "select all
        // available" action. Otherwise it only operates on the chosen fighters.
        const std::vector<Character*>& source =
            selected.empty() ? g_rosterCache : selected;

        g_teamA.clear();
        g_teamB.clear();
        g_pool.clear();
        std::vector<BalanceCandidate> candidates;
        for (size_t i = 0; i < source.size(); ++i)
        {
            Character* c = source[i];
            if (!IsAssignable(c))
                continue;
            if (IsTeamUiMode())
            {
                BalanceCandidate candidate = {};
                candidate.fighter = c;
                candidate.score = byRating
                    ? LeaderboardStore::GetRating(c)
                    : (c->getStats()
                        ? c->getStats()->getOverallSkillLevel_0_100()
                        : 0.0f);
                candidates.push_back(candidate);
            }
            else
            {
                g_pool.push_back(c);
            }
        }

        if (IsTeamUiMode())
        {
            std::sort(candidates.begin(), candidates.end(), BetterBalanceCandidate);
            const size_t targetA = (candidates.size() + 1) / 2;
            const size_t targetB = candidates.size() / 2;
            float scoreA = 0.0f;
            float scoreB = 0.0f;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                const bool toA = g_teamA.size() < targetA &&
                    (g_teamB.size() >= targetB || scoreA <= scoreB);
                if (toA)
                {
                    g_teamA.push_back(candidates[i].fighter);
                    scoreA += candidates[i].score;
                }
                else
                {
                    g_teamB.push_back(candidates[i].fighter);
                    scoreB += candidates[i].score;
                }
            }
        }
        g_balanceMenuOpen = false;
        UpdateBalanceMenuVisibility();
        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnBalanceBySkill(MyGUI::Widget* /*sender*/)
    {
        BalanceTeams(false);
    }

    void OnBalanceByRating(MyGUI::Widget* /*sender*/)
    {
        BalanceTeams(true);
    }

    void OnClearTeams(MyGUI::Widget* /*sender*/)
    {
        if (SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        g_teamA.clear();
        g_teamB.clear();
        g_pool.clear();
        SetPreview(NULL);
        RefreshRosterList();
        RefreshAssignmentLists();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnSwapTeams(MyGUI::Widget* /*sender*/)
    {
        if (!IsTeamUiMode() || SparSession::IsActive() || ArenaIngress::IsPending())
            return;
        g_teamA.swap(g_teamB);
        RefreshRosterList();
        RefreshAssignmentLists();
        SetPreview(g_previewChar);
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnStart(MyGUI::Widget* /*sender*/)
    {
        if (!CanStart())
            return;

        RefreshRosterCaches();

        std::vector<Character*> match;
        CollectMatchFighters(match);

        std::vector<Character*> prisonerFighters;
        for (size_t i = 0; i < match.size(); ++i)
        {
            if (PrisonerUtil::IsRosterPrisoner(match[i]))
                prisonerFighters.push_back(match[i]);
        }

        std::vector<Character*> handlers;
        BuildOrderedHandlers(prisonerFighters, handlers);

        std::string prepStatus;
        if (!prisonerFighters.empty())
        {
            if (!PrisonerUtil::PrepareMatch(prisonerFighters, handlers, prepStatus))
            {
                if (PrisonerUtil::MatchIncludesPrisoner())
                {
                    std::string returnStatus;
                    PrisonerUtil::ReturnAllToCages(returnStatus);
                    if (!returnStatus.empty())
                        prepStatus = returnStatus;
                }
                if (g_status)
                    g_status->setCaption(prepStatus.c_str());
                RefreshStatusAndButtons();
                return;
            }
        }
        else
        {
            PrisonerUtil::ClearMatchState();
        }

        const std::vector<Character*>& escorts = PrisonerUtil::GetMatchHandlers();
        Character** escortPtr = escorts.empty() ? NULL : const_cast<Character**>(&escorts[0]);
        const int escortCount = static_cast<int>(escorts.size());

        std::vector<Character*> fighters;
        std::vector<MatchRules::MatchTeam> teams;
        MatchRules::MatchMode mode = MatchRules::ModeTeamAvB;
        bool ok = false;
        SparSession::SetKoEliminationEnabled(g_koEliminationEnabled);

        if (IsTeamUiMode())
        {
            for (size_t i = 0; i < g_teamA.size(); ++i)
            {
                fighters.push_back(g_teamA[i]);
                teams.push_back(MatchRules::TeamA);
            }
            for (size_t i = 0; i < g_teamB.size(); ++i)
            {
                fighters.push_back(g_teamB[i]);
                teams.push_back(MatchRules::TeamB);
            }
            mode = (g_uiMode == UiTeams1v1)
                ? MatchRules::ModeTeams1v1
                : MatchRules::ModeTeamAvB;
            ok = ArenaIngress::Begin(
                mode,
                &fighters[0],
                &teams[0],
                static_cast<int>(fighters.size()),
                escortPtr,
                escortCount);
        }
        else
        {
            mode = MatchRules::ModeLastStanding;
            for (size_t i = 0; i < g_pool.size(); ++i)
            {
                fighters.push_back(g_pool[i]);
                teams.push_back(MatchRules::TeamNone);
            }
            ok = ArenaIngress::Begin(
                mode,
                &fighters[0],
                &teams[0],
                static_cast<int>(fighters.size()),
                escortPtr,
                escortCount);
        }

        if (!ok)
        {
            if (PrisonerUtil::MatchIncludesPrisoner())
            {
                std::string returnStatus;
                PrisonerUtil::ReturnAllToCages(returnStatus);
                if (g_status)
                {
                    const std::string ingressStatus = ArenaIngress::GetStatus();
                    g_status->setCaption(returnStatus.empty()
                        ? ingressStatus.c_str()
                        : returnStatus.c_str());
                }
            }
            else if (g_status)
            {
                g_status->setCaption(ArenaIngress::GetStatus().c_str());
            }
        }

        if (ok)
        {
            if (g_window)
                g_window->setVisible(false);
        }
        else
            RefreshStatusAndButtons();
    }

    void OnStop(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending())
            ArenaIngress::Cancel();
        SparSession::Stop(SparSession::StopManual);
        RefreshStatusAndButtons();
    }

    void SetColumnVisible(std::vector<MyGUI::Widget*>& widgets, bool visible)
    {
        for (size_t i = 0; i < widgets.size(); ++i)
        {
            if (widgets[i] && widgets[i]->getVisible() != visible)
                widgets[i]->setVisible(visible);
        }
    }

    void RefreshCompactDrawers()
    {
        if (g_rosterDrawerButton)
            g_rosterDrawerButton->setVisible(g_compactMode);
        if (g_settingsDrawerButton)
            g_settingsDrawerButton->setVisible(g_compactMode);
        if (g_titleEyebrow)
            g_titleEyebrow->setVisible(!g_compactMode);

        SetColumnVisible(
            g_rosterColumnWidgets, !g_compactMode || g_rosterDrawerOpen);
        SetColumnVisible(
            g_settingsColumnWidgets, !g_compactMode || g_settingsDrawerOpen);
        const bool settingsVisible = !g_compactMode || g_settingsDrawerOpen;
        if (g_previewImage)
            g_previewImage->setVisible(
                settingsVisible && g_previewChar && g_previewChar->isValid());
        if (settingsVisible && g_startButton && g_stopButton)
        {
            const bool busy = SparSession::IsActive() || ArenaIngress::IsPending();
            g_startButton->setVisible(!busy);
            g_stopButton->setVisible(busy);
        }
    }

    void OnToggleRosterDrawer(MyGUI::Widget* /*sender*/)
    {
        if (!g_compactMode)
            return;
        g_rosterDrawerOpen = !g_rosterDrawerOpen;
        if (g_rosterDrawerOpen)
            g_settingsDrawerOpen = false;
        RefreshCompactDrawers();
    }

    void OnToggleSettingsDrawer(MyGUI::Widget* /*sender*/)
    {
        if (!g_compactMode)
            return;
        g_settingsDrawerOpen = !g_settingsDrawerOpen;
        if (g_settingsDrawerOpen)
            g_rosterDrawerOpen = false;
        RefreshCompactDrawers();
    }

    void RefreshResponsiveMode()
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render)
            return;
        const MyGUI::IntSize size = render->getViewSize();
        if (size.height <= 0)
            return;
        const bool compact =
            static_cast<float>(size.width) / static_cast<float>(size.height) < 1.5f;
        if (compact == g_compactMode)
            return;

        g_compactMode = compact;
        g_rosterDrawerOpen = false;
        g_settingsDrawerOpen = false;
        RefreshCompactDrawers();
    }

    MyGUI::ScrollView* MakeScroll(MyGUI::Widget* parent, float x, float y, float w, float h, const char* name)
    {
        MyGUI::ScrollView* scroll = parent->createWidgetReal<MyGUI::ScrollView>(
            "ScrollView", x, y, w, h, MyGUI::Align::Default, name);
        if (scroll)
        {
            scroll->setVisibleHScroll(false);
            scroll->setVisibleVScroll(true);
            scroll->setCanvasAlign(MyGUI::Align::Default);
            TintWidget(scroll, kBgDark);
        }
        return scroll;
    }

    MyGUI::Button* MakeButton(MyGUI::Widget* parent, float x, float y, float w, float h,
        const char* name, const char* caption, void (*handler)(MyGUI::Widget*))
    {
        MyGUI::Button* btn = parent->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", x, y, w, h, MyGUI::Align::Default, name);
        btn->setCaption(caption);
        btn->setFontHeight(16);
        btn->setTextColour(kTextPrimary);
        btn->setTextShadow(true);
        btn->setTextShadowColour(MyGUI::Colour::Black);
        btn->eventMouseButtonClick += MyGUI::newDelegate(handler);
        return btn;
    }

    MyGUI::TextBox* MakeLabel(MyGUI::Widget* parent, float x, float y, float w, float h,
        const char* name, const char* caption)
    {
        MyGUI::TextBox* lbl = parent->createWidgetReal<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText", x, y, w, h, MyGUI::Align::Default, name);
        lbl->setCaption(caption);
        lbl->setFontHeight(16);
        TintWidget(lbl, kTextPrimary);
        return lbl;
    }

    MyGUI::Button* MakeSurface(
        MyGUI::Widget* parent, float x, float y, float w, float h, const char* name)
    {
        MyGUI::Button* surface = parent->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", x, y, w, h, MyGUI::Align::Default, name);
        surface->setCaption("");
        surface->setNeedMouseFocus(false);
        TintWidget(surface, kBgDark);
        return surface;
    }

    void CreatePanel()
    {
        if (g_window)
            return;

        MyGUI::Gui* guiInstance = MyGUI::Gui::getInstancePtr();
        if (!guiInstance)
        {
            ErrorLog("Proving Grounds: MyGUI not ready");
            return;
        }

        g_window = guiInstance->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX", 0.06f, 0.05f, 0.88f, 0.82f,
            MyGUI::Align::Center, "Window", "ProvingGroundsArenaWindow");
        g_window->setCaption("Proving Grounds — Arena");
        g_window->setVisible(false);
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
        TintWidget(g_window, kBgDark);

        InstallArenaTopBar(g_window);

        MyGUI::Widget* client = g_window->getClientWidget();

        // Custom metal panel art — full client, behind all controls.
        // Must live under gui/images so Kenshi puts it in MyGUI's GUI resource group
        // (same pattern as Character Inspector's gui/silhouette).
        g_arenaBg = client->createWidgetReal<MyGUI::ImageBox>(
            "ImageBox", 0.0f, 0.0f, 1.0f, 1.0f, MyGUI::Align::Default, "PG_ArenaBg");
        ApplyGuiTexture(g_arenaBg, kArenaBgTexture);

        // Stable 25 / 50 / 25 composition aligned to the texture's metal frame.
        g_titleEyebrow = NULL;
        g_titleHero = MakeLabel(
            client, 0.28f, 0.018f, 0.42f, 0.045f, "PG_Hero", "Arena Setup");
        g_titleHero->setFontHeight(20);
        g_titleHero->setTextAlign(MyGUI::Align::Center);
        TintWidget(g_titleHero, kTextPrimary);

        MyGUI::TextBox* rosterHeading = MakeLabel(
            client, 0.05f, 0.055f, 0.13f, 0.04f, "PG_LblRoster", "Squad Roster");
        rosterHeading->setFontHeight(19);
        g_rosterCount = MakeLabel(
            client, 0.175f, 0.059f, 0.075f, 0.035f, "PG_RosterCount", "0 fighters");
        TintWidget(g_rosterCount, kMuted);
        MyGUI::TextBox* searchLabel = NULL;
        g_rosterSearchSurface = MakeSurface(
            client, 0.05f, 0.103f, 0.20f, 0.052f, "PG_RosterSearchSurface");
        TintWidget(g_rosterSearchSurface, kMuted);
        if (::gui)
        {
            g_rosterSearch = ::gui->createEditBox(
                client,
                0.103f,
                0.05f,
                0.20f,
                0.052f,
                "PG_RosterSearch",
                false);
        }
        else
        {
            g_rosterSearch = client->createWidgetReal<MyGUI::EditBox>(
                "EditBox", 0.05f, 0.103f, 0.20f, 0.052f,
                MyGUI::Align::Default, "PG_RosterSearch");
        }
        if (!g_rosterSearch)
        {
            ErrorLog("Proving Grounds: failed to create roster search edit box");
            return;
        }
        g_rosterSearch->setFontHeight(16);
        g_rosterSearch->setMaxTextLength(40);
        g_rosterSearch->setEditReadOnly(false);
        g_rosterSearch->setNeedMouseFocus(true);
        g_rosterSearch->setNeedKeyFocus(true);
        g_rosterSearch->setVisibleHScroll(false);
        g_rosterSearch->setVisibleVScroll(false);
        g_rosterSearch->setTextAlign(MyGUI::Align::VCenter | MyGUI::Align::Left);
        g_rosterSearch->setOnlyText("Search fighters...");
        g_rosterSearch->setTextColour(kMuted);
        g_rosterSearch->eventEditTextChange += MyGUI::newDelegate(OnRosterSearchChanged);
        g_rosterSearch->eventKeySetFocus += MyGUI::newDelegate(OnRosterSearchFocus);
        g_rosterSearch->eventKeyLostFocus += MyGUI::newDelegate(OnRosterSearchBlur);
        g_rosterSearch->eventMouseButtonPressed +=
            MyGUI::newDelegate(OnRosterSearchMousePressed);
        g_rosterSearch->eventEditSelectAccept +=
            MyGUI::newDelegate(OnRosterSearchAccepted);

        g_filterAll = MakeButton(
            client, 0.05f, 0.165f, 0.06f, 0.045f, "PG_FilterAll", "All", OnFilterAll);
        g_filterReady = MakeButton(
            client, 0.115f, 0.165f, 0.06f, 0.045f, "PG_FilterReady", "Ready", OnFilterReady);
        g_filterAssigned = MakeButton(
            client, 0.18f, 0.165f, 0.07f, 0.045f,
            "PG_FilterAssigned", "Assigned", OnFilterAssigned);
        g_filterUnavailable = NULL;
        g_filterAll->setFontHeight(14);
        g_filterReady->setFontHeight(14);
        g_filterAssigned->setFontHeight(13);
        MyGUI::TextBox* lblRosterHint = MakeLabel(
            client, 0.05f, 0.218f, 0.20f, 0.035f, "PG_RosterHint",
            "Assign: double-click/drag | Remove: right-click");
        TintWidget(lblRosterHint, kMuted);
        g_rosterScroll = MakeScroll(client, 0.05f, 0.258f, 0.174f, 0.652f, "PG_Roster");
        g_rosterScrollUp = MakeButton(
            client, 0.226f, 0.265f, 0.022f, 0.05f,
            "PG_RosterScrollUp", "^", OnScrollButton);
        g_rosterScrollDown = MakeButton(
            client, 0.226f, 0.852f, 0.022f, 0.05f,
            "PG_RosterScrollDown", "v", OnScrollButton);
        g_rosterScrollUp->setUserString("scroll", "0:1");
        g_rosterScrollDown->setUserString("scroll", "0:-1");

        MyGUI::TextBox* modeHeading = MakeLabel(
            client, 0.28f, 0.075f, 0.42f, 0.04f, "PG_LblMode", "Match Mode");
        modeHeading->setFontHeight(19);
        g_modeAvB = MakeButton(
            client, 0.28f, 0.118f, 0.135f, 0.058f, "PG_ModeAvB", "Teams", OnModeAvB);
        g_modeTeams1v1 = MakeButton(
            client, 0.42f, 0.118f, 0.13f, 0.058f, "PG_ModeTeams1v1", "Teams 1v1", OnModeTeams1v1);
        g_modeLast = MakeButton(
            client, 0.555f, 0.118f, 0.145f, 0.058f, "PG_ModeLast", "Last Man Standing", OnModeLast);

        g_lblDest = MakeLabel(
            client, 0.28f, 0.188f, 0.42f, 0.032f, "PG_LblDest",
            "Double-click assigns to Team A");
        TintWidget(g_lblDest, kTeamA);
        g_btnDestA = MakeButton(
            client, 0.28f, 0.195f, 0.205f, 0.055f,
            "PG_DestA", "Assign to Team A", OnDestA);
        g_btnDestB = MakeButton(
            client, 0.495f, 0.195f, 0.205f, 0.055f,
            "PG_DestB", "Assign to Team B", OnDestB);

        g_lblTeamA = MakeLabel(
            client, 0.28f, 0.262f, 0.205f, 0.04f, "PG_LblTeamA", "Team A | 0 fighters | ~0 combat");
        TintWidget(g_lblTeamA, kTeamA);
        g_lblTeamB = MakeLabel(
            client, 0.495f, 0.262f, 0.205f, 0.04f, "PG_LblTeamB", "Team B | 0 fighters | ~0 combat");
        TintWidget(g_lblTeamB, kTeamB);
        g_teamASurface = MakeSurface(
            client, 0.28f, 0.304f, 0.205f, 0.438f, "PG_TeamASurface");
        g_teamBSurface = MakeSurface(
            client, 0.495f, 0.304f, 0.205f, 0.438f, "PG_TeamBSurface");
        g_teamAScroll = MakeScroll(client, 0.282f, 0.308f, 0.175f, 0.430f, "PG_TeamA");
        g_teamBScroll = MakeScroll(client, 0.497f, 0.308f, 0.175f, 0.430f, "PG_TeamB");
        g_teamAScrollUp = MakeButton(
            client, 0.459f, 0.315f, 0.022f, 0.05f,
            "PG_TeamAScrollUp", "^", OnScrollButton);
        g_teamAScrollDown = MakeButton(
            client, 0.459f, 0.681f, 0.022f, 0.05f,
            "PG_TeamAScrollDown", "v", OnScrollButton);
        g_teamBScrollUp = MakeButton(
            client, 0.674f, 0.315f, 0.022f, 0.05f,
            "PG_TeamBScrollUp", "^", OnScrollButton);
        g_teamBScrollDown = MakeButton(
            client, 0.674f, 0.681f, 0.022f, 0.05f,
            "PG_TeamBScrollDown", "v", OnScrollButton);
        g_teamAScrollUp->setUserString("scroll", "1:1");
        g_teamAScrollDown->setUserString("scroll", "1:-1");
        g_teamBScrollUp->setUserString("scroll", "2:1");
        g_teamBScrollDown->setUserString("scroll", "2:-1");
        TintWidget(g_teamAScroll, kTeamA);
        TintWidget(g_teamBScroll, kTeamB);
        ResetDropZoneVisuals();

        g_lblPool = MakeLabel(
            client, 0.28f, 0.262f, 0.42f, 0.04f, "PG_LblPool", "Fighter pool  |  0 fighters");
        g_poolSurface = MakeSurface(
            client, 0.28f, 0.304f, 0.42f, 0.438f, "PG_PoolSurface");
        g_poolScroll = MakeScroll(client, 0.282f, 0.308f, 0.390f, 0.430f, "PG_Pool");
        g_poolScrollUp = MakeButton(
            client, 0.674f, 0.315f, 0.022f, 0.05f,
            "PG_PoolScrollUp", "^", OnScrollButton);
        g_poolScrollDown = MakeButton(
            client, 0.674f, 0.681f, 0.022f, 0.05f,
            "PG_PoolScrollDown", "v", OnScrollButton);
        g_poolScrollUp->setUserString("scroll", "3:1");
        g_poolScrollDown->setUserString("scroll", "3:-1");
        TintWidget(g_poolSurface, kReady);

        g_btnAutoBalance = MakeButton(
            client, 0.28f, 0.76f, 0.135f, 0.055f,
            "PG_AutoBalance", "Balance", OnBalanceMenu);
        g_btnBalanceSkill = MakeButton(
            client, 0.42f, 0.76f, 0.13f, 0.055f,
            "PG_BalanceSkill", "By Skill", OnBalanceBySkill);
        g_btnBalanceRating = MakeButton(
            client, 0.555f, 0.76f, 0.145f, 0.055f,
            "PG_BalanceRating", "By Rating", OnBalanceByRating);
        g_btnBalanceSkill->setVisible(false);
        g_btnBalanceRating->setVisible(false);
        g_btnSwapTeams = MakeButton(
            client, 0.42f, 0.76f, 0.13f, 0.055f,
            "PG_SwapTeams", "Swap Teams", OnSwapTeams);
        g_btnClearTeams = MakeButton(
            client, 0.555f, 0.76f, 0.145f, 0.055f,
            "PG_ClearTeams", "Clear Teams", OnClearTeams);
        TintWidget(g_btnClearTeams, kDanger);
        g_btnRemove = MakeButton(
            client, 0.74f, 0.282f, 0.20f, 0.05f,
            "PG_Remove", "Remove Fighter", OnRemove);
        TintWidget(g_btnRemove, kDanger);
        MyGUI::TextBox* builderHint = MakeLabel(
            client, 0.28f, 0.825f, 0.42f, 0.032f, "PG_BuilderHint",
            "Presets: left-click load | right-click save");
        TintWidget(builderHint, kMuted);
        for (int i = 0; i < 3; ++i)
        {
            char name[32];
            char caption[32];
            sprintf_s(name, "PG_Preset%d", i + 1);
            sprintf_s(caption, "Preset %d", i + 1);
            g_presetButtons[i] = MakeButton(
                client,
                0.28f + (static_cast<float>(i) * 0.1425f),
                0.862f,
                0.135f,
                0.045f,
                name,
                caption,
                OnPresetButtonClick);
            g_presetButtons[i]->setUserString("preset", i == 0 ? "0" : (i == 1 ? "1" : "2"));
            g_presetButtons[i]->eventMouseButtonPressed +=
                MyGUI::newDelegate(OnPresetMousePressed);
        }
        RefreshPresetButtons();

        MyGUI::TextBox* selectedHeading = MakeLabel(
            client, 0.74f, 0.075f, 0.20f, 0.04f, "PG_LblPreview", "Selected Fighter");
        selectedHeading->setFontHeight(19);
        g_previewImage = client->createWidgetReal<MyGUI::ImageBox>(
            "ImageBox", 0.74f, 0.12f, 0.065f, 0.10f, MyGUI::Align::Default, "PG_PreviewImg");
        if (g_previewImage)
            g_previewImage->setVisible(false);
        g_previewName = MakeLabel(
            client, 0.815f, 0.12f, 0.125f, 0.045f,
            "PG_PreviewName", "No fighter selected");
        g_previewName->setFontHeight(18);
        TintWidget(g_previewName, kBrass);
        g_previewMeta = MakeLabel(
            client, 0.815f, 0.167f, 0.125f, 0.04f,
            "PG_PreviewMeta", "Select a roster or team row");
        TintWidget(g_previewMeta, kMuted);
        g_previewStats = MakeLabel(
            client, 0.74f, 0.225f, 0.20f, 0.055f,
            "PG_PreviewStats", "Choose a fighter to review readiness.");
        g_previewStats->setFontHeight(14);
        TintWidget(g_previewStats, kTextPrimary);

        MyGUI::TextBox* settingsHeading = MakeLabel(
            client, 0.74f, 0.335f, 0.20f, 0.04f, "PG_LblSettings", "Match Settings");
        settingsHeading->setFontHeight(19);
        MyGUI::TextBox* locationLabel = MakeLabel(
            client, 0.74f, 0.378f, 0.20f, 0.027f, "PG_LblLoc", "LOCATION");
        g_locArena = MakeButton(
            client, 0.74f, 0.408f, 0.097f, 0.052f, "PG_LocArena", "Arena", OnLocationArena);
        g_locBanner = MakeButton(
            client, 0.843f, 0.408f, 0.097f, 0.052f, "PG_LocBanner", "Banner", OnLocationBanner);
        g_koEliminationButton = MakeButton(
            client, 0.74f, 0.478f, 0.20f, 0.052f,
            "PG_KoElimination", "KO Elimination: OFF", OnKoEliminationToggle);

        MyGUI::TextBox* summaryHeading = MakeLabel(
            client, 0.74f, 0.548f, 0.20f, 0.035f, "PG_LblSummary", "Match Summary");
        g_summary = MakeLabel(
            client, 0.74f, 0.586f, 0.20f, 0.115f, "PG_Summary", "Team A vs Team B");
        TintWidget(g_summary, kTextPrimary);

        g_status = MakeLabel(
            client, 0.74f, 0.735f, 0.20f, 0.07f,
            "PG_Status", "Assign at least one fighter to each team.");
        TintWidget(g_status, kDanger);
        g_startButton = MakeButton(
            client, 0.74f, 0.82f, 0.20f, 0.075f, "PG_Start", "Start Match", OnStart);
        g_startButton->setFontHeight(18);
        TintWidget(g_startButton, kAmber);
        g_stopButton = MakeButton(
            client, 0.74f, 0.82f, 0.20f, 0.075f, "PG_Stop", "Stop Spar", OnStop);
        g_stopButton->setFontHeight(18);
        TintWidget(g_stopButton, kDanger);

        // Narrow-aspect access: side columns collapse into explicit drawers.
        g_rosterDrawerButton = MakeButton(
            client, 0.05f, 0.018f, 0.14f, 0.052f,
            "PG_RosterDrawer", "Squad Roster", OnToggleRosterDrawer);
        g_settingsDrawerButton = MakeButton(
            client, 0.80f, 0.018f, 0.14f, 0.052f,
            "PG_SettingsDrawer", "Match Settings", OnToggleSettingsDrawer);
        g_rosterDrawerButton->setVisible(false);
        g_settingsDrawerButton->setVisible(false);

        g_rosterColumnWidgets.clear();
        g_rosterColumnWidgets.push_back(rosterHeading);
        g_rosterColumnWidgets.push_back(g_rosterCount);
        g_rosterColumnWidgets.push_back(searchLabel);
        g_rosterColumnWidgets.push_back(g_rosterSearchSurface);
        g_rosterColumnWidgets.push_back(g_rosterSearch);
        g_rosterColumnWidgets.push_back(g_filterAll);
        g_rosterColumnWidgets.push_back(g_filterReady);
        g_rosterColumnWidgets.push_back(g_filterAssigned);
        g_rosterColumnWidgets.push_back(g_filterUnavailable);
        g_rosterColumnWidgets.push_back(lblRosterHint);
        g_rosterColumnWidgets.push_back(g_rosterScroll);
        g_rosterColumnWidgets.push_back(g_rosterScrollUp);
        g_rosterColumnWidgets.push_back(g_rosterScrollDown);

        g_settingsColumnWidgets.clear();
        g_settingsColumnWidgets.push_back(selectedHeading);
        g_settingsColumnWidgets.push_back(g_previewImage);
        g_settingsColumnWidgets.push_back(g_previewName);
        g_settingsColumnWidgets.push_back(g_previewMeta);
        g_settingsColumnWidgets.push_back(g_previewStats);
        g_settingsColumnWidgets.push_back(g_btnRemove);
        g_settingsColumnWidgets.push_back(settingsHeading);
        g_settingsColumnWidgets.push_back(locationLabel);
        g_settingsColumnWidgets.push_back(g_locArena);
        g_settingsColumnWidgets.push_back(g_locBanner);
        g_settingsColumnWidgets.push_back(g_koEliminationButton);
        g_settingsColumnWidgets.push_back(summaryHeading);
        g_settingsColumnWidgets.push_back(g_summary);
        g_settingsColumnWidgets.push_back(g_status);
        g_settingsColumnWidgets.push_back(g_startButton);
        g_settingsColumnWidgets.push_back(g_stopButton);

        UpdateModeVisibility();
        UpdateLocationHighlight();
        UpdateRosterFilterHighlight();
        RefreshAll();
        RefreshResponsiveMode();
        DebugLog("Proving Grounds: Arena UI created with custom background");
    }

    void ShowWindow()
    {
        if (!g_window)
            CreatePanel();
        if (g_window)
        {
            if (!g_arenaTop)
                InstallArenaTopBar(g_window);
            // Re-apply in case TitleScreen created the panel before GUI paths were ready.
            ApplyArenaChrome();
            g_window->setVisible(true);
        }
    }

    void HidePanel()
    {
        if (!g_window || !g_window->getVisible())
            return;
        g_window->setVisible(false);
    }

    void TogglePanel()
    {
        if (g_window && g_window->getVisible())
        {
            g_window->setVisible(false);
            return;
        }

        ShowWindow();
        if (g_window)
            RefreshAll();
    }

    void OnWindowButtonPressed(MyGUI::Widget* /*sender*/, const std::string& name)
    {
        // Kenshi_WindowCX close chrome uses MyGUI's "Close" button name.
        if (name == "close" || name == "Close")
            HidePanel();
    }

    void PollF8Toggle()
    {
        if (!kF8OpensDebugMenu && !kF8OpensArenaUi)
        {
            g_f8WasDown = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            return;
        }
        const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (down && !g_f8WasDown)
        {
            if (kF8OpensDebugMenu)
                DebugMenu::Toggle();
            else if (kF8OpensArenaUi)
                TogglePanel();
        }
        g_f8WasDown = down;
    }

    bool PollEscClose()
    {
        const bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool consumed = false;
        if (down && !g_escWasDown)
        {
            if (DebugMenu::IsVisible())
            {
                DebugMenu::Close();
                consumed = true;
            }
            else if (LeaderboardUI::IsVisible())
            {
                LeaderboardUI::Close();
                consumed = true;
            }
            else if (ResultsUI::IsVisible())
            {
                ResultsUI::Close();
                consumed = true;
            }
            else if (g_window && g_window->getVisible())
            {
                HidePanel();
                consumed = true;
            }
        }
        g_escWasDown = down;
        if (consumed && key)
        {
            key->escape = false;
            key->escape_msg = false;
        }
        return consumed;
    }

    TitleScreen* TitleScreen_hook(TitleScreen* thisptr)
    {
        TitleScreen* title = TitleScreen_orig(thisptr);
        CreatePanel();
        DebugMenu::Create();
        return title;
    }

    MyGUI::IntPoint ScrollOffset(MyGUI::ScrollView* scroll)
    {
        if (!scroll)
            return MyGUI::IntPoint();
        const ListKind kind = ListKindForScroll(scroll);
        return MyGUI::IntPoint(
            0, g_listScrollOffset[static_cast<int>(kind)]);
    }

    bool SamePoint(const MyGUI::IntPoint& a, const MyGUI::IntPoint& b)
    {
        return a.left == b.left && a.top == b.top;
    }

    void RouteArenaWheelFallback(
        int wheel,
        const MyGUI::IntPoint& rosterBefore,
        const MyGUI::IntPoint& teamABefore,
        const MyGUI::IntPoint& teamBBefore,
        const MyGUI::IntPoint& poolBefore)
    {
        if (wheel == 0 || !g_window || !g_window->getVisible())
            return;

        const MyGUI::IntPoint mouse =
            MyGUI::InputManager::getInstance().getMousePositionByLayer();
        MyGUI::ScrollView* scroll = NULL;
        MyGUI::IntPoint before;
        if (PointInside(g_rosterScroll, mouse))
        {
            scroll = g_rosterScroll;
            before = rosterBefore;
        }
        else if (PointInside(g_teamAScroll, mouse))
        {
            scroll = g_teamAScroll;
            before = teamABefore;
        }
        else if (PointInside(g_teamBScroll, mouse))
        {
            scroll = g_teamBScroll;
            before = teamBBefore;
        }
        else if (PointInside(g_poolScroll, mouse))
        {
            scroll = g_poolScroll;
            before = poolBefore;
        }

        if (!scroll)
            return;

        MyGUI::IntPoint offset = ScrollOffset(scroll);
        if (!SamePoint(offset, before))
            return; // Native MyGUI routing already handled this wheel event.

        int magnitude = wheel < 0 ? -wheel : wheel;
        int notches = magnitude >= 120 ? magnitude / 120 : 1;
        int pixels = 50 * notches;
        ScrollListBy(scroll, wheel > 0 ? pixels : -pixels);
        if (scroll == g_rosterScroll)
            UpdateScrollButtonsForKind(ListRoster);
        else if (scroll == g_teamAScroll)
            UpdateScrollButtonsForKind(ListTeamA);
        else if (scroll == g_teamBScroll)
            UpdateScrollButtonsForKind(ListTeamB);
        else
            UpdateScrollButtonsForKind(ListPool);
    }

    void ForgottenGUI_update_hook(ForgottenGUI* thisptr)
    {
        const int wheel = key ? key->mWheel : 0;
        const MyGUI::IntPoint rosterBefore = ScrollOffset(g_rosterScroll);
        const MyGUI::IntPoint teamABefore = ScrollOffset(g_teamAScroll);
        const MyGUI::IntPoint teamBBefore = ScrollOffset(g_teamBScroll);
        const MyGUI::IntPoint poolBefore = ScrollOffset(g_poolScroll);
        const bool consumedEsc = PollEscClose();

        ForgottenGUI_update_orig(thisptr);
        if (consumedEsc && key)
        {
            key->escape = false;
            key->escape_msg = false;
        }
        RouteArenaWheelFallback(
            wheel, rosterBefore, teamABefore, teamBBefore, poolBefore);
        ContextMenuHooks::RefreshArenaCursor(thisptr);
        PollF8Toggle();
        ArenaIngress::Tick();
        PrisonerUtil::ProtectMatchPrisoners();
        PrisonerUtil::TickReturn();
        KOWatcher::Tick();
        ResultsUI::Tick();
        LeaderboardUI::Tick();
        if (g_window && g_window->getVisible())
        {
            RefreshResponsiveMode();
            RefreshStatusAndButtons();
            RefreshPointerHighlights();
        }
    }
}

namespace ArenaUI
{
    void ShowFromRegistry(RootObject* registry)
    {
        if (ArenaIngress::IsPending())
        {
            ShowWindow();
            RefreshAll();
            if (g_status)
                g_status->setCaption("Walk-in already in progress");
            return;
        }

        ArenaIngress::BindRegistry(registry);
        ShowWindow();
        RefreshAll();
        if (g_status)
            g_status->setCaption(ArenaIngress::GetStatus().c_str());
    }

    bool InstallHooks()
    {
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&TitleScreen::_CONSTRUCTOR),
                TitleScreen_hook,
                &TitleScreen_orig))
        {
            ErrorLog("Proving Grounds: failed to hook TitleScreen constructor");
            return false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::update),
                ForgottenGUI_update_hook,
                &ForgottenGUI_update_orig))
        {
            ErrorLog("Proving Grounds: failed to hook ForgottenGUI::update");
            return false;
        }

        DebugLog("Proving Grounds: ArenaUI hooks installed");
        return true;
    }
}
