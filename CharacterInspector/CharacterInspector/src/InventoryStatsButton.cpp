#include "InventoryStatsButton.h"
#include "StatsOverviewUI.h"

#include <Debug.h>
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/Globals.h>
#include <kenshi/InputHandler.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/InventoryGUI.h>
#pragma warning(pop)

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    void (*InventoryGUI_update_orig)(InventoryGUI*) = NULL;
    void (*ForgottenGUI_update_orig)(ForgottenGUI*) = NULL;
    void (*ForgottenGUI_closeAllInventories_orig)(ForgottenGUI*) = NULL;
    void (*ForgottenGUI_closeAllWindows_orig)(ForgottenGUI*) = NULL;

    std::map<MyGUI::Widget*, InventoryGUI*> g_statsButtonToInventory;
    // Inventories without LIMBS (containers, etc.) — never call getWidget again (it spams MyGUI.log).
    std::set<InventoryGUI*> g_skipNoLimbs;

    void ClearInventoryTracking()
    {
        g_statsButtonToInventory.clear();
        g_skipNoLimbs.clear();
    }

    void ClearEscapeFlags()
    {
        if (!key)
            return;
        key->escape = false;
        key->escape_msg = false;
    }

    void ForgottenGUI_update_hook(ForgottenGUI* thisptr)
    {
        if (StatsOverviewUI::isOpen() && key && (key->escape_msg || key->escape))
        {
            ClearEscapeFlags();
            StatsOverviewUI::handleEscape();
        }

        ForgottenGUI_update_orig(thisptr);

        if (StatsOverviewUI::isOpen() && thisptr && !thisptr->isAnyInventoryWindowOpen())
        {
            StatsOverviewUI::close();
            ClearInventoryTracking();
        }
    }

    void ForgottenGUI_closeAllInventories_hook(ForgottenGUI* thisptr)
    {
        StatsOverviewUI::close();
        ClearInventoryTracking();
        ForgottenGUI_closeAllInventories_orig(thisptr);
    }

    void ForgottenGUI_closeAllWindows_hook(ForgottenGUI* thisptr)
    {
        StatsOverviewUI::close();
        ClearInventoryTracking();
        ForgottenGUI_closeAllWindows_orig(thisptr);
    }

    std::string ToUpper(const std::string& s)
    {
        std::string out = s;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (char)toupper((unsigned char)out[i]);
        return out;
    }

    bool CaptionIsLimbs(MyGUI::Widget* widget)
    {
        MyGUI::TextBox* text = widget->castType<MyGUI::TextBox>(false);
        if (!text)
            return false;
        return ToUpper(text->getCaption().asUTF8()) == "LIMBS";
    }

    MyGUI::Widget* FindLimbsRecursive(MyGUI::Widget* root)
    {
        if (!root)
            return NULL;

        if (CaptionIsLimbs(root))
            return root;

        size_t count = root->getChildCount();
        for (size_t i = 0; i < count; ++i)
        {
            MyGUI::Widget* found = FindLimbsRecursive(root->getChildAt(i));
            if (found)
                return found;
        }
        return NULL;
    }

    MyGUI::Widget* FindNamedChildSilent(MyGUI::Widget* root, const std::string& name)
    {
        if (!root || name.empty())
            return NULL;
        // findWidget does not log; layoutMgr->getWidget / assignWidget does.
        return root->findWidget(name);
    }

    MyGUI::Widget* FindLimbsButton(InventoryGUI* inv)
    {
        if (!inv || !inv->layoutMgr)
            return NULL;

        MyGUI::Window* window = inv->layoutMgr->getWindow();
        MyGUI::Widget* root = window ? (MyGUI::Widget*)window : inv->win;
        if (!root)
            return NULL;

        const char* names[] = {
            "LimbsButton",
            "limbsButton",
            "LIMBS",
            "ButtonLimbs",
            "btnLimbs",
            "Limbs"
        };
        const std::string& prefix = inv->layoutMgr->mPrefix;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            if (!prefix.empty())
            {
                MyGUI::Widget* w = FindNamedChildSilent(root, prefix + names[i]);
                if (w)
                    return w;
            }
            MyGUI::Widget* w = FindNamedChildSilent(root, names[i]);
            if (w)
                return w;
        }

        return FindLimbsRecursive(root);
    }

    const int kButtonGap = 4;
    // Cap short-button height; inflated True Dark coords often exceed this.
    const int kMaxShortButtonHeight = 28;

    bool CaptionEqualsIgnoreCase(MyGUI::Widget* widget, const char* expectedUpper)
    {
        MyGUI::TextBox* text = widget->castType<MyGUI::TextBox>(false);
        if (!text)
            return false;
        return ToUpper(text->getCaption().asUTF8()) == expectedUpper;
    }

    bool NameLooksLikeOpenBag(const std::string& n)
    {
        const char* names[] = {
            "OpenBagButton",
            "openBagButton",
            "OPENBAG",
            "BagButton",
            "btnOpenBag",
            "OpenBag"
        };
        for (size_t j = 0; j < sizeof(names) / sizeof(names[0]); ++j)
        {
            if (n == names[j] || n.find(names[j]) != std::string::npos)
                return true;
        }
        return false;
    }

    MyGUI::Widget* FindOpenBagNearLimbs(MyGUI::Widget* limbs)
    {
        if (!limbs || !limbs->getParent())
            return NULL;

        MyGUI::Widget* parent = limbs->getParent();
        MyGUI::Widget* best = NULL;
        int bestTop = -1;
        const MyGUI::IntCoord limbsCoord = limbs->getCoord();

        size_t count = parent->getChildCount();
        for (size_t i = 0; i < count; ++i)
        {
            MyGUI::Widget* child = parent->getChildAt(i);
            if (!child || child == limbs)
                continue;
            if (child->getName() == "StatsButtonCI")
                continue;

            if (!NameLooksLikeOpenBag(child->getName()) && !CaptionEqualsIgnoreCase(child, "OPEN BAG"))
                continue;

            MyGUI::IntCoord c = child->getCoord();
            if (c.top < limbsCoord.top && c.top >= bestTop)
            {
                bestTop = c.top;
                best = child;
            }
        }

        if (best)
            return best;

        // Fallback: nearest sibling above LIMBS by top.
        best = NULL;
        bestTop = -1;
        for (size_t i = 0; i < count; ++i)
        {
            MyGUI::Widget* child = parent->getChildAt(i);
            if (!child || child == limbs)
                continue;
            if (child->getName() == "StatsButtonCI")
                continue;
            MyGUI::IntCoord c = child->getCoord();
            if (c.top < limbsCoord.top && c.top >= bestTop)
            {
                bestTop = c.top;
                best = child;
            }
        }
        return best;
    }

    MyGUI::Widget* FindNextSiblingBelow(MyGUI::Widget* limbs, MyGUI::Widget* ignore)
    {
        if (!limbs || !limbs->getParent())
            return NULL;

        MyGUI::Widget* parent = limbs->getParent();
        MyGUI::Widget* best = NULL;
        int bestTop = 0x7fffffff;
        const MyGUI::IntCoord limbsCoord = limbs->getCoord();

        size_t count = parent->getChildCount();
        for (size_t i = 0; i < count; ++i)
        {
            MyGUI::Widget* child = parent->getChildAt(i);
            if (!child || child == limbs || child == ignore)
                continue;
            if (child->getName() == "StatsButtonCI")
                continue;
            MyGUI::IntCoord c = child->getCoord();
            if (c.top >= limbsCoord.top + limbsCoord.height && c.top < bestTop)
            {
                bestTop = c.top;
                best = child;
            }
        }
        return best;
    }

    int ChooseShortHeight(MyGUI::Widget* openBag, MyGUI::Widget* limbs, MyGUI::Widget* below)
    {
        int h = limbs->getCoord().height;
        if (openBag)
        {
            int bagH = openBag->getCoord().height;
            if (bagH > 0 && bagH < h)
                h = bagH;
        }
        if (h > kMaxShortButtonHeight)
            h = kMaxShortButtonHeight;

        // Fit LIMBS + gap + STATS between OPEN BAG (or LIMBS top) and the next sibling.
        if (below)
        {
            int top = openBag
                ? (openBag->getCoord().top + openBag->getCoord().height + kButtonGap)
                : limbs->getCoord().top;
            int available = below->getCoord().top - top - kButtonGap;
            // two buttons + one gap between them
            int fitH = (available - kButtonGap) / 2;
            if (fitH < h)
                h = fitH;
        }

        if (h < 12)
            h = 12;
        return h;
    }

    bool NeedsPackLayout(MyGUI::Widget* limbs, MyGUI::Widget* openBag, MyGUI::Widget* below, int shortH)
    {
        MyGUI::IntCoord c = limbs->getCoord();
        if (c.height > shortH + 2)
            return true;
        if (openBag && c.height > openBag->getCoord().height + 2)
            return true;

        if (!below)
            return false;

        // Would STATS (same height as LIMBS) overlap the next sibling?
        int statsBottom = c.top + c.height + kButtonGap + c.height;
        return statsBottom > below->getCoord().top;
    }

    void OnStatsClicked(MyGUI::Widget* sender)
    {
        std::map<MyGUI::Widget*, InventoryGUI*>::iterator it = g_statsButtonToInventory.find(sender);
        if (it == g_statsButtonToInventory.end() || !it->second)
        {
            ErrorLog("Character Inspector: STATS click missing inventory");
            return;
        }

        Character* character = it->second->getCallbackCharacter();
        if (!character)
            character = it->second->_NV_getCallbackCharacter();

        if (!character)
        {
            ErrorLog("Character Inspector: inventory has no character");
            return;
        }

        // Same character + already open → toggle closed.
        if (StatsOverviewUI::isOpen() && StatsOverviewUI::currentCharacter() == character)
        {
            StatsOverviewUI::close();
            return;
        }

        StatsOverviewUI::open(character);
    }

    void InjectStatsButton(InventoryGUI* inv)
    {
        if (!inv || !inv->layoutMgr)
            return;

        if (g_skipNoLimbs.find(inv) != g_skipNoLimbs.end())
            return;

        MyGUI::Widget* limbs = FindLimbsButton(inv);
        if (!limbs)
        {
            g_skipNoLimbs.insert(inv);
            return;
        }

        MyGUI::Widget* parent = limbs->getParent();
        if (!parent)
            return;

        MyGUI::Widget* openBag = FindOpenBagNearLimbs(limbs);
        MyGUI::Widget* below = FindNextSiblingBelow(limbs, NULL);
        const int shortH = ChooseShortHeight(openBag, limbs, below);
        const bool pack = NeedsPackLayout(limbs, openBag, below, shortH);

        MyGUI::IntCoord limbsC = limbs->getCoord();
        MyGUI::IntCoord statsC;

        if (pack && openBag)
        {
            MyGUI::IntCoord bag = openBag->getCoord();
            limbsC.left = bag.left;
            limbsC.width = bag.width;
            limbsC.top = bag.top + bag.height + kButtonGap;
            limbsC.height = shortH;
            limbs->setCoord(limbsC);

            statsC = limbsC;
            statsC.top = limbsC.top + limbsC.height + kButtonGap;
        }
        else if (pack)
        {
            // No OPEN BAG found: still shrink LIMBS in place and stack STATS under it.
            limbsC.height = shortH;
            limbs->setCoord(limbsC);

            statsC.left = limbsC.left;
            statsC.top = limbsC.top + limbsC.height + kButtonGap;
            statsC.width = limbsC.width;
            statsC.height = shortH;
        }
        else
        {
            statsC.left = limbsC.left;
            statsC.top = limbsC.top + limbsC.height + kButtonGap;
            statsC.width = limbsC.width;
            statsC.height = limbsC.height;
        }

        MyGUI::Widget* existing = FindNamedChildSilent(parent, "StatsButtonCI");
        if (!existing)
        {
            MyGUI::Window* window = inv->layoutMgr->getWindow();
            MyGUI::Widget* root = window ? (MyGUI::Widget*)window : inv->win;
            if (root)
                existing = FindNamedChildSilent(root, "StatsButtonCI");
        }

        MyGUI::Button* stats = existing ? existing->castType<MyGUI::Button>(false) : NULL;
        if (stats)
        {
            stats->setCoord(statsC);
            g_statsButtonToInventory[stats] = inv;
            return;
        }

        stats = parent->createWidget<MyGUI::Button>(
            "Kenshi_Button1",
            statsC.left,
            statsC.top,
            statsC.width,
            statsC.height,
            MyGUI::Align::Default,
            "StatsButtonCI");
        if (!stats)
        {
            ErrorLog("Character Inspector: failed to create STATS button");
            return;
        }

        stats->setCaption("STATS");
        stats->eventMouseButtonClick += MyGUI::newDelegate(OnStatsClicked);
        g_statsButtonToInventory[stats] = inv;
        DebugLog("Character Inspector: STATS button injected");
    }

    void InventoryGUI_update_hook(InventoryGUI* thisptr)
    {
        if (StatsOverviewUI::isOpen() && key && (key->escape_msg || key->escape))
        {
            ClearEscapeFlags();
            StatsOverviewUI::handleEscape();
        }

        InventoryGUI_update_orig(thisptr);
        InjectStatsButton(thisptr);

        if (StatsOverviewUI::isOpen() && thisptr)
        {
            bool tracked = false;
            for (std::map<MyGUI::Widget*, InventoryGUI*>::iterator it = g_statsButtonToInventory.begin();
                 it != g_statsButtonToInventory.end();
                 ++it)
            {
                if (it->second == thisptr)
                {
                    tracked = true;
                    break;
                }
            }

            if (tracked)
            {
                Character* character = thisptr->getCallbackCharacter();
                if (!character)
                    character = thisptr->_NV_getCallbackCharacter();
                if (character && character != StatsOverviewUI::currentCharacter())
                    StatsOverviewUI::open(character);
            }
        }
    }
}

namespace InventoryStatsButton
{
    bool InstallHooks()
    {
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&InventoryGUI::_NV_update),
                InventoryGUI_update_hook,
                &InventoryGUI_update_orig))
        {
            ErrorLog("Character Inspector: failed to hook InventoryGUI::_NV_update");
            return false;
        }
        DebugLog("Character Inspector: hooked InventoryGUI::_NV_update");

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::update),
                ForgottenGUI_update_hook,
                &ForgottenGUI_update_orig))
        {
            ErrorLog("Character Inspector: failed to hook ForgottenGUI::update");
        }
        else
        {
            DebugLog("Character Inspector: hooked ForgottenGUI::update");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::closeAllInventories),
                ForgottenGUI_closeAllInventories_hook,
                &ForgottenGUI_closeAllInventories_orig))
        {
            ErrorLog("Character Inspector: failed to hook ForgottenGUI::closeAllInventories");
        }
        else
        {
            DebugLog("Character Inspector: hooked ForgottenGUI::closeAllInventories");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::closeAllWindows),
                ForgottenGUI_closeAllWindows_hook,
                &ForgottenGUI_closeAllWindows_orig))
        {
            ErrorLog("Character Inspector: failed to hook ForgottenGUI::closeAllWindows");
        }
        else
        {
            DebugLog("Character Inspector: hooked ForgottenGUI::closeAllWindows");
        }

        return true;
    }
}
