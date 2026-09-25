#include "ArenaUI.h"
#include "ArenaRingGuard.h"
#include "WorldLifecycle.h"
#include "NativeUI.h"
#include "MarksHUD.h"

#include "LeaderboardLayout.h"
#include "TownArena.h"
#include "TownArenaUI.h"
#include "TownBookie.h"
#include "ArenaIdentity.h"
#include "ArenaIngress.h"
#include "ArenaMedical.h"
#include "ContextMenuHooks.h"
#include "DebugMenu.h"
#include "PGConfig.h"
#include "PGSettingsUI.h"
#include "FrameCadencePolicy.h"
#include "KOWatcher.h"
#include "LeaderboardUI.h"
#include "LeaderboardStore.h"
#include "MatchRules.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerRecruitmentUI.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "RewardsUI.h"
#include "RosterStatus.h"
#include "SparSession.h"
#include "SquadUtil.h"

#include "PGLog.h"
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Globals.h>
#include <kenshi/InputHandler.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/OptionsWindow.h>
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
#include <mygui/MyGUI_WidgetToolTip.h>
#include <mygui/MyGUI_Window.h>

#include <ogre/OgreVector3.h>

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <string>
#include <stdexcept>
#include <vector>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    enum UiMode { UiAvB, UiTeams1v1, UiLastStanding };
    enum AssignDest { DestTeamA, DestTeamB };
    enum ListKind { ListRoster, ListTeamA, ListTeamB, ListPool };
    enum RosterFilter { FilterAll };

    struct PortraitRow
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* image;
        MyGUI::Widget* handlerFrame;
        MyGUI::ImageBox* handlerImage;
        MyGUI::TextBox* label;
        MyGUI::Widget* statusBack;
        MyGUI::Widget* statusFill;
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
        ArenaMedical::Protocol medicalProtocol;
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
    void (*OptionsWindow_update_orig)(OptionsWindow*) = NULL;

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_titleHero = NULL;

    MyGUI::ScrollView* g_rosterScroll = NULL;
    MyGUI::ScrollView* g_teamAScroll = NULL;
    MyGUI::ScrollView* g_teamBScroll = NULL;
    MyGUI::ScrollView* g_poolScroll = NULL;
    MyGUI::EditBox* g_rosterSearch = NULL;
    MyGUI::TextBox* g_rosterCount = NULL;

    MyGUI::Button* g_modeAvB = NULL;
    MyGUI::Button* g_modeTeams1v1 = NULL;
    MyGUI::Button* g_modeLast = NULL;
    MyGUI::Button* g_locArena = NULL;
    MyGUI::Button* g_locSmallArena = NULL;
    MyGUI::Button* g_locBanner = NULL;
    MyGUI::Button* g_medicalProtocolButton = NULL;
    MyGUI::EditBox* g_settingsTip = NULL;
    MyGUI::Widget* g_settingsTipOwner = NULL;
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
    MyGUI::Button* g_btnRecruitment = NULL;
    MyGUI::Button* g_btnSwapTeams = NULL;
    MyGUI::Button* g_presetButtons[3] = { NULL, NULL, NULL };

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

    UiMode g_uiMode = UiAvB;
    ArenaMedical::Protocol g_medicalProtocol = ArenaMedical::RingsideAid;
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
    const float kIdleUiRefreshIntervalSec = 0.20f;
    DWORD g_uiRefreshLastTick = 0;
    float g_uiRefreshElapsedSec = kIdleUiRefreshIntervalSec;

    float AdvanceUiRefreshClock()
    {
        const DWORD now = GetTickCount();
        const float dt = g_uiRefreshLastTick == 0 ? 0.0f :
            static_cast<float>(now - g_uiRefreshLastTick) / 1000.0f;
        g_uiRefreshLastTick = now;
        return dt > 1.0f ? 1.0f : dt;
    }

    void ResetUiRefreshClock()
    {
        g_uiRefreshLastTick = 0;
        g_uiRefreshElapsedSec = kIdleUiRefreshIntervalSec;
    }

    const int kRowH = 56;
    const int kPortraitSize = 48;
    const int kHandlerPortraitSize = 22;
    const int kStatusBarHeight = 6;
    const float kMaxFighterDistanceFromRegistry = 2500.0f;
    MyGUI::ScrollView* g_columns[3] = { NULL, NULL, NULL };
    MyGUI::Widget* g_columnContent[3] = { NULL, NULL, NULL };
    MyGUI::TextBox* g_metrics = NULL;
    MyGUI::IntSize g_viewSize;
    bool g_layoutReady = false;
    struct ButtonPresentation
    {
        MyGUI::Button* button;
        MyGUI::EditBox* text;
        std::string caption;
        int naturalWidth;
    };
    std::vector<ButtonPresentation> g_buttonPresentation;
    void LayoutPanel();

    void Passive(MyGUI::Widget* widget)
    {
        if (!widget) return;
        widget->setNeedMouseFocus(false);
        widget->setNeedKeyFocus(false);
        for (size_t i = 0; i < widget->getChildCount(); ++i)
            Passive(widget->getChildAt(i));
        MyGUI::Widget* client = widget->getClientWidget();
        if (client && client != widget) Passive(client);
    }

    MyGUI::Colour StatusColour(RosterStatusPolicy::Band band)
    {
        if (band == RosterStatusPolicy::Green) return MyGUI::Colour(.18f, .72f, .28f);
        if (band == RosterStatusPolicy::Orange) return MyGUI::Colour(.95f, .52f, .12f);
        return MyGUI::Colour(.78f, .16f, .12f);
    }

    void RefreshStatusBar(MyGUI::Widget* back, MyGUI::Widget* fill, Character* character)
    {
        if (!back || !fill) return;
        RosterStatus::Snapshot status = {};
        const bool visible = RosterStatus::Read(character, status);
        back->setVisible(visible);
        if (!visible) return;
        const int width = static_cast<int>(back->getWidth() * status.recovery + .5f);
        fill->setVisible(width > 0);
        fill->setCoord(0, 0, width, back->getHeight());
        fill->setColour(StatusColour(status.band));
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
        PortraitRow row = {};
        row.character = c;
        row.kind = kind;
        if (!scroll || !c) return row;
        char name[64], tag[32];
        sprintf_s(name, "PG_Row_%d_%d", static_cast<int>(kind), index);
        row.root = NativeUI::Button(scroll, MyGUI::IntCoord(0, 0, width, kRowH), name, "");
        row.root->eventMouseButtonClick += MyGUI::newDelegate(OnPortraitRowClick);
        row.root->eventMouseButtonDoubleClick += MyGUI::newDelegate(OnPortraitRowDoubleClick);
        row.root->eventMouseWheel += MyGUI::newDelegate(OnPortraitRowWheel);
        row.root->eventMouseDrag += MyGUI::newDelegate(OnPortraitRowDrag);
        row.root->eventMouseButtonReleased += MyGUI::newDelegate(OnPortraitRowRelease);
        sprintf_s(tag, "%d:%d", static_cast<int>(kind), index);
        row.root->setUserString("pg", tag);
        row.image = row.root->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(4, 4, kPortraitSize, kPortraitSize), MyGUI::Align::Default);
        Passive(row.image);
        BindPortrait(row.image, c);
        row.label = row.root->createWidget<MyGUI::EditBox>("Kenshi_PaintedWordWrapEmpty",
            MyGUI::IntCoord(60, 4, (std::max)(1, width - 68), 40), MyGUI::Align::Default);
        std::string caption = c->getName();
        if (kind == ListRoster && (IsAssigned(c) || !IsAssignable(c)))
            caption += "\n" + FighterDetailLabel(c);
        else if (kind != ListRoster && !FighterUnavailableReason(c).empty())
            caption += "\nUnavailable - " + FighterUnavailableReason(c);
        row.label->setCaption(caption);
        Passive(row.label);
        if (kind == ListRoster)
        {
            row.statusBack = row.root->createWidget<MyGUI::Widget>("WhiteSkin",
                MyGUI::IntCoord(60, 46, (std::max)(1, width - 68), kStatusBarHeight), MyGUI::Align::Default);
            row.statusBack->setColour(MyGUI::Colour(.12f, .12f, .12f));
            row.statusFill = row.statusBack->createWidget<MyGUI::Widget>("WhiteSkin",
                MyGUI::IntCoord(0, 0, 1, kStatusBarHeight), MyGUI::Align::Default);
            Passive(row.statusBack);
            RefreshStatusBar(row.statusBack, row.statusFill, c);
        }
        Character* handler = kind != ListRoster && PrisonerUtil::IsRosterPrisoner(c) &&
            !c->isUnconcious() ? FindHandlerAssignment(c) : NULL;
        if (handler)
        {
            row.handlerImage = row.root->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(4, 4, kHandlerPortraitSize, kHandlerPortraitSize), MyGUI::Align::Default);
            Passive(row.handlerImage);
            BindPortrait(row.handlerImage, handler);
            row.handlerLabel = row.root->createWidget<MyGUI::EditBox>("Kenshi_PaintedWordWrapEmpty",
                MyGUI::IntCoord(32, 4, (std::max)(1, width - 40), 24), MyGUI::Align::Default);
            row.handlerLabel->setCaption("Handler: " + handler->getName());
            Passive(row.handlerLabel);
        }
        static_cast<MyGUI::Button*>(row.root)->setStateSelected(c == g_previewChar);
        return row;
    }

    void LayoutRows(MyGUI::ScrollView* scroll, std::vector<PortraitRow>& rows)
    {
        if (!scroll || !g_metrics) return;
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = (std::max)(1, g_metrics->getFontHeight());
        const int offset = -scroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const int width = (std::max)(1, scroll->getViewCoord().width);
            int y = 0;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                PortraitRow& row = rows[i];
                const int textX = kPortraitSize + 2 * gap;
                row.label->setCoord(textX, gap, (std::max)(1, width - textX - gap), body);
                const int labelH = (std::max)(body, row.label->getTextSize().height);
                row.label->setSize(row.label->getWidth(), labelH);
                row.image->setPosition(gap, gap);
                int textH = labelH;
                if (row.statusBack)
                {
                    row.statusBack->setCoord(textX, gap + labelH + gap,
                        (std::max)(1, width - textX - gap), kStatusBarHeight);
                    RefreshStatusBar(row.statusBack, row.statusFill, row.character);
                    textH += gap + kStatusBarHeight;
                }
                int height = (std::max)(kPortraitSize, textH) + 2 * gap;
                if (row.handlerLabel)
                {
                    const int hx = kHandlerPortraitSize + 2 * gap;
                    row.handlerLabel->setCoord(hx, height, (std::max)(1, width - hx - gap), body);
                    const int handlerH = (std::max)(body, row.handlerLabel->getTextSize().height);
                    row.handlerLabel->setSize(row.handlerLabel->getWidth(), handlerH);
                    row.handlerImage->setPosition(gap, height);
                    height += (std::max)(kHandlerPortraitSize, handlerH) + gap;
                }
                row.root->setCoord(0, y, width, height);
                y += height + gap;
            }
            scroll->setCanvasSize(width, (std::max)(y, scroll->getViewCoord().height));
        }
        scroll->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(offset,
            scroll->getCanvasSize().height, scroll->getViewCoord().height)));
    }

    void RebuildList(MyGUI::ScrollView* scroll, std::vector<PortraitRow>& rows,
        const std::vector<Character*>& chars, ListKind kind)
    {
        if (!scroll) return;
        const int offset = -scroll->getViewOffset().top;
        ClearScrollChildren(scroll);
        rows.clear();
        const int width = (std::max)(1, scroll->getViewCoord().width);
        if (chars.empty())
        {
            MyGUI::EditBox* empty = NativeUI::WrappedLabel(scroll,
                MyGUI::IntCoord(8, 8, (std::max)(1, width - 16), 80), "PG_EmptyList",
                kind == ListRoster ? "No fighters match this filter." : "Drop fighters here");
            empty->setSize(empty->getWidth(), (std::max)(24, empty->getTextSize().height));
            Passive(empty);
        }
        for (size_t i = 0; i < chars.size(); ++i)
            if (chars[i]) rows.push_back(MakeRow(scroll, chars[i], kind, static_cast<int>(rows.size()), width));
        LayoutRows(scroll, rows);
        scroll->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(offset,
            scroll->getCanvasSize().height, scroll->getViewCoord().height)));

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
        if (!widget) return false;
        // Intersect every ancestor: native scroll canvases can extend outside
        // their viewport, and hidden compact columns are not drop targets.
        for (MyGUI::Widget* current = widget; current; current = current->getParent())
        {
            if (!current->getVisible()) return false;
            const MyGUI::IntCoord coord = current->getAbsoluteCoord();
            if (point.left < coord.left || point.left >= coord.right() ||
                point.top < coord.top || point.top >= coord.bottom()) return false;
        }
        return true;
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
        if (row.root) static_cast<MyGUI::Button*>(row.root)->setStateSelected(row.character == g_previewChar);
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
        if (!scroll) return;
        const int offset = LeaderboardLayout::ClampOffset(-scroll->getViewOffset().top - pixels,
            scroll->getCanvasSize().height, scroll->getViewCoord().height);
        scroll->setViewOffset(MyGUI::IntPoint(0, -offset));

    }

    void UpdateScrollButtonsForKind(ListKind kind)
    {
        MyGUI::ScrollView* scroll = ScrollForListKind(kind);
        if (scroll) scroll->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(
            -scroll->getViewOffset().top, scroll->getCanvasSize().height, scroll->getViewCoord().height)));
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
        MyGUI::TextBox* labels[] = {g_lblTeamA, g_lblTeamB, g_lblPool};
        for (int i = 0; i < 3; ++i)
        {
            if (!labels[i]) continue;
            const std::string caption = labels[i]->getCaption().asUTF8();
            const size_t line = caption.find('\n');
            if (line != std::string::npos) labels[i]->setCaption(caption.substr(0, line));
        }
    }

    void ApplyDragHighlights(Character* c, const MyGUI::IntPoint& point)
    {
        const bool valid = IsAssignable(c) && !SparSession::IsActive() &&
            !ArenaIngress::IsPending() && !PrisonerUtil::IsReturning();
        ResetDropZoneVisuals();
        RefreshRowStates();
        PortraitRow* target = FindAssignedPrisonerRowAtPoint(point);
        if (target)
        {
            const bool handler = valid && IsHandlerCandidate(c);
            static_cast<MyGUI::Button*>(target->root)->setStateSelected(handler);
            if (g_status) g_status->setCaption(handler ? "Release to assign handler." : "Cannot assign this handler.");
            return;
        }
        MyGUI::TextBox* heading = NULL;
        if (IsTeamUiMode() && PointInside(g_teamAScroll, point)) heading = g_lblTeamA;
        else if (IsTeamUiMode() && PointInside(g_teamBScroll, point)) heading = g_lblTeamB;
        else if (!IsTeamUiMode() && PointInside(g_poolScroll, point)) heading = g_lblPool;
        if (heading) heading->setCaption(heading->getCaption() + (valid ? "\nRelease to assign" : "\nCannot assign"));
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
    void UpdateMedicalProtocolButton();
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
        if (g_lblTeamA)
            g_lblTeamA->setVisible(teamMode);
        if (g_lblTeamB)
            g_lblTeamB->setVisible(teamMode);
        if (g_btnDestA)
        {
            g_btnDestA->setVisible(true);
            g_btnDestA->setCaption(teamMode ? "Assign to Team A" : "Add Selected Fighter");

        }
        if (g_btnDestB)
            g_btnDestB->setVisible(teamMode);
        UpdateBalanceMenuVisibility();
        if (g_lblDest)
            g_lblDest->setVisible(true);

        const bool poolMode = !teamMode;
        if (g_poolScroll)
            g_poolScroll->setVisible(poolMode);
        if (g_lblPool)
            g_lblPool->setVisible(poolMode);

        if (g_btnRemove)
            g_btnRemove->setVisible(true);

        if (g_modeAvB) g_modeAvB->setStateSelected(g_uiMode == UiAvB);
        if (g_modeTeams1v1) g_modeTeams1v1->setStateSelected(g_uiMode == UiTeams1v1);
        if (g_modeLast) g_modeLast->setStateSelected(g_uiMode == UiLastStanding);
        UpdateLocationHighlight();
        UpdateDestHighlight();
    }

    void UpdateLocationHighlight()
    {
        const ArenaIngress::LocationMode location = ArenaIngress::GetLocationMode();
        if (g_locArena) g_locArena->setStateSelected(location == ArenaIngress::LocationArena);
        if (g_locSmallArena) g_locSmallArena->setStateSelected(location == ArenaIngress::LocationSmallArena);
        if (g_locBanner) g_locBanner->setStateSelected(location == ArenaIngress::LocationBanner);
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

    void OnLocationSmallArena(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending() || SparSession::IsActive())
            return;
        ArenaIngress::SetLocationMode(ArenaIngress::LocationSmallArena);
        UpdateLocationHighlight();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    const char* GetMedicalProtocolTip()
    {
        switch (g_medicalProtocol)
        {
        case ArenaMedical::Bloodsport:
            return "No treatment until the match ends. No auto prisoner aid.";
        case ArenaMedical::RingsideAid:
        default:
            return "No aid for active fighters; OK once eliminated. Prisoners get stabilized.";
        }
    }

    void HideSettingsTip()
    {
        g_settingsTipOwner = NULL;
        if (g_settingsTip)
            g_settingsTip->setVisible(false);
    }

    void PlaceSettingsTip(const MyGUI::IntPoint& absolutePoint)
    {
        if (!g_settingsTip || !g_window)
            return;

        const MyGUI::IntPoint windowPos = g_window->getClientWidget()->getAbsolutePosition();
        const MyGUI::IntSize tipSize = g_settingsTip->getSize();
        const MyGUI::IntSize windowSize = g_window->getClientWidget()->getSize();

        int left = absolutePoint.left - windowPos.left + 14;
        int top = absolutePoint.top - windowPos.top + 18;
        if (left + tipSize.width > windowSize.width - 8)
            left = absolutePoint.left - windowPos.left - tipSize.width - 8;
        if (top + tipSize.height > windowSize.height - 8)
            top = absolutePoint.top - windowPos.top - tipSize.height - 8;
        if (left < 8)
            left = 8;
        if (top < 8)
            top = 8;

        g_settingsTip->setPosition(left, top);
    }

    void ShowSettingsTip(MyGUI::Widget* owner, const MyGUI::IntPoint& absolutePoint)
    {
        if (!g_settingsTip || !owner)
            return;

        const std::string& tip = owner->getUserString("tip");
        if (tip.empty())
        {
            HideSettingsTip();
            return;
        }

        g_settingsTipOwner = owner;
        g_settingsTip->setCaption(tip);
        const int gap = NativeUI::Spacing(g_metrics);
        const MyGUI::IntSize clientSize = g_window->getClientWidget()->getSize();
        const int width = (std::min)(clientSize.width - 2 * gap, g_metrics->getFontHeight() * 28);
        g_settingsTip->setSize((std::max)(1, width), g_metrics->getFontHeight() * 3);
        g_settingsTip->setSize((std::max)(1, width), (std::min)(clientSize.height - 2 * gap,
            g_settingsTip->getTextSize().height + 3 * gap));
        PlaceSettingsTip(absolutePoint);
        g_settingsTip->setVisible(true);
        g_settingsTip->upLayerItem();
    }

    void OnSettingsToolTip(MyGUI::Widget* sender, const MyGUI::ToolTipInfo& info)
    {
        if (info.type == MyGUI::ToolTipInfo::Hide)
        {
            if (g_settingsTipOwner == sender)
                HideSettingsTip();
            return;
        }

        if (info.type == MyGUI::ToolTipInfo::Show)
        {
            ShowSettingsTip(sender, info.point);
            return;
        }

        if (info.type == MyGUI::ToolTipInfo::Move &&
            g_settingsTip &&
            g_settingsTip->getVisible() &&
            g_settingsTipOwner == sender)
        {
            PlaceSettingsTip(info.point);
        }
    }

    void BindSettingsToolTip(MyGUI::Button* button)
    {
        if (!button)
            return;
        button->setNeedToolTip(true);
        button->eventToolTip += MyGUI::newDelegate(OnSettingsToolTip);
    }

    void RefreshSettingsTipIfShowing(MyGUI::Widget* owner)
    {
        if (!g_settingsTip ||
            !g_settingsTip->getVisible() ||
            g_settingsTipOwner != owner ||
            !owner)
        {
            return;
        }

        const std::string& tip = owner->getUserString("tip");
        if (tip.empty())
        {
            HideSettingsTip();
            return;
        }

        g_settingsTip->setCaption(tip);
        const int gap = NativeUI::Spacing(g_metrics);
        const MyGUI::IntSize clientSize = g_window->getClientWidget()->getSize();
        const int width = (std::min)(clientSize.width - 2 * gap, g_metrics->getFontHeight() * 28);
        g_settingsTip->setSize((std::max)(1, width), g_metrics->getFontHeight() * 3);
        g_settingsTip->setSize((std::max)(1, width), (std::min)(clientSize.height - 2 * gap,
            g_settingsTip->getTextSize().height + 3 * gap));
    }

    void UpdateMedicalProtocolButton()
    {
        if (!g_medicalProtocolButton)
            return;

        std::string caption = "Medical: ";
        caption += ArenaMedical::GetProtocolName(g_medicalProtocol);
        g_medicalProtocolButton->setCaption(caption);

        g_medicalProtocolButton->setUserString("tip", GetMedicalProtocolTip());
        RefreshSettingsTipIfShowing(g_medicalProtocolButton);
    }

    void OnMedicalProtocolToggle(MyGUI::Widget* /*sender*/)
    {
        if (ArenaIngress::IsPending() || SparSession::IsActive())
            return;

        g_medicalProtocol = ArenaMedical::NextProtocol(g_medicalProtocol);
        ArenaMedical::SetProtocol(g_medicalProtocol);
        UpdateMedicalProtocolButton();
        RefreshSummary();
        RefreshStatusAndButtons();
    }

    void OnRecruitPrisoners(MyGUI::Widget* /*sender*/)
    {
        if (g_window)
        {
            HideSettingsTip();
            g_window->setVisible(false);
        }
        PrisonerRecruitmentUI::Show();
    }

    void UpdateDestHighlight()
    {
        if (g_btnDestA) g_btnDestA->setStateSelected(!IsTeamUiMode() || g_dest == DestTeamA);
        if (g_btnDestB) g_btnDestB->setStateSelected(IsTeamUiMode() && g_dest == DestTeamB);
        if (g_lblDest) g_lblDest->setCaption(!IsTeamUiMode() ? "Double-click adds to the pool" :
            g_dest == DestTeamA ? "Double-click assigns to Team A" : "Double-click assigns to Team B");
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
        if (g_rosterScroll) g_rosterScroll->setViewOffset(MyGUI::IntPoint());
        RefreshRosterList();
    }

    void OnRosterSearchFocus(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        if (!sender || !g_rosterSearch || !g_searchPlaceholderActive)
            return;
        g_searchPlaceholderActive = false;
        g_rosterSearch->setOnlyText("");

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

            return;
        }
        g_searchPlaceholderActive = true;
        g_rosterQuery.clear();
        if (g_rosterScroll) g_rosterScroll->setViewOffset(MyGUI::IntPoint());
        g_rosterSearch->setOnlyText("Search fighters...");

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
        RefreshStatusBar(row.statusBack, row.statusFill, row.character);

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

    void RefreshRosterStatusBars()
    {
        for (size_t i = 0; i < g_rosterRows.size(); ++i)
            RefreshStatusBar(g_rosterRows[i].statusBack, g_rosterRows[i].statusFill,
                g_rosterRows[i].character);
    }

    void RefreshSummary()
    {
        if (!g_summary)
            return;

        const char* location = "Banner";
        if (ArenaIngress::GetLocationMode() == ArenaIngress::LocationArena)
            location = "Arena";
        else if (ArenaIngress::GetLocationMode() == ArenaIngress::LocationSmallArena)
            location = "Small Arena";
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
        if (TownArena::IsBusy() || TownArena::HasBooking())
            return false;
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
        if (PrisonerUtil::IsReturning())
            return "Returning prisoners to their cages...";
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
        const bool canStart = edit && CanStart();

        if (active)
            RefreshRosterStatusBars();
        else
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
        if (g_status && (TownArena::IsBusy() || TownArena::HasBooking()))
            g_status->setCaption("Arena reserved by a town event.");

        if (g_startButton)
        {
            g_startButton->setVisible(!active && !pending && !TownArena::IsBusy());
            g_startButton->setEnabled(canStart);

        }
        if (g_stopButton)
        {
            g_stopButton->setVisible(active || pending || TownArena::IsBusy());
            g_stopButton->setEnabled(active || pending || TownArena::IsBusy());
            g_stopButton->setCaption(pending ? "Cancel Match Start" : "Stop Fight");

        }

        if (g_modeAvB)
            g_modeAvB->setEnabled(edit);
        if (g_modeTeams1v1)
            g_modeTeams1v1->setEnabled(edit);
        if (g_modeLast)
            g_modeLast->setEnabled(edit);
        if (g_locArena)
            g_locArena->setEnabled(edit);
        if (g_locSmallArena)
            g_locSmallArena->setEnabled(edit);
        if (g_locBanner)
            g_locBanner->setEnabled(edit);
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

        RefreshCompactDrawers();
    }

    void RefreshAll()
    {
        if (!g_window) return;
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
        preset.medicalProtocol = g_medicalProtocol;
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
        g_medicalProtocol = preset.medicalProtocol;
        g_teamA = preset.teamA;
        g_teamB = preset.teamB;
        g_pool = preset.pool;
        RemoveMissingPresetFighters(g_teamA);
        RemoveMissingPresetFighters(g_teamB);
        RemoveMissingPresetFighters(g_pool);
        ArenaIngress::SetLocationMode(preset.location);
        ArenaMedical::SetProtocol(g_medicalProtocol);
        g_medicalProtocol = ArenaMedical::GetProtocol();
        SetPreview(NULL);
        UpdateModeVisibility();
        UpdateLocationHighlight();
        UpdateMedicalProtocolButton();
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
        ArenaMedical::SetProtocol(g_medicalProtocol);

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
            {
                HideSettingsTip();
                g_window->setVisible(false);
            }
        }
        else
            RefreshStatusAndButtons();
    }

    void OnStop(MyGUI::Widget* /*sender*/)
    {
        if (TownArena::IsBusy())
        {
            TownArena::Cancel();
            RefreshStatusAndButtons();
            return;
        }
        if (ArenaIngress::IsPending())
            ArenaIngress::Cancel();
        SparSession::Stop(SparSession::StopManual);
        RefreshStatusAndButtons();
    }

    void RefreshCompactDrawers()
    {
        if (!g_layoutReady) return;
        g_rosterDrawerButton->setVisible(g_compactMode);
        g_settingsDrawerButton->setVisible(g_compactMode);
        g_rosterDrawerButton->setStateSelected(g_rosterDrawerOpen);
        g_settingsDrawerButton->setStateSelected(g_settingsDrawerOpen);
        g_columns[0]->setVisible(!g_compactMode || g_rosterDrawerOpen);
        g_columns[2]->setVisible(!g_compactMode || g_settingsDrawerOpen);
        LayoutPanel();
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
        if (!g_layoutReady) return;
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (render && render->getViewSize() != g_viewSize)
        {
            g_viewSize = render->getViewSize();
            g_window->setCoord(g_viewSize.width / 25, g_viewSize.height / 25,
                g_viewSize.width * 23 / 25, g_viewSize.height * 23 / 25);
        }
        const bool compact = g_window->getClientWidget()->getWidth() < g_metrics->getFontHeight() * 76;
        if (compact != g_compactMode)
        {
            g_compactMode = compact;
            g_rosterDrawerOpen = false;
            g_settingsDrawerOpen = false;
        }
        RefreshCompactDrawers();
    }

    MyGUI::TextBox* PanelLabel(MyGUI::Widget* parent, const char* name, const char* caption)
    {
        return NativeUI::WrappedLabel(parent, MyGUI::IntCoord(0, 0, 200, 24), name, caption);
    }

    MyGUI::Button* PanelButton(MyGUI::Widget* parent, const char* name,
        const char* caption, void (*handler)(MyGUI::Widget*))
    {
        MyGUI::Button* result = NativeUI::Button(parent, MyGUI::IntCoord(0, 0, 200, 32), name, caption);
        result->eventMouseButtonClick += MyGUI::newDelegate(handler);
        ButtonPresentation presentation;
        presentation.button = result;
        presentation.text = result->createWidget<MyGUI::EditBox>("Kenshi_PaintedWordWrapEmpty",
            MyGUI::IntCoord(0, 0, 200, 32), MyGUI::Align::Default);
        presentation.caption = caption;
        presentation.naturalWidth = result->getTextSize().width;
        presentation.text->setCaption(caption);
        Passive(presentation.text);
        result->setCaption("");
        g_buttonPresentation.push_back(presentation);
        return result;
    }

    ButtonPresentation& ButtonText(MyGUI::Button* button)
    {
        for (size_t i = 0; i < g_buttonPresentation.size(); ++i)
            if (g_buttonPresentation[i].button == button) return g_buttonPresentation[i];
        throw std::runtime_error("missing arena button presentation");
    }

    void SyncButtonText()
    {
        for (size_t i = 0; i < g_buttonPresentation.size(); ++i)
        {
            ButtonPresentation& item = g_buttonPresentation[i];
            const std::string caption = item.button->getCaption().asUTF8();
            if (caption.empty()) continue;
            item.caption = caption;
            item.naturalWidth = item.button->getTextSize().width;
            item.text->setCaption(caption);
            item.button->setCaption("");
        }
    }

    int PlaceLabel(MyGUI::TextBox* label, int x, int y, int width)
    {
        const int body = (std::max)(1, g_metrics->getFontHeight());
        label->setCoord(x, y, (std::max)(1, width), body);
        const int height = (std::max)(body, label->getTextSize().height);
        label->setSize(label->getWidth(), height);
        return height;
    }

    MyGUI::TextBox* Heading(int column, const char* name)
    {
        return g_columnContent[column]->findWidget(name)->castType<MyGUI::TextBox>();
    }

    int PlaceButtons(MyGUI::Button** buttons, int count, int x, int y, int width)
    {
        const int gap = NativeUI::Spacing(g_metrics);
        int minWidth = 0;
        for (int i = 0; i < count; ++i)
            minWidth = (std::max)(minWidth, ButtonText(buttons[i]).naturalWidth + 4 * gap);
        const int columns = (std::max)(1, (std::min)(count, (width + gap) / (minWidth + gap)));
        const int cell = (std::max)(1, (width - (columns - 1) * gap) / columns);
        int bottom = y;
        for (int row = 0; row < count; row += columns)
        {
            int height = NativeUI::RowHeight(g_metrics, 0);
            for (int i = row; i < (std::min)(row + columns, count); ++i)
                height = (std::max)(height, PlaceLabel(ButtonText(buttons[i]).text,
                    gap, gap, cell - 2 * gap) + 2 * gap);
            for (int i = row; i < (std::min)(row + columns, count); ++i)
                buttons[i]->setCoord(x + (i - row) * (cell + gap), bottom, cell, height);
            bottom += height + gap;
        }
        return bottom - y - gap;
    }

    void LayoutColumn(int column)
    {
        MyGUI::ScrollView* scroll = g_columns[column];
        const MyGUI::IntPoint offset = scroll->getViewOffset();
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = (std::max)(1, g_metrics->getFontHeight());
        const int buttonH = NativeUI::RowHeight(g_metrics, 0);
        for (int pass = 0; pass < 2; ++pass)
        {
            const int width = (std::max)(1, scroll->getViewCoord().width - 2 * gap);
            const int viewH = scroll->getViewCoord().height;
            int y = gap;
            if (column == 0)
            {
                y += PlaceLabel(Heading(0, "PG_LblRoster"), gap, y, width) + gap;
                y += PlaceLabel(g_rosterCount, gap, y, width) + gap;
                g_rosterSearch->setCoord(gap, y, width, buttonH + gap); y += buttonH + 2 * gap;
                y += PlaceLabel(Heading(0, "PG_RosterHint"), gap, y, width) + gap;
                const int listH = (std::max)(body * 9, viewH - y - gap);
                g_rosterScroll->setCoord(gap, y, width, listH); y += listH + gap;
                LayoutRows(g_rosterScroll, g_rosterRows);
            }
            else if (column == 1)
            {
                y += PlaceLabel(Heading(1, "PG_LblMode"), gap, y, width) + gap;
                MyGUI::Button* modes[] = {g_modeAvB, g_modeTeams1v1, g_modeLast};
                y += PlaceButtons(modes, 3, gap, y, width) + gap;
                y += PlaceLabel(g_lblDest, gap, y, width) + gap;
                MyGUI::Button* assign[] = {g_btnDestA, g_btnDestB};
                y += PlaceButtons(assign, IsTeamUiMode() ? 2 : 1, gap, y, width) + gap;
                const int listH = (std::max)(body * 9, viewH - y - body * 12);
                if (IsTeamUiMode())
                {
                    const bool stacked = width < body * 32;
                    const int half = stacked ? width : (width - gap) / 2;
                    const int hA = PlaceLabel(g_lblTeamA, gap, y, half);
                    if (stacked)
                    {
                        y += hA + gap;
                        g_teamAScroll->setCoord(gap, y, half, listH); y += listH + gap;
                        y += PlaceLabel(g_lblTeamB, gap, y, half) + gap;
                        g_teamBScroll->setCoord(gap, y, half, listH); y += listH + gap;
                    }
                    else
                    {
                        const int hB = PlaceLabel(g_lblTeamB, 2 * gap + half, y, half);
                        y += (std::max)(hA, hB) + gap;
                        g_teamAScroll->setCoord(gap, y, half, listH);
                        g_teamBScroll->setCoord(2 * gap + half, y, half, listH); y += listH + gap;
                    }
                    LayoutRows(g_teamAScroll, g_teamARows);
                    LayoutRows(g_teamBScroll, g_teamBRows);
                }
                else
                {
                    y += PlaceLabel(g_lblPool, gap, y, width) + gap;
                    g_poolScroll->setCoord(gap, y, width, listH); y += listH + gap;
                    LayoutRows(g_poolScroll, g_poolRows);
                }
                MyGUI::Button* balance[] = {g_btnAutoBalance, g_btnBalanceSkill, g_btnBalanceRating};
                MyGUI::Button* edit[] = {g_btnAutoBalance, g_btnSwapTeams, g_btnClearTeams};
                MyGUI::Button* poolEdit[] = {g_btnAutoBalance, g_btnClearTeams};
                y += PlaceButtons(g_balanceMenuOpen ? balance : IsTeamUiMode() ? edit : poolEdit,
                    g_balanceMenuOpen || IsTeamUiMode() ? 3 : 2, gap, y, width) + gap;
                y += PlaceLabel(Heading(1, "PG_BuilderHint"), gap, y, width) + gap;
                y += PlaceButtons(g_presetButtons, 3, gap, y, width) + gap;
            }
            else
            {
                y += PlaceLabel(Heading(2, "PG_LblPreview"), gap, y, width) + gap;
                g_previewImage->setCoord(gap, y, kPortraitSize, kPortraitSize);
                const int textX = 2 * gap + kPortraitSize;
                int textY = y;
                textY += PlaceLabel(g_previewName, textX, textY, width - kPortraitSize - gap) + gap;
                textY += PlaceLabel(g_previewMeta, textX, textY, width - kPortraitSize - gap) + gap;
                y = (std::max)(textY, y + kPortraitSize + gap);
                y += PlaceLabel(g_previewStats, gap, y, width) + gap;
                y += PlaceButtons(&g_btnRemove, 1, gap, y, width) + gap;
                y += PlaceLabel(Heading(2, "PG_LblSettings"), gap, y, width) + gap;
                y += PlaceLabel(Heading(2, "PG_LblLoc"), gap, y, width) + gap;
                MyGUI::Button* locations[] = {g_locArena, g_locSmallArena, g_locBanner};
                y += PlaceButtons(locations, 3, gap, y, width) + gap;
                y += PlaceButtons(&g_medicalProtocolButton, 1, gap, y, width) + gap;
                y += PlaceLabel(Heading(2, "PG_LblSummary"), gap, y, width) + gap;
                y += PlaceLabel(g_summary, gap, y, width) + gap;
            }
            g_columnContent[column]->setSize(width + 2 * gap, y);
            scroll->setCanvasSize(width + 2 * gap, (std::max)(y, viewH));
        }
        scroll->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(-offset.top,
            scroll->getCanvasSize().height, scroll->getViewCoord().height)));
    }

    void LayoutPanel()
    {
        if (!g_layoutReady) return;
        SyncButtonText();
        MyGUI::Widget* client = g_window->getClientWidget();
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = (std::max)(1, g_metrics->getFontHeight());
        const int width = (std::max)(1, client->getWidth() - 2 * gap);
        const int buttonH = NativeUI::RowHeight(g_metrics, 0);
        const int headerH = buttonH + 2 * gap;
        const int footerH = (std::min)(client->getHeight() / 3, body * 4 + 2 * gap);
        const int footerY = (std::max)(headerH, client->getHeight() - footerH - gap);
        const int contentH = (std::max)(1, footerY - headerH - gap);
        const int actionW = (std::min)(width / 2,
            (std::max)(ButtonText(g_stopButton).naturalWidth, ButtonText(g_startButton).naturalWidth) + 4 * gap);
        g_status->setCoord(gap, footerY, (std::max)(1, width - actionW - gap), footerH);
        PlaceButtons(&g_startButton, 1, gap + width - actionW, footerY, actionW);
        PlaceButtons(&g_stopButton, 1, gap + width - actionW, footerY, actionW);
        if (g_compactMode)
        {
            g_titleHero->setVisible(false);
            MyGUI::Button* header[] = {
                g_rosterDrawerButton,
                g_settingsDrawerButton,
                g_btnRecruitment
            };
            const int actualHeader = PlaceButtons(header, 3, gap, gap, width);
            const int top = (std::max)(headerH, actualHeader + 2 * gap);
            const bool drawer = g_rosterDrawerOpen || g_settingsDrawerOpen;
            const int sideW = drawer ? (std::max)(1, width * 2 / 5) : 0;
            g_columns[1]->setCoord(gap + (drawer ? sideW + gap : 0), top,
                (std::max)(1, width - (drawer ? sideW + gap : 0)), (std::max)(1, footerY - top - gap));
            g_columns[0]->setCoord(gap, top, (std::max)(1, sideW), (std::max)(1, footerY - top - gap));
            g_columns[2]->setCoord(gap, top, (std::max)(1, sideW), (std::max)(1, footerY - top - gap));
        }
        else
        {
            g_titleHero->setVisible(true);
            const int recruitWidth = (std::min)(width / 2,
                ButtonText(g_btnRecruitment).naturalWidth + 4 * gap);
            PlaceLabel(g_titleHero, gap, gap,
                (std::max)(1, width - recruitWidth - gap));
            PlaceButtons(&g_btnRecruitment, 1,
                gap + width - recruitWidth, gap, recruitWidth);
            const int sideW = (width - 2 * gap) / 4;
            g_columns[0]->setCoord(gap, headerH, sideW, contentH);
            g_columns[1]->setCoord(2 * gap + sideW, headerH, width - 2 * sideW - 2 * gap, contentH);
            g_columns[2]->setCoord(gap + width - sideW, headerH, sideW, contentH);
        }
        for (int i = 0; i < 3; ++i)
            if (g_columns[i]->getVisible()) LayoutColumn(i);
    }

    void DestroyPanelWidgets()
    {
        g_layoutReady = false;
        if (g_window) MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_titleHero = NULL;
        g_rosterScroll = NULL;
        g_teamAScroll = NULL;
        g_teamBScroll = NULL;
        g_poolScroll = NULL;
        g_rosterSearch = NULL;
        g_rosterCount = NULL;
        g_modeAvB = NULL;
        g_modeTeams1v1 = NULL;
        g_modeLast = NULL;
        g_locArena = NULL;
        g_locSmallArena = NULL;
        g_locBanner = NULL;
        g_medicalProtocolButton = NULL;
        g_settingsTip = NULL;
        g_settingsTipOwner = NULL;
        g_lblTeamA = NULL;
        g_lblTeamB = NULL;
        g_lblPool = NULL;
        g_lblDest = NULL;
        g_btnDestA = NULL;
        g_btnDestB = NULL;
        g_btnRemove = NULL;
        g_btnAutoBalance = NULL;
        g_btnBalanceSkill = NULL;
        g_btnBalanceRating = NULL;
        g_btnClearTeams = NULL;
        g_btnRecruitment = NULL;
        g_btnSwapTeams = NULL;
        g_previewImage = NULL;
        g_previewName = NULL;
        g_previewMeta = NULL;
        g_previewStats = NULL;
        g_summary = NULL;
        g_startButton = NULL;
        g_stopButton = NULL;
        g_status = NULL;
        g_rosterDrawerButton = NULL;
        g_settingsDrawerButton = NULL;
        g_metrics = NULL;
        for (int i = 0; i < 3; ++i)
        {
            g_columns[i] = NULL; g_columnContent[i] = NULL; g_presetButtons[i] = NULL;
        }
        g_buttonPresentation.clear();
        g_viewSize = MyGUI::IntSize();
        g_rosterRows.clear(); g_teamARows.clear(); g_teamBRows.clear(); g_poolRows.clear();
        g_dragCharacter = NULL;
        g_rowDragActive = false;
    }

    void CreatePanel()
    {
        if (g_window) return;
        MyGUI::Gui* guiInstance = MyGUI::Gui::getInstancePtr();
        if (!guiInstance) return;
        try
        {
            g_window = guiInstance->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX",
                .04f, .04f, .92f, .92f, MyGUI::Align::Center, "Window", "ProvingGroundsArenaWindow");
            g_window->setCaption("Proving Grounds - Arena");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            g_metrics = NativeUI::Label(client, MyGUI::IntCoord(0, 0, 100, 24), "PG_Metrics", "Arena");
            g_metrics->setVisible(false);
            for (int i = 0; i < 3; ++i)
            {
                char name[32]; sprintf_s(name, "PG_Column%d", i);
                g_columns[i] = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 300), name);
                g_columnContent[i] = g_columns[i]->createWidget<MyGUI::Widget>("PanelEmpty",
                    MyGUI::IntCoord(0, 0, 300, 300), MyGUI::Align::Default);
            }
            MyGUI::Widget* roster = g_columnContent[0];
            MyGUI::Widget* builder = g_columnContent[1];
            MyGUI::Widget* settings = g_columnContent[2];
            g_titleHero = PanelLabel(client, "PG_Hero", "Arena Setup");
            g_btnRecruitment = PanelButton(client, "PG_PrisonerRecruitment",
                "Recruit Prisoners", OnRecruitPrisoners);
            PanelLabel(roster, "PG_LblRoster", "Squad Roster");
            g_rosterCount = PanelLabel(roster, "PG_RosterCount", "0 fighters");
            g_rosterSearch = roster->createWidget<MyGUI::EditBox>("Kenshi_EditBox",
                MyGUI::IntCoord(0, 0, 200, 32), MyGUI::Align::Default, "PG_RosterSearch");
            g_rosterSearch->setMaxTextLength(40);
            g_rosterSearch->setEditReadOnly(false);
            g_rosterSearch->setOnlyText("Search fighters...");
            g_rosterSearch->eventEditTextChange += MyGUI::newDelegate(OnRosterSearchChanged);
            g_rosterSearch->eventKeySetFocus += MyGUI::newDelegate(OnRosterSearchFocus);
            g_rosterSearch->eventKeyLostFocus += MyGUI::newDelegate(OnRosterSearchBlur);
            g_rosterSearch->eventMouseButtonPressed += MyGUI::newDelegate(OnRosterSearchMousePressed);
            g_rosterSearch->eventEditSelectAccept += MyGUI::newDelegate(OnRosterSearchAccepted);
            PanelLabel(roster, "PG_RosterHint", "Double-click or drag to assign. Right-click to remove.");
            g_rosterScroll = NativeUI::Scroll(roster, MyGUI::IntCoord(0, 0, 200, 300), "PG_Roster");
            PanelLabel(builder, "PG_LblMode", "Match Mode");
            g_modeAvB = PanelButton(builder, "PG_ModeAvB", "Teams", OnModeAvB);
            g_modeTeams1v1 = PanelButton(builder, "PG_ModeTeams1v1", "Teams 1v1", OnModeTeams1v1);
            g_modeLast = PanelButton(builder, "PG_ModeLast", "Last Man Standing", OnModeLast);
            g_lblDest = PanelLabel(builder, "PG_LblDest", "Double-click assigns to Team A");
            g_btnDestA = PanelButton(builder, "PG_DestA", "Assign to Team A", OnDestA);
            g_btnDestB = PanelButton(builder, "PG_DestB", "Assign to Team B", OnDestB);
            g_lblTeamA = PanelLabel(builder, "PG_LblTeamA", "Team A");
            g_lblTeamB = PanelLabel(builder, "PG_LblTeamB", "Team B");
            g_lblPool = PanelLabel(builder, "PG_LblPool", "Fighter pool");
            g_teamAScroll = NativeUI::Scroll(builder, MyGUI::IntCoord(0, 0, 200, 300), "PG_TeamA");
            g_teamBScroll = NativeUI::Scroll(builder, MyGUI::IntCoord(0, 0, 200, 300), "PG_TeamB");
            g_poolScroll = NativeUI::Scroll(builder, MyGUI::IntCoord(0, 0, 200, 300), "PG_Pool");
            g_btnAutoBalance = PanelButton(builder, "PG_AutoBalance", "Balance", OnBalanceMenu);
            g_btnBalanceSkill = PanelButton(builder, "PG_BalanceSkill", "By Skill", OnBalanceBySkill);
            g_btnBalanceRating = PanelButton(builder, "PG_BalanceRating", "By Rating", OnBalanceByRating);
            g_btnSwapTeams = PanelButton(builder, "PG_SwapTeams", "Swap Teams", OnSwapTeams);
            g_btnClearTeams = PanelButton(builder, "PG_ClearTeams", "Clear Teams", OnClearTeams);
            PanelLabel(builder, "PG_BuilderHint", "Presets: left-click to load, right-click to save.");
            for (int i = 0; i < 3; ++i)
            {
                char name[32], caption[32], index[4];
                sprintf_s(name, "PG_Preset%d", i + 1); sprintf_s(caption, "Preset %d", i + 1); sprintf_s(index, "%d", i);
                g_presetButtons[i] = PanelButton(builder, name, caption, OnPresetButtonClick);
                g_presetButtons[i]->setUserString("preset", index);
                g_presetButtons[i]->eventMouseButtonPressed += MyGUI::newDelegate(OnPresetMousePressed);
            }
            PanelLabel(settings, "PG_LblPreview", "Selected Fighter");
            g_previewImage = settings->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(0, 0, kPortraitSize, kPortraitSize), MyGUI::Align::Default, "PG_PreviewImg");
            g_previewImage->setVisible(false);
            g_previewName = PanelLabel(settings, "PG_PreviewName", "No fighter selected");
            g_previewMeta = PanelLabel(settings, "PG_PreviewMeta", "Select a roster or team row");
            g_previewStats = PanelLabel(settings, "PG_PreviewStats", "Choose a fighter to review readiness.");
            g_btnRemove = PanelButton(settings, "PG_Remove", "Remove Fighter", OnRemove);
            PanelLabel(settings, "PG_LblSettings", "Match Settings");
            PanelLabel(settings, "PG_LblLoc", "Location");
            g_locArena = PanelButton(settings, "PG_LocArena", "Arena", OnLocationArena);
            g_locSmallArena = PanelButton(settings, "PG_LocSmallArena", "Small", OnLocationSmallArena);
            g_locBanner = PanelButton(settings, "PG_LocBanner", "Banner", OnLocationBanner);
            g_medicalProtocolButton = PanelButton(settings, "PG_MedicalProtocol", "Medical: Ringside Aid", OnMedicalProtocolToggle);
            BindSettingsToolTip(g_medicalProtocolButton);
            PanelLabel(settings, "PG_LblSummary", "Match Summary");
            g_summary = PanelLabel(settings, "PG_Summary", "Team A vs Team B");
            g_status = client->createWidget<MyGUI::EditBox>("Kenshi_WordWrap",
                MyGUI::IntCoord(0, 0, 200, 80), MyGUI::Align::Default, "PG_Status");
            g_startButton = PanelButton(client, "PG_Start", "Start Fight", OnStart);
            g_stopButton = PanelButton(client, "PG_Stop", "Stop Fight", OnStop);
            g_rosterDrawerButton = PanelButton(client, "PG_RosterDrawer", "Squad Roster", OnToggleRosterDrawer);
            g_settingsDrawerButton = PanelButton(client, "PG_SettingsDrawer", "Match Settings", OnToggleSettingsDrawer);
            g_settingsTip = client->createWidget<MyGUI::EditBox>("Kenshi_WordWrap",
                MyGUI::IntCoord(0, 0, 280, 80), MyGUI::Align::Default, "PG_SettingsTip");
            g_settingsTip->setVisible(false);
            Passive(g_settingsTip);
            g_layoutReady = true;
            UpdateModeVisibility();
            UpdateLocationHighlight();
            UpdateMedicalProtocolButton();
            RefreshPresetButtons();
            RefreshResponsiveMode();
            RefreshAll();
            PGLog::Debug("Proving Grounds: native Arena UI created");
        }
        catch (...)
        {
            DestroyPanelWidgets();
            PGLog::Error("Proving Grounds: failed to create native Arena UI");
        }
    }

    void ShowWindow()
    {
        if (!g_window)
            CreatePanel();
        if (g_window)
        {
            RefreshResponsiveMode();
            g_window->setVisible(true);
            ResetUiRefreshClock();
        }
    }

    void HidePanel()
    {
        if (!g_window || !g_window->getVisible())
            return;
        HideSettingsTip();
        g_window->setVisible(false);
        ResetUiRefreshClock();
    }

    void TogglePanel()
    {
        if (g_window && g_window->getVisible())
        {
            HidePanel();
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
        const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (down && !g_f8WasDown && PGConfig::F8OpensDebugMenu())
        {
            PGSettingsUI::Close();
            DebugMenu::Toggle();
        }
        g_f8WasDown = down;
    }

    bool PollEscClose()
    {
        const bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool consumed = false;
        if (down && !g_escWasDown)
        {
            if (TownBookie::IsVisible())
            {
                TownBookie::Close();
                consumed = true;
            }
            else if (TownArenaUI::IsVisible())
            {
                TownArenaUI::Close();
                consumed = true;
            }
            else if (PrisonerRecruitmentUI::IsVisible())
            {
                PrisonerRecruitmentUI::Close();
                consumed = true;
            }
            else if (PGSettingsUI::IsVisible())
            {
                PGSettingsUI::Close();
                consumed = true;
            }
            else if (DebugMenu::IsVisible())
            {
                DebugMenu::Close();
                consumed = true;
            }
            else if (LeaderboardUI::IsVisible())
            {
                LeaderboardUI::Close();
                consumed = true;
            }
            else if (RewardsUI::IsVisible())
            {
                RewardsUI::Close();
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
        return scroll->getViewOffset();
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

    void RouteColumnWheelFallback(int wheel, const MyGUI::IntPoint* before)
    {
        if (!wheel || !g_window || !g_window->getVisible()) return;
        const MyGUI::IntPoint mouse = MyGUI::InputManager::getInstance().getMousePositionByLayer();
        MyGUI::ScrollView* lists[] = {g_rosterScroll, g_teamAScroll, g_teamBScroll, g_poolScroll};
        for (int i = 0; i < 4; ++i)
            if (PointInside(lists[i], mouse)) return;
        for (int i = 0; i < 3; ++i)
        {
            if (!PointInside(g_columns[i], mouse)) continue;
            if (SamePoint(before[i], g_columns[i]->getViewOffset()))
                ScrollListBy(g_columns[i], wheel < 0 ? -NativeUI::RowHeight(g_metrics, 0) * 2 : NativeUI::RowHeight(g_metrics, 0) * 2);
            break;
        }
    }

    void ForgottenGUI_update_hook(ForgottenGUI* thisptr)
    {
        const int wheel = key ? key->mWheel : 0;
        MyGUI::IntPoint columnsBefore[3];
        for (int i = 0; i < 3; ++i) columnsBefore[i] = ScrollOffset(g_columns[i]);
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
        RouteColumnWheelFallback(wheel, columnsBefore);
        ContextMenuHooks::Tick(thisptr);
        ContextMenuHooks::RefreshArenaCursor(thisptr);
        PollF8Toggle();
        DebugMenu::Tick();
        ArenaIngress::Tick();
        PrisonerUtil::TickMatchUpkeep();
        PrisonerUtil::TickReturn();
        WorldLifecycle::Tick();
        KOWatcher::Tick();
        TownArena::Tick();
        // Both arenas run the same session, so the guard is ticked once here rather
        // than by either of them -- it serves the town bout and the player-run ones.
        ArenaRingGuard::Tick();
        TownArenaUI::Tick();
        MarksHUD::Tick(thisptr);
        TownBookie::Tick();
        PrisonerUtil::TickRingsideAid();
        ResultsUI::Tick();
        LeaderboardUI::Tick();
        RewardsUI::Tick();
        PrisonerRecruitmentUI::Tick();
        if (g_window && g_window->getVisible())
        {
            const bool refreshIdleUi = FrameCadencePolicy::Advance(
                g_uiRefreshElapsedSec,
                AdvanceUiRefreshClock(),
                kIdleUiRefreshIntervalSec);
            if (refreshIdleUi)
            {
                RefreshResponsiveMode();
                RefreshStatusAndButtons();
            }
            if (g_rowDragActive || refreshIdleUi)
                RefreshPointerHighlights();
        }
    }

    void OptionsWindow_update_hook(OptionsWindow* thisptr)
    {
        OptionsWindow_update_orig(thisptr);
        PGSettingsUI::InjectModsTabUI(thisptr);
    }
}

namespace ArenaUI
{
    void AbandonWorldState()
    {
        MarksHUD::AbandonWorldState();
        TownArenaUI::AbandonWorldState();
        RewardsUI::AbandonWorldState();
        PrisonerRecruitmentUI::AbandonWorldState();
        g_squadCache.clear();
        g_rosterCache.clear();
        g_filteredRoster.clear();
        g_teamA.clear();
        g_teamB.clear();
        g_pool.clear();
        g_rosterRows.clear();
        g_teamARows.clear();
        g_teamBRows.clear();
        g_poolRows.clear();
        g_handlerAssignments.clear();

        for (int i = 0; i < 3; ++i)
        {
            g_setupPresets[i].valid = false;
            g_setupPresets[i].teamA.clear();
            g_setupPresets[i].teamB.clear();
            g_setupPresets[i].pool.clear();
        }
        if (g_window)
        {
            MyGUI::ScrollView* lists[] = {g_rosterScroll, g_teamAScroll, g_teamBScroll, g_poolScroll};
            for (int i = 0; i < 4; ++i) if (lists[i]) lists[i]->setViewOffset(MyGUI::IntPoint());
            for (int i = 0; i < 3; ++i) if (g_columns[i]) g_columns[i]->setViewOffset(MyGUI::IntPoint());
        }

        g_previewChar = NULL;
        g_dragCharacter = NULL;
        g_rowDragActive = false;
        g_balanceMenuOpen = false;
        g_rosterQuery.clear();
        g_searchPlaceholderActive = true;
        ResetUiRefreshClock();

        // UI objects belong to MyGUI rather than the game world. Keep the panel
        // allocated, but hide it until rows can be rebuilt from the new world.
        if (MyGUI::Gui::getInstancePtr())
        {
            HideSettingsTip();
            if (g_window)
                g_window->setVisible(false);
            if (g_previewImage)
                g_previewImage->setVisible(false);
            if (g_status)
                g_status->setCaption("Idle");
        }
    }

    void ShowFromRegistry(RootObject* registry, bool bindRegistry)
    {
        TownBookie::Close();
        PrisonerRecruitmentUI::AbandonWorldState();
        if (ArenaIdentity::IsTownRegistry(registry))
        {
            HidePanel();
            TownArenaUI::Show(static_cast<Building*>(registry), bindRegistry);
            return;
        }
        TownArenaUI::Close();
        if (ArenaIngress::IsPending())
        {
            ShowWindow();
            RefreshAll();
            if (g_status)
                g_status->setCaption(ArenaIngress::GetStatus().c_str());
            return;
        }

        if (bindRegistry)
            ArenaIngress::BindRegistry(registry);
        ShowWindow();
        RefreshAll();
        Character* opener = ArenaIngress::GetUiOpener();
        if (opener && opener->isValid() && VectorContains(g_rosterCache, opener))
            SetPreview(opener);
        if (g_status)
        {
            g_status->setCaption(SparSession::IsActive()
                ? SparSession::GetStatus().c_str()
                : ArenaIngress::GetStatus().c_str());
        }
    }

    Character* GetPreviewCharacter()
    {
        if (g_previewChar && g_previewChar->isValid())
            return g_previewChar;
        return NULL;
    }

    bool InstallHooks()
    {
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&TitleScreen::_CONSTRUCTOR),
                TitleScreen_hook,
                &TitleScreen_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook TitleScreen constructor");
            return false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::update),
                ForgottenGUI_update_hook,
                &ForgottenGUI_update_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook ForgottenGUI::update");
            return false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&OptionsWindow::_NV_update),
                OptionsWindow_update_hook,
                &OptionsWindow_update_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook OptionsWindow::_NV_update for Mods settings");
            return false;
        }

        PGLog::Debug("Proving Grounds: ArenaUI hooks installed");
        return true;
    }
}
