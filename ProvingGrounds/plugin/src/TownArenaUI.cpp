#include "TownArenaUI.h"
#include "TownArena.h"
#include "ArenaIdentity.h"
#include "ArenaMarks.h"
#include "ArenaCombatProfile.h"
#include "SquadUtil.h"
#include "NativeUI.h"
#include "TownRosterSelection.h"
#include "TownCardLayout.h"
#include "TownChallengeBuyInPolicy.h"
#include "TownChallengePolicy.h"
#include "PGConfig.h"
#include "TownMatchmakingRuntime.h"
#include "LeaderboardLayout.h"
#include "RosterStatus.h"
#include "PGLog.h"
#include <kenshi/Character.h>
#include <kenshi/Building/Building.h>
#include <kenshi/gui/PortraitManager.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_Colour.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_WidgetToolTip.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <Windows.h>

namespace
{
    struct FighterRow {
        std::string id;
        MyGUI::Button* button;
        MyGUI::ImageBox* portrait;
        MyGUI::EditBox* text;
        MyGUI::Widget* statusBack;
        MyGUI::Widget* statusFill;
        MyGUI::Widget* border[4];
    };
    struct CardWidgets {
        MyGUI::Widget* root;
        MyGUI::Widget* accent;
        MyGUI::Widget* fade[16];
        MyGUI::Widget* border[4];
        MyGUI::EditBox* heading;
        MyGUI::ImageBox* portraits[4];
        MyGUI::EditBox* placeholders[4];
        MyGUI::EditBox* names[4];
        MyGUI::EditBox* stats[4];
        std::string portraitKeys[4];
        int portraitCount;
        MyGUI::EditBox* text;
        MyGUI::EditBox* terms;
        MyGUI::Button* book;
    };
    MyGUI::Window* window = NULL;
    MyGUI::EditBox* fighterTip = NULL;
    MyGUI::Widget* tipBackground = NULL;
    MyGUI::Widget* tipOwner = NULL;
    MyGUI::Button* booking = NULL;
    MyGUI::Button* refreshButton = NULL;
    MyGUI::Button* divisions[3] = {};
    MyGUI::ScrollView* challenges = NULL;
    MyGUI::ScrollView* sidebar = NULL;
    MyGUI::ScrollView* roster = NULL;
    MyGUI::EditBox* squad = NULL;
    MyGUI::EditBox* rosterHint = NULL;
    MyGUI::EditBox* emptyRoster = NULL;
    MyGUI::EditBox* direct = NULL;
    MyGUI::EditBox* nextRefresh = NULL;
    MyGUI::EditBox* status = NULL;
    MyGUI::EditBox* progression = NULL;
    MyGUI::TextBox* metrics = NULL;
    MyGUI::Widget* skarnPanel = NULL;
    MyGUI::ImageBox* skarnPortrait = NULL;
    MyGUI::EditBox* skarnPlaceholder = NULL;
    MyGUI::EditBox* skarnState = NULL;
    MyGUI::EditBox* skarnTipLines[7] = {};
    std::string skarnPortraitKey;
    CardWidgets cards[6] = {};
    std::vector<FighterRow> rows;
    std::vector<std::string> lineup;
    std::string lastStatus;
    int selectedOffer = -1, displayedDivision = -1;
    bool showingSelectionPrompt = false;
    DWORD lastRefresh = 0;
    MyGUI::IntSize screenSize;

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int Body() { return Max(1, metrics->getFontHeight()); }
    int Gap() { return NativeUI::Spacing(metrics); }
    bool Locked() { return TownArena::IsLineupLocked(); }
    bool Assigned(const std::string& id) { return std::find(lineup.begin(), lineup.end(), id) != lineup.end(); }
    void SetText(MyGUI::TextBox* text, const std::string& caption) {
        if (text->getCaption().asUTF8() != caption) text->setCaption(caption);
    }
    void Passive(MyGUI::Widget* widget) {
        while (widget) {
            widget->setNeedMouseFocus(false); widget->setNeedKeyFocus(false);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }
    void HideTip() {
        tipOwner = NULL;
        if (tipBackground) tipBackground->setVisible(false);
    }
    void ScrollBy(MyGUI::ScrollView* scroll, int relative) {
        if (!scroll || !relative) return;
        HideTip();
        const int offset = LeaderboardLayout::ClampOffset(-scroll->getViewOffset().top +
            (relative < 0 ? Body() * 3 : -Body() * 3), scroll->getCanvasSize().height, scroll->getViewCoord().height);
        scroll->setViewOffset(MyGUI::IntPoint(TownCardLayout::ClampViewOffset(scroll->getViewOffset().left,
            scroll->getCanvasSize().width, scroll->getViewCoord().width), -offset));
    }
    void Wheel(MyGUI::Widget* sender, int relative) {
        for (MyGUI::Widget* current = sender; current; current = current->getParent()) {
            if (current == roster) { ScrollBy(roster, relative); return; }
            if (current == sidebar) { ScrollBy(sidebar, relative); return; }
            if (current == challenges) { ScrollBy(challenges, relative); return; }
        }
    }
    void BindWheel(MyGUI::Widget* widget) {
        while (widget) {
            widget->setNeedMouseFocus(true);
            widget->eventMouseWheel += MyGUI::newDelegate(Wheel);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }
    int Wrap(MyGUI::EditBox* text, int x, int y, int width, int limit = 0) {
        const size_t offset = text->getVScrollPosition();
        text->setCoord(x, y, Max(1, width), Max(100, Body() * 4));
        const int insets = Max(0, text->getHeight() - text->getTextRegion().height);
        int height = Max(Body(), text->getTextSize().height) + insets;
        if (limit > 0) height = Min(height, Max(Body() + insets, limit));
        text->setSize(Max(1, width), height); text->setVScrollPosition(offset);
        return height;
    }
    void PlaceTip(const MyGUI::IntPoint& point) {
        const MyGUI::IntPoint origin = window->getClientWidget()->getAbsolutePosition();
        const MyGUI::IntSize size = window->getClientWidget()->getSize();
        const int gap = Gap();
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
            tipOwner = sender;
            const int gap = Gap();
            const MyGUI::IntSize size = window->getClientWidget()->getSize();
            if (sender->getUserString("skarnTip") == "1") {
                fighterTip->setVisible(false);
                int width = Max(Body() * 22, 180), y = gap;
                for (int i = 0; i < 7; ++i) {
                    skarnTipLines[i]->setVisible(true);
                    skarnTipLines[i]->setSize(size.width, Body() * 2);
                    width = Max(width, skarnTipLines[i]->getTextSize().width + 3 * gap);
                }
                width = Min(width, size.width - 2 * gap);
                for (int i = 0; i < 7; ++i)
                    y += Wrap(skarnTipLines[i], gap, y, width - 2 * gap) + (i == 0 || i == 5 ? gap : gap / 2);
                tipBackground->setSize(width, y + gap / 2);
            } else {
                const std::string& text = sender->getUserString("fighterTip");
                if (text.empty()) { HideTip(); return; }
                for (int i = 0; i < 7; ++i) skarnTipLines[i]->setVisible(false);
                fighterTip->setVisible(true);
                SetText(fighterTip, text);
                // Measure at the available width, then fit the longest rendered line.
                const int maxWidth = Max(1, size.width - 2 * gap);
                fighterTip->setSize(maxWidth, Max(100, Body() * 4));
                const int inset = Max(0, fighterTip->getWidth() - fighterTip->getTextRegion().width);
                const int width = Min(maxWidth, fighterTip->getTextSize().width + inset + 2 * gap);
                Wrap(fighterTip, 0, 0, width, size.height - 2 * gap);
                tipBackground->setSize(fighterTip->getSize());
            }
            PlaceTip(info.point);
            tipBackground->setVisible(true);
            tipBackground->upLayerItem();
        } else if (info.type == MyGUI::ToolTipInfo::Move && tipOwner == sender && fighterTip) {
            PlaceTip(info.point);
        }
    }
    void BindTip(MyGUI::Widget* widget) {
        const std::string skarn = widget ? widget->getUserString("skarnTip") : "";
        while (widget) {
            if (!skarn.empty()) widget->setUserString("skarnTip", skarn);
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
    bool Inside(MyGUI::Widget* widget, const MyGUI::IntPoint& point) {
        if (!widget) return false;
        for (; widget; widget = widget->getParent()) {
            if (!widget->getVisible()) return false;
            const MyGUI::IntCoord r = widget->getAbsoluteCoord();
            if (point.left < r.left || point.left >= r.right() || point.top < r.top || point.top >= r.bottom()) return false;
        }
        return true;
    }
    Character* Resolve(const std::string& id) {
        if (id.empty()) return NULL;
        std::vector<Character*> current;
        SquadUtil::CollectPlayerSquad(current);
        for (size_t i = 0; i < current.size(); ++i)
            if (TownMatchmakingRuntime::SelectionIdentity(current[i]) == id) return current[i];
        return NULL;
    }
    std::vector<Character*> Selected() {
        std::vector<Character*> result;
        for (size_t i = 0; i < lineup.size(); ++i) {
            Character* c = Resolve(lineup[i]);
            if (c) result.push_back(c);
        }
        return result;
    }
    // GUI delegates must finish dispatch before roster rows can be destroyed.
    // The normal outer UI tick consumes this dirty flag after MyGUI returns.
    void RefreshNow() { lastRefresh = 0; }
    void Feedback(const std::string& message) { if (status) status->setCaption(message); }
    void SelectionVisuals(FighterRow& row) {
        const bool selected = Assigned(row.id);
        row.button->setStateSelected(selected);
        for (int edge = 0; edge < 4; ++edge) {
            // Use the active theme's readable foreground for selection, leaving
            // the rarity palette exclusive to challenge cards.
            row.border[edge]->setColour(metrics->getTextColour());
            row.border[edge]->setVisible(selected);
        }
    }
    MyGUI::Colour StatusColour(RosterStatusPolicy::Band band) {
        if (band == RosterStatusPolicy::Green) return MyGUI::Colour(.18f, .72f, .28f);
        if (band == RosterStatusPolicy::Orange) return MyGUI::Colour(.95f, .52f, .12f);
        return MyGUI::Colour(.78f, .16f, .12f);
    }
    void RefreshStatusBar(FighterRow& row) {
        RosterStatus::Snapshot status = {};
        Character* character = Resolve(row.id);
        const bool visible = RosterStatus::Read(character, status);
        row.statusBack->setVisible(visible);
        if (!visible) return;
        const int width = static_cast<int>(row.statusBack->getWidth() * status.recovery + .5f);
        row.statusFill->setVisible(width > 0);
        row.statusFill->setCoord(0, 0, width, row.statusBack->getHeight());
        row.statusFill->setColour(StatusColour(status.band));
    }
    void RowReleased(MyGUI::Widget* sender, int, int, MyGUI::MouseButton button) {
        // One physical left release toggles once, including rapid repeated clicks.
        // No double-click action or alternate drag/right-click selection path.
        if (button != MyGUI::MouseButton::Left ||
            !Inside(sender, MyGUI::InputManager::getInstance().getMousePositionByLayer())) return;
        const std::string id = sender->getUserString("fighter");
        std::string reason;
        const bool ready = TownArena::GetPlayerReadiness(Resolve(id), reason);
        if (TownRosterSelection::Toggle(lineup, id, ready, Locked())) {
            for (size_t i = 0; i < rows.size(); ++i) SelectionVisuals(rows[i]);
            Feedback(TownArena::GetStatus());
            RefreshNow();
        }
        else if (Locked()) Feedback("Cancel the booking before editing the team.");
        else if (!ready) Feedback(reason);
        else Feedback("Town teams can contain up to three fighters.");
    }
    void SelectOffer(MyGUI::Widget* sender) {
        selectedOffer = std::atoi(sender->getUserString("slot").c_str());
        for (int i = 0; i < 5; ++i) cards[i].book->setStateSelected(selectedOffer == i);
    }
    void BindOfferText(MyGUI::Widget* widget, const char* slot) {
        while (widget) {
            widget->setUserString("slot", slot); widget->setNeedMouseFocus(true);
            widget->eventMouseWheel += MyGUI::newDelegate(Wheel);
            widget->eventMouseButtonClick += MyGUI::newDelegate(SelectOffer);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }
    void Book(MyGUI::Widget*) {
        if (TownArena::HasBooking()) TownArena::CancelBooking();
        else {
            const std::vector<Character*> selected = Selected();
            if (selected.size() != 1 || lineup.size() != 1) { Feedback("Assign exactly one fighter for a direct 1v1."); return; }
            TownArena::BookPlayer(selected[0]);
        }
        lastStatus = TownArena::GetStatus(); Feedback(lastStatus); RefreshNow();
    }
    void Challenge(MyGUI::Widget* sender) {
        const std::vector<Character*> selected = Selected();
        if (selected.size() != lineup.size() || selected.empty()) { Feedback("Assign 1-3 available squad fighters before booking."); return; }
        TownArena::BookChallenge(selected, std::atoi(sender->getUserString("slot").c_str()));
        lastStatus = TownArena::GetStatus(); Feedback(lastStatus); RefreshNow();
    }
    void BuyRefresh(MyGUI::Widget*) {
        TownArena::BuyChallengeRefresh();
        lastStatus = TownArena::GetStatus(); Feedback(lastStatus); RefreshNow();
    }
    void Division(MyGUI::Widget* sender) {
        TownArena::SetDivision(std::atoi(sender->getUserString("division").c_str())); RefreshNow();
    }
    void WindowButton(MyGUI::Widget*, const std::string& name) {
        if (name == "close" || name == "Close") TownArenaUI::Close();
    }
    void BindPortrait(MyGUI::ImageBox* image, Character* c) {
        image->setVisible(c != NULL);
        PortraitManager* manager = PortraitManager::getInstance();
        if (c && c->isValid() && manager) manager->setImageWidget(c->getHandle(), image, true);
    }
    void RefreshSkarn(Character* skarn) {
        const bool unlocked = TownArena::IsSkarnUnlocked();
        const std::string key = TownMatchmakingRuntime::Identity(skarn);
        if (key != skarnPortraitKey) {
            BindPortrait(skarnPortrait, key.empty() ? NULL : skarn);
            skarnPortraitKey = key;
        }
        skarnPortrait->setVisible(!key.empty());
        skarnPortrait->setAlpha(unlocked ? 1.f : .28f);
        skarnPlaceholder->setVisible(key.empty());
        SetText(skarnPlaceholder, unlocked ? "SKARN\nPORTRAIT UNAVAILABLE" : "SKARN\nLOCKED");
        SetText(skarnState, unlocked ? "UNLOCKED" : "LOCKED");
        skarnState->setTextColour(unlocked ? MyGUI::Colour(.25f,.82f,.36f) : MyGUI::Colour(.88f,.20f,.16f));

        const unsigned wins = TownArena::GetUniqueWins();
        const unsigned bits[] = {TownChallengePolicy::UniqueRessaVane, TownChallengePolicy::UniqueBumPer,
            TownChallengePolicy::UniqueSenn, TownChallengePolicy::UniqueTorka, TownChallengePolicy::UniqueVeyr};
        const char* names[] = {"Ressa Vane", "Bum-Per", "Senn", "Torka", "Veyr the Gilded"};
        SetText(skarnTipLines[0], unlocked ? "Skarn unlocked" : "Skarn locked");
        skarnTipLines[0]->setTextColour(unlocked ? MyGUI::Colour(.25f,.82f,.36f) : MyGUI::Colour(.88f,.20f,.16f));
        for (int i = 0; i < 5; ++i) {
            const bool won = (wins & bits[i]) != 0;
            SetText(skarnTipLines[i + 1], std::string(won ? "Completed - " : "Incomplete - ") + names[i]);
            skarnTipLines[i + 1]->setTextColour(won ? MyGUI::Colour(.25f,.82f,.36f) : MyGUI::Colour(.88f,.20f,.16f));
        }
        SetText(skarnTipLines[6], unlocked ? "Skarn may now join the fighter pool." :
            "Defeat every unique fighter to unlock Skarn.");
        skarnTipLines[6]->setTextColour(unlocked ? MyGUI::Colour(.25f,.82f,.36f) : MyGUI::Colour(.88f,.20f,.16f));
    }
    FighterRow MakeRow(MyGUI::Widget* parent, const std::string& id, Character* c) {
        FighterRow row; row.id = id;
        row.button = NativeUI::Button(parent, MyGUI::IntCoord(0, 0, 200, 64), "", "");
        row.button->setUserString("fighter", id);
        row.button->eventMouseButtonReleased += MyGUI::newDelegate(RowReleased);
        BindWheel(row.button);
        row.portrait = row.button->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, 48, 48), MyGUI::Align::Default);
        Passive(row.portrait); BindPortrait(row.portrait, c);
        row.text = row.button->createWidget<MyGUI::EditBox>("Kenshi_PaintedWordWrapEmpty",
            MyGUI::IntCoord(0, 0, 100, 24), MyGUI::Align::Default);
        Passive(row.text);
        row.statusBack = row.button->createWidget<MyGUI::Widget>("WhiteSkin",
            MyGUI::IntCoord(0, 0, 100, 6), MyGUI::Align::Default);
        row.statusBack->setColour(MyGUI::Colour(.12f, .12f, .12f));
        row.statusFill = row.statusBack->createWidget<MyGUI::Widget>("WhiteSkin",
            MyGUI::IntCoord(0, 0, 1, 6), MyGUI::Align::Default);
        Passive(row.statusBack);
        Passive(row.statusFill);
        for (int edge = 0; edge < 4; ++edge) {
            row.border[edge] = row.button->createWidget<MyGUI::Widget>("WhiteSkin",
                MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
            Passive(row.border[edge]);
        }
        SelectionVisuals(row);
        return row;
    }
    int LayoutRow(FighterRow& row, int x, int y, int width) {
        const int gap = Gap();
        const int textX = 48 + 2 * gap;
        const int textHeight = Wrap(row.text, textX, gap, width - textX - gap);
        row.statusBack->setCoord(textX, gap + textHeight + gap, Max(1, width - textX - gap), 6);
        RefreshStatusBar(row);
        const int height = Max(48, textHeight + gap + 6) + 2 * gap;
        row.portrait->setPosition(gap, gap);
        row.button->setCoord(x, y, Max(1, width), height);
        const int edge = Min(Max(2, gap / 3), Max(1, width / 2));
        row.border[0]->setCoord(0, 0, Max(1, width), edge);
        row.border[1]->setCoord(0, height - edge, Max(1, width), edge);
        row.border[2]->setCoord(0, 0, edge, height);
        row.border[3]->setCoord(Max(0, width - edge), 0, edge, height);
        return height;
    }
    void ClearRows() {
        for (size_t i = 0; i < rows.size(); ++i) MyGUI::Gui::getInstance().destroyWidget(rows[i].button);
        rows.clear();
    }
    void RefreshRoster() {
        std::vector<Character*> current, visible;
        std::vector<std::string> allIds, ids, captions;
        SquadUtil::CollectPlayerSquad(current);
        for (size_t i = 0; i < current.size(); ++i) allIds.push_back(TownMatchmakingRuntime::SelectionIdentity(current[i]));
        // A reservation keeps its original identity list even if a member disappears;
        // booking/ingress own cancellation. UI resolution never substitutes someone.
        if (!Locked()) TownRosterSelection::Prune(lineup, allIds);
        for (size_t i = 0; i < current.size(); ++i) {
            const std::string id = allIds[i];
            if (id.empty()) continue;
            std::string reason;
            const bool ready = TownArena::GetPlayerReadiness(current[i], reason);
            const bool assigned = Assigned(id);
            visible.push_back(current[i]); ids.push_back(id);
            std::string caption = current[i]->getName();
            const std::string inlineStatus = RosterStatusPolicy::InlineStatus(ready, reason);
            if (!inlineStatus.empty()) caption += " - " + inlineStatus;
            if (assigned) caption += "\nSelected";
            else if (!ready && inlineStatus.empty()) caption += "\n" + reason;
            if (assigned && !ready && inlineStatus.empty()) caption += " - " + reason;
            captions.push_back(caption);
        }
        bool rebuild = rows.size() != ids.size();
        for (size_t i = 0; !rebuild && i < ids.size(); ++i) rebuild = rows[i].id != ids[i];
        if (rebuild) {
            ClearRows();
            for (size_t i = 0; i < ids.size(); ++i) rows.push_back(MakeRow(roster, ids[i], visible[i]));
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            SetText(rows[i].text, captions[i]); SelectionVisuals(rows[i]); RefreshStatusBar(rows[i]);
        }
        emptyRoster->setVisible(rows.empty());
        char count[96];
        sprintf_s(count, "Selected team: %u / 3%s", static_cast<unsigned>(lineup.size()), Locked() ? " - locked" : "");
        SetText(squad, count);
    }
    void MakeAccent(CardWidgets& card) {
        // Native WhiteSkin supplies the solid drawing primitive. Colour is
        // semantic rarity decoration only; never tint the card, text or buttons.
        try {
            card.accent = card.root->createWidget<MyGUI::Widget>("PanelEmpty", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
            Passive(card.accent);
            for (int i = 0; i < 16; ++i) {
                card.fade[i] = card.accent->createWidget<MyGUI::Widget>("WhiteSkin", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
                Passive(card.fade[i]); card.fade[i]->setAlpha(.12f * (16 - i) / 16.f);
            }
            for (int i = 0; i < 4; ++i) {
                card.border[i] = card.accent->createWidget<MyGUI::Widget>("WhiteSkin", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
                Passive(card.border[i]); card.border[i]->setAlpha(i == 0 ? .9f : .35f);
            }
        } catch (...) {
            if (card.accent) MyGUI::Gui::getInstance().destroyWidget(card.accent);
            card.accent = NULL;
            PGLog::Error("Proving Grounds: optional town rarity accent unavailable; keeping native card text");
        }
    }
    void LayoutAccent(CardWidgets& card, int width, int height, int rarity, bool uniqueLegendary) {
        if (!card.accent) return;
        card.accent->setVisible(rarity >= 0);
        if (rarity < 0) return;
        const MyGUI::Colour colours[] = {MyGUI::Colour(.22f,.52f,.95f), MyGUI::Colour(.22f,.76f,.36f),
            MyGUI::Colour(.65f,.34f,.92f), MyGUI::Colour(1.f,.49f,.12f)};
        const MyGUI::Colour colour = colours[Min(3, rarity)];
        const MyGUI::Colour borderColour = uniqueLegendary ? MyGUI::Colour(1.f,.78f,.20f) : colour;
        card.accent->setSize(width, height);
        for (int i = 0; i < 16; ++i) {
            const int left = width * i / 16, right = width * (i + 1) / 16;
            card.fade[i]->setCoord(left, 0, Max(1, right - left), height); card.fade[i]->setColour(colour);
        }
        const int rail = Max(3, Gap() / 2);
        card.border[0]->setCoord(0, 0, rail, height);
        card.border[1]->setCoord(rail, 0, Max(1, width - rail), 1);
        card.border[2]->setCoord(rail, height - 1, Max(1, width - rail), 1);
        card.border[3]->setCoord(width - 1, 0, 1, height);
        for (int i = 0; i < 4; ++i) card.border[i]->setColour(borderColour);
    }
    void RefreshCard(CardWidgets& card, int slot, const std::vector<Character*>& opponents, const std::vector<std::string>& names) {
        // Split only the existing display caption; offer identity and opponent
        // resolution come from the booking owner, never from display names.
        const std::string caption = TownArena::GetChallengeCaption(slot, false);
        const size_t newline = caption.find('\n');
        SetText(card.heading, caption.substr(0, newline));
        const std::string detail = newline == std::string::npos ? "" : caption.substr(newline + 1);
        SetText(card.text, detail);
        const TownChallengeBuyInPolicy::Quote quote = TownArena::GetChallengeBuyIn(slot);
        const std::string unavailableReason = TownArena::GetChallengeUnavailableReason(slot);
        const bool unavailable = !unavailableReason.empty();
        const bool searching = TownArena::IsChallengeSearching(slot);
        std::string explanation = unavailableReason;
        if (searching) explanation = "Looking for a new fighter...\n" + explanation;
        else if (TownArena::IsBestAvailableChallenge(slot))
            explanation = "Best available challenge: no eligible lineup reaches the normal rarity band.";
        if (TownArena::IsPlayerMatch() && TownArena::IsAftercare())
            explanation += (explanation.empty() ? "" : "\n") + std::string("Aftercare underway. You can change your team; booking resumes when care finishes.");
        if (explanation.empty() && !quote.Valid()) explanation = detail;
        SetText(card.text, explanation);
        card.text->setVisible(!explanation.empty());
        card.terms->setVisible(true);
        card.book->setVisible(true);
        card.portraitCount = Max(1, Min(4, static_cast<int>(opponents.size())));
        for (int member = 0; member < 4; ++member) {
            Character* c = member < static_cast<int>(opponents.size()) ? opponents[member] : NULL;
            const std::string key = TownMatchmakingRuntime::Identity(c);
            const ArenaCombatProfile::Profile profile = ArenaCombatProfile::Read(c);
            const std::string tip = c && c->isValid() ? c->getName() + "\n" + profile.style +
                "\n\n" + ArenaCombatProfile::StatText(profile) : "";
            SetTip(card.portraits[member], tip);
            SetTip(card.placeholders[member], tip);
            if (key != card.portraitKeys[member]) {
                BindPortrait(card.portraits[member], key.empty() ? NULL : c);
                card.portraitKeys[member] = key;
            }
            const bool portraitVisible = member < card.portraitCount && !key.empty();
            card.portraits[member]->setVisible(portraitVisible);
            card.portraits[member]->setAlpha(unavailable ? .32f : 1.f);
            card.placeholders[member]->setVisible(member < card.portraitCount && !portraitVisible);
            SetText(card.placeholders[member], searching ? "Searching..." : "Portrait unavailable");
            card.names[member]->setVisible(member < static_cast<int>(opponents.size()));
            SetText(card.names[member], member < static_cast<int>(names.size()) ? names[member] : "Unavailable");
            card.stats[member]->setVisible(member < static_cast<int>(opponents.size()));
            char compactStats[160];
            if (profile.valid && profile.martial)
                sprintf_s(compactStats, "ATK %.0f | DOD %.0f | STR %.0f\nTOU %.0f | DEX %.0f",
                    profile.attack, profile.defence, profile.strength,
                    profile.toughness, profile.dexterity);
            else if (profile.valid)
                sprintf_s(compactStats, "ATK %.0f | DEF %.0f | STR %.0f\nTOU %.0f | DEX %.0f | WPN %.0f",
                    profile.attack, profile.defence, profile.strength,
                    profile.toughness, profile.dexterity, profile.weaponSkill);
            else sprintf_s(compactStats, "Combat stats unavailable");
            SetText(card.stats[member], compactStats);
        }
        char terms[256];
        if (quote.Valid()) sprintf_s(terms, "Buy-in: %d Cats\nOdds: %.2fx\nWin returns: %d Cats",
            quote.buyIn, quote.payout / static_cast<double>(quote.buyIn), quote.payout);
        else sprintf_s(terms, "Buy-in / odds unavailable");
        char marks[128];
        const int division = TownArena::GetDivision();
        const int rarity = TownArena::GetChallengeRarity(slot);
        sprintf_s(marks, "\nMarks: %.2fx (division %.2fx / rarity %.2fx / config %.2fx)",
            ArenaMarks::ChallengeMultiplier(division, rarity) *
                PGConfig::ChallengeValues().winMarksMultiplier,
            ArenaMarks::DivisionMultiplier(division), ArenaMarks::RarityMultiplier(rarity),
            PGConfig::ChallengeValues().winMarksMultiplier);
        SetText(card.terms, std::string(terms) + marks);
    }
    void RefreshSelectionPrompt(CardWidgets& card) {
        HideTip();
        SetText(card.heading, "Select a recovered fighter");
        SetText(card.text, "Choose 1-3 recovered squad fighters to reveal the current challenge cards.");
        card.text->setVisible(true);
        card.terms->setVisible(false);
        card.book->setVisible(false);
        card.portraitCount = 0;
        for (int member = 0; member < 4; ++member) {
            card.portraits[member]->setVisible(false);
            card.placeholders[member]->setVisible(false);
            card.names[member]->setVisible(false);
            card.stats[member]->setVisible(false);
        }
    }
    int LayoutCardContent(CardWidgets& card, int width) {
        const int gap = Gap(), pad = gap + Max(3, gap / 2);
        const int inner = Max(1, width - pad - gap);
        int y = gap;
        y += Wrap(card.heading, pad, y, inner) + gap;
        // A large solo portrait; pairs share a row and trios use two rows.
        // Missing members keep their position so portrait order matches names.
        const int count = card.portraitCount;
        const int columns = Max(1, Min(2, count)), portraitRows = count > 0 ? (count + columns - 1) / columns : 0;
        const int cell = Max(1, (inner - (columns - 1) * gap) / columns);
        const int portraitSize = Max(1, Min(count == 1 ? 192 : 112, cell));
        for (int row = 0; row < portraitRows; ++row) {
            const int rowCount = Min(columns, count - row * columns);
            const int rowWidth = rowCount * cell + (rowCount - 1) * gap;
            int memberDetailsHeight = 0, imageHeight = portraitSize;
            for (int column = 0; column < rowCount; ++column) {
                const int member = row * columns + column;
                const int x = pad + (inner - rowWidth) / 2 + column * (cell + gap);
                card.portraits[member]->setCoord(x + (cell - portraitSize) / 2, y, portraitSize, portraitSize);
                const int placeholderHeight = Wrap(card.placeholders[member], x, y, cell);
                if (card.placeholders[member]->getVisible()) imageHeight = Max(imageHeight, placeholderHeight);
                card.placeholders[member]->setPosition(x, y + Max(0, (portraitSize - placeholderHeight) / 2));
            }
            for (int column = 0; column < rowCount; ++column) {
                const int member = row * columns + column;
                const int x = pad + (inner - rowWidth) / 2 + column * (cell + gap);
                if (card.names[member]->getVisible()) {
                    int detailY = y + imageHeight + gap / 2;
                    detailY += Wrap(card.names[member], x, detailY, cell) + gap / 3;
                    const int statsHeight = Wrap(card.stats[member], x, detailY, cell);
                    memberDetailsHeight = Max(memberDetailsHeight, detailY + statsHeight - (y + imageHeight + gap / 2));
                }
            }
            y += imageHeight + gap / 2 + memberDetailsHeight + gap;
        }
        if (card.text->getVisible()) y += Wrap(card.text, pad, y, inner) + gap;
        if (card.terms->getVisible()) y += Wrap(card.terms, pad, y, inner) + gap;
        return card.book->getVisible() ? Max(Body() * 28, y + NativeUI::RowHeight(booking, 0) + gap) : y + gap;
    }
    int Buttons(MyGUI::Button** buttons, int count, int x, int y, int width) {
        const int gap = Gap(), height = NativeUI::RowHeight(booking, 0);
        int natural = 0;
        for (int i = 0; i < count; ++i) natural = Max(natural, buttons[i]->getTextSize().width + 4 * gap);
        const int columns = Max(1, Min(count, (width + gap) / (natural + gap)));
        const int cell = Max(1, (width - (columns - 1) * gap) / columns);
        for (int i = 0; i < count; ++i) buttons[i]->setCoord(x + (i % columns) * (cell + gap),
            y + (i / columns) * (height + gap), cell, height);
        return ((count + columns - 1) / columns) * (height + gap) - gap;
    }
    void LayoutSkarn(int x, int y, int width, int height) {
        const int gap = Gap();
        skarnPanel->setCoord(x, y, width, height);
        const int labelHeight = Body() + gap;
        const int portraitSize = Max(1, Min(width - 2 * gap, height - labelHeight - 2 * gap));
        const int portraitX = (width - portraitSize) / 2;
        skarnPortrait->setCoord(portraitX, gap, portraitSize, portraitSize);
        skarnPlaceholder->setCoord(gap, gap, Max(1, width - 2 * gap), portraitSize);
        skarnState->setCoord(gap, gap + portraitSize, Max(1, width - 2 * gap), labelHeight);
    }
    void LayoutRoster() {
        const int gap = Gap();
        const MyGUI::IntPoint old = roster->getViewOffset();
        for (int pass = 0; pass < 2; ++pass) {
            const int width = Max(1, roster->getViewCoord().width);
            int y = 0;
            for (size_t i = 0; i < rows.size(); ++i) y += LayoutRow(rows[i], 0, y, width) + gap;
            if (rows.empty()) y = Wrap(emptyRoster, gap, gap, width - 2 * gap) + 2 * gap;
            roster->setCanvasSize(width, Max(y, roster->getViewCoord().height));
        }
        roster->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(-old.top,
            roster->getCanvasSize().height, roster->getViewCoord().height)));
    }
    void LayoutSidebar() {
        const int gap = Gap();
        const MyGUI::IntPoint old = sidebar->getViewOffset();
        for (int pass = 0; pass < 2; ++pass) {
            const int width = Max(1, sidebar->getViewCoord().width - 2 * gap);
            int y = gap;
            y += Wrap(squad, gap, y, width) + gap;
            y += Wrap(rosterHint, gap, y, width) + gap;
            const int listH = Max(Body() * 8, sidebar->getViewCoord().height - y - gap);
            roster->setCoord(gap, y, width, listH); y += listH + gap;
            LayoutRoster();
            sidebar->setCanvasSize(width + 2 * gap, Max(y, sidebar->getViewCoord().height));
        }
        sidebar->setViewOffset(MyGUI::IntPoint(0, -LeaderboardLayout::ClampOffset(-old.top,
            sidebar->getCanvasSize().height, sidebar->getViewCoord().height)));
    }
    void LayoutTownPanel() {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (render && render->getViewSize() != screenSize) {
            HideTip();
            screenSize = render->getViewSize();
            window->setCoord(screenSize.width / 25, screenSize.height / 25, screenSize.width * 23 / 25, screenSize.height * 23 / 25);
        }
        const MyGUI::IntSize size = window->getClientWidget()->getSize();
        const int gap = Gap(), width = Max(1, size.width - 2 * gap);
        const int buttonHeight = NativeUI::RowHeight(booking, 0);
        const int buttonTop = size.height - gap - buttonHeight;
        booking->setCoord(gap, buttonTop, width, buttonHeight);
        const int statusHeight = Wrap(status, gap, 0, width, Max(Body(), size.height / 9));
        const int statusTop = buttonTop - gap - statusHeight;
        status->setPosition(gap, statusTop);
        const int available = Max(1, statusTop - 2 * gap);
        const bool stacked = width < Body() * 42;
        if (stacked) {
            const int sideHeight = Max(1, (available - gap) * 2 / 5);
            sidebar->setCoord(gap, gap, width, sideHeight);
            challenges->setCoord(gap, sideHeight + 2 * gap, width, Max(1, available - sideHeight - gap));
        } else {
            const int sideWidth = Min(width * 2 / 5, Max(Body() * 14, width / 5));
            sidebar->setCoord(gap, gap, sideWidth, available);
            challenges->setCoord(sideWidth + 2 * gap, gap, Max(1, width - sideWidth - gap), available);
        }
        LayoutSidebar();
        const MyGUI::IntPoint old = challenges->getViewOffset();
        for (int pass = 0; pass < 2; ++pass) {
            const int rowWidth = Max(1, challenges->getViewCoord().width - 2 * gap);
            int y = gap;
            const int headerTop = y;
            const int skarnWidth = Min(Max(Body() * 8, 96), Max(1, rowWidth / 3));
            const int headerWidth = Max(1, rowWidth - skarnWidth - gap);
            y += Buttons(divisions, 3, gap, y, headerWidth) + gap;
            const int actionWidth = Min(headerWidth / 3, Max(Body() * 13, headerWidth / 5));
            const int timerWidth = Min(headerWidth / 3, Max(Body() * 13, headerWidth / 5));
            const int directWidth = headerWidth - actionWidth - timerWidth - 2 * gap;
            if (directWidth >= Body() * 16) {
                const int directHeight = Wrap(direct, gap, y, directWidth);
                const int timerHeight = Wrap(nextRefresh, gap + directWidth + gap, y, timerWidth);
                refreshButton->setCoord(gap + directWidth + gap + timerWidth + gap,
                    y, actionWidth, buttonHeight);
                y += Max(buttonHeight, Max(directHeight, timerHeight)) + gap;
            } else {
                y += Wrap(direct, gap, y, headerWidth) + gap;
                const int stackedActionWidth = Min(headerWidth, Max(Body() * 13, headerWidth / 3));
                const int stackedTimerWidth = headerWidth - stackedActionWidth - gap;
                if (stackedTimerWidth >= Body() * 8) {
                    const int timerHeight = Wrap(nextRefresh, gap, y, stackedTimerWidth);
                    refreshButton->setCoord(gap + stackedTimerWidth + gap, y,
                        stackedActionWidth, buttonHeight);
                    y += Max(buttonHeight, timerHeight) + gap;
                } else {
                    y += Wrap(nextRefresh, gap, y, headerWidth) + gap;
                    refreshButton->setCoord(gap, y, headerWidth, buttonHeight);
                    y += buttonHeight + gap;
                }
            }
            const int skarnHeight = Max(Body() * 6, y - headerTop - gap);
            LayoutSkarn(gap + headerWidth + gap, headerTop, skarnWidth, skarnHeight);
            y = Max(y, headerTop + skarnHeight + gap);
            std::vector<int> visible;
            int minimum = Body() * 13;
            const int pad = gap + Max(3, gap / 2);
            for (int i = 0; i < 5; ++i) if (cards[i].root->getVisible()) {
                visible.push_back(i);
                minimum = Max(minimum, cards[i].book->getTextSize().width + 5 * gap + pad);
            }
            const bool singleRow = displayedDivision == 2 && !showingSelectionPrompt;
            TownCardLayout::Grid grid;
            if (showingSelectionPrompt) {
                grid.columns = 1; grid.width = rowWidth; grid.gap = gap;
            } else {
                grid = singleRow ? TownCardLayout::SingleRow(rowWidth, minimum, gap, static_cast<int>(visible.size())) :
                    TownCardLayout::Fit(rowWidth, minimum, gap);
            }
            const int columns = Max(1, grid.columns);
            const int rowCount = static_cast<int>((visible.size() + columns - 1) / columns);
            int rowHeights[5] = {};
            for (int row = 0; row < rowCount; ++row) {
                const int first = row * columns;
                const int count = Min(columns, static_cast<int>(visible.size()) - first);
                for (int column = 0; column < count; ++column)
                    rowHeights[row] = Max(rowHeights[row], LayoutCardContent(cards[visible[first + column]], grid.width));
            }
            int baseProgressionTop = y;
            for (int row = 0; row < rowCount; ++row) baseProgressionTop += rowHeights[row] + gap;
            const int progressionHeight = Wrap(progression, gap, baseProgressionTop, rowWidth);
            const int spare = challenges->getViewCoord().height - (baseProgressionTop + progressionHeight + gap);
            if (spare > 0 && rowCount > 0 && !showingSelectionPrompt) {
                for (int row = 0; row < rowCount; ++row)
                    rowHeights[row] += spare / rowCount + (row < spare % rowCount ? 1 : 0);
            }
            for (int row = 0; row < rowCount; ++row) {
                const int first = row * columns;
                const int count = Min(columns, static_cast<int>(visible.size()) - first);
                const int height = rowHeights[row];
                for (int column = 0; column < count; ++column) {
                    const int slot = visible[first + column];
                    CardWidgets& card = cards[slot];
                    card.root->setCoord(gap + grid.Left(column), y, grid.width, height);
                    card.book->setCoord(pad, height - buttonHeight - gap,
                        Max(1, grid.width - pad - gap), buttonHeight);
                    LayoutAccent(card, grid.width, height, showingSelectionPrompt ? -1 : TownArena::GetChallengeRarity(slot),
                        !showingSelectionPrompt && TownArena::IsUniqueLegendaryChallenge(slot));
                }
                y += height + gap;
            }
            y += Wrap(progression, gap, y, rowWidth) + gap;
            const int contentWidth = singleRow ? Max(rowWidth, grid.ContentWidth(static_cast<int>(visible.size()))) : rowWidth;
            challenges->setCanvasSize(contentWidth + 2 * gap, Max(y, challenges->getViewCoord().height));
        }
        challenges->setViewOffset(MyGUI::IntPoint(
            TownCardLayout::ClampViewOffset(old.left, challenges->getCanvasSize().width, challenges->getViewCoord().width),
            TownCardLayout::ClampViewOffset(old.top, challenges->getCanvasSize().height, challenges->getViewCoord().height)));
    }
    void AbandonWindow() {
        if (window) MyGUI::Gui::getInstance().destroyWidget(window);
        window = NULL; booking = NULL;
        refreshButton = NULL;
        fighterTip = NULL; tipBackground = tipOwner = NULL;
        challenges = sidebar = roster = NULL;
        squad = rosterHint = emptyRoster = direct = status = progression = NULL;
        nextRefresh = NULL;
        skarnPanel = NULL; skarnPortrait = NULL; skarnPlaceholder = skarnState = NULL;
        skarnPortraitKey.clear();
        for (int i = 0; i < 7; ++i) skarnTipLines[i] = NULL;
        metrics = NULL; rows.clear();
        for (int i = 0; i < 6; ++i) cards[i] = CardWidgets();
        for (int i = 0; i < 3; ++i) divisions[i] = NULL;
        lastRefresh = 0; selectedOffer = displayedDivision = -1;
        showingSelectionPrompt = false;
        screenSize = MyGUI::IntSize();
    }
    MyGUI::EditBox* Text(MyGUI::Widget* parent, const char* name, const char* caption, bool bounded = false) {
        MyGUI::EditBox* result = parent->createWidget<MyGUI::EditBox>(bounded ? "Kenshi_WordWrap" : "Kenshi_WordWrapEmpty",
            MyGUI::IntCoord(0, 0, 100, 100), MyGUI::Align::Default, name);
        result->setCaption(caption);
        if (parent == sidebar || parent == challenges || parent == roster) BindWheel(result);
        return result;
    }
    MyGUI::Button* Button(MyGUI::Widget* parent, const char* name, const char* caption, void (*handler)(MyGUI::Widget*)) {
        MyGUI::Button* result = NativeUI::Button(parent, MyGUI::IntCoord(0, 0, 100, 24), name, caption);
        result->eventMouseButtonClick += MyGUI::newDelegate(handler); BindWheel(result); return result;
    }
    bool EnsureWindow() {
        if (window) return true;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr(); if (!gui) return false;
        try {
            window = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", .04f,.04f,.92f,.92f,
                MyGUI::Align::Center, "Window", "PG_TownRegistry");
            window->setCaption("Town Arena Registry - Rust Crucible"); window->setVisible(false);
            window->eventWindowButtonPressed += MyGUI::newDelegate(WindowButton);
            MyGUI::Widget* client = window->getClientWidget();
            if (!client) throw std::runtime_error("window has no client widget");
            const MyGUI::IntCoord initial(0,0,300,300);
            metrics = NativeUI::Label(client, MyGUI::IntCoord(0,0,100,24), "PG_TownMetrics", "Team"); metrics->setVisible(false);
            sidebar = NativeUI::Scroll(client, initial, "PG_TownSidebar");
            challenges = NativeUI::Scroll(client, initial, "PG_TownChallenges");
            challenges->setVisibleHScroll(true);
            squad = Text(sidebar, "PG_TownTeam", "Selected team: 0 / 3");
            rosterHint = Text(sidebar, "PG_TownRosterHint", "Click a fighter to select. Click again to deselect.");
            roster = NativeUI::Scroll(sidebar, initial, "PG_TownRoster");
            emptyRoster = Text(roster, "PG_TownEmptyRoster", "No squad fighters available.");
            direct = Text(challenges, "PG_TownDirect", "", true);
            nextRefresh = Text(challenges, "PG_TownNextRefresh", "Next Refresh: calculating...", true);
            refreshButton = Button(challenges, "PG_TownBuyRefresh", "Buy Refresh (500 Cats)", BuyRefresh);
            progression = Text(challenges, "PG_TownProgress", "", true);
            skarnPanel = challenges->createWidget<MyGUI::Widget>("PanelEmpty", initial, MyGUI::Align::Default, "PG_TownSkarnStatus");
            skarnPortrait = skarnPanel->createWidget<MyGUI::ImageBox>("ImageBox", initial, MyGUI::Align::Default, "PG_TownSkarnPortrait");
            skarnPlaceholder = Text(skarnPanel, "PG_TownSkarnPlaceholder", "SKARN\nLOCKED");
            skarnPlaceholder->setTextAlign(MyGUI::Align::Center);
            skarnState = Text(skarnPanel, "PG_TownSkarnState", "LOCKED");
            skarnState->setTextAlign(MyGUI::Align::HCenter);
            skarnPanel->setUserString("skarnTip", "1"); skarnPortrait->setUserString("skarnTip", "1");
            skarnPlaceholder->setUserString("skarnTip", "1");
            BindTip(skarnPanel); BindTip(skarnPortrait); BindTip(skarnPlaceholder);
            status = Text(client, "PG_TownStatus", "", true);
            const char* labels[] = {"Easy", "Medium", "Hard"};
            for (int i = 0; i < 3; ++i) {
                char name[40], value[8]; sprintf_s(name,"PG_TownDivision%d",i); sprintf_s(value,"%d",i);
                divisions[i] = Button(challenges, name, labels[i], Division); divisions[i]->setUserString("division",value);
            }
            booking = Button(client, "PG_TownBook", "Book direct 1v1", Book);
            for (int i = 0; i < 5; ++i) {
                CardWidgets& card = cards[i];
                char name[40], slot[8]; sprintf_s(name,"PG_TownCard%d",i); sprintf_s(slot,"%d",i);
                card.root = challenges->createWidget<MyGUI::Widget>("PanelEmpty", initial, MyGUI::Align::Default, name);
                BindWheel(card.root); MakeAccent(card);
                card.heading = Text(card.root, "", ""); BindOfferText(card.heading, slot);
                card.portraitCount = 1;
                for (int member = 0; member < 4; ++member) {
                    card.portraits[member] = card.root->createWidget<MyGUI::ImageBox>("ImageBox",
                        MyGUI::IntCoord(0, 0, 96, 96), MyGUI::Align::Default);
                    card.portraits[member]->setVisible(false); BindOfferText(card.portraits[member], slot);
                    BindTip(card.portraits[member]);
                    card.placeholders[member] = Text(card.root, "", "");
                    card.placeholders[member]->setTextAlign(MyGUI::Align::Center);
                    BindOfferText(card.placeholders[member], slot);
                    BindTip(card.placeholders[member]);
                    card.names[member] = Text(card.root, "", "");
                    card.names[member]->setTextAlign(MyGUI::Align::HCenter);
                    card.names[member]->setTextColour(MyGUI::Colour(.94f, .80f, .48f));
                    BindOfferText(card.names[member], slot);
                    card.stats[member] = Text(card.root, "", "");
                    card.stats[member]->setTextAlign(MyGUI::Align::HCenter);
                    card.stats[member]->setTextColour(MyGUI::Colour(.78f, .80f, .82f));
                    BindOfferText(card.stats[member], slot);
                }
                card.terms = Text(card.root, "", ""); BindOfferText(card.terms, slot);
                card.text = Text(card.root, "", ""); BindOfferText(card.text, slot);
                card.book = Button(card.root, "", "Book challenge", Challenge); card.book->setUserString("slot",slot);
            }
            // A separate overlap node keeps background and text above the cards together.
            tipBackground = client->createWidget<MyGUI::Widget>(MyGUI::WidgetStyle::Overlapped, "WhiteSkin", initial,
                MyGUI::Align::Default, "", "PG_TownFighterTipBackground");
            tipBackground->setColour(MyGUI::Colour(.06f, .06f, .06f));
            tipBackground->setAlpha(1.0f);
            fighterTip = Text(tipBackground, "PG_TownFighterTip", "", true);
            for (int i = 0; i < 7; ++i) {
                char name[40]; sprintf_s(name, "PG_TownSkarnTip%d", i);
                skarnTipLines[i] = Text(tipBackground, name, "", true);
                skarnTipLines[i]->setVisible(false); Passive(skarnTipLines[i]);
            }
            tipBackground->setVisible(false);
            Passive(tipBackground);
            Passive(fighterTip);
            return true;
        } catch (const std::exception& error) { PGLog::Error(std::string("Proving Grounds: town registry creation failed: ") + error.what()); }
        catch (...) { PGLog::Error("Proving Grounds: town registry creation failed"); }
        AbandonWindow(); return false;
    }
}
namespace TownArenaUI
{
    bool IsVisible() { return window && window->getVisible(); }
    void Close() { HideTip(); if (window) window->setVisible(false); selectedOffer = -1; }
    void AbandonWorldState() {
        lineup.clear(); lastStatus.clear();
        if (MyGUI::Gui::getInstancePtr()) AbandonWindow();
    }
    void Tick() {
        if (!IsVisible()) return;
        const DWORD now = GetTickCount();
        if (lastRefresh && now - lastRefresh < 250) return;
        lastRefresh = now;
        try {
            RefreshRoster();
            TownArena::UpdateMatchmakingPreview(Selected());
            SetText(direct, TownArena::GetDirectCaption());
            SetText(nextRefresh, TownArena::GetNextChallengeRefreshCaption());
            char refreshCaption[64];
            sprintf_s(refreshCaption, "Buy Refresh (%d Cats)", TownArena::GetChallengeRefreshCost());
            if (refreshButton->getCaption().asUTF8() != std::string(refreshCaption))
                refreshButton->setCaption(refreshCaption);
            refreshButton->setEnabled(TownArena::CanBuyChallengeRefresh());
            booking->setCaption(TownArena::HasBooking() ? "Cancel booking" : "Book direct 1v1");
            booking->setEnabled(!TownArena::IsPlayerMatch() && (TownArena::HasBooking() || lineup.size() == 1));
            if (displayedDivision != TownArena::GetDivision()) selectedOffer = -1;
            displayedDivision = TownArena::GetDivision();
            for (int i = 0; i < 3; ++i) { divisions[i]->setStateSelected(displayedDivision == i); divisions[i]->setEnabled(!Locked()); }
            SetText(progression, TownArena::GetProgressCaption());
            std::vector<Character*> opponents[6];
            std::vector<std::string> opponentNames[6];
            TownArena::GetChallengePortraits(opponents, opponentNames);
            RefreshSkarn(opponents[5].empty() ? NULL : opponents[5][0]);
            showingSelectionPrompt = lineup.empty();
            if (showingSelectionPrompt) {
                selectedOffer = -1;
                RefreshSelectionPrompt(cards[0]);
                cards[0].root->setVisible(true);
                cards[0].book->setStateSelected(false);
                for (int i = 1; i < 5; ++i) cards[i].root->setVisible(false);
            } else {
                for (int i = 0; i < 5; ++i) {
                    const bool visible = TownArena::IsChallengeVisible(i);
                    if (selectedOffer == i && !visible) selectedOffer = -1;
                    cards[i].root->setVisible(visible);
                    RefreshCard(cards[i], i, opponents[i], opponentNames[i]);
                    cards[i].book->setStateSelected(selectedOffer == i);
                    cards[i].book->setEnabled(!Locked() && !TownArena::IsPlayerMatch() && TownArena::GetChallengeBuyIn(i).Valid());
                }
            }
            if (lastStatus != TownArena::GetStatus()) { lastStatus = TownArena::GetStatus(); Feedback(lastStatus); }
            LayoutTownPanel();
            if (tipOwner && !Inside(tipOwner, MyGUI::InputManager::getInstance().getMousePositionByLayer()))
                HideTip();
        } catch (const std::exception& error) { PGLog::Error(std::string("Proving Grounds: town registry refresh failed: ") + error.what()); AbandonWindow(); }
        catch (...) { PGLog::Error("Proving Grounds: town registry refresh failed"); AbandonWindow(); }
    }
    void Show(Building* registry, bool bindRegistry) {
        if (!ArenaIdentity::IsTownRegistry(registry) || !ArenaIdentity::IsRegistryFinished(registry)) return;
        if (bindRegistry) TownArena::BindRegistry(registry);
        if (!EnsureWindow()) return;
        TownArena::RefreshMatchmakingRoster();
        lastStatus = TownArena::GetStatus(); Feedback(lastStatus);
        window->setVisible(true); RefreshNow(); Tick();
    }
}
