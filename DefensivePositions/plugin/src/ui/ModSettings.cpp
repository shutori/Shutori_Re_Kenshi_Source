#include "ModSettings.h"

#include "DefenseButton.h"

#include <Debug.h>
#include <Windows.h>
#include <core/Functions.h>

#include <kenshi/gui/OptionsWindow.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_TabControl.h>
#include <mygui/MyGUI_TabItem.h>
#include <mygui/MyGUI_Delegate.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace
{
    bool g_showGuiButton = true;

    const char* const kHeaderName = "DefensivePositionsHeader";
    const char* const kToggleName = "DefensivePositionsShowGui";

    MyGUI::Button* g_toggleBtn = nullptr;

    std::string GetConfigFilePath()
    {
        char path[MAX_PATH];
        HMODULE hm = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetConfigFilePath),
            &hm);
        GetModuleFileNameA(hm, path, sizeof(path));
        std::string dir(path);
        const size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos)
            dir = dir.substr(0, pos + 1);
        return dir + "DefensivePositions.cfg";
    }

    void SaveConfig()
    {
        const std::string cfgPath = GetConfigFilePath();
        std::ofstream file(cfgPath.c_str());
        if (!file.is_open())
        {
            ErrorLog("DefensivePositions: Could not write config: " + cfgPath);
            return;
        }

        file << "# Defensive Positions configuration\n"
             << "show_gui_button=" << (g_showGuiButton ? 1 : 0) << "\n";
    }

    void LoadConfig()
    {
        const std::string cfgPath = GetConfigFilePath();
        std::ifstream file(cfgPath.c_str());
        if (!file.is_open())
        {
            DebugLog("DefensivePositions: No config file, using defaults (DEF button on)");
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string k = line.substr(0, eq);
            const std::string v = line.substr(eq + 1);
            if (k == "show_gui_button")
                g_showGuiButton = (atoi(v.c_str()) != 0);
        }

        DebugLog(std::string("DefensivePositions: show_gui_button=") +
                 (g_showGuiButton ? "1" : "0"));
    }

    // Kenshi prefixes widget names as "prefix_Name"; match by suffix
    // (same approach as KenshiRotate / RE_Kenshi).
    MyGUI::Widget* FindWidgetBySuffix(MyGUI::EnumeratorWidgetPtr enumerator,
                                      const std::string& name)
    {
        while (enumerator.next())
        {
            const std::string widgetName = enumerator.current()->getName();
            const size_t splitPos = widgetName.find('_');
            if (splitPos != std::string::npos &&
                widgetName.substr(splitPos + 1) == name)
                return enumerator.current();

            if (enumerator.current()->getChildCount() > 0)
            {
                MyGUI::Widget* child =
                    FindWidgetBySuffix(enumerator.current()->getEnumerator(), name);
                if (child)
                    return child;
            }
        }
        return nullptr;
    }

    void UpdateToggleCaption()
    {
        if (!g_toggleBtn)
            return;
        g_toggleBtn->setCaption(g_showGuiButton ? "Show DEF button: ON"
                                                : "Show DEF button: OFF");
    }

    void OnToggleClicked(MyGUI::Widget* /*sender*/)
    {
        g_showGuiButton = !g_showGuiButton;
        SaveConfig();
        UpdateToggleCaption();
        DefenseButton::applyGuiVisibility();
        DebugLog(std::string("DefensivePositions: show_gui_button set to ") +
                 (g_showGuiButton ? "1" : "0"));
    }

    MyGUI::TabItem* FindModsTab(MyGUI::TabControl* tabCtrl)
    {
        if (!tabCtrl || tabCtrl->getItemCount() == 0)
            return nullptr;

        for (size_t i = 0; i < tabCtrl->getItemCount(); ++i)
        {
            if (tabCtrl->getItemNameAt(i) == "MODS")
                return tabCtrl->getItemAt(i);
        }

        for (size_t i = 0; i < tabCtrl->getItemCount(); ++i)
        {
            const std::string widgetName = tabCtrl->getItemAt(i)->getName();
            const size_t splitPos = widgetName.find('_');
            if (splitPos == std::string::npos)
                continue;
            const std::string suffix = widgetName.substr(splitPos + 1);
            if (suffix == "Mods" || suffix == "ModTab")
                return tabCtrl->getItemAt(i);
        }

        if (tabCtrl->getItemCount() >= 6)
            return tabCtrl->getItemAt(5);

        return nullptr;
    }

    void InjectModsTabUI()
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        MyGUI::Widget* optionsTabWidget =
            FindWidgetBySuffix(gui->getEnumerator(), "OptionsTab");
        if (!optionsTabWidget)
            return;

        MyGUI::TabControl* tabCtrl = optionsTabWidget->castType<MyGUI::TabControl>(false);
        if (!tabCtrl)
            return;

        MyGUI::TabItem* tab = FindModsTab(tabCtrl);
        if (!tab)
            return;

        // Widgets are destroyed when Options closes; recreate when needed.
        if (tab->findWidget(kToggleName) != nullptr)
            return;

        g_toggleBtn = nullptr;

        // Left column — KenshiRotate sits around x=0.60.
        const float x = 0.05f;
        const float baseY = 0.09f;
        const float w = 0.28f;

        MyGUI::TextBox* header = tab->createWidgetReal<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText",
            x, baseY, w, 0.04f,
            MyGUI::Align::Top | MyGUI::Align::Left,
            kHeaderName);
        header->setCaption("Defensive Positions");

        g_toggleBtn = tab->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1",
            x, baseY + 0.045f, w, 0.04f,
            MyGUI::Align::Top | MyGUI::Align::Left,
            kToggleName);
        g_toggleBtn->eventMouseButtonClick += MyGUI::newDelegate(OnToggleClicked);
        UpdateToggleCaption();

        DebugLog("DefensivePositions: Injected settings into Options → MODS");
    }

    void (*OptionsWindow_update_orig)(OptionsWindow*) = nullptr;
    void OptionsWindow_update_hook(OptionsWindow* thisptr)
    {
        OptionsWindow_update_orig(thisptr);
        InjectModsTabUI();
    }
}

namespace ModSettings
{
    void install()
    {
        LoadConfig();

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&OptionsWindow::_NV_update),
                OptionsWindow_update_hook,
                &OptionsWindow_update_orig))
            ErrorLog("DefensivePositions: Could not hook OptionsWindow::update for MODS settings");
    }

    bool showGuiButton()
    {
        return g_showGuiButton;
    }
}
