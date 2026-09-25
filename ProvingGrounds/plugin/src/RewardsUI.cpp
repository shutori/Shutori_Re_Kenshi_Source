#include "RewardsUI.h"
#include "NativeUI.h"

#include "ArenaRewards.h"
#include "LeaderboardStore.h"
#include "RewardDelivery.h"
#include "RewardsLayout.h"
#include "SquadUtil.h"

#include "PGLog.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Item.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/gui/InventoryGUI.h>
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/util/hand.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    MyGUI::Window* g_window = NULL;
    MyGUI::ScrollView* g_detailScroll = NULL;
    MyGUI::ScrollView* g_profileScroll = NULL;
    int g_profileContentHeight = 0;
    MyGUI::IntSize g_clientSize;
    MyGUI::IntSize g_screenSize;
    int g_fontHeight = 0;
    int g_fighterContentHeight = 0;
    int g_detailContentHeight = 0;
    MyGUI::ScrollView* g_fighterScroll = NULL;
    MyGUI::TextBox* g_title = NULL;
    MyGUI::TextBox* g_subtitle = NULL;
    MyGUI::TextBox* g_fightersHeading = NULL;
    MyGUI::TextBox* g_selectedHeading = NULL;
    MyGUI::TextBox* g_selectedName = NULL;
    MyGUI::TextBox* g_selectedCareer = NULL;
    MyGUI::TextBox* g_status = NULL;
    MyGUI::Button* g_close = NULL;
    MyGUI::Button* g_t1ArmourTab = NULL;
    MyGUI::Button* g_t2ArmourTab = NULL;
    MyGUI::Button* g_t3ArmourTab = NULL;
    Character* g_selected = NULL;
    Character* g_preferred = NULL;

    enum CatalogTab
    {
        CatalogGeneral = 0,
        CatalogArmour,
        CatalogWeapons
    };
    CatalogTab g_catalogTab = CatalogGeneral;
    int g_tabOffsets[3] = { 0, 0, 0 };

    struct FighterRow
    {
        MyGUI::Button* root;
        MyGUI::ImageBox* portrait;
        MyGUI::EditBox* name;
        MyGUI::TextBox* marks;
        MyGUI::Widget* rail;
        Character* character;
    };

    struct GeneralCard
    {
        ArenaRewards::GeneralItem item;
        MyGUI::ImageBox* icon;
        MyGUI::IntCoord iconArea;
        MyGUI::IntSize textureSize;
        bool iconLoaded;
        MyGUI::TextBox* name;
        MyGUI::TextBox* details;
        MyGUI::Button* buy;
        bool visible;
    };

    struct LicenceCard
    {
        ArenaRewards::ArmourSet set;
        MyGUI::TextBox* heading;
        MyGUI::TextBox* details;
        MyGUI::Button* buy;
        bool unlocked;
    };

    struct RewardCard
    {
        ArenaRewards::Piece piece;
        MyGUI::Widget* accent;
        MyGUI::Widget* fade[16];
        MyGUI::Widget* border[4];
        MyGUI::ImageBox* icon;
        MyGUI::IntCoord iconArea;
        MyGUI::IntSize textureSize;
        bool iconLoaded;
        bool visible;
        MyGUI::TextBox* name;
        MyGUI::TextBox* progress;
        MyGUI::Button* claim;
        int rarity;
    };

    std::vector<FighterRow> g_rows;
    RewardCard g_cards[ArenaRewards::PieceCount];
    GeneralCard g_general[ArenaRewards::GeneralItemCount];
    LicenceCard g_licences[ArenaRewards::ArmourRowCount];
    LicenceCard g_weaponLicence;
    MyGUI::TextBox* g_generalHeadings[2] = { NULL, NULL };

    const ArenaRewards::GeneralItem kSupplyDisplayOrder[] =
    {
        ArenaRewards::GeneralAdvancedFirstAid,
        ArenaRewards::GeneralPitSurgeonRoll,
        ArenaRewards::GeneralSkeletonRepair,
        ArenaRewards::GeneralIronmenderCase,
        ArenaRewards::GeneralSplintKit,
        ArenaRewards::GeneralBoneSetterBraces
    };

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int BodyHeight() { return Max(1, Max(g_title->getFontHeight(), g_title->getTextSize().height)); }
    void LayoutWindow();
    void FitWindowToScreen();
    void AbandonWindow();
    int WrappedHeight(MyGUI::TextBox* label, int x, int y, int width)
    {
        label->setCoord(x, y, Max(1, width), BodyHeight());
        const int insets = Max(0, label->getHeight() - label->getTextRegion().height);
        const int height = Max(BodyHeight(), label->getTextSize().height + insets);
        label->setSize(Max(1, width), height);
        return height;
    }
    void OnWheel(MyGUI::Widget* sender, int relative)
    {
        if (!relative) return;
        MyGUI::Widget* ancestor = sender;
        while (ancestor && ancestor != g_fighterScroll && ancestor != g_detailScroll && ancestor != g_profileScroll)
            ancestor = ancestor->getParent();
        MyGUI::ScrollView* scroll = ancestor == g_fighterScroll ? g_fighterScroll :
            (ancestor == g_profileScroll ? g_profileScroll : g_detailScroll);
        if (!ancestor || !scroll) return;
        const int content = scroll == g_fighterScroll ? g_fighterContentHeight :
            (scroll == g_profileScroll ? g_profileContentHeight : g_detailContentHeight);
        const int maximum = Max(0, content - scroll->getViewCoord().height);
        const int offset = -scroll->getViewOffset().top + (relative < 0 ? 3 : -3) * BodyHeight();
        scroll->setViewOffset(MyGUI::IntPoint(0, -Min(maximum, Max(0, offset))));
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

    const char* ArmourRowName(ArenaRewards::ArmourRow row)
    {
        switch (row)
        {
        case ArenaRewards::ArmourRowPitfighter: return "PITFIGHTER";
        case ArenaRewards::ArmourRowRetainer: return "RETAINER LEATHER";
        case ArenaRewards::ArmourRowEnforcer: return "PLATE ENFORCER";
        case ArenaRewards::ArmourRowWarlord: return "WARLORD";
        default: return "ARMOUR";
        }
    }

    const char* ArmourRowManifest(ArenaRewards::ArmourRow row)
    {
        switch (row)
        {
        case ArenaRewards::ArmourRowPitfighter:
            return "Footwraps / Harness / Jaw Guard / Pants";
        case ArenaRewards::ArmourRowRetainer:
            return "Pants / Jacket / Boots / Helmet";
        case ArenaRewards::ArmourRowEnforcer:
            return "Legplates / Plated Boots / Chest / Helmet";
        case ArenaRewards::ArmourRowWarlord:
            return "Cloak / Plateskirt / Plate Boots / Armour / Helmet";
        default:
            return "";
        }
    }

    void MakeRewardAccent(RewardCard& card, const std::string& prefix)
    {
        card.accent = NULL;
        try
        {
            card.accent = g_detailScroll->createWidget<MyGUI::Widget>(
                "PanelEmpty", MyGUI::IntCoord(0, 0, 1, 1),
                MyGUI::Align::Default, prefix + "_Accent");
            card.accent->setNeedMouseFocus(false);
            card.accent->setVisible(false);
            for (int i = 0; i < 16; ++i)
            {
                char suffix[16];
                sprintf_s(suffix, "_Fade%d", i);
                card.fade[i] = card.accent->createWidget<MyGUI::Widget>(
                    "WhiteSkin", MyGUI::IntCoord(0, 0, 1, 1),
                    MyGUI::Align::Default, prefix + suffix);
                card.fade[i]->setNeedMouseFocus(false);
                card.fade[i]->setAlpha(.12f * (16 - i) / 16.f);
            }
            for (int i = 0; i < 4; ++i)
            {
                char suffix[16];
                sprintf_s(suffix, "_Border%d", i);
                card.border[i] = card.accent->createWidget<MyGUI::Widget>(
                    "WhiteSkin", MyGUI::IntCoord(0, 0, 1, 1),
                    MyGUI::Align::Default, prefix + suffix);
                card.border[i]->setNeedMouseFocus(false);
                card.border[i]->setAlpha(i == 0 ? .9f : .35f);
            }
        }
        catch (...)
        {
            if (card.accent)
                MyGUI::Gui::getInstance().destroyWidget(card.accent);
            card.accent = NULL;
            PGLog::Error("Proving Grounds: optional reward rarity accent unavailable");
        }
    }

    void LayoutRewardAccent(
        RewardCard& card,
        int left,
        int top,
        int width,
        int height)
    {
        if (!card.accent)
            return;
        card.accent->setVisible(card.visible);
        if (!card.visible)
            return;
        const MyGUI::Colour colours[] = {
            MyGUI::Colour(.22f, .52f, .95f),
            MyGUI::Colour(.22f, .76f, .36f),
            MyGUI::Colour(.65f, .34f, .92f),
            MyGUI::Colour(1.f, .49f, .12f)
        };
        const MyGUI::Colour colour = colours[Min(3, Max(0, card.rarity))];
        card.accent->setCoord(left, top, width, height);
        for (int i = 0; i < 16; ++i)
        {
            const int start = width * i / 16;
            const int end = width * (i + 1) / 16;
            card.fade[i]->setCoord(start, 0, Max(1, end - start), height);
            card.fade[i]->setColour(colour);
        }
        const int rail = Max(3, NativeUI::Spacing(g_title) / 2);
        card.border[0]->setCoord(0, 0, rail, height);
        card.border[1]->setCoord(rail, 0, Max(1, width - rail), 1);
        card.border[2]->setCoord(rail, height - 1, Max(1, width - rail), 1);
        card.border[3]->setCoord(width - 1, 0, 1, height);
        for (int i = 0; i < 4; ++i)
            card.border[i]->setColour(colour);
    }
    void LayoutIcon(RewardCard& card)
    {
        card.icon->setCoord(card.iconArea);
        if (ArenaRewards::IsWeapon(card.piece) && card.textureSize.width > 0 && card.textureSize.height > 0)
        {
            const float widthScale = static_cast<float>(card.iconArea.width) / card.textureSize.width;
            const float heightScale = static_cast<float>(card.iconArea.height) / card.textureSize.height;
            const float scale = widthScale < heightScale ? widthScale : heightScale;
            const int width = Max(1, static_cast<int>(card.textureSize.width * scale + .5f));
            const int height = Max(1, static_cast<int>(card.textureSize.height * scale + .5f));
            card.icon->setCoord(card.iconArea.left + (card.iconArea.width - width) / 2,
                card.iconArea.top + (card.iconArea.height - height) / 2, width, height);
        }
    }

    void LayoutGeneralIcon(GeneralCard& card)
    {
        card.icon->setCoord(card.iconArea);
        if (card.textureSize.width <= 0 || card.textureSize.height <= 0)
            return;
        const float widthScale = static_cast<float>(card.iconArea.width) /
            card.textureSize.width;
        const float heightScale = static_cast<float>(card.iconArea.height) /
            card.textureSize.height;
        const float scale = widthScale < heightScale ? widthScale : heightScale;
        const int width = Max(1, static_cast<int>(
            card.textureSize.width * scale + .5f));
        const int height = Max(1, static_cast<int>(
            card.textureSize.height * scale + .5f));
        card.icon->setCoord(
            card.iconArea.left + (card.iconArea.width - width) / 2,
            card.iconArea.top + (card.iconArea.height - height) / 2,
            width, height);
    }

    void BindPortrait(MyGUI::ImageBox* image, Character* character)
    {
        if (!image || !character || !character->isValid())
        {
            if (image)
                image->setVisible(false);
            return;
        }
        PortraitManager* manager = PortraitManager::getInstance();
        if (manager) manager->setImageWidget(character->getHandle(), image, true);
        image->setVisible(manager != NULL);
    }

    void BindRewardIcon(RewardCard& card)
    {
        if (!card.icon || !ou || !ou->theFactory)
            return;
        if (card.iconLoaded)
            return; // Native icon texture is already resolved and cached.

        const ArenaRewards::Reward& reward = ArenaRewards::Get(
            card.piece, ArenaRewards::FirstTier(card.piece));
        GameData* itemData = RewardDelivery::ResolveRewardItemData(card.piece);
        if (!itemData)
        {
            card.icon->setVisible(false);
            return;
        }

        Item* previewItem = RewardDelivery::CreateRewardItem(
            itemData, reward.qualityLevel, reward.modelStringId,
            reward.manufacturerStringId);
        if (!previewItem)
        {
            card.icon->setVisible(false);
            return;
        }

        std::string textureName;
        iVector2 textureSize;
        InventoryIcon::createIconImage(previewItem, textureName, textureSize);
        ou->destroy(previewItem, false, "Proving Grounds reward icon resolved");

        if (textureName.empty())
        {
            card.icon->setVisible(false);
            return;
        }

        card.icon->setImageTexture(textureName);
        card.textureSize = MyGUI::IntSize(textureSize.x, textureSize.y);
        card.iconLoaded = true;
        if (ArenaRewards::IsWeapon(card.piece) && textureSize.x > 0 && textureSize.y > 0)
            card.icon->setImageTile(card.textureSize);
        LayoutIcon(card);
        card.icon->setVisible(card.visible);
    }

    void BindGeneralIcon(GeneralCard& card)
    {
        if (!card.icon || !ou || !ou->theFactory || card.iconLoaded)
            return;
        const ArenaRewards::GeneralReward& reward =
            ArenaRewards::GetGeneral(card.item);
        GameData* itemData = ou->gamedata.getData(reward.stringId);
        if (!itemData)
        {
            card.icon->setVisible(false);
            return;
        }
        Item* previewItem = RewardDelivery::CreateRewardItem(itemData, -1);
        if (!previewItem)
        {
            card.icon->setVisible(false);
            return;
        }
        std::string textureName;
        iVector2 textureSize;
        InventoryIcon::createIconImage(previewItem, textureName, textureSize);
        ou->destroy(previewItem, false,
            "Proving Grounds general reward icon resolved");
        if (textureName.empty())
        {
            card.icon->setVisible(false);
            return;
        }
        card.icon->setImageTexture(textureName);
        card.textureSize = MyGUI::IntSize(textureSize.x, textureSize.y);
        card.iconLoaded = true;
        LayoutGeneralIcon(card);
        card.icon->setVisible(card.visible);
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

    bool IsLiveSquadSelection(Character* character)
    {
        if (!character || !character->isValid())
            return false;
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].character == character)
                return true;
        }
        return false;
    }

    void RefreshDetails();
    void RefreshAll();

    void OnFighterClicked(MyGUI::Widget* sender)
    {
        while (sender && sender->getParent() && sender != g_fighterScroll)
        {
            bool found = false;
            for (size_t i = 0; i < g_rows.size(); ++i)
                if (g_rows[i].root == sender) { found = true; break; }
            if (found) break;
            sender = sender->getParent();
        }
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].root == sender)
            {
                if (g_selected != g_rows[i].character)
                {
                    g_detailScroll->setViewOffset(MyGUI::IntPoint(0, 0));
                }
                g_selected = g_rows[i].character;
                RefreshDetails();
                return;
            }
        }
    }

    void OnClaimClicked(MyGUI::Widget* sender)
    {
        if (!g_selected || !g_selected->isValid())
            return;
        for (int i = 0; i < ArenaRewards::PieceCount; ++i)
        {
            if (g_cards[i].claim != sender)
                continue;
            std::string status;
            RewardDelivery::ClaimNext(g_selected, g_cards[i].piece, status);
            if (g_status)
                g_status->setCaption(status.c_str());
            RefreshDetails();
            return;
        }
    }

    void OnCatalogTabClicked(MyGUI::Widget* sender)
    {
        CatalogTab next = CatalogGeneral;
        if (sender == g_t2ArmourTab)
            next = CatalogArmour;
        else if (sender == g_t3ArmourTab)
            next = CatalogWeapons;
        if (g_catalogTab == next)
            return;
        g_tabOffsets[g_catalogTab] = Max(0,
            -g_detailScroll->getViewOffset().top);
        g_catalogTab = next;
        g_detailScroll->setViewOffset(MyGUI::IntPoint(
            0, -g_tabOffsets[g_catalogTab]));
        RefreshDetails();
    }

    void OnGeneralClicked(MyGUI::Widget* sender)
    {
        if (!g_selected || !g_selected->isValid())
            return;
        for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
        {
            if (g_general[i].buy != sender)
                continue;
            std::string status;
            RewardDelivery::PurchaseGeneral(
                g_selected, g_general[i].item, status);
            g_status->setCaption(status.c_str());
            RefreshDetails();
            return;
        }
    }

    void OnLicenceClicked(MyGUI::Widget* sender)
    {
        if (!g_selected || !g_selected->isValid())
            return;
        if (g_weaponLicence.buy == sender)
        {
            std::string status;
            RewardDelivery::PurchaseWeaponLicence(g_selected, status);
            g_status->setCaption(status.c_str());
            RefreshDetails();
            return;
        }
        for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
        {
            if (g_licences[i].buy != sender)
                continue;
            std::string status;
            RewardDelivery::PurchaseLicence(
                g_selected, g_licences[i].set, status);
            g_status->setCaption(status.c_str());
            RefreshDetails();
            return;
        }
    }

    void OnCloseClicked(MyGUI::Widget*) { RewardsUI::Close(); }

    void OnWindowButtonPressed(MyGUI::Widget*, const std::string& name)
    {
        if (name == "close" || name == "Close")
            RewardsUI::Close();
    }

    void AbandonWindow()
    {
        if (g_window) MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_fighterScroll = g_detailScroll = g_profileScroll = NULL;
        g_title = g_subtitle = g_fightersHeading = g_selectedHeading = NULL;
        g_selectedName = g_selectedCareer = g_status = NULL;
        g_close = NULL;
        g_t1ArmourTab = g_t2ArmourTab = g_t3ArmourTab = NULL;
        g_rows.clear();
        for (int i = 0; i < ArenaRewards::PieceCount; ++i) g_cards[i] = RewardCard();
        for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i) g_general[i] = GeneralCard();
        for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
            g_licences[i] = LicenceCard();
        g_weaponLicence = LicenceCard();
        g_generalHeadings[0] = g_generalHeadings[1] = NULL;
        g_selected = g_preferred = NULL;
        g_tabOffsets[0] = g_tabOffsets[1] = g_tabOffsets[2] = 0;
        g_clientSize = g_screenSize = MyGUI::IntSize();
    }

    void LayoutFighters()
    {
        const int gap = NativeUI::Spacing(g_title);
        const int body = BodyHeight();
        const int image = Max(40, body * 3);
        const int oldOffset = -g_fighterScroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_fighterScroll->getViewCoord();
            const int width = Max(1, view.width - 2 * gap);
            int y = gap;
            for (size_t i = 0; i < g_rows.size(); ++i)
            {
                FighterRow& row = g_rows[i];
                row.root->setCoord(gap, y, width, body);
                const int textX = image + 2 * gap;
                const int textWidth = Max(1, width - textX - gap);
                const int nameHeight = WrappedHeight(row.name, textX, gap, textWidth);
                const int marksHeight = Max(body, row.marks->getTextSize().height);
                row.marks->setCoord(textX, gap + nameHeight + gap, textWidth, marksHeight);
                const int height = Max(image, nameHeight + gap + marksHeight) + 2 * gap;
                row.root->setSize(width, height);
                row.portrait->setCoord(gap, gap, image, image);
                if (row.rail)
                    row.rail->setCoord(0, 0, Max(2, gap / 2), height);
                y += height + gap;
            }
            g_fighterContentHeight = y;
            g_fighterScroll->setCanvasSize(Max(1, view.width), Max(y, view.height));
            if (g_fighterScroll->getViewCoord().width == view.width) break;
        }
        const int maximum = Max(0, g_fighterContentHeight - g_fighterScroll->getViewCoord().height);
        g_fighterScroll->setViewOffset(MyGUI::IntPoint(0, -Min(maximum, Max(0, oldOffset))));
    }

    int LayoutRewardCard(
        RewardCard& card,
        int left,
        int top,
        int width,
        int body,
        int gap,
        int preferredHeight)
    {
        const int actionHeight = NativeUI::RowHeight(card.claim, 0);
        const int minimumHeight = Max(body * 7, actionHeight + body * 3 + 4 * gap);
        const int height = Max(minimumHeight, preferredHeight);
        const bool weapon = ArenaRewards::IsWeapon(card.piece);
        const int imageHeight = Min(height - actionHeight - 3 * gap,
            Max(body * 5, weapon ? height * 3 / 4 : height / 2));
        const int imageWidth = weapon
            ? Min(width * 45 / 100, imageHeight * 2)
            : imageHeight;
        const int actionWidth = Min(width,
            card.claim->getTextSize().width + 4 * gap);
        const int contentLeft = left + 2 * gap;
        const int contentTop = top + gap;
        const int textX = contentLeft + imageWidth + gap;
        const int textWidth = Max(1, left + width - gap - textX);
        int textBottom = contentTop + WrappedHeight(
            card.name, textX, contentTop, textWidth);
        textBottom += gap + WrappedHeight(
            card.progress, textX, textBottom + gap, textWidth);
        card.iconArea = MyGUI::IntCoord(
            contentLeft, contentTop, imageWidth, Max(1, imageHeight));
        LayoutIcon(card);
        card.claim->setCoord(
            left + width - actionWidth - gap,
            top + height - actionHeight - gap,
            actionWidth, actionHeight);
        LayoutRewardAccent(card, left, top, width, height);
        return top + height + gap;
    }

    int LayoutGeneralCard(
        GeneralCard& card,
        int left,
        int top,
        int width,
        int body,
        int gap,
        int preferredHeight)
    {
        const int actionHeight = NativeUI::RowHeight(card.buy, 0);
        const int actionWidth = Min(width,
            card.buy->getTextSize().width + 4 * gap);
        const int iconSize = Max(48, body * 4);
        const int rowTop = top + gap;
        const int textX = left + gap + iconSize + gap;
        const bool fixed = preferredHeight > 0;
        const bool stackAction = !fixed && width <
            iconSize + actionWidth + body * 14 + 3 * gap;
        const int textWidth = Max(1,
            width - iconSize - 2 * gap -
            (stackAction ? 0 : actionWidth + gap));
        const int nameHeight = WrappedHeight(
            card.name, textX, rowTop, textWidth);
        const int detailsTop = rowTop + nameHeight + gap;
        const int detailsHeight = WrappedHeight(
            card.details, textX, detailsTop, textWidth);
        const int contentHeight = nameHeight + gap + detailsHeight;
        int height = fixed ? preferredHeight :
            Max(iconSize, contentHeight) + 2 * gap;
        card.iconArea = MyGUI::IntCoord(
            left + gap, rowTop, iconSize, iconSize);
        LayoutGeneralIcon(card);
        if (fixed)
        {
            height = Max(height, actionHeight + 3 * gap);
            card.buy->setCoord(
                left + width - actionWidth - gap,
                top + height - actionHeight - gap,
                actionWidth, actionHeight);
        }
        else if (stackAction)
        {
            card.buy->setCoord(textX,
                rowTop + Max(iconSize, contentHeight) + gap,
                actionWidth, actionHeight);
            height += gap + actionHeight;
        }
        else
        {
            height = Max(height, actionHeight + 2 * gap);
            card.buy->setCoord(
                left + width - actionWidth - gap,
                top + (height - actionHeight) / 2,
                actionWidth, actionHeight);
        }
        return top + height + gap;
    }

    void LayoutDetails()
    {
        const int gap = NativeUI::Spacing(g_title);
        const int body = BodyHeight();
        const int oldOffset = -g_detailScroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_detailScroll->getViewCoord();
            const int width = Max(1, view.width - 2 * gap);
            int y = gap;

            if (g_catalogTab == CatalogGeneral)
            {
                y += WrappedHeight(g_generalHeadings[0], gap, y, width) + gap;
                for (int i = ArenaRewards::GeneralArenaBuildingsI;
                     i <= ArenaRewards::GeneralArenaBuildingsII; ++i)
                {
                    GeneralCard& card = g_general[i];
                    if (!card.visible)
                        continue;
                    y = LayoutGeneralCard(
                        card, gap, y, width, body, gap, 0);
                }

                y += WrappedHeight(g_generalHeadings[1], gap, y, width) + gap;
                int supplyCount = 0;
                const int displayCount = sizeof(kSupplyDisplayOrder) /
                    sizeof(kSupplyDisplayOrder[0]);
                for (int i = 0; i < displayCount; ++i)
                {
                    if (g_general[kSupplyDisplayOrder[i]].visible)
                        ++supplyCount;
                }
                const int columns = RewardsLayout::FightSupplyColumns(
                    supplyCount);
                const int cellWidth = Max(1,
                    (width - (columns - 1) * gap) / columns);
                const int cardHeight = Max(body * 8, 120);
                int visibleIndex = 0;
                for (int i = 0; i < displayCount; ++i)
                {
                    GeneralCard& card = g_general[kSupplyDisplayOrder[i]];
                    if (!card.visible)
                        continue;
                    const int column = visibleIndex % columns;
                    const int row = visibleIndex / columns;
                    LayoutGeneralCard(card,
                        gap + column * (cellWidth + gap),
                        y + row * (cardHeight + gap),
                        cellWidth, body, gap, cardHeight);
                    ++visibleIndex;
                }
                if (visibleIndex > 0)
                {
                    const int rows = (visibleIndex + columns - 1) / columns;
                    y += rows * (cardHeight + gap);
                }
            }
            else if (g_catalogTab == CatalogArmour)
            {
                for (int row = 0; row < ArenaRewards::ArmourRowCount; ++row)
                {
                    const ArenaRewards::ArmourRow armourRow =
                        static_cast<ArenaRewards::ArmourRow>(row);
                    LicenceCard& licence = g_licences[row];
                    y += WrappedHeight(licence.heading, gap, y, width) + gap;
                    y += WrappedHeight(licence.details, gap, y, width) + gap;
                    if (!licence.unlocked)
                    {
                        const int actionHeight = NativeUI::RowHeight(
                            licence.buy, 0);
                        const int actionWidth = Min(width,
                            licence.buy->getTextSize().width + 4 * gap);
                        licence.buy->setCoord(
                            gap, y, actionWidth, actionHeight);
                        y += actionHeight + 2 * gap;
                        const int iconSize = Max(32, body * 2);
                        int iconX = gap;
                        for (int i = 0; i < ArenaRewards::PieceCount; ++i)
                        {
                            RewardCard& manifest = g_cards[i];
                            if (ArenaRewards::IsWeapon(manifest.piece) ||
                                ArenaRewards::RowFor(manifest.piece) !=
                                    armourRow)
                                continue;
                            manifest.iconArea = MyGUI::IntCoord(
                                iconX, y, iconSize, iconSize);
                            LayoutIcon(manifest);
                            manifest.icon->setVisible(manifest.iconLoaded);
                            iconX += iconSize + gap;
                        }
                        y += iconSize + 2 * gap;
                    }
                    const int columns = RewardsLayout::GridColumns(
                        width, body * 15, gap, 4);
                    const int cellWidth = Max(1,
                        (width - (columns - 1) * gap) / columns);
                    const int cardHeight = Max(body * 8, 120);
                    int visibleIndex = 0;
                    for (int i = 0; i < ArenaRewards::PieceCount; ++i)
                    {
                        RewardCard& card = g_cards[i];
                        if (!card.visible ||
                            ArenaRewards::RowFor(card.piece) != armourRow)
                            continue;
                        const int column = visibleIndex % columns;
                        const int row = visibleIndex / columns;
                        LayoutRewardCard(card,
                            gap + column * (cellWidth + gap),
                            y + row * (cardHeight + gap),
                            cellWidth, body, gap, cardHeight);
                        ++visibleIndex;
                    }
                    if (visibleIndex > 0)
                    {
                        const int rows = (visibleIndex + columns - 1) / columns;
                        y += rows * (cardHeight + gap);
                    }
                    y += gap;
                }
            }
            else
            {
                y += WrappedHeight(
                    g_weaponLicence.heading, gap, y, width) + gap;
                y += WrappedHeight(
                    g_weaponLicence.details, gap, y, width) + gap;
                if (!g_weaponLicence.unlocked)
                {
                    const int actionHeight = NativeUI::RowHeight(
                        g_weaponLicence.buy, 0);
                    const int actionWidth = Min(width,
                        g_weaponLicence.buy->getTextSize().width + 4 * gap);
                    g_weaponLicence.buy->setCoord(
                        gap, y, actionWidth, actionHeight);
                    y += actionHeight + 2 * gap;
                }
                const int columns = RewardsLayout::GridColumns(
                    width, body * 20, gap, 2);
                const int cellWidth = Max(1,
                    (width - (columns - 1) * gap) / columns);
                const int cardHeight = columns == 2
                    ? RewardsLayout::CompactRowHeight(
                        RewardsLayout::FillRowHeight(
                            view.height, 2, gap, body * 9), body * 9)
                    : Max(body * 9, 140);
                int visibleIndex = 0;
                for (int i = 0; i < ArenaRewards::PieceCount; ++i)
                {
                    RewardCard& card = g_cards[i];
                    if (!card.visible)
                        continue;
                    const int column = visibleIndex % columns;
                    const int row = visibleIndex / columns;
                    LayoutRewardCard(card,
                        gap + column * (cellWidth + gap),
                        y + row * (cardHeight + gap),
                        cellWidth, body, gap, cardHeight);
                    ++visibleIndex;
                }
                const int rows = visibleIndex > 0
                    ? (visibleIndex + columns - 1) / columns : 0;
                y += rows * (cardHeight + gap);
            }

            g_detailContentHeight = y;
            g_detailScroll->setCanvasSize(
                Max(1, view.width), Max(y, view.height));
            if (g_detailScroll->getViewCoord().width == view.width)
                break;
        }
        const int maximum = Max(0,
            g_detailContentHeight - g_detailScroll->getViewCoord().height);
        const int clamped = Min(maximum, Max(0, oldOffset));
        g_tabOffsets[g_catalogTab] = clamped;
        g_detailScroll->setViewOffset(MyGUI::IntPoint(0, -clamped));
    }

    void LayoutProfile()
    {
        const int gap = NativeUI::Spacing(g_title);
        const int body = BodyHeight();
        const int oldOffset = -g_profileScroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_profileScroll->getViewCoord();
            const int width = Max(1, view.width - 2 * gap);
            int y = gap;
            y += WrappedHeight(g_selectedCareer, gap, y, width) + gap;
            MyGUI::Button* tabs[] = {
                g_t1ArmourTab, g_t2ArmourTab, g_t3ArmourTab
            };
            if (g_t1ArmourTab->getVisible())
            {
                int tabWidth = 0, tabHeight = 0;
                for (int i = 0; i < 3; ++i)
                {
                    tabWidth = Max(tabWidth, tabs[i]->getTextSize().width + 4 * gap);
                    tabHeight = Max(tabHeight, NativeUI::RowHeight(tabs[i], 0));
                }
                const int columns = Max(1, Min(3,
                    (width + gap) / (tabWidth + gap)));
                const int cellWidth = Max(1, (width - (columns - 1) * gap) / columns);
                for (int i = 0; i < 3; ++i)
                    tabs[i]->setCoord(gap + (i % columns) * (cellWidth + gap),
                        y + (i / columns) * (tabHeight + gap), cellWidth, tabHeight);
                y += ((3 + columns - 1) / columns) * (tabHeight + gap);
            }
            g_profileContentHeight = y;
            g_profileScroll->setCanvasSize(Max(1, view.width), Max(y, view.height));
            if (g_profileScroll->getViewCoord().width == view.width) break;
        }
        const int maximum = Max(0, g_profileContentHeight - g_profileScroll->getViewCoord().height);
        g_profileScroll->setViewOffset(MyGUI::IntPoint(0, -Min(maximum, Max(0, oldOffset))));
    }

    void FitWindowToScreen()
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render) return;
        const MyGUI::IntSize size = render->getViewSize();
        if (size.width <= 0 || size.height <= 0 || size == g_screenSize) return;
        g_screenSize = size;
        const int width = size.width * 90 / 100;
        const int height = size.height * 90 / 100;
        g_window->setCoord((size.width - width) / 2, (size.height - height) / 2, width, height);
    }

    void LayoutWindow()
    {
        const MyGUI::IntSize size = g_window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(g_title);
        const int body = BodyHeight();
        const int width = Max(1, size.width - 2 * gap);
        const int closeHeight = NativeUI::RowHeight(g_close, 0);
        const int closeWidth = Min(width, g_close->getTextSize().width + 4 * gap);
        const int closeTop = Max(gap, size.height - gap - closeHeight);
        g_close->setCoord((size.width - closeWidth) / 2, closeTop, closeWidth, closeHeight);
        // Native text panels keep long transaction messages readable by scrolling.
        g_status->setCoord(gap, gap, width, body * 2 + 2 * gap);
        const int statusInsets = Max(0, g_status->getHeight() - g_status->getTextRegion().height);
        int statusHeight = Min(g_status->getTextSize().height + statusInsets, body * 2 + statusInsets);
        int statusTop = Max(gap, closeTop - gap - statusHeight);
        g_subtitle->setCoord(gap, gap, width, body * 2 + 2 * gap);
        const int subtitleInsets = Max(0, g_subtitle->getHeight() - g_subtitle->getTextRegion().height);
        const int subtitleHeight = Min(g_subtitle->getTextSize().height + subtitleInsets, body * 2 + subtitleInsets);
        g_subtitle->setSize(width, subtitleHeight);
        int top = gap + subtitleHeight + gap;
        int bottom = statusTop - gap;
        const bool stacked = RewardsLayout::ShouldStack(width, body);
        const int fighterInsets = Max(0, g_fighterScroll->getHeight() - g_fighterScroll->getViewCoord().height);
        const int profileInsets = Max(0, g_profileScroll->getHeight() - g_profileScroll->getViewCoord().height);
        const int detailInsets = Max(0, g_detailScroll->getHeight() - g_detailScroll->getViewCoord().height);
        // Three independent canvases must each retain room for at least a full
        // text line. At large fonts, reclaim the optional introduction/heading
        // before allocating the fighter pane; the recruit action stays fixed.
        const int minimumFighter = body + fighterInsets;
        const int minimumProfile = body + profileInsets;
        const int minimumDetail = body + detailInsets;
        const bool compactHeight = stacked && bottom - top <
            6 * body + fighterInsets + profileInsets + detailInsets + 5 * gap;
        g_subtitle->setVisible(!compactHeight);
        g_fightersHeading->setVisible(!compactHeight);
        if (compactHeight)
        {
            statusHeight = Min(statusHeight, body + statusInsets);
            statusTop = Max(gap, closeTop - gap - statusHeight);
            top = gap;
            bottom = statusTop - gap;
        }
        g_status->setCoord(gap, statusTop, width, statusHeight);
        const int fighterWidth = stacked ? width :
            RewardsLayout::FighterWidth(width, body);
        const int detailLeft = stacked ? gap : gap + fighterWidth + 2 * gap;
        const int detailWidth = stacked ? width : Max(1, width - fighterWidth - 2 * gap);
        g_fightersHeading->setCoord(gap, top, fighterWidth, body);
        int detailTop = top;
        if (stacked)
        {
            const int headingHeight = compactHeight ? 0 : body + gap;
            const int maximumFighter = bottom - top - headingHeight -
                minimumProfile - minimumDetail - 2 * gap;
            const int fighterHeight = Max(minimumFighter,
                Min(Max(body * 2, (bottom - top) / 4), maximumFighter));
            g_fighterScroll->setCoord(gap, top + headingHeight, fighterWidth, fighterHeight);
            detailTop = top + headingHeight + gap + fighterHeight;
        }
        else
            g_fighterScroll->setCoord(gap, top + body + gap, fighterWidth, Max(1, bottom - top - body - gap));
        int detailBottom = bottom;
        // Keep the profile above reward rows. Its own native canvas bounds long
        // names and large-font category choices without consuming the action area.
        const int profileLimit = Max(minimumProfile,
            Min((detailBottom - detailTop - gap) / 2, detailBottom - detailTop - gap - minimumDetail));
        g_profileScroll->setCoord(detailLeft, detailTop, detailWidth, profileLimit);
        LayoutProfile();
        const int profileHeight = Min(profileLimit,
            g_profileContentHeight + Max(0, g_profileScroll->getHeight() - g_profileScroll->getViewCoord().height));
        g_profileScroll->setSize(detailWidth, Max(1, profileHeight));
        LayoutProfile();
        detailTop += Max(1, profileHeight) + gap;
        g_detailScroll->setCoord(detailLeft, detailTop, detailWidth, Max(1, detailBottom - detailTop));
        LayoutFighters();
        LayoutDetails();
        g_clientSize = size;
        g_fontHeight = body;
    }

    void EnsureWindow()
    {
        if (g_window) return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return;
        const char* resource = "Kenshi_WindowCX";
        try
        {
            g_window = gui->createWidgetReal<MyGUI::Window>(resource, .05f, .05f, .90f, .90f,
                MyGUI::Align::Center, "Window", "ProvingGroundsRewardsWindow");
            g_window->setCaption("Proving Grounds - Arena Rewards");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client) throw std::runtime_error("window has no client widget");
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            resource = "Kenshi_ScrollView";
            g_profileScroll = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 200), "PG_RewardsProfileScroll");
            resource = "Kenshi_TextboxStandardText";
            g_title = NativeUI::Label(client, initial, "PG_RewardsTitle", "ARENA REWARDS");
            g_title->setVisible(false);
            g_fightersHeading = NativeUI::Label(client, initial, "PG_RewardsFightersHeading", "FIGHTERS");
            g_selectedHeading = NativeUI::Label(g_profileScroll, initial, "PG_RewardsSelectedHeading", "");
            g_selectedHeading->setVisible(false);
            resource = "Kenshi_WordWrap";
            g_subtitle = client->createWidget<MyGUI::EditBox>(resource, initial, MyGUI::Align::Default, "PG_RewardsSubtitle");
            g_subtitle->setCaption("NULL'S CRUCIBLE CATALOGUE  |  Victory buys privilege");
            g_status = client->createWidget<MyGUI::EditBox>(resource, initial, MyGUI::Align::Default, "PG_RewardsStatus");
            g_status->setCaption("Earn Marks in rated arena matches.");
            resource = "Kenshi_WordWrapEmpty";
            g_selectedName = NativeUI::WrappedLabel(g_profileScroll, initial, "PG_RewardsSelected", "");
            g_selectedName->setVisible(false);
            g_selectedCareer = NativeUI::WrappedLabel(g_profileScroll, initial, "PG_RewardsCareer", "No rewards available");
            resource = "Kenshi_ScrollView";
            g_fighterScroll = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 200), "PG_RewardsFighterScroll");
            g_detailScroll = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 200), "PG_RewardsDetailScroll");
            resource = "Kenshi_Button1";
            g_t1ArmourTab = NativeUI::Button(g_profileScroll, initial, "PG_RewardsGeneralTab", "GENERAL");
            g_t2ArmourTab = NativeUI::Button(g_profileScroll, initial, "PG_RewardsArmourTab", "ARMOUR");
            g_t3ArmourTab = NativeUI::Button(g_profileScroll, initial, "PG_RewardsWeaponsTab", "WEAPONS");
            MyGUI::Button* tabs[] = {g_t1ArmourTab, g_t2ArmourTab, g_t3ArmourTab};
            for (int i = 0; i < 3; ++i)
            {
                tabs[i]->eventMouseButtonClick += MyGUI::newDelegate(OnCatalogTabClicked);
                BindWheel(tabs[i]);
            }
            BindWheel(g_selectedHeading);
            BindWheel(g_selectedName);
            BindWheel(g_selectedCareer);
            for (int i = 0; i < ArenaRewards::PieceCount; ++i)
            {
                RewardCard& card = g_cards[i];
                card.piece = static_cast<ArenaRewards::Piece>(i);
                card.iconLoaded = false;
                card.visible = false;
                card.rarity = RewardsLayout::RarityIndex(
                    ArenaRewards::FirstTier(card.piece), false);
                const ArenaRewards::Reward& base = ArenaRewards::Get(
                    card.piece, ArenaRewards::FirstTier(card.piece));
                char name[64];
                sprintf_s(name, "PG_Reward_%d", i);
                const std::string prefix(name);
                MakeRewardAccent(card, prefix);
                resource = "ImageBox";
                card.icon = g_detailScroll->createWidget<MyGUI::ImageBox>(resource, initial,
                    MyGUI::Align::Default, prefix + "_Icon");
                card.iconArea = initial;
                card.icon->setVisible(false);
                resource = "Kenshi_WordWrapEmpty";
                card.name = NativeUI::WrappedLabel(g_detailScroll, initial, prefix + "_Name", base.pieceName);
                card.progress = NativeUI::WrappedLabel(g_detailScroll, initial, prefix + "_Progress", base.tierName);
                resource = "Kenshi_Button1";
                card.claim = NativeUI::Button(g_detailScroll, initial, prefix + "_Claim", "CLAIM");
                card.claim->eventMouseButtonClick += MyGUI::newDelegate(OnClaimClicked);
                BindWheel(card.icon);
                BindWheel(card.name);
                BindWheel(card.progress);
                BindWheel(card.claim);
            }

            for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
            {
                GeneralCard& card = g_general[i];
                card.item = static_cast<ArenaRewards::GeneralItem>(i);
                card.visible = false;
                card.iconLoaded = false;
                const ArenaRewards::GeneralReward& reward =
                    ArenaRewards::GetGeneral(card.item);
                char name[64];
                sprintf_s(name, "PG_General_%d", i);
                const std::string prefix(name);
                resource = "ImageBox";
                card.icon = g_detailScroll->createWidget<MyGUI::ImageBox>(
                    resource, initial, MyGUI::Align::Default,
                    prefix + "_Icon");
                card.iconArea = initial;
                card.icon->setVisible(false);
                resource = "Kenshi_WordWrapEmpty";
                card.name = NativeUI::WrappedLabel(g_detailScroll, initial,
                    prefix + "_Name", reward.name);
                card.details = NativeUI::WrappedLabel(g_detailScroll, initial,
                    prefix + "_Details", "");
                card.buy = NativeUI::Button(g_detailScroll, initial,
                    prefix + "_Buy", "BUY");
                card.buy->eventMouseButtonClick +=
                    MyGUI::newDelegate(OnGeneralClicked);
                BindWheel(card.icon);
                BindWheel(card.name);
                BindWheel(card.details);
                BindWheel(card.buy);
            }

            g_generalHeadings[0] = NativeUI::WrappedLabel(
                g_detailScroll, initial, "PG_GeneralBlueprintsHeading",
                "|  ARENA BLUEPRINTS");
            g_generalHeadings[1] = NativeUI::WrappedLabel(
                g_detailScroll, initial, "PG_GeneralSuppliesHeading",
                "|  FIGHT SUPPLIES");
            BindWheel(g_generalHeadings[0]);
            BindWheel(g_generalHeadings[1]);

            for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
            {
                LicenceCard& card = g_licences[i];
                const ArenaRewards::ArmourRow row =
                    static_cast<ArenaRewards::ArmourRow>(i);
                card.set = ArenaRewards::SetForRow(row);
                card.unlocked = false;
                char name[64];
                sprintf_s(name, "PG_Licence_%d", i);
                const std::string prefix(name);
                card.heading = NativeUI::WrappedLabel(g_detailScroll, initial,
                    prefix + "_Heading", std::string("|  ") +
                    ArmourRowName(row));
                card.details = NativeUI::WrappedLabel(g_detailScroll, initial,
                    prefix + "_Details", "");
                card.buy = NativeUI::Button(g_detailScroll, initial,
                    prefix + "_Buy", "UNLOCK LICENCE");
                card.buy->eventMouseButtonClick +=
                    MyGUI::newDelegate(OnLicenceClicked);
                BindWheel(card.heading);
                BindWheel(card.details);
                BindWheel(card.buy);
            }

            g_weaponLicence.set = ArenaRewards::ArmourSetNone;
            g_weaponLicence.unlocked = false;
            g_weaponLicence.heading = NativeUI::WrappedLabel(
                g_detailScroll, initial, "PG_WeaponLicence_Heading",
                "|  WEAPONS LICENCE");
            g_weaponLicence.details = NativeUI::WrappedLabel(
                g_detailScroll, initial, "PG_WeaponLicence_Details", "");
            g_weaponLicence.buy = NativeUI::Button(
                g_detailScroll, initial, "PG_WeaponLicence_Buy",
                "UNLOCK LICENCE");
            g_weaponLicence.buy->eventMouseButtonClick +=
                MyGUI::newDelegate(OnLicenceClicked);
            BindWheel(g_weaponLicence.heading);
            BindWheel(g_weaponLicence.details);
            BindWheel(g_weaponLicence.buy);

            resource = "Kenshi_Button1";
            g_close = NativeUI::Button(client, initial, "PG_RewardsClose", "Close catalogue");
            g_close->eventMouseButtonClick += MyGUI::newDelegate(OnCloseClicked);
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: native rewards creation failed at ") + resource + ": " + error.what());
            AbandonWindow();
        }
        catch (...)
        {
            PGLog::Error(std::string("Proving Grounds: native rewards creation failed at ") + resource);
            AbandonWindow();
        }
    }

    void RebuildRows()
    {
        ClearRows();
        if (!g_fighterScroll)
            return;
        std::vector<Character*> squad;
        SquadUtil::CollectPlayerSquad(squad);
        for (size_t i = 0; i < squad.size(); ++i)
        {
            Character* character = squad[i];
            if (!character || !character->isValid() || character->isDead())
                continue;
            char widgetName[64];
            sprintf_s(widgetName, "PG_RewardFighter_%d", static_cast<int>(i));
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            MyGUI::Button* root = NativeUI::Button(g_fighterScroll, initial, widgetName, "");
            const std::string rowName = character->getName();
            root->eventMouseButtonClick += MyGUI::newDelegate(OnFighterClicked);
            BindWheel(root);
            sprintf_s(widgetName, "PG_RewardPortrait_%d", static_cast<int>(i));
            MyGUI::ImageBox* portrait = root->createWidget<MyGUI::ImageBox>(
                "ImageBox", initial, MyGUI::Align::Default, widgetName);
            BindPortrait(portrait, character);
            portrait->eventMouseButtonClick += MyGUI::newDelegate(OnFighterClicked);
            BindWheel(portrait);
            sprintf_s(widgetName, "PG_RewardFighterName_%d", static_cast<int>(i));
            // Button surfaces use the native painted text family in vanilla and Dark UI.
            MyGUI::EditBox* name = root->createWidget<MyGUI::EditBox>(
                "Kenshi_PaintedWordWrapEmpty", initial, MyGUI::Align::Default, widgetName);
            name->setCaption(rowName);
            MyGUI::Widget* hit = name;
            while (hit)
            {
                hit->eventMouseButtonClick += MyGUI::newDelegate(OnFighterClicked);
                MyGUI::Widget* client = hit->getClientWidget();
                if (client == hit) break;
                hit = client;
            }
            BindWheel(name);
            sprintf_s(widgetName, "PG_RewardMarks_%d", static_cast<int>(i));
            MyGUI::TextBox* marksLabel = root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxPaintedText", initial, MyGUI::Align::Default, widgetName);
            char marksText[32];
            sprintf_s(marksText, "%d Marks", LeaderboardStore::GetMarks(character));
            marksLabel->setCaption(marksText);
            marksLabel->eventMouseButtonClick += MyGUI::newDelegate(OnFighterClicked);
            BindWheel(marksLabel);
            MyGUI::Widget* rail = NULL;
            try
            {
                rail = root->createWidget<MyGUI::Widget>("WhiteSkin",
                    MyGUI::IntCoord(0, 0, 3, 24), MyGUI::Align::Default,
                    std::string(widgetName) + "_Rail");
                rail->setColour(MyGUI::Colour(0.58f, 0.25f, 0.12f));
            }
            catch (...)
            {
                rail = NULL;
            }
            FighterRow row;
            row.root = root;
            row.portrait = portrait;
            row.name = name;
            row.marks = marksLabel;
            row.rail = rail;
            row.character = character;
            g_rows.push_back(row);
        }
        if (!IsLiveSquadSelection(g_selected))
        {
            g_selected = NULL;
            if (g_preferred && IsLiveSquadSelection(g_preferred))
                g_selected = g_preferred;
            else if (!g_rows.empty())
                g_selected = g_rows[0].character;
        }
    }

    void UpdateCatalogVisibility(bool visible)
    {
        MyGUI::Button* tabs[3] = {
            g_t1ArmourTab, g_t2ArmourTab, g_t3ArmourTab
        };
        for (int i = 0; i < 3; ++i)
        {
            tabs[i]->setVisible(visible);
            tabs[i]->setStateSelected(
                g_catalogTab == static_cast<CatalogTab>(i));
        }

        for (int i = 0; i < ArenaRewards::PieceCount; ++i)
        {
            const bool weapon = ArenaRewards::IsWeapon(g_cards[i].piece);
            const bool inTab = weapon ? g_catalogTab == CatalogWeapons :
                g_catalogTab == CatalogArmour &&
                LeaderboardStore::HasFactionUnlock(
                    ArenaRewards::GetLicence(
                        ArenaRewards::SetFor(g_cards[i].piece)).stableId);
            const bool show = visible && inTab;
            g_cards[i].visible = show;
            g_cards[i].icon->setVisible(show && g_cards[i].iconLoaded);
            g_cards[i].name->setVisible(show);
            g_cards[i].progress->setVisible(show);
            g_cards[i].claim->setVisible(show);
            if (g_cards[i].accent)
                g_cards[i].accent->setVisible(show);
        }

        const bool arenaOne = LeaderboardStore::HasFactionUnlock(
            "blueprint.arena1");
        g_generalHeadings[0]->setVisible(
            visible && g_catalogTab == CatalogGeneral);
        g_generalHeadings[1]->setVisible(
            visible && g_catalogTab == CatalogGeneral);
        for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
        {
            GeneralCard& card = g_general[i];
            card.visible = visible && g_catalogTab == CatalogGeneral &&
                ArenaRewards::IsGeneralVisible(card.item, arenaOne);
            card.name->setVisible(card.visible);
            card.details->setVisible(card.visible);
            card.buy->setVisible(card.visible);
            card.icon->setVisible(card.visible && card.iconLoaded);
        }
        for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
        {
            LicenceCard& card = g_licences[i];
            const bool show = visible && g_catalogTab == CatalogArmour;
            card.unlocked = LeaderboardStore::HasFactionUnlock(
                ArenaRewards::GetLicence(card.set).stableId);
            card.heading->setVisible(show);
            card.details->setVisible(show);
            card.buy->setVisible(show && !card.unlocked);
        }
        const bool showWeaponLicence =
            visible && g_catalogTab == CatalogWeapons;
        g_weaponLicence.unlocked = LeaderboardStore::HasFactionUnlock(
            ArenaRewards::GetWeaponLicence().stableId);
        g_weaponLicence.heading->setVisible(showWeaponLicence);
        g_weaponLicence.details->setVisible(showWeaponLicence);
        g_weaponLicence.buy->setVisible(
            showWeaponLicence && !g_weaponLicence.unlocked);
    }

    void UpdateDetails()
    {
        if (!g_selected || !g_selected->isValid())
        {
            if (g_title) g_title->setCaption("ARENA REWARDS");
            if (g_subtitle) g_subtitle->setCaption("NULL'S CRUCIBLE CATALOGUE  |  Victory buys privilege");
            if (g_fightersHeading) g_fightersHeading->setCaption("FIGHTERS");
            if (g_selectedCareer) g_selectedCareer->setCaption("NO ELIGIBLE FIGHTER  |  0 ARENA MARKS");
            UpdateCatalogVisibility(true);
            for (int i = 0; i < ArenaRewards::PieceCount; ++i)
            {
                g_cards[i].claim->setEnabled(false);
                g_cards[i].claim->setCaption("CLAIM");
                g_cards[i].progress->setCaption("Select a fighter to review requirements.");
                g_cards[i].rarity = RewardsLayout::RarityIndex(
                    ArenaRewards::FirstTier(g_cards[i].piece), false);
            }
            for (size_t i = 0; i < g_rows.size(); ++i)
            {
                g_rows[i].root->setStateSelected(false);
                if (g_rows[i].rail)
                    g_rows[i].rail->setVisible(false);
            }
            for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
                g_general[i].buy->setEnabled(false);
            for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
                g_licences[i].buy->setEnabled(false);
            g_weaponLicence.buy->setEnabled(false);
            return;
        }

        const int marks = LeaderboardStore::GetMarks(g_selected);
        if (g_title) g_title->setCaption("ARENA REWARDS");
        if (g_fightersHeading) g_fightersHeading->setCaption("FIGHTERS");
        char career[64];
        sprintf_s(career, "%d ARENA MARKS", marks);
        g_selectedCareer->setCaption(career);

        UpdateCatalogVisibility(true);

        for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
        {
            GeneralCard& card = g_general[i];
            const ArenaRewards::GeneralReward& reward =
                ArenaRewards::GetGeneral(card.item);
            char details[128];
            sprintf_s(details, "%s  |  %d Marks%s",
                card.item == ArenaRewards::GeneralArenaBuildingsI ||
                card.item == ArenaRewards::GeneralArenaBuildingsII
                    ? "PHYSICAL BLUEPRINT BOOK" : "REPEATABLE FIGHT SUPPLY",
                reward.cost,
                marks < reward.cost ? "  |  INSUFFICIENT MARKS" : "");
            card.details->setCaption(details);
            card.buy->setCaption(
                reward.grantsUnlockId &&
                LeaderboardStore::HasFactionUnlock(reward.grantsUnlockId)
                    ? "BUY AGAIN" : "BUY");
            card.buy->setEnabled(card.visible && marks >= reward.cost);
        }

        for (int i = 0; i < ArenaRewards::ArmourRowCount; ++i)
        {
            LicenceCard& card = g_licences[i];
            const ArenaRewards::ArmourRow row =
                static_cast<ArenaRewards::ArmourRow>(i);
            const ArenaRewards::Licence& licence =
                ArenaRewards::GetLicence(card.set);
            const bool prerequisite = !licence.prerequisiteId ||
                LeaderboardStore::HasFactionUnlock(licence.prerequisiteId);
            const char* manifest = ArmourRowManifest(row);
            char details[256];
            if (card.unlocked)
                sprintf_s(details,
                    "LICENCE ACTIVE  |  %s", manifest);
            else if (!prerequisite)
                sprintf_s(details,
                    "LOCKED  |  Previous armour licence required  |  %s",
                    manifest);
            else
                sprintf_s(details,
                    "FACTION LICENCE  |  %d Marks%s  |  %s",
                    licence.cost,
                    marks < licence.cost ? "  |  INSUFFICIENT MARKS" : "",
                    manifest);
            card.details->setCaption(details);
            card.buy->setEnabled(ArenaRewards::CanPurchaseLicence(
                card.set, card.unlocked, prerequisite, marks));
            card.buy->setCaption(prerequisite
                ? "UNLOCK LICENCE" : "PREREQUISITE REQUIRED");
        }

        const ArenaRewards::Licence& weaponLicence =
            ArenaRewards::GetWeaponLicence();
        char weaponLicenceDetails[192];
        if (g_weaponLicence.unlocked)
            sprintf_s(weaponLicenceDetails,
                "LICENCE ACTIVE  |  All weapon rewards available");
        else
            sprintf_s(weaponLicenceDetails,
                "FACTION LICENCE  |  %d Marks%s  |  Unlocks all weapon rewards",
                weaponLicence.cost,
                marks < weaponLicence.cost ? "  |  INSUFFICIENT MARKS" : "");
        g_weaponLicence.details->setCaption(weaponLicenceDetails);
        g_weaponLicence.buy->setEnabled(
            ArenaRewards::CanPurchaseWeaponLicence(
                g_weaponLicence.unlocked, marks));
        g_weaponLicence.buy->setCaption("UNLOCK LICENCE");

        for (int i = 0; i < ArenaRewards::PieceCount; ++i)
        {
            RewardCard& card = g_cards[i];
            if (ArenaRewards::IsWeapon(card.piece) &&
                !g_weaponLicence.unlocked)
            {
                card.progress->setCaption("WEAPONS LICENCE REQUIRED");
                card.claim->setCaption("LOCKED");
                card.claim->setEnabled(false);
                card.rarity = RewardsLayout::RarityIndex(
                    ArenaRewards::FirstTier(card.piece), false);
                continue;
            }
            const ArenaRewards::Offer offer = RewardDelivery::ResolveOffer(
                g_selected, card.piece);
            if (offer.kind == ArenaRewards::OfferComplete)
            {
                const int lastTier = static_cast<int>(
                    ArenaRewards::FirstTier(card.piece)) +
                    ArenaRewards::TiersFor(card.piece) - 1;
                const ArenaRewards::Reward& top = ArenaRewards::Get(
                    card.piece, static_cast<ArenaRewards::Tier>(lastTier));
                char done[128];
                sprintf_s(done, "%s CLAIMED", top.tierName);
                card.progress->setCaption(done);
                card.claim->setCaption("COMPLETE");
                card.claim->setEnabled(false);
                card.rarity = RewardsLayout::RarityIndex(lastTier, true);

                continue;
            }

            const ArenaRewards::Reward& reward = ArenaRewards::Get(
                card.piece, offer.tier);
            char progress[160];
            if (offer.kind == ArenaRewards::OfferReplace)
            {
                sprintf_s(progress, "REPLACE: %s  |  %d Marks%s",
                    reward.tierName,
                    offer.cost,
                    marks < offer.cost ? "  |  INSUFFICIENT MARKS" : "");
                card.claim->setCaption("REPLACE");
            }
            else
            {
                sprintf_s(progress, "NEXT: %s  |  %d Marks%s",
                    reward.tierName,
                    offer.cost,
                    marks < offer.cost ? "  |  INSUFFICIENT MARKS" : "");
                card.claim->setCaption(
                    offer.tier == ArenaRewards::FirstTier(card.piece)
                        ? "CLAIM" : "UPGRADE");
            }
            card.progress->setCaption(progress);
            card.rarity = RewardsLayout::RarityIndex(offer.tier, false);
            const bool enabled = marks >= offer.cost;
            card.claim->setEnabled(enabled);

        }
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            char rowMarks[32];
            sprintf_s(rowMarks, "%d Marks",
                LeaderboardStore::GetMarks(g_rows[i].character));
            g_rows[i].marks->setCaption(rowMarks);
            g_rows[i].root->setStateSelected(g_rows[i].character == g_selected);
            if (g_rows[i].rail)
                g_rows[i].rail->setVisible(
                    g_rows[i].character == g_selected);

        }
    }

    void RefreshDetails()
    {
        if (!g_window) return;
        try { UpdateDetails(); FitWindowToScreen(); LayoutWindow(); }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native rewards content/layout failed");
            AbandonWindow();
        }
    }

    void RefreshAll()
    {
        try
        {
            RebuildRows();
            for (int i = 0; i < ArenaRewards::PieceCount; ++i)
                BindRewardIcon(g_cards[i]);
            for (int i = 0; i < ArenaRewards::GeneralItemCount; ++i)
                BindGeneralIcon(g_general[i]);
            RefreshDetails();
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native rewards content failed (Kenshi_Button1/Kenshi_PaintedWordWrapEmpty/Kenshi_TextboxPaintedText/ImageBox)");
            AbandonWindow();
        }
    }
}

namespace RewardsUI
{
    void Show(Character* preferred)
    {
        EnsureWindow();
        if (!g_window)
        {
            PGLog::Error("Proving Grounds: Rewards UI could not create window");
            return;
        }
        g_preferred = (preferred && preferred->isValid()) ? preferred : NULL;
        g_selected = g_preferred;
        g_fighterScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        g_profileScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        g_detailScroll->setViewOffset(MyGUI::IntPoint(
            0, -g_tabOffsets[g_catalogTab]));
        RefreshAll();
        if (!g_window) return;
        if (g_selected && g_selected->isValid())
        {
            char statusLine[128];
            sprintf_s(statusLine, "Purchases go to %s.",
                g_selected->getName().c_str());
            g_status->setCaption(statusLine);
        }
        else
        {
            g_status->setCaption("No eligible fighters available.");
        }
        RefreshDetails();
        if (!g_window) return;
        g_window->setVisible(true);
    }

    void Close() { if (g_window) g_window->setVisible(false); }
    bool IsVisible() { return g_window && g_window->getVisible(); }
    void Tick()
    {
        if (!IsVisible()) return;
        try
        {
            FitWindowToScreen();
            if (g_window->getClientWidget()->getSize() != g_clientSize || BodyHeight() != g_fontHeight)
                LayoutWindow();
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native rewards resize failed");
            AbandonWindow();
        }
    }

    void AbandonWorldState()
    {
        Close();
        ClearRows();
        g_selected = NULL;
        g_preferred = NULL;
    }
}
