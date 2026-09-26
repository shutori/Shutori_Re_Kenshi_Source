#include "PGSettingsUI.h"

#include "DebugMenu.h"
#include "NativeUI.h"
#include "PGConfig.h"
#include "PGLog.h"

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ScrollBar.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TabControl.h>
#include <mygui/MyGUI_TabItem.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <kenshi/gui/OptionsWindow.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>


namespace
{
    const int kSettingCount = 19;
    enum SettingId
    {
        RecruitmentMinimum, RecruitmentMaximum, RecruitmentStep, RecruitmentCombatScale,
        BookieMinimum, BookieMaximum, BuyInEasy, BuyInMedium, BuyInHard,
        ChallengeDifficultySetting, RefreshHours, RefreshBaseCats, RefreshMultiplier,
        CooldownHours, WinMarks, UniqueMarks, SkarnMarks, CrowdPerformance, F8Destination
    };

    struct Row
    {
        MyGUI::TextBox* label;
        MyGUI::EditBox* value;
        MyGUI::ScrollBar* slider;
        MyGUI::Button* minus;
        MyGUI::Button* plus;
    };

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_heading = NULL;
    MyGUI::EditBox* g_description = NULL;
    MyGUI::ScrollView* g_scroll = NULL;
    MyGUI::EditBox* g_status = NULL;
    MyGUI::Button* g_save = NULL;
    MyGUI::Button* g_reset = NULL;
    MyGUI::Button* g_close = NULL;
    MyGUI::Window* g_restartPopup = NULL;
    Row g_rows[kSettingCount] = {};
    PGConfig::EditableSettings g_settings;
    bool g_layoutReady = false;
    bool g_inLayout = false;
    bool g_refreshing = false;
    bool g_numericDirty[kSettingCount] = {};

    const char* const kLabels[kSettingCount] =
    {
        "Recruitment minimum (Marks)", "Recruitment maximum (Marks)",
        "Recruitment rounding step (Marks)", "Recruitment combat scale",
        "Bookie minimum stake (Cats)", "Bookie maximum stake (Cats)",
        "Easy challenge buy-in (Cats)", "Medium challenge buy-in (Cats)",
        "Hard challenge buy-in (Cats)", "Challenge opponent difficulty",
        "Offer refresh interval (in-game hours)", "First paid refresh cost (Cats)",
        "Paid refresh cost multiplier", "Skarn cooldown (in-game hours)",
        "Challenge win Marks multiplier", "Unique-fighter Marks bonus multiplier",
        "Skarn Marks bonus multiplier", "Crowd performance", "F8 debug menu"
    };

    const char* const kDescriptions[kSettingCount] =
    {
        "Lowest Marks fee after stat pricing; range 0-1,000,000 and cannot exceed the maximum.",
        "Highest Marks fee before the Marks multiplier; range 0-1,000,000 and cannot be below the minimum.",
        "Round prisoner fees up to this many Marks; range 1-1,000,000.",
        "Marks added per average combat-stat point; range 0-100.",
        "Lowest bookie stake in Cats; range 0-1,000,000, in 10-Cat increments, at or below maximum.",
        "Highest bookie stake in Cats; range 0-1,000,000, in 10-Cat increments, at or above minimum.",
        "Easy challenge base buy-in in Cats, before opponent-power adjustment; range 0-1,000,000.",
        "Medium challenge base buy-in in Cats, before opponent-power adjustment; range 0-1,000,000.",
        "Hard challenge base buy-in in Cats, before opponent-power adjustment; range 0-1,000,000.",
        "Biases generated challenge opponents weaker or stronger; does not change stats.",
        "How often challenge offers refresh in in-game hours; range 1-168.",
        "Starting Cats cost for the first paid offer refresh; range 1-1,000,000.",
        "Paid-refresh growth factor until a challenge is booked; range 1.00-10.00.",
        "In-game hours after a Skarn challenge attempt before another Skarn challenge can be booked; other challenges are unaffected. Range 0-720.",
        "Multiplier for Marks awarded for winning a challenge; range 0-100.",
        "Multiplier for the unique-fighter Marks bonus in challenges; range 0-100.",
        "Multiplier for Marks awarded for defeating Skarn; range 0-100.",
        "Crowd upkeep preset. Takes effect on the next arena bout after saving.",
        "When enabled, F8 toggles the debug menu. When disabled, F8 does nothing."
    };

    const size_t kSliderSteps = 1000;
    const double kSettingMaximum = 1000000.0;
    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    void Layout(MyGUI::Widget* = NULL);
    void OnSettingHover(MyGUI::Widget*, MyGUI::Widget*);

    bool IsNumeric(int id)
    {
        return id != ChallengeDifficultySetting && id != CrowdPerformance && id != F8Destination;
    }

    bool IsInteger(int id)
    {
        return id != RecruitmentCombatScale && id != RefreshMultiplier &&
            id != WinMarks && id != UniqueMarks && id != SkarnMarks;
    }

    bool IsSectionStart(int id)
    {
        return id == BookieMinimum || id == BuyInEasy || id == WinMarks ||
            id == CrowdPerformance || id == F8Destination;
    }

    bool GetLimits(int id, double& low, double& high, double& sliderStep)
    {
        low = 0.0;
        high = kSettingMaximum;
        sliderStep = 1.0;
        switch (id)
        {
        case RecruitmentMinimum: high = g_settings.recruitment.maximum; sliderStep = 5.0; break;
        case RecruitmentMaximum: low = g_settings.recruitment.minimum; sliderStep = 5.0; break;
        case RecruitmentStep: low = 1.0; break;
        case RecruitmentCombatScale: high = 100.0; sliderStep = 0.1; break;
        case BookieMinimum: high = g_settings.bookie.maximum; sliderStep = 10.0; break;
        case BookieMaximum: low = g_settings.bookie.minimum; sliderStep = 10.0; break;
        case BuyInEasy: case BuyInMedium: case BuyInHard: sliderStep = 100.0; break;
        case RefreshHours: low = 1.0; high = 168.0; break;
        case RefreshBaseCats: low = 1.0; high = 1000000.0; sliderStep = 100.0; break;
        case RefreshMultiplier: low = 1.0; high = 10.0; sliderStep = 0.01; break;
        case CooldownHours: high = 720.0; break;
        case WinMarks: case UniqueMarks: case SkarnMarks: high = 100.0; sliderStep = 0.01; break;
        default: return false;
        }
        return true;
    }

    double NumberValue(int id)
    {
        switch (id)
        {
        case RecruitmentMinimum: return g_settings.recruitment.minimum;
        case RecruitmentMaximum: return g_settings.recruitment.maximum;
        case RecruitmentStep: return g_settings.recruitment.step;
        case RecruitmentCombatScale: return g_settings.recruitment.combatScale;
        case BookieMinimum: return g_settings.bookie.minimum;
        case BookieMaximum: return g_settings.bookie.maximum;
        case BuyInEasy: return g_settings.challengeBase[0];
        case BuyInMedium: return g_settings.challengeBase[1];
        case BuyInHard: return g_settings.challengeBase[2];
        case RefreshHours: return g_settings.challenges.refreshHours;
        case RefreshBaseCats: return g_settings.challenges.refreshBaseCostCats;
        case RefreshMultiplier: return g_settings.challenges.refreshCostMultiplier;
        case CooldownHours: return g_settings.challenges.cooldownHours;
        case WinMarks: return g_settings.challenges.winMarksMultiplier;
        case UniqueMarks: return g_settings.challenges.uniqueBonusMultiplier;
        case SkarnMarks: return g_settings.challenges.skarnBonusMultiplier;
        default: return 0.0;
        }
    }

    void SetNumberValue(int id, double value)
    {
        const int integer = static_cast<int>(value + 0.5);
        switch (id)
        {
        case RecruitmentMinimum: g_settings.recruitment.minimum = integer; break;
        case RecruitmentMaximum: g_settings.recruitment.maximum = integer; break;
        case RecruitmentStep: g_settings.recruitment.step = integer; break;
        case RecruitmentCombatScale: g_settings.recruitment.combatScale = value; break;
        case BookieMinimum: g_settings.bookie.minimum = integer; break;
        case BookieMaximum: g_settings.bookie.maximum = integer; break;
        case BuyInEasy: g_settings.challengeBase[0] = integer; break;
        case BuyInMedium: g_settings.challengeBase[1] = integer; break;
        case BuyInHard: g_settings.challengeBase[2] = integer; break;
        case RefreshHours: g_settings.challenges.refreshHours = integer; break;
        case RefreshBaseCats: g_settings.challenges.refreshBaseCostCats = integer; break;
        case RefreshMultiplier: g_settings.challenges.refreshCostMultiplier = value; break;
        case CooldownHours: g_settings.challenges.cooldownHours = integer; break;
        case WinMarks: g_settings.challenges.winMarksMultiplier = value; break;
        case UniqueMarks: g_settings.challenges.uniqueBonusMultiplier = value; break;
        case SkarnMarks: g_settings.challenges.skarnBonusMultiplier = value; break;
        }
    }

    void Passive(MyGUI::Widget* widget)
    {
        if (!widget) return;
        widget->setNeedMouseFocus(false);
        widget->setNeedKeyFocus(false);
    }

    void SetStatus(const std::string& text)
    {
        if (g_status) g_status->setCaption(text);
    }

    std::string ValueText(int id)
    {
        char text[80];
        switch (id)
        {
        case RecruitmentMinimum: sprintf_s(text, "%d", g_settings.recruitment.minimum); break;
        case RecruitmentMaximum: sprintf_s(text, "%d", g_settings.recruitment.maximum); break;
        case RecruitmentStep: sprintf_s(text, "%d", g_settings.recruitment.step); break;
        case RecruitmentCombatScale: sprintf_s(text, "%.2f", g_settings.recruitment.combatScale); break;
        case BookieMinimum: sprintf_s(text, "%d", g_settings.bookie.minimum); break;
        case BookieMaximum: sprintf_s(text, "%d", g_settings.bookie.maximum); break;
        case BuyInEasy: sprintf_s(text, "%d", g_settings.challengeBase[0]); break;
        case BuyInMedium: sprintf_s(text, "%d", g_settings.challengeBase[1]); break;
        case BuyInHard: sprintf_s(text, "%d", g_settings.challengeBase[2]); break;
        case ChallengeDifficultySetting:
            sprintf_s(text, "%s", g_settings.challenges.difficulty == PGConfig::ChallengeEasy ? "Easy" :
                g_settings.challenges.difficulty == PGConfig::ChallengeHard ? "Hard" : "Normal"); break;
        case RefreshHours: sprintf_s(text, "%d", g_settings.challenges.refreshHours); break;
        case RefreshBaseCats: sprintf_s(text, "%d", g_settings.challenges.refreshBaseCostCats); break;
        case RefreshMultiplier: sprintf_s(text, "%.2f", g_settings.challenges.refreshCostMultiplier); break;
        case CooldownHours: sprintf_s(text, "%d", g_settings.challenges.cooldownHours); break;
        case WinMarks: sprintf_s(text, "%.2f", g_settings.challenges.winMarksMultiplier); break;
        case UniqueMarks: sprintf_s(text, "%.2f", g_settings.challenges.uniqueBonusMultiplier); break;
        case SkarnMarks: sprintf_s(text, "%.2f", g_settings.challenges.skarnBonusMultiplier); break;
        case CrowdPerformance: sprintf_s(text, "%s", ArenaPerformance::Name(g_settings.performanceProfile)); break;
        case F8Destination: sprintf_s(text, "%s", g_settings.f8OpensDebugMenu ? "Enabled" : "Disabled"); break;
        default: sprintf_s(text, "%s", ""); break;
        }
        return text;
    }

    bool Adjust(int id, int direction)
    {
        switch (id)
        {
        case ChallengeDifficultySetting:
            g_settings.challenges.difficulty = static_cast<PGConfig::ChallengeDifficulty>(
                (static_cast<int>(g_settings.challenges.difficulty) + direction + 3) % 3);
            return true;
        case CrowdPerformance:
            g_settings.performanceProfile = static_cast<ArenaPerformance::Profile>(
                (static_cast<int>(g_settings.performanceProfile) - direction + 3) % 3);
            return true;
        case F8Destination: g_settings.f8OpensDebugMenu = !g_settings.f8OpensDebugMenu; return true;
        default: return false;
        }
    }

    void SetRowId(MyGUI::Widget* widget, int id)
    {
        if (!widget) return;
        char text[8];
        sprintf_s(text, "%02d", id);
        widget->setUserString("setting", text);
        widget->setNeedMouseFocus(true);
        widget->eventMouseSetFocus += MyGUI::newDelegate(OnSettingHover);
    }

    int ParseRow(MyGUI::Widget* sender)
    {
        if (!sender) return -1;
        const std::string key = sender->getUserString("setting");
        if (key.size() != 2 || key[0] < '0' || key[0] > '9' || key[1] < '0' || key[1] > '9') return -1;
        return (key[0] - '0') * 10 + (key[1] - '0');
    }

    void OnSettingHover(MyGUI::Widget* sender, MyGUI::Widget*)
    {
        const int id = ParseRow(sender);
        if (id >= 0 && id < kSettingCount) SetStatus(kDescriptions[id]);
    }

    void UpdateRow(int id)
    {
        if (id < 0 || id >= kSettingCount) return;
        Row& row = g_rows[id];
        g_refreshing = true;
        row.value->setOnlyText(ValueText(id));
        if (IsNumeric(id) && row.slider)
        {
            double low, high, sliderStep;
            GetLimits(id, low, high, sliderStep);
            (void)sliderStep;
            size_t position = 0;
            if (high > low)
            {
                double normalized = (NumberValue(id) - low) / (high - low);
                if (normalized < 0.0) normalized = 0.0;
                if (normalized > 1.0) normalized = 1.0;
                position = static_cast<size_t>(normalized * kSliderSteps + 0.5);
            }
            row.slider->setScrollPosition(position);
        }
        g_refreshing = false;
    }

    void Refresh()
    {
        if (!g_window) return;
        for (int i = 0; i < kSettingCount; ++i)
        {
            g_rows[i].label->setCaption(kLabels[i]);
            UpdateRow(i);
        }
        Layout();
    }

    void OnAdjust(MyGUI::Widget* sender)
    {
        const int id = ParseRow(sender);
        if (id < 0 || id >= kSettingCount) return;
        const int direction = atoi(sender->getUserString("direction").c_str());
        if (!Adjust(id, direction)) return;
        SetStatus("Unsaved setting. Save to keep this change.");
        Refresh();
    }

    bool ParseNumberText(int id, const std::string& text, double& value)
    {
        char* end = NULL;
        errno = 0;
        const double parsed = strtod(text.c_str(), &end);
        if (end == text.c_str()) return false;
        while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
        if (*end) return false;
        if (!_finite(parsed))
        {
            if (errno != ERANGE) return false;
            value = parsed;
            return true;
        }
        if (IsInteger(id))
        {
            double integerPart = 0.0;
            if (modf(parsed, &integerPart) != 0.0) return false;
        }
        value = parsed;
        return true;
    }

    bool CommitRow(int id, bool refreshAll = true)
    {
        if (id < 0 || id >= kSettingCount || !IsNumeric(id)) return true;
        if (!g_numericDirty[id]) return true;
        double value = 0.0;
        if (!ParseNumberText(id, g_rows[id].value->getOnlyText().asUTF8(), value))
        {
            g_numericDirty[id] = false;
            UpdateRow(id);
            SetStatus("Enter a whole number or decimal value; the previous value was restored.");
            return false;
        }
        double low, high, sliderStep;
        GetLimits(id, low, high, sliderStep);
        bool clamped = false;
        if (value < low) { value = low; clamped = true; }
        if (value > high) { value = high; clamped = true; }
        if (id == BookieMinimum || id == BookieMaximum)
        {
            value = static_cast<int>((value + PGConfig::kStakeStep / 2) / PGConfig::kStakeStep) * PGConfig::kStakeStep;
            if (value < low) value = low;
            if (value > high) value = high;
        }
        if (!IsInteger(id)) value = static_cast<int>(value * 100.0 + 0.5) / 100.0;
        SetNumberValue(id, value);
        g_numericDirty[id] = false;
        if (refreshAll) Refresh();
        else UpdateRow(id);
        if (clamped) SetStatus("Value was clamped to the nearest supported limit.");
        else SetStatus("Unsaved setting. Save to keep this change.");
        return true;
    }

    void OnValueAccept(MyGUI::EditBox* sender)
    {
        const int id = ParseRow(sender);
        CommitRow(id);
    }

    void OnValueChanged(MyGUI::EditBox* sender)
    {
        if (g_refreshing) return;
        const int id = ParseRow(sender);
        if (id >= 0 && id < kSettingCount && IsNumeric(id)) g_numericDirty[id] = true;
    }

    void OnSliderChanged(MyGUI::ScrollBar* sender, size_t position)
    {
        if (g_refreshing) return;
        const int id = ParseRow(sender);
        if (id < 0 || id >= kSettingCount || !IsNumeric(id)) return;
        g_numericDirty[id] = true;
        double low, high, sliderStep;
        GetLimits(id, low, high, sliderStep);
        double value = low;
        if (high > low)
        {
            if (position >= kSliderSteps) value = high;
            else value = low + (high - low) * static_cast<double>(position) / kSliderSteps;
            value = static_cast<int>(value / sliderStep + 0.5) * sliderStep;
            if (value < low) value = low;
            if (value > high) value = high;
        }
        SetNumberValue(id, value);
        Refresh();
        SetStatus("Unsaved setting. Save to keep this change.");
    }

    bool CommitAllValues()
    {
        for (int i = 0; i < kSettingCount; ++i)
            if (!CommitRow(i, false)) return false;
        Refresh();
        return true;
    }

    bool HasRestartRequiredChanges(const PGConfig::EditableSettings& before,
        const PGConfig::EditableSettings& after)
    {
        return before.recruitment.minimum != after.recruitment.minimum ||
            before.recruitment.maximum != after.recruitment.maximum ||
            before.recruitment.step != after.recruitment.step ||
            before.recruitment.combatScale != after.recruitment.combatScale ||
            before.bookie.minimum != after.bookie.minimum ||
            before.bookie.maximum != after.bookie.maximum ||
            before.challenges.difficulty != after.challenges.difficulty ||
            before.challenges.refreshHours != after.challenges.refreshHours ||
            before.challenges.refreshBaseCostCats != after.challenges.refreshBaseCostCats ||
            before.challenges.refreshCostMultiplier != after.challenges.refreshCostMultiplier ||
            before.challenges.cooldownHours != after.challenges.cooldownHours ||
            before.challenges.winMarksMultiplier != after.challenges.winMarksMultiplier ||
            before.challenges.uniqueBonusMultiplier != after.challenges.uniqueBonusMultiplier ||
            before.challenges.skarnBonusMultiplier != after.challenges.skarnBonusMultiplier ||
            before.challengeBase[0] != after.challengeBase[0] ||
            before.challengeBase[1] != after.challengeBase[1] ||
            before.challengeBase[2] != after.challengeBase[2];
    }

    void OnRestartPopupClose(MyGUI::Widget*)
    {
        if (g_restartPopup) g_restartPopup->setVisible(false);
    }

    void OnRestartPopupWindowButton(MyGUI::Widget*, const std::string& name)
    {
        if ((name == "close" || name == "Close") && g_restartPopup)
            g_restartPopup->setVisible(false);
    }

    void ShowRestartPopup()
    {
        if (!g_window) return;
        if (!g_restartPopup)
        {
            g_restartPopup = g_window->createWidget<MyGUI::Window>("Kenshi_WindowCX",
                MyGUI::IntCoord(0, 0, 420, 160), MyGUI::Align::Default, "PG_RestartPopup");
            g_restartPopup->setCaption("Restart required");
            g_restartPopup->eventWindowButtonPressed +=
                MyGUI::newDelegate(OnRestartPopupWindowButton);
            MyGUI::Widget* message = NativeUI::WrappedLabel(g_restartPopup,
                MyGUI::IntCoord(16, 42, 388, 60), "PG_RestartMessage",
                "Gameplay settings were saved. Restart Kenshi for these changes to take effect.");
            Passive(message);
            MyGUI::Button* ok = NativeUI::Button(g_restartPopup,
                MyGUI::IntCoord(140, 112, 140, 28), "PG_RestartOK", "OK");
            ok->eventMouseButtonClick += MyGUI::newDelegate(OnRestartPopupClose);
        }
        const MyGUI::IntSize size = g_window->getClientWidget()->getSize();
        const MyGUI::IntSize popupSize = g_restartPopup->getSize();
        g_restartPopup->setPosition((size.width - popupSize.width) / 2,
            (size.height - popupSize.height) / 2);
        g_restartPopup->setVisible(true);
    }

    void OnSave(MyGUI::Widget*)
    {
        if (!CommitAllValues()) return;
        const PGConfig::EditableSettings before = PGConfig::GetEditableSettings();
        const bool restartRequired = HasRestartRequiredChanges(before, g_settings);
        std::string error;
        if (!PGConfig::SaveEditableSettings(g_settings, error))
        {
            SetStatus(error);
            return;
        }
        for (int i = 0; i < kSettingCount; ++i) g_numericDirty[i] = false;
        SetStatus("Saved. Crowd performance applies next bout; F8 applies immediately. Other gameplay settings require restart; shop-price JSON was left unchanged.");
        if (restartRequired)
            ShowRestartPopup();
    }

    void OnResetDefaults(MyGUI::Widget*)
    {
        g_settings = PGConfig::ShippedEditableSettings();
        for (int i = 0; i < kSettingCount; ++i) g_numericDirty[i] = false;
        Refresh();
        SetStatus("Shipped defaults are staged. Select Save Settings to write them; shop prices remain unchanged.");
    }

    void OnClose(MyGUI::Widget*)
    {
        PGSettingsUI::Close();
    }

    void OnWindowButton(MyGUI::Widget*, const std::string& name)
    {
        if (name == "close" || name == "Close") PGSettingsUI::Close();
    }

    MyGUI::Button* FindReSettingsButton(MyGUI::EnumeratorWidgetPtr enumerator)
    {
        while (enumerator.next())
        {
            MyGUI::Widget* widget = enumerator.current();
            MyGUI::Button* button = widget->castType<MyGUI::Button>(false);
            if (button)
            {
                std::string caption = button->getCaption().asUTF8();
                for (size_t i = 0; i < caption.size(); ++i)
                    caption[i] = static_cast<char>(toupper(static_cast<unsigned char>(caption[i])));
                if (caption.find("RE_KENSHI") != std::string::npos)
                    return button;
            }
            if (widget->getChildCount())
            {
                MyGUI::Button* nested = FindReSettingsButton(widget->getEnumerator());
                if (nested) return nested;
            }
        }
        return NULL;
    }


    void Layout(MyGUI::Widget*)
    {
        if (!g_layoutReady || g_inLayout || !g_window) return;
        g_inLayout = true;
        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client) { g_inLayout = false; return; }
        const MyGUI::IntSize size = client->getSize();
        const int gap = NativeUI::Spacing(g_save);
        const int width = Max(1, size.width - 2 * gap);
        const int top = gap;
        const int headingHeight = Max(28, NativeUI::RowHeight(g_save, 0));
        g_heading->setCoord(gap, top, width, headingHeight);
        const int descHeight = Max(42, NativeUI::RowHeight(g_save, 0) * 2);
        g_description->setCoord(gap, top + headingHeight, width, descHeight);
        const int buttonHeight = NativeUI::RowHeight(g_save, 0);
        const int buttonTop = Max(top + headingHeight + descHeight + gap,
            size.height - gap - buttonHeight);
        const int statusHeight = Max(buttonHeight * 2, NativeUI::RowHeight(g_save, 0));
        const int statusTop = Max(buttonTop - statusHeight - gap, top + headingHeight + descHeight);
        g_status->setCoord(gap, statusTop, width, statusHeight);
        const int buttonWidth = (width - 2 * gap) / 3;
        g_save->setCoord(gap, buttonTop, buttonWidth, buttonHeight);
        g_reset->setCoord(gap + buttonWidth + gap, buttonTop, buttonWidth, buttonHeight);
        g_close->setCoord(gap + 2 * (buttonWidth + gap), buttonTop,
            Max(1, width - 2 * buttonWidth - 2 * gap), buttonHeight);
        const int scrollTop = top + headingHeight + descHeight + gap;
        const int scrollBottom = Max(scrollTop, statusTop - gap);
        g_scroll->setCoord(gap, scrollTop, width, Max(1, scrollBottom - scrollTop));
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_scroll->getViewCoord();
            const int contentWidth = Max(1, view.width - 2 * gap);
            const int buttonW = Max(34, NativeUI::RowHeight(g_save, 0));
            const int labelW = Max(100, contentWidth * 42 / 100);
            const int valueW = Max(72, contentWidth * 15 / 100);
            const int enumLabelW = Max(100, contentWidth * 53 / 100);
            int y = gap;
            const int rowH = Max(40, buttonHeight);
            const int sectionGap = Max(gap * 2, rowH / 3);
            for (int i = 0; i < kSettingCount; ++i)
            {
                if (IsSectionStart(i)) y += sectionGap;
                if (IsNumeric(i))
                {
                    const int sliderLeft = gap + labelW + gap;
                    const int sliderWidth = Max(40, contentWidth - labelW - valueW - 3 * gap);
                    const int sliderHeight = Max(18, buttonHeight / 2);
                    g_rows[i].label->setCoord(gap, y, labelW, rowH);
                    g_rows[i].slider->setCoord(sliderLeft, y + (rowH - sliderHeight) / 2,
                        sliderWidth, sliderHeight);
                    g_rows[i].value->setCoord(sliderLeft + sliderWidth + gap, y,
                        Max(1, contentWidth - labelW - sliderWidth - 2 * gap), rowH);
                }
                else
                {
                    const int enumValueW = Max(70, contentWidth - enumLabelW - 2 * buttonW - 3 * gap);
                    g_rows[i].label->setCoord(gap, y, enumLabelW, rowH);
                    g_rows[i].minus->setCoord(gap + enumLabelW + gap, y, buttonW, rowH);
                    g_rows[i].value->setCoord(gap + enumLabelW + 2 * gap + buttonW, y, enumValueW, rowH);
                    g_rows[i].plus->setCoord(gap + enumLabelW + 3 * gap + buttonW + enumValueW, y, buttonW, rowH);
                }
                y += rowH + gap;
            }
            g_scroll->setCanvasSize(Max(1, view.width), y + gap);
            if (view.width == g_scroll->getViewCoord().width) break;
        }
        g_inLayout = false;
    }

    void DestroyWidgets()
    {
        g_layoutReady = false;
        g_inLayout = false;
        if (g_window && MyGUI::Gui::getInstancePtr())
            MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_heading = NULL;
        g_description = NULL;
        g_scroll = NULL;
        g_status = NULL;
        g_save = g_reset = g_close = NULL;
        g_restartPopup = NULL;
        for (int i = 0; i < kSettingCount; ++i)
        {
            g_rows[i].label = NULL;
            g_rows[i].value = NULL;
            g_rows[i].slider = NULL;
            g_rows[i].minus = NULL;
            g_rows[i].plus = NULL;
            g_numericDirty[i] = false;
        }
    }

    void Create()
    {
        if (g_window) return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return;
        try
        {
            g_settings = PGConfig::GetEditableSettings();
            g_window = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX",
                0.16f, 0.05f, 0.68f, 0.90f, MyGUI::Align::Center,
                "Window", "PG_ConfigSettings");
            g_window->setCaption("Proving Grounds Settings");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButton);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client) throw std::runtime_error("settings window has no client");
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            g_heading = NativeUI::Label(client, initial, "PG_ConfigHeading", "PROVING GROUNDS CONFIGURATION");
            g_description = NativeUI::WrappedLabel(client, initial, "PG_ConfigDescription",
                "Gameplay changes require a restart; crowd performance applies to the next bout after saving. Gear, weapons, supplies, licences, remain in pg_config.json.");
            g_status = NativeUI::WrappedLabel(client, initial, "PG_ConfigStatus", "Config changes are staged until saved.");
            Passive(g_description);
            Passive(g_status);
            g_scroll = NativeUI::Scroll(client, initial, "PG_ConfigScroll");
            for (int i = 0; i < kSettingCount; ++i)
            {
                char name[48];
                sprintf_s(name, "PG_ConfigRow%dLabel", i);
                g_rows[i].label = NativeUI::Label(g_scroll, initial, name, kLabels[i]);
                g_rows[i].label->setTextAlign(MyGUI::Align::VCenter | MyGUI::Align::Left);
                SetRowId(g_rows[i].label, i);
                sprintf_s(name, "PG_ConfigRow%dValue", i);
                g_rows[i].value = g_scroll->createWidget<MyGUI::EditBox>(
                    "Kenshi_EditBox", initial, MyGUI::Align::Default, name);
                g_rows[i].value->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
                g_rows[i].value->setMaxTextLength(32);
                SetRowId(g_rows[i].value, i);
                if (IsNumeric(i))
                {
                    g_rows[i].value->eventEditSelectAccept += MyGUI::newDelegate(OnValueAccept);
                    g_rows[i].value->eventEditTextChange += MyGUI::newDelegate(OnValueChanged);
                    sprintf_s(name, "PG_ConfigRow%dSlider", i);
                    g_rows[i].slider = g_scroll->createWidget<MyGUI::ScrollBar>(
                        "Kenshi_ScrollBarH", initial, MyGUI::Align::Default, name);
                    g_rows[i].slider->setScrollRange(kSliderSteps + 1);
                    g_rows[i].slider->setScrollPage(10);
                    g_rows[i].slider->setScrollViewPage(100);
                    g_rows[i].slider->setScrollWheelPage(0);
                    g_rows[i].slider->setTrackSize(28);
                    g_rows[i].slider->setMinTrackSize(28);
                    g_rows[i].slider->setMoveToClick(true);
                    SetRowId(g_rows[i].slider, i);
                    g_rows[i].slider->eventScrollChangePosition += MyGUI::newDelegate(OnSliderChanged);
                }
                else
                {
                    g_rows[i].value->setEditStatic(true);
                    sprintf_s(name, "PG_ConfigRow%dMinus", i);
                    g_rows[i].minus = NativeUI::Button(g_scroll, initial, name, "-");
                    SetRowId(g_rows[i].minus, i);
                    g_rows[i].minus->setUserString("direction", "-1");
                    g_rows[i].minus->eventMouseButtonClick += MyGUI::newDelegate(OnAdjust);
                    sprintf_s(name, "PG_ConfigRow%dPlus", i);
                    g_rows[i].plus = NativeUI::Button(g_scroll, initial, name, "+");
                    SetRowId(g_rows[i].plus, i);
                    g_rows[i].plus->setUserString("direction", "1");
                    g_rows[i].plus->eventMouseButtonClick += MyGUI::newDelegate(OnAdjust);
                }
            }
            g_save = NativeUI::Button(client, initial, "PG_ConfigSave", "SAVE SETTINGS");
            g_reset = NativeUI::Button(client, initial, "PG_ConfigReset", "RESET DEFAULTS");
            g_close = NativeUI::Button(client, initial, "PG_ConfigClose", "CLOSE");
            g_save->eventMouseButtonClick += MyGUI::newDelegate(OnSave);
            g_reset->eventMouseButtonClick += MyGUI::newDelegate(OnResetDefaults);
            g_close->eventMouseButtonClick += MyGUI::newDelegate(OnClose);
            client->eventChangeCoord += MyGUI::newDelegate(Layout);
            g_layoutReady = true;
            Refresh();
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: settings page creation failed: ") + error.what());
            DestroyWidgets();
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: settings page creation failed");
            DestroyWidgets();
        }
    }

    void OnSettingsButton(MyGUI::Widget*)
    {
        PGSettingsUI::Toggle();
    }
}

namespace PGSettingsUI
{
    void InjectModsTabUI(OptionsWindow* options)
    {
        if (!options || !options->created) return;
        MyGUI::TabControl* tabs = options->tabs;
        if (!tabs || !tabs->getItemCount()) return;

        MyGUI::TabItem* mods = NULL;
        for (size_t i = 0; i < tabs->getItemCount(); ++i)
        {
            if (tabs->getItemNameAt(i) == "MODS")
            {
                mods = tabs->getItemAt(i);
                break;
            }
        }
        if (!mods)
        {
            for (size_t i = 0; i < tabs->getItemCount(); ++i)
            {
                const std::string name = tabs->getItemAt(i)->getName();
                const size_t split = name.find('_');
                if (split != std::string::npos &&
                    (name.substr(split + 1) == "Mods" || name.substr(split + 1) == "ModTab"))
                {
                    mods = tabs->getItemAt(i);
                    break;
                }
            }
        }
        if (!mods || mods->findWidget("PGSettingsButton")) return;

        MyGUI::Button* reSettings = FindReSettingsButton(mods->getEnumerator());
        if (!reSettings || !reSettings->getParent()) return;
        const MyGUI::IntCoord reCoord = reSettings->getCoord();
        const int gap = Max(4, reCoord.height / 8);
        const int buttonWidth = Min(reCoord.width, reCoord.left - gap);
        if (buttonWidth < 80) return;
        MyGUI::Button* button = reSettings->getParent()->createWidget<MyGUI::Button>(
            "Kenshi_Button1",
            MyGUI::IntCoord(reCoord.left - gap - buttonWidth, reCoord.top,
                buttonWidth, reCoord.height),
            MyGUI::Align::Default, "PGSettingsButton");
        button->setCaption("PG CONFIG");
        button->eventMouseButtonClick += MyGUI::newDelegate(OnSettingsButton);
        PGLog::Debug("Proving Grounds: injected settings button into the Mods tab");
    }

    void Toggle()
    {
        Create();
        if (!g_window) return;
        const bool show = !g_window->getVisible();
        g_window->setVisible(show);
        if (show)
        {
            g_settings = PGConfig::GetEditableSettings();
            for (int i = 0; i < kSettingCount; ++i) g_numericDirty[i] = false;
            Refresh();
            g_window->upLayerItem();
        }
    }

    bool IsVisible()
    {
        return g_window && g_window->getVisible();
    }

    void Close()
    {
        if (g_window) g_window->setVisible(false);
    }

    void AbandonWorldState()
    {
        Close();
        DestroyWidgets();
    }
}
