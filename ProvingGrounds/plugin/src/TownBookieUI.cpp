#include "TownBookieUI.h"
#include "TownBettingPolicy.h"
#include "TownMatchmakingRuntime.h"
#include "NativeUI.h"
#include "PGLog.h"
#include <kenshi/Character.h>
#include <kenshi/gui/PortraitManager.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_WidgetToolTip.h>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {
    const int kMembers = 4;
    const int kAdjustments[] = {-1000, -100, -10, 10, 100, 1000};
    struct Card {
        MyGUI::Widget* root;
        MyGUI::ImageBox* portrait;
        MyGUI::EditBox *name, *record, *placeholder;
        MyGUI::Widget* accent;
        std::string boundKey;
        bool hasPortrait;
        Card() : root(NULL), portrait(NULL), name(NULL), record(NULL),
            placeholder(NULL), accent(NULL), hasPortrait(false) {}
    };
    MyGUI::Window* window = NULL;
    MyGUI::ScrollView* roster = NULL;
    MyGUI::EditBox* fighterTip = NULL;
    MyGUI::Widget* tipBackground = NULL;
    MyGUI::Widget* tipOwner = NULL;
    MyGUI::EditBox *header = NULL, *timer = NULL, *status = NULL, *slip = NULL, *versus = NULL;
    MyGUI::EditBox* stakeValue = NULL;
    MyGUI::EditBox* heading[2] = {};
    MyGUI::Button *sides[2] = {}, *adjustments[6] = {}, *bet = NULL, *arrange = NULL;
    Card cards[2][kMembers];
    TownBookieUI::Actions actions;
    int counts[2] = {}, contentHeight = 0;
    MyGUI::IntSize screenSize;
    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int Body() { return Max(1, bet->getFontHeight()); }
    void Caption(MyGUI::TextBox* label, const std::string& text) {
        if (label->getCaption().asUTF8() != text) label->setCaption(text);
    }
    int Wrap(MyGUI::EditBox* label, int x, int y, int width, int maximum = 0) {
        const size_t oldScroll = label->getVScrollPosition();
        label->setCoord(x, y, Max(1, width), Max(100, Body() * 4));
        const int inset = Max(0, label->getHeight() - label->getTextRegion().height);
        int height = Max(Body(), label->getTextSize().height) + inset;
        if (maximum) height = Min(height, Max(Body() + inset, maximum));
        label->setSize(Max(1, width), height);
        label->setVScrollPosition(oldScroll);
        return height;
    }
    void HideTip() {
        tipOwner = NULL;
        if (tipBackground) tipBackground->setVisible(false);
    }
    void Passive(MyGUI::Widget* widget) {
        widget->setNeedMouseFocus(false);
        widget->setNeedKeyFocus(false);
        for (size_t i = 0; i < widget->getChildCount(); ++i) Passive(widget->getChildAt(i));
        MyGUI::Widget* client = widget->getClientWidget();
        if (client && client != widget) Passive(client);
    }
    void PlaceTip(const MyGUI::IntPoint& point) {
        const MyGUI::IntPoint origin = window->getClientWidget()->getAbsolutePosition();
        const MyGUI::IntSize size = window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(bet);
        int x = point.left - origin.left + 2 * gap;
        int y = point.top - origin.top + 3 * gap;
        if (x + tipBackground->getWidth() > size.width - gap)
            x = point.left - origin.left - tipBackground->getWidth() - gap;
        if (y + tipBackground->getHeight() > size.height - gap)
            y = point.top - origin.top - tipBackground->getHeight() - gap;
        tipBackground->setPosition(Max(gap, x), Max(gap, y));
    }
    void PortraitTip(MyGUI::Widget* sender, const MyGUI::ToolTipInfo& info) {
        if (info.type == MyGUI::ToolTipInfo::Hide) {
            if (tipOwner == sender) HideTip();
        } else if (info.type == MyGUI::ToolTipInfo::Show && fighterTip && window->getVisible()) {
            const std::string& text = sender->getUserString("fighterTip");
            if (text.empty()) { HideTip(); return; }
            tipOwner = sender;
            Caption(fighterTip, text);
            const int gap = NativeUI::Spacing(bet);
            const MyGUI::IntSize size = window->getClientWidget()->getSize();
            // Measure at the available width, then fit the longest rendered line.
            const int maxWidth = Max(1, size.width - 2 * gap);
            fighterTip->setSize(maxWidth, Max(100, Body() * 4));
            const int inset = Max(0, fighterTip->getWidth() - fighterTip->getTextRegion().width);
            const int width = Min(maxWidth, fighterTip->getTextSize().width + inset + 2 * gap);
            Wrap(fighterTip, 0, 0, width, size.height - 2 * gap);
            tipBackground->setSize(fighterTip->getSize());
            PlaceTip(info.point);
            tipBackground->setVisible(true);
            tipBackground->upLayerItem();
        } else if (info.type == MyGUI::ToolTipInfo::Move && tipOwner == sender && fighterTip) {
            PlaceTip(info.point);
        }
    }
    void BindTip(MyGUI::Widget* widget) {
        while (widget) {
            widget->setNeedToolTip(true);
            widget->eventToolTip += MyGUI::newDelegate(PortraitTip);
            MyGUI::Widget* next = widget->getClientWidget();
            if (next == widget) break;
            widget = next;
        }
    }
    void SetTip(MyGUI::Widget* widget, const std::string& text) {
        while (widget) {
            if (widget->getUserString("fighterTip") != text) {
                if (tipOwner == widget) HideTip();
                widget->setUserString("fighterTip", text);
            }
            MyGUI::Widget* next = widget->getClientWidget();
            if (next == widget) break;
            widget = next;
        }
    }
    void Wheel(MyGUI::Widget*, int delta) {
        if (!roster || !delta) return;
        HideTip();
        const int limit = Max(0, contentHeight - roster->getViewCoord().height);
        int offset = -roster->getViewOffset().top + (delta < 0 ? 3 : -3) * Body();
        roster->setViewOffset(MyGUI::IntPoint(0, -Max(0, Min(limit, offset))));
    }
    void BindWheel(MyGUI::Widget* widget) {
        while (widget) {
            widget->setNeedMouseFocus(true);
            widget->eventMouseWheel += MyGUI::newDelegate(Wheel);
            MyGUI::Widget* next = widget->getClientWidget();
            if (next == widget) break;
            widget = next;
        }
    }
    void Select(MyGUI::Widget* sender) {
        if (actions.selectSide) actions.selectSide(std::atoi(sender->getUserString("side").c_str()));
    }
    void Stake(MyGUI::Widget* sender) {
        if (actions.changeStake) actions.changeStake(std::atoi(sender->getUserString("stake").c_str()));
    }
    void Bet(MyGUI::Widget*) { if (actions.placeBet) actions.placeBet(); }
    void Arrange(MyGUI::Widget*) { if (actions.arrange) actions.arrange(); }
    void WindowButton(MyGUI::Widget*, const std::string& name) {
        if ((name == "close" || name == "Close") && actions.close) actions.close();
    }
    MyGUI::EditBox* Text(MyGUI::Widget* parent, const std::string& name, bool scroll) {
        MyGUI::EditBox* label = NativeUI::WrappedLabel(parent, MyGUI::IntCoord(0, 0, 100, 24), name, "");
        if (scroll) BindWheel(label);
        return label;
    }
    MyGUI::EditBox* BoundedText(MyGUI::Widget* parent, const std::string& name) {
        // Unlike WordWrapEmpty this native skin supplies a scrollbar for text
        // whose measured height must be capped to keep the controls reachable.
        return parent->createWidget<MyGUI::EditBox>("Kenshi_WordWrap",
            MyGUI::IntCoord(0, 0, 100, 24), MyGUI::Align::Default, name);
    }
    void CreateCard(int side, int slot) {
        Card& card = cards[side][slot];
        char prefix[48]; sprintf_s(prefix, "PG_BookieCard%d_%d", side, slot);
        const std::string key(prefix);
        const MyGUI::IntCoord initial(0, 0, 100, 24);
        card.root = roster->createWidget<MyGUI::Widget>("PanelEmpty", initial, MyGUI::Align::Default, key);
        BindWheel(card.root);
        card.portrait = card.root->createWidget<MyGUI::ImageBox>("ImageBox", initial, MyGUI::Align::Default, key + "Portrait");
        BindWheel(card.portrait);
        BindTip(card.portrait);
        card.name = Text(card.root, key + "Name", true);
        card.record = Text(card.root, key + "Record", true);
        card.placeholder = Text(card.root, key + "Placeholder", true);
        BindTip(card.placeholder);
        card.placeholder->setCaption("Portrait unavailable");
        card.placeholder->setTextAlign(MyGUI::Align::Center);
        // Decoration is optional; missing skin must not prevent betting.
        try {
            card.accent = card.root->createWidget<MyGUI::Widget>("WhiteSkin", initial, MyGUI::Align::Default, key + "Accent");
            card.accent->setColour(side == 0 ? MyGUI::Colour(.65f, .30f, .23f) : MyGUI::Colour(.30f, .48f, .63f));
            card.accent->setAlpha(.65f);
            card.accent->setNeedMouseFocus(false);
        } catch (...) { card.accent = NULL; }
    }
    bool EnsureWindow() {
        if (window) return true;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return false;
        try {
            window = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", .06f, .05f, .88f, .90f,
                MyGUI::Align::Center, "Window", "PG_Bookie");
            window->setCaption("Scratch Bookie - Proving Grounds");
            window->setVisible(false);
            window->eventWindowButtonPressed += MyGUI::newDelegate(WindowButton);
            MyGUI::Widget* client = window->getClientWidget();
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            header = BoundedText(client, "PG_BookieHeader");
            timer = BoundedText(client, "PG_BookieTimer");
            timer->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
            timer->setTextColour(MyGUI::Colour(.95f, .72f, .22f));
            roster = NativeUI::Scroll(client, initial, "PG_BookieRoster");
            status = BoundedText(client, "PG_BookieStatus");
            slip = BoundedText(client, "PG_BookieSlip");
            stakeValue = Text(client, "PG_BookieStakeValue", false);
            stakeValue->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
            stakeValue->setTextColour(MyGUI::Colour(.95f, .72f, .22f));
            versus = Text(roster, "PG_BookieVS", true);
            versus->setCaption("VS"); versus->setTextAlign(MyGUI::Align::Center);
            bet = NativeUI::Button(client, initial, "PG_BookieBet", "Place bet");
            bet->eventMouseButtonClick += MyGUI::newDelegate(Bet);
            arrange = NativeUI::Button(client, initial, "PG_BookieArrange", "Arrange next NPC bout");
            arrange->eventMouseButtonClick += MyGUI::newDelegate(Arrange);
            for (int i = 0; i < 6; ++i) {
                char name[40], caption[32], value[16];
                sprintf_s(name, "PG_BookieStakeAdjust%d", i); sprintf_s(value, "%d", kAdjustments[i]);
                sprintf_s(caption, "%+d", kAdjustments[i]);
                adjustments[i] = NativeUI::Button(client, initial, name, caption);
                adjustments[i]->setUserString("stake", value);
                adjustments[i]->eventMouseButtonClick += MyGUI::newDelegate(Stake);
            }
            for (int side = 0; side < 2; ++side) {
                char name[40], value[8]; sprintf_s(value, "%d", side);
                sprintf_s(name, "PG_BookieTeam%d", side);
                heading[side] = Text(roster, name, true);
                sprintf_s(name, "PG_BookieSide%d", side);
                sides[side] = NativeUI::Button(client, initial, name, side == 0 ? "Select Team A" : "Select Team B");
                sides[side]->setUserString("side", value);
                sides[side]->eventMouseButtonClick += MyGUI::newDelegate(Select);
                for (int i = 0; i < kMembers; ++i) CreateCard(side, i);
            }
            // A separate overlap node keeps background and text above the cards together.
            tipBackground = client->createWidget<MyGUI::Widget>(MyGUI::WidgetStyle::Overlapped, "WhiteSkin", initial,
                MyGUI::Align::Default, "", "PG_BookieFighterTipBackground");
            tipBackground->setColour(MyGUI::Colour(.06f, .06f, .06f));
            tipBackground->setAlpha(1.0f);
            fighterTip = BoundedText(tipBackground, "PG_BookieFighterTip");
            tipBackground->setVisible(false);
            Passive(tipBackground);
            return true;
        } catch (const std::exception& error) {
            PGLog::Error(std::string("Proving Grounds: bookie UI creation failed: ") + error.what());
        } catch (...) { PGLog::Error("Proving Grounds: bookie UI creation failed"); }
        TownBookieUI::Destroy();
        return false;
    }
    int LayoutCard(Card& card, int x, int y, int width, bool solo) {
        const int gap = NativeUI::Spacing(bet), inner = Max(1, width - 2 * gap);
        card.root->setCoord(x, y, Max(1, width), 1000);
        int top = gap;
        if (card.accent) card.accent->setCoord(gap, 0, inner, Max(1, gap / 3));
        top += Wrap(card.name, gap, top, inner) + gap;
        const bool beside = inner >= Body() * 27;
        const int portrait = Max(1, Min(solo ? Body() * 13 : Body() * 8,
            beside ? inner * 45 / 100 : Min(inner, Body() * 10)));
        const int px = beside ? gap : gap + (inner - portrait) / 2;
        card.portrait->setCoord(px, top, portrait, portrait);
        card.placeholder->setCoord(px, top, portrait, portrait);
        const int detailX = beside ? gap + portrait + gap : gap;
        const int detailWidth = beside ? Max(1, inner - portrait - gap) : inner;
        int detailY = beside ? top : top + portrait + gap;
        detailY += Wrap(card.record, detailX, detailY, detailWidth) + gap;
        const int height = Max(top + portrait, detailY) + 2 * gap;
        card.root->setSize(Max(1, width), height);
        return height;
    }
    void Layout() {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (render && render->getViewSize() != screenSize) {
            HideTip();
            screenSize = render->getViewSize();
            const int width = screenSize.width * 88 / 100, height = screenSize.height * 90 / 100;
            window->setCoord((screenSize.width - width) / 2, (screenSize.height - height) / 2, width, height);
        }
        const MyGUI::IntSize size = window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(bet), width = Max(1, size.width - 2 * gap);
        const int row = Max(NativeUI::RowHeight(bet, 0), NativeUI::RowHeight(arrange, 0));
        const int infoHalf = Max(1, (width - gap) / 2);
        const int headerWidth = timer->getVisible() ? infoHalf : width;
        const int headerH = Wrap(header, gap, gap, headerWidth, Max(Body() * 2, size.height / 9));
        int topH = headerH;
        if (timer->getVisible()) {
            const int timerH = Wrap(timer, gap * 2 + infoHalf, gap,
                Max(1, width - infoHalf - gap), Max(Body() * 2, size.height / 9));
            topH = Max(topH, timerH);
        }
        int top = gap + topH + gap;
        const int actionMin = Max(bet->getTextSize().width, arrange->getTextSize().width) + 4 * gap;
        const int actionColumns = width >= 2 * actionMin + gap ? 2 : 1;
        const int actionHeight = actionColumns == 2 ? row : 2 * row + gap;
        int bottom = size.height - gap - actionHeight;
        const int half = Max(1, (width - gap) / 2);
        bet->setCoord(gap, bottom, actionColumns == 2 ? half : width, row);
        arrange->setCoord(actionColumns == 2 ? gap * 2 + half : gap,
            actionColumns == 2 ? bottom : bottom + row + gap,
            actionColumns == 2 ? Max(1, width - half - gap) : width, row);
        const int slipH = Wrap(slip, gap, 0, infoHalf, Max(Body() * 3, size.height / 8));
        const int statusH = Wrap(status, gap * 2 + infoHalf, 0,
            Max(1, width - infoHalf - gap), Max(Body() * 3, size.height / 8));
        const int infoH = Max(slipH, statusH);
        bottom -= infoH + gap;
        slip->setPosition(gap, bottom);
        status->setPosition(gap * 2 + infoHalf, bottom);
        const int stakeColumn = Max(1, (width - 6 * gap) / 7);
        bottom -= row + gap;
        for (int i = 0; i < 3; ++i)
            adjustments[i]->setCoord(gap + i * (stakeColumn + gap), bottom, stakeColumn, row);
        stakeValue->setCoord(gap + 3 * (stakeColumn + gap), bottom, stakeColumn, row);
        for (int i = 3; i < 6; ++i)
            adjustments[i]->setCoord(gap + (i + 1) * (stakeColumn + gap), bottom,
                i == 5 ? Max(1, size.width - gap - (gap + (i + 1) * (stakeColumn + gap))) : stakeColumn, row);
        // Both sides remain selectable even when one team's cards are offscreen.
        bottom -= row + gap;
        sides[0]->setCoord(gap, bottom, half, row);
        sides[1]->setCoord(gap * 2 + half, bottom, Max(1, width - half - gap), row);
        const int rosterTop = top;
        roster->setCoord(gap, rosterTop, width, Max(1, bottom - rosterTop - gap));
        const int oldOffset = -roster->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass) {
            const MyGUI::IntCoord view = roster->getViewCoord();
            const int usable = Max(1, view.width - 2 * gap);
            const bool columns = usable >= Body() * 58;
            const int divider = columns ? Body() * 3 : 0;
            const int teamWidth = columns ? Max(1, (usable - divider - 2 * gap) / 2) : usable;
            versus->setVisible(columns);
            if (columns) versus->setCoord(gap * 2 + teamWidth, gap + Body() * 7, divider, Body() * 3);
            int teamTop = gap, maximum = gap;
            for (int side = 0; side < 2; ++side) {
                const int x = columns ? gap + side * (teamWidth + divider + 2 * gap) : gap;
                int y = teamTop + Wrap(heading[side], x, teamTop, teamWidth) + gap;
                const int cardColumns = counts[side] > 1 && teamWidth >= Body() * 35 ? 2 : 1;
                const int cardWidth = Max(1, (teamWidth - (cardColumns - 1) * gap) / cardColumns);
                for (int i = 0; i < counts[side]; i += cardColumns) {
                    int height = 0;
                    for (int j = 0; j < cardColumns && i + j < counts[side]; ++j)
                        height = Max(height, LayoutCard(cards[side][i+j], x + j * (cardWidth + gap), y,
                            cardWidth, counts[side] == 1));
                    y += height + gap;
                }
                y += 2 * gap;
                maximum = Max(maximum, y);
                if (!columns) teamTop = y;
            }
            contentHeight = maximum;
            roster->setCanvasSize(Max(1, view.width), Max(view.height, maximum));
            if (view.width == roster->getViewCoord().width) break;
        }
        roster->setViewOffset(MyGUI::IntPoint(0, -Max(0, Min(oldOffset, contentHeight - roster->getViewCoord().height))));
    }
    void BindCard(Card& card, const TownBookieUI::FighterView& fighter) {
        Caption(card.name, fighter.name + "\n" + fighter.style);
        Caption(card.record, fighter.record);
        const std::string tip = fighter.name + "\n" + fighter.style + "\n\n" + fighter.stats;
        SetTip(card.portrait, tip);
        SetTip(card.placeholder, tip);
        Character* actor = fighter.actor.getCharacter();
        const std::string key = actor && actor->isValid() ? TownMatchmakingRuntime::SelectionIdentity(actor) : "";
        if (key != card.boundKey || (!key.empty() && !card.hasPortrait)) {
            card.boundKey = key;
            card.hasPortrait = false;
            PortraitManager* manager = PortraitManager::getInstance();
            if (!key.empty() && manager) {
                manager->setImageWidget(actor->getHandle(), card.portrait, true);
                card.hasPortrait = true;
            }
        }
        card.portrait->setVisible(card.hasPortrait && !key.empty());
        card.placeholder->setVisible(!card.hasPortrait || key.empty());
    }
}
namespace TownBookieUI {
    bool IsVisible() { return window && window->getVisible(); }
    void Close() { HideTip(); if (window) window->setVisible(false); }
    void Destroy() {
        if (window && MyGUI::Gui::getInstancePtr()) MyGUI::Gui::getInstance().destroyWidget(window);
        window = NULL; roster = NULL; header = timer = status = slip = versus = NULL;
        stakeValue = NULL; bet = arrange = NULL;
        fighterTip = NULL; tipBackground = NULL; tipOwner = NULL;
        for (int side = 0; side < 2; ++side) {
            heading[side] = NULL; sides[side] = NULL; counts[side] = 0;
            for (int i = 0; i < kMembers; ++i) cards[side][i] = Card();
        }
        for (int i = 0; i < 6; ++i) adjustments[i] = NULL;
        actions = Actions(); screenSize = MyGUI::IntSize(); contentHeight = 0;
    }
    void Refresh(const View& view) {
        if (!window) return;
        try {
            Caption(header, view.header); Caption(timer, view.timer);
            Caption(status, view.status); Caption(slip, view.slip);
            timer->setVisible(!view.timer.empty());
            roster->setVisible(true); status->setVisible(true); slip->setVisible(true);
            bet->setVisible(true); stakeValue->setVisible(true);
            bet->setEnabled(view.canBet); arrange->setEnabled(view.canArrange);
            char stakeCaption[48]; sprintf_s(stakeCaption, "STAKE: %d CATS", view.stake);
            Caption(stakeValue, stakeCaption);
            for (int i = 0; i < 6; ++i) {
                adjustments[i]->setVisible(true);
                adjustments[i]->setEnabled(view.canStake &&
                    TownBettingPolicy::AdjustStake(view.stake, kAdjustments[i]) != view.stake);
            }
            for (int side = 0; side < 2; ++side) {
                sides[side]->setVisible(true);
                counts[side] = Min(kMembers, static_cast<int>(view.fighters[side].size()));
                Caption(heading[side], std::string(side == 0 ? "TEAM A" : "TEAM B") +
                    (view.selectedSide == side ? " [SELECTED]" : "") + "  |  " + view.odds[side]);
                sides[side]->setStateSelected(view.selectedSide == side);
                sides[side]->setCaption(side == 0 ? (view.selectedSide == side ? "Team A selected" : "Select Team A") :
                    (view.selectedSide == side ? "Team B selected" : "Select Team B"));
                sides[side]->setEnabled(view.canSelect);
                for (int i = 0; i < kMembers; ++i) {
                    if (i >= counts[side]) {
                        SetTip(cards[side][i].portrait, "");
                        SetTip(cards[side][i].placeholder, "");
                    }
                    cards[side][i].root->setVisible(i < counts[side]);
                    if (i < counts[side]) BindCard(cards[side][i], view.fighters[side][i]);
                }
            }
            Layout();
        } catch (const std::exception& error) {
            PGLog::Error(std::string("Proving Grounds: bookie UI refresh failed: ") + error.what()); Destroy();
        } catch (...) { PGLog::Error("Proving Grounds: bookie UI refresh failed"); Destroy(); }
    }
    bool Show(const View& view, const Actions& callbacks) {
        if (!EnsureWindow()) return false;
        actions = callbacks; window->setVisible(true); Refresh(view);
        return IsVisible();
    }
}
