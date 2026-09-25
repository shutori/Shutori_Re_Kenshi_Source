#include "MarksHUD.h"
#include "LeaderboardStore.h"
#include "NativeUI.h"
#include "PGLog.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/GameData.h>
#include <kenshi/Globals.h>
#include <kenshi/Town.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/MainBarGUI.h>
#pragma warning(pop)
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_TextBox.h>
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <exception>

namespace
{
    MyGUI::Widget* panel = NULL;
    MyGUI::TextBox* label = NULL;
    DWORD lastRefresh = 0;
    bool failed = false;

    void Hide() { if (panel) panel->setVisible(false); }

    bool InScratch(Character* character)
    {
        if (!character || !character->isValid() || !character->isPlayerCharacter()) return false;
        TownBase* town = character->getCurrentTownLocation();
        GameData* data = town ? town->getGameData() : NULL;
        return data && data->stringID == "90-Proving Grounds.mod";
    }

    void Refresh(ForgottenGUI* gui)
    {
        if (!ou || !LeaderboardStore::HasActiveSave() || !gui || !gui->mainbar) { Hide(); return; }
        Character* character = gui->selectedPlayerCharacter.getCharacter();
        MyGUI::TextBox* money = gui->mainbar->moneyText;
        if (!InScratch(character) || !money || !money->getInheritedVisible()) { Hide(); return; }
        MyGUI::Gui* widgets = MyGUI::Gui::getInstancePtr();
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!widgets || !render) { Hide(); return; }
        if (!panel) {
            // Own a root widget, so native HUD recreation cannot leave a dangling child.
            panel = widgets->createWidget<MyGUI::Widget>("WhiteSkin", MyGUI::IntCoord(0, 0, 1, 1),
                MyGUI::Align::Default, "Main", "PG_MarksHUD");
            panel->setColour(MyGUI::Colour(0.12f, 0.10f, 0.08f));
            panel->setNeedMouseFocus(false);
            panel->setNeedKeyFocus(false);
            label = NativeUI::Label(panel, MyGUI::IntCoord(0, 0, 1, 1), "PG_MarksHUDText", "");
            label->setNeedMouseFocus(false);
            label->setNeedKeyFocus(false);
            label->setTextAlign(MyGUI::Align::Center);
        }
        char amount[48];
        sprintf_s(amount, "Marks: %d", LeaderboardStore::GetMarks(character));
        label->setCaption(amount);
        const int gap = NativeUI::Spacing(label);
        const MyGUI::IntSize screen = render->getViewSize();
        int left = money->getAbsoluteLeft();
        int top = money->getAbsoluteTop();
        int right = left + money->getWidth();
        MyGUI::TextBox* moneyLabel = gui->mainbar->moneyLabel;
        if (moneyLabel && moneyLabel->getInheritedVisible()) {
            left = std::min(left, moneyLabel->getAbsoluteLeft());
            top = std::min(top, moneyLabel->getAbsoluteTop());
            right = std::max(right, moneyLabel->getAbsoluteLeft() + moneyLabel->getWidth());
        }
        const int width = std::min(screen.width, std::max(right - left, label->getTextSize().width + 2 * gap));
        const int height = NativeUI::RowHeight(label, 0);
        left = std::max(0, std::min(left, screen.width - width));
        top = std::max(0, top - height - gap);
        panel->setCoord(left, top, width, height);
        label->setCoord(gap, gap, std::max(1, width - 2 * gap), height - 2 * gap);
        panel->setVisible(true);
    }
}

namespace MarksHUD
{
    void Tick(ForgottenGUI* gui)
    {
        if (failed) return;
        const DWORD now = GetTickCount();
        if (lastRefresh && now - lastRefresh < 250) return;
        lastRefresh = now;
        try { Refresh(gui); }
        catch (const std::exception& error) {
            Hide(); failed = true;
            PGLog::Error(std::string("Proving Grounds: Marks HUD unavailable: ") + error.what());
        }
    }

    void AbandonWorldState()
    {
        if (panel && MyGUI::Gui::getInstancePtr()) MyGUI::Gui::getInstance().destroyWidget(panel);
        panel = NULL; label = NULL; lastRefresh = 0; failed = false;
    }
}
