#include "DefenseButton.h"
#include "ModSettings.h"

#include <Debug.h>
#include <core/Functions.h>

// kenshi/gui/MainBarGUI.h uses MainBarGUI* in MainTabPortraitPlatoon's
// declarations before the class itself is declared, so it does not compile
// standalone; declaring the name first is enough to make the header usable.
class MainBarGUI;
#include <kenshi/gui/MainBarGUI.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_MouseButton.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Colour.h>

#include <sstream>
#include <string>

namespace
{
    DefenseButton::Callback g_onSave = nullptr;
    DefenseButton::Callback g_onReturn = nullptr;
    DefenseButton::Callback g_onDiscard = nullptr;

    const bool kShowStatusDebugUi = false;

    const char* const kButtonName = "DefensivePositionsDefenseButton";
    const char* const kTooltipName = "DefensivePositionsDefenseTooltip";
    const char* const kStatusName = "DefensivePositionsDefenseStatus";
    const char* const kLegacyWindowName = "DefensivePositionsDefenseWindow";
    // Pre-rename widget names — destroy on create so an old DLL session leftover
    // cannot leave duplicate HUD chrome after upgrading.
    const char* const kOldButtonName = "CombatFormationsDefenseButton";
    const char* const kOldTooltipName = "CombatFormationsDefenseTooltip";
    const char* const kOldStatusName = "CombatFormationsDefenseStatus";
    const char* const kOldWindowName = "CombatFormationsDefenseWindow";

    const char* const kButtonSkin = "Kenshi_Button1Skin";
    const char* const kButtonFont = "Kenshi_PaintedTextFont_Large";
    const char* const kTooltipFont = "Kenshi_PaintedTextFont_Small";
    const char* const kTooltipSkin = "Kenshi_FloatinglPanelSmallSkin";
    const char* const kStatusSkin = "Kenshi_GenericTextBox";

    const int kButtonW = 72;
    const int kButtonH = 36;
    const int kDownOverlapPx = 18;
    const int kNudgeUpPx = 23;
    const int kNudgeLeftPx = 10;

    const int kTipW = 380;
    const int kTipH = 56;

    const char* const kDefaultTooltip = "LMB Return  |  MMB Discard  |  RMB Save";

    MainBarGUI* g_mainBar = nullptr;
    MyGUI::TextBox* g_tooltip = nullptr;

    std::string g_hoverStatus = kDefaultTooltip;
    bool g_statusIsTransient = false;

    MyGUI::Widget* FindByName(const char* name)
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return nullptr;

        return gui->findWidgetT(name, false);
    }

    void DestroyNamedWidget(const char* name)
    {
        MyGUI::Widget* w = FindByName(name);
        if (w)
            MyGUI::Gui::getInstance().destroyWidget(w);
    }

    // Drop popup-layer widgets we own. Do NOT destroy the DEF button here when
    // MainBar is dying — it is a MainBar child and double-free crashes Kenshi.
    void DestroyOrphanPopupUi()
    {
        DestroyNamedWidget(kLegacyWindowName);
        DestroyNamedWidget(kTooltipName);
        DestroyNamedWidget(kStatusName);
        g_tooltip = nullptr;
    }

    void DestroyExistingDefenseUi()
    {
        DestroyNamedWidget(kLegacyWindowName);
        DestroyNamedWidget(kButtonName);
        DestroyNamedWidget(kTooltipName);
        DestroyNamedWidget(kStatusName);
        DestroyNamedWidget(kOldWindowName);
        DestroyNamedWidget(kOldButtonName);
        DestroyNamedWidget(kOldTooltipName);
        DestroyNamedWidget(kOldStatusName);
        g_tooltip = nullptr;
    }

    std::string ToSingleLine(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '\n' || c == '\r')
            {
                if (!out.empty() && out[out.size() - 1] != ' ')
                    out.push_back(' ');
            }
            else
            {
                out.push_back(c);
            }
        }
        if (out.size() > 120)
            out.resize(120);
        return out;
    }

    void ApplyTooltipCaption()
    {
        // Re-resolve in case MyGUI already destroyed the widget underneath us.
        if (g_tooltip && FindByName(kTooltipName) != static_cast<MyGUI::Widget*>(g_tooltip))
            g_tooltip = nullptr;

        if (!g_tooltip)
            return;

        g_tooltip->setCaption(g_hoverStatus);
        g_tooltip->setFontName(kTooltipFont);
        g_tooltip->setTextAlign(MyGUI::Align::Center);
        g_tooltip->setTextColour(MyGUI::Colour(0.85f, 0.78f, 0.55f));
    }

    void ResetHoverToHelp()
    {
        g_hoverStatus = kDefaultTooltip;
        g_statusIsTransient = false;
        ApplyTooltipCaption();
    }

    void SetTooltipVisible(bool visible)
    {
        if (g_tooltip && FindByName(kTooltipName) != static_cast<MyGUI::Widget*>(g_tooltip))
            g_tooltip = nullptr;

        if (!g_tooltip)
            return;

        if (visible)
            ApplyTooltipCaption();
        g_tooltip->setVisible(visible);
    }

    void OnDefenseButtonClick(MyGUI::Widget* /*sender*/)
    {
        if (g_onReturn)
            g_onReturn();
    }

    void OnDefenseButtonPressed(MyGUI::Widget* /*sender*/, int /*left*/, int /*top*/, MyGUI::MouseButton id)
    {
        if (id == MyGUI::MouseButton::Right && g_onSave)
            g_onSave();
        else if (id == MyGUI::MouseButton::Middle && g_onDiscard)
            g_onDiscard();
    }

    void OnDefenseButtonMouseSetFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*old*/)
    {
        SetTooltipVisible(true);
    }

    void OnDefenseButtonMouseLostFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*newFocus*/)
    {
        SetTooltipVisible(false);
        if (g_statusIsTransient)
            ResetHoverToHelp();
    }

    void WireButtonEvents(MyGUI::Button* button)
    {
        button->setCaption("DEF");
        button->setFontName(kButtonFont);
        button->setTextAlign(MyGUI::Align::Center);
        button->setVisible(true);
        button->setNeedMouseFocus(true);
        button->eventMouseButtonClick += MyGUI::newDelegate(OnDefenseButtonClick);
        button->eventMouseButtonPressed += MyGUI::newDelegate(OnDefenseButtonPressed);
        button->eventMouseSetFocus += MyGUI::newDelegate(OnDefenseButtonMouseSetFocus);
        button->eventMouseLostFocus += MyGUI::newDelegate(OnDefenseButtonMouseLostFocus);
    }

    MyGUI::Widget* FindSpeedButtonsPanel(MyGUI::Widget* root)
    {
        if (!root)
            return nullptr;

        const std::string prefix = root->getUserString("BaseLayoutPrefix");
        if (!prefix.empty())
        {
            MyGUI::Widget* speed = root->findWidget(prefix + "SpeedButtonsPanel");
            if (speed)
                return speed;
        }

        return root->findWidget("SpeedButtonsPanel");
    }

    void CreateTooltipAbove(const MyGUI::IntCoord& buttonAbs)
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        int tipX = buttonAbs.left + (buttonAbs.width - kTipW) / 2;
        int tipY = buttonAbs.top - kTipH - 4;
        if (tipY < 0)
            tipY = buttonAbs.top + buttonAbs.height + 4;

        g_tooltip = gui->createWidget<MyGUI::TextBox>(
            kTooltipSkin, tipX, tipY, kTipW, kTipH, MyGUI::Align::Default,
            "Popup", kTooltipName);
        g_tooltip->setNeedMouseFocus(false);
        ApplyTooltipCaption();
        g_tooltip->setVisible(false);
    }

    bool CreateAsMainBarChild()
    {
        if (!g_mainBar)
            return false;

        MyGUI::Widget* root = g_mainBar->getWidget();
        if (!root)
            return false;

        MyGUI::Widget* speed = FindSpeedButtonsPanel(root);
        MyGUI::Widget* parent = speed ? speed->getParent() : root;
        if (!parent)
            return false;

        int bx = 0;
        int by = 0;
        if (speed)
        {
            const MyGUI::IntCoord s = speed->getCoord();
            bx = s.left + s.width - kButtonW - kNudgeLeftPx;
            by = s.top - kButtonH + kDownOverlapPx - kNudgeUpPx;
            if (by < 0)
                by = 0;

            std::ostringstream msg;
            msg << "DefensivePositions: DEF anchored to SpeedButtonsPanel at ("
                << bx << "," << by << ") speed=(" << s.left << "," << s.top
                << " " << s.width << "x" << s.height << ")";
            DebugLog(msg.str());
        }
        else
        {
            const MyGUI::IntSize psz = parent->getSize();
            bx = psz.width * 80 / 100;
            by = psz.height * 66 / 100;
            DebugLog("DefensivePositions: SpeedButtonsPanel not found, used fallback DEF pixel pos");
        }

        MyGUI::Button* button = parent->createWidget<MyGUI::Button>(
            kButtonSkin, bx, by, kButtonW, kButtonH, MyGUI::Align::Default, kButtonName);
        WireButtonEvents(button);
        CreateTooltipAbove(button->getAbsoluteCoord());

        if (kShowStatusDebugUi)
        {
            MyGUI::EditBox* status = parent->createWidget<MyGUI::EditBox>(
                kStatusSkin, 8, 8, 280, 120, MyGUI::Align::Default, kStatusName);
            status->setEditReadOnly(true);
            status->setEditMultiLine(true);
            status->setEditStatic(true);
            status->setCaption(g_hoverStatus);
        }

        return true;
    }

    void CreateOnMiddleFrontLayer()
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        const float bx = 0.81f;
        const float by = 0.655f;
        const float bw = 0.045f;
        const float bh = 0.028f;

        MyGUI::Button* button = gui->createWidgetReal<MyGUI::Button>(
            kButtonSkin, bx, by, bw, bh, MyGUI::Align::Default,
            "MiddleFront", kButtonName);
        WireButtonEvents(button);
        CreateTooltipAbove(button->getAbsoluteCoord());

        DebugLog("DefensivePositions: Defense DEF button created on MiddleFront layer (fallback)");
    }

    void CreateDefenseUI(bool forceRebuild)
    {
        // Critical: g_mainBar is cleared in the MainBar destructor. During save
        // load the old MainBar dies before the new one is built; touching the
        // stale pointer here freezes/crashes Kenshi.
        if (!g_mainBar)
            return;

        if (!ModSettings::showGuiButton())
        {
            if (FindByName(kButtonName) || FindByName(kTooltipName))
                DestroyExistingDefenseUi();
            return;
        }

        if (!forceRebuild && FindByName(kButtonName))
            return;

        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        MyGUI::Widget* root = g_mainBar->getWidget();
        if (!root)
            return;

        DestroyExistingDefenseUi();

        if (!CreateAsMainBarChild())
            CreateOnMiddleFrontLayer();
    }

    MainBarGUI* (*MainBarGUI_ctor_orig)(MainBarGUI*) = nullptr;
    MainBarGUI* MainBarGUI_ctor_hook(MainBarGUI* thisptr)
    {
        g_mainBar = MainBarGUI_ctor_orig(thisptr);
        CreateDefenseUI(/*forceRebuild=*/true);
        return g_mainBar;
    }

    void (*MainBarGUI_dtor_orig)(MainBarGUI*) = nullptr;
    void MainBarGUI_dtor_hook(MainBarGUI* thisptr)
    {
        if (g_mainBar == thisptr)
        {
            DebugLog("DefensivePositions: MainBarGUI destroyed — clearing DEF pointers");
            g_mainBar = nullptr;
            // Button is owned by MainBar and will be destroyed with it.
            // Tooltip lives on Popup and must be removed manually.
            DestroyOrphanPopupUi();
        }

        MainBarGUI_dtor_orig(thisptr);
    }
}

namespace DefenseButton
{
    void install(Callback onSave, Callback onReturn, Callback onDiscard)
    {
        g_onSave = onSave;
        g_onReturn = onReturn;
        g_onDiscard = onDiscard;

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&MainBarGUI::_CONSTRUCTOR), MainBarGUI_ctor_hook, &MainBarGUI_ctor_orig))
            ErrorLog("DefensivePositions: Could not hook MainBarGUI constructor for Defense button");

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&MainBarGUI::_DESTRUCTOR), MainBarGUI_dtor_hook, &MainBarGUI_dtor_orig))
            ErrorLog("DefensivePositions: Could not hook MainBarGUI destructor for Defense button");
    }

    void ensureCreated()
    {
        // No-op while MainBar is absent (title screen / mid-load). Avoids the
        // dangling-pointer crash that happened when we kept calling getWidget()
        // on a destroyed MainBarGUI during save load.
        CreateDefenseUI(/*forceRebuild=*/false);
    }

    void applyGuiVisibility()
    {
        CreateDefenseUI(/*forceRebuild=*/true);
    }

    void setStatusText(const std::string& text)
    {
        DebugLog("DefensivePositions: status: " + text);

        if (text.empty())
        {
            ResetHoverToHelp();
        }
        else
        {
            g_hoverStatus = ToSingleLine(text);
            g_statusIsTransient = true;
            ApplyTooltipCaption();
            if (g_tooltip && FindByName(kTooltipName) == static_cast<MyGUI::Widget*>(g_tooltip))
                g_tooltip->setVisible(true);
        }

        if (!kShowStatusDebugUi)
            return;

        MyGUI::Widget* w = FindByName(kStatusName);
        if (!w)
            return;

        MyGUI::EditBox* status = w->castType<MyGUI::EditBox>(false);
        if (status)
            status->setCaption(text);
    }
}