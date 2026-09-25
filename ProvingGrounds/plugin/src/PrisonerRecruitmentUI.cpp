#include "PrisonerRecruitmentUI.h"

#include "ArenaIngress.h"
#include "ArenaUI.h"
#include "LeaderboardStore.h"
#include "NativeUI.h"
#include "PrisonerRecruitment.h"
#include "PrisonerRecruitmentPresentation.h"
#include "PrisonerUtil.h"
#include "SparSession.h"

#include "PGLog.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
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
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    struct PrisonerRow
    {
        MyGUI::Button* root;
        MyGUI::ImageBox* portrait;
        MyGUI::EditBox* name;
        MyGUI::TextBox* marks;
        Character* character;
    };

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_metrics = NULL;
    MyGUI::EditBox* g_subtitle = NULL;
    MyGUI::TextBox* g_rosterHeading = NULL;
    MyGUI::ScrollView* g_rosterScroll = NULL;
    MyGUI::ScrollView* g_detailScroll = NULL;
    MyGUI::ImageBox* g_portrait = NULL;
    MyGUI::EditBox* g_name = NULL;
    MyGUI::EditBox* g_stats = NULL;
    MyGUI::EditBox* g_explanation = NULL;
    MyGUI::EditBox* g_status = NULL;
    MyGUI::Button* g_recruit = NULL;
    MyGUI::Button* g_close = NULL;
    std::vector<Character*> g_candidates;
    std::vector<PrisonerRow> g_rows;
    Character* g_selected = NULL;
    Character* g_boundPortrait = NULL;
    MyGUI::IntSize g_clientSize;
    MyGUI::IntSize g_screenSize;
    int g_fontHeight = 0;
    int g_rosterContentHeight = 0;
    int g_detailContentHeight = 0;
    std::string g_message;

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }

    void Passive(MyGUI::Widget* widget)
    {
        if (!widget)
            return;
        widget->setNeedMouseFocus(false);
        widget->setNeedKeyFocus(false);
        for (size_t i = 0; i < widget->getChildCount(); ++i)
            Passive(widget->getChildAt(i));
        MyGUI::Widget* client = widget->getClientWidget();
        if (client && client != widget)
            Passive(client);
    }

    int BodyHeight()
    {
        return g_metrics ? Max(1, g_metrics->getFontHeight()) : 16;
    }

    int WrappedHeight(MyGUI::EditBox* label, int x, int y, int width)
    {
        label->setCoord(x, y, Max(1, width), BodyHeight());
        const int insets = Max(0,
            label->getHeight() - label->getTextRegion().height);
        const int height = Max(BodyHeight(),
            label->getTextSize().height + insets);
        label->setSize(Max(1, width), height);
        return height;
    }

    void BindPortrait(MyGUI::ImageBox* image, Character* character)
    {
        if (!image)
            return;
        if (!character || !character->isValid())
        {
            image->setVisible(false);
            return;
        }
        PortraitManager* manager = PortraitManager::getInstance();
        if (manager)
            manager->setImageWidget(character->getHandle(), image, true);
        image->setVisible(manager != NULL);
    }

    bool Contains(const std::vector<Character*>& values, Character* character)
    {
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] == character)
                return true;
        return false;
    }

    bool SameCandidates(const std::vector<Character*>& values)
    {
        if (values.size() != g_candidates.size())
            return false;
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] != g_candidates[i])
                return false;
        return true;
    }

    void ClearRows()
    {
        for (size_t i = 0; i < g_rows.size(); ++i)
            if (g_rows[i].root)
                MyGUI::Gui::getInstance().destroyWidget(g_rows[i].root);
        g_rows.clear();
    }

    void LayoutWindow();
    void RefreshAll(bool preserveMessage = true);

    void Select(Character* character)
    {
        if (!Contains(g_candidates, character))
            character = NULL;
        g_selected = character;
        g_message.clear();
        RefreshAll(false);
    }

    void OnRowClicked(MyGUI::Widget* sender)
    {
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].root == sender)
            {
                Select(g_rows[i].character);
                return;
            }
        }
    }

    void RebuildRows()
    {
        ClearRows();
        if (!g_rosterScroll)
            return;
        const MyGUI::IntCoord initial(0, 0, 100, 24);
        for (size_t i = 0; i < g_candidates.size(); ++i)
        {
            Character* character = g_candidates[i];
            char name[64];
            sprintf_s(name, "PG_RecruitmentRow_%d", static_cast<int>(i));
            PrisonerRow row = {};
            row.character = character;
            row.root = NativeUI::Button(g_rosterScroll, initial, name, "");
            row.root->eventMouseButtonClick += MyGUI::newDelegate(OnRowClicked);

            sprintf_s(name, "PG_RecruitmentPortrait_%d", static_cast<int>(i));
            row.portrait = row.root->createWidget<MyGUI::ImageBox>(
                "ImageBox", initial, MyGUI::Align::Default, name);
            Passive(row.portrait);
            BindPortrait(row.portrait, character);

            sprintf_s(name, "PG_RecruitmentName_%d", static_cast<int>(i));
            row.name = row.root->createWidget<MyGUI::EditBox>(
                "Kenshi_PaintedWordWrapEmpty", initial,
                MyGUI::Align::Default, name);
            Passive(row.name);
            row.name->setCaption(character->getName());

            sprintf_s(name, "PG_RecruitmentMarks_%d", static_cast<int>(i));
            row.marks = row.root->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxPaintedText", initial,
                MyGUI::Align::Default, name);
            Passive(row.marks);
            g_rows.push_back(row);
        }
    }

    bool RefreshCandidates()
    {
        std::vector<Character*> candidates;
        const std::vector<Character*>& roster =
            PrisonerUtil::GetRosterPrisoners();
        for (size_t i = 0; i < roster.size(); ++i)
        {
            Character* prisoner = roster[i];
            if (!prisoner || !prisoner->isValid() || prisoner->isDead() ||
                Contains(candidates, prisoner))
                continue;
            candidates.push_back(prisoner);
        }

        const bool changed = !SameCandidates(candidates);
        g_candidates = candidates;
        if (!Contains(g_candidates, g_selected))
            g_selected = g_candidates.empty() ? NULL : g_candidates[0];
        if (changed)
            RebuildRows();
        return changed;
    }

    PrisonerRecruitmentPresentation::State PresentationState()
    {
        PrisonerRecruitmentPresentation::Input input;
        input.hasSelection = g_selected && g_selected->isValid();
        input.livingRosterPrisoner = input.hasSelection &&
            !g_selected->isDead() && PrisonerUtil::IsRosterPrisoner(g_selected);
        input.busy = ArenaIngress::IsPending() || SparSession::IsActive() ||
            PrisonerUtil::HasRuntimeActivity();
        input.unconscious = input.hasSelection && g_selected->isUnconcious();
        input.playerReady = ou && ou->player;
        if (input.hasSelection)
        {
            input.currentMarks = LeaderboardStore::GetMarks(g_selected);
            input.requiredMarks = PrisonerRecruitment::RequiredMarks(g_selected);
        }
        return PrisonerRecruitmentPresentation::Evaluate(input);
    }

    void UpdateDetails()
    {
        const PrisonerRecruitmentPresentation::State state =
            PresentationState();
        g_recruit->setCaption(state.actionCaption);
        g_recruit->setEnabled(state.enabled);
        g_explanation->setCaption(state.explanation);

        if (g_selected && g_selected->isValid())
        {
            g_name->setCaption(g_selected->getName());
            char stats[128];
            sprintf_s(stats,
                "Combat %.0f  |  %d / %d Arena Marks",
                PrisonerRecruitment::AverageCombatStat(g_selected),
                LeaderboardStore::GetMarks(g_selected),
                PrisonerRecruitment::RequiredMarks(g_selected));
            g_stats->setCaption(stats);
            if (g_boundPortrait != g_selected)
            {
                BindPortrait(g_portrait, g_selected);
                g_boundPortrait = g_selected;
            }
        }
        else
        {
            g_name->setCaption("No prisoner selected");
            g_stats->setCaption("No recruitment contract available.");
            g_portrait->setVisible(false);
            g_boundPortrait = NULL;
        }

        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            PrisonerRow& row = g_rows[i];
            char marks[48];
            sprintf_s(marks, "%d Marks",
                LeaderboardStore::GetMarks(row.character));
            row.marks->setCaption(marks);
            row.root->setStateSelected(row.character == g_selected);
        }
        g_status->setCaption(g_message.empty()
            ? "Select a prisoner to review their freedom contract."
            : g_message.c_str());
    }

    void LayoutRows()
    {
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = BodyHeight();
        const int image = Max(48, body * 3);
        const int oldOffset = -g_rosterScroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_rosterScroll->getViewCoord();
            const int width = Max(1, view.width);
            int y = 0;
            for (size_t i = 0; i < g_rows.size(); ++i)
            {
                PrisonerRow& row = g_rows[i];
                const int textX = image + 2 * gap;
                const int textWidth = Max(1, width - textX - gap);
                row.name->setCoord(textX, gap, textWidth, body);
                const int nameHeight = Max(body,
                    row.name->getTextSize().height);
                row.name->setSize(textWidth, nameHeight);
                row.marks->setCoord(textX, gap + nameHeight + gap,
                    textWidth, body);
                const int height = Max(image,
                    nameHeight + gap + body) + 2 * gap;
                row.root->setCoord(0, y, width, height);
                row.portrait->setCoord(gap, gap, image, image);
                y += height + gap;
            }
            g_rosterContentHeight = y;
            g_rosterScroll->setCanvasSize(Max(1, view.width),
                Max(y, view.height));
            if (g_rosterScroll->getViewCoord().width == view.width)
                break;
        }
        const int maximum = Max(0, g_rosterContentHeight -
            g_rosterScroll->getViewCoord().height);
        g_rosterScroll->setViewOffset(MyGUI::IntPoint(
            0, -Min(maximum, Max(0, oldOffset))));
    }

    void LayoutDetails()
    {
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = BodyHeight();
        const int oldOffset = -g_detailScroll->getViewOffset().top;
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_detailScroll->getViewCoord();
            const int width = Max(1, view.width - 2 * gap);
            const int portrait = Min(Max(80, body * 7), width);
            int y = gap;
            g_portrait->setCoord(gap, y, portrait, portrait);
            y += portrait + gap;
            y += WrappedHeight(g_name, gap, y, width) + gap;
            y += WrappedHeight(g_stats, gap, y, width) + gap;
            y += WrappedHeight(g_explanation, gap, y, width) + gap;
            g_detailContentHeight = y;
            g_detailScroll->setCanvasSize(Max(1, view.width),
                Max(y, view.height));
            if (g_detailScroll->getViewCoord().width == view.width)
                break;
        }
        const int maximum = Max(0, g_detailContentHeight -
            g_detailScroll->getViewCoord().height);
        g_detailScroll->setViewOffset(MyGUI::IntPoint(
            0, -Min(maximum, Max(0, oldOffset))));
    }

    void FitWindowToScreen()
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render)
            return;
        const MyGUI::IntSize size = render->getViewSize();
        if (size.width <= 0 || size.height <= 0 || size == g_screenSize)
            return;
        g_screenSize = size;
        const int width = size.width * 88 / 100;
        const int height = size.height * 88 / 100;
        g_window->setCoord((size.width - width) / 2,
            (size.height - height) / 2, width, height);
    }

    void LayoutWindow()
    {
        if (!g_window)
            return;
        const MyGUI::IntSize size = g_window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(g_metrics);
        const int body = BodyHeight();
        const int width = Max(1, size.width - 2 * gap);
        const int buttonHeight = Max(
            NativeUI::RowHeight(g_metrics, 0),
            NativeUI::RowHeight(g_recruit, 0));
        const int closeWidth = Min(width / 3,
            g_close->getTextSize().width + 4 * gap);
        const int buttonTop = Max(gap, size.height - gap - buttonHeight);
        g_close->setCoord(gap, buttonTop, Max(1, closeWidth), buttonHeight);
        g_recruit->setCoord(gap + closeWidth + gap, buttonTop,
            Max(1, width - closeWidth - gap), buttonHeight);

        g_status->setCoord(gap, gap, width, body * 2 + 2 * gap);
        const int statusInsets = Max(0,
            g_status->getHeight() - g_status->getTextRegion().height);
        const int statusHeight = Min(body * 2 + statusInsets,
            Max(body, g_status->getTextSize().height + statusInsets));
        const int statusTop = Max(gap,
            buttonTop - gap - statusHeight);
        g_status->setCoord(gap, statusTop, width, statusHeight);

        g_subtitle->setCoord(gap, gap, width, body * 2 + 2 * gap);
        const int subtitleInsets = Max(0,
            g_subtitle->getHeight() - g_subtitle->getTextRegion().height);
        const int subtitleHeight = Max(body,
            g_subtitle->getTextSize().height + subtitleInsets);
        g_subtitle->setSize(width, subtitleHeight);
        const int contentTop = gap + subtitleHeight + gap;
        const int contentHeight = Max(1, statusTop - gap - contentTop);
        const bool stacked = width < body * 55;

        if (stacked)
        {
            g_rosterHeading->setCoord(gap, contentTop, width, body);
            const int splitHeight = Max(1,
                contentHeight - body - 2 * gap);
            const int rosterHeight = Max(1, splitHeight * 2 / 5);
            g_rosterScroll->setCoord(gap, contentTop + body + gap,
                width, rosterHeight);
            const int detailTop = contentTop + body + 2 * gap + rosterHeight;
            g_detailScroll->setCoord(gap, detailTop, width,
                Max(1, contentTop + contentHeight - detailTop));
        }
        else
        {
            const int rosterWidth = Max(body * 15, width * 32 / 100);
            g_rosterHeading->setCoord(gap, contentTop, rosterWidth, body);
            g_rosterScroll->setCoord(gap, contentTop + body + gap,
                rosterWidth, Max(1, contentHeight - body - gap));
            g_detailScroll->setCoord(gap + rosterWidth + gap, contentTop,
                Max(1, width - rosterWidth - gap), contentHeight);
        }
        LayoutRows();
        LayoutDetails();
        g_clientSize = size;
        g_fontHeight = body;
    }

    void AbandonWindow()
    {
        if (g_window)
            MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_metrics = g_rosterHeading = NULL;
        g_subtitle = g_name = g_stats = g_explanation = g_status = NULL;
        g_rosterScroll = g_detailScroll = NULL;
        g_portrait = NULL;
        g_recruit = g_close = NULL;
        g_rows.clear();
        g_candidates.clear();
        g_selected = NULL;
        g_boundPortrait = NULL;
        g_clientSize = g_screenSize = MyGUI::IntSize();
        g_fontHeight = 0;
        g_message.clear();
    }

    void OnRecruitClicked(MyGUI::Widget*)
    {
        if (!g_selected || !g_selected->isValid())
            return;
        std::string status;
        PrisonerRecruitment::Recruit(g_selected, status);
        g_message = status;
        RefreshAll(true);
    }

    void OnCloseClicked(MyGUI::Widget*)
    {
        PrisonerRecruitmentUI::Close();
    }

    void OnWindowButtonPressed(MyGUI::Widget*, const std::string& name)
    {
        if (name == "close" || name == "Close")
            PrisonerRecruitmentUI::Close();
    }

    void EnsureWindow()
    {
        if (g_window)
            return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;
        const char* resource = "Kenshi_WindowCX";
        try
        {
            g_window = gui->createWidgetReal<MyGUI::Window>(resource,
                .06f, .06f, .88f, .88f, MyGUI::Align::Center,
                "Window", "ProvingGroundsRecruitmentWindow");
            g_window->setCaption("Proving Grounds - Prisoner Recruitment");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed +=
                MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client)
                throw std::runtime_error("window has no client widget");
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            g_metrics = NativeUI::Label(client, initial,
                "PG_RecruitmentMetrics", "Recruitment");
            g_metrics->setVisible(false);
            resource = "Kenshi_WordWrap";
            g_subtitle = client->createWidget<MyGUI::EditBox>(resource,
                initial, MyGUI::Align::Default, "PG_RecruitmentSubtitle");
            g_subtitle->setCaption(
                "CONTRACTS OF FREEDOM  |  Prisoners spend their own Arena Marks");
            g_status = client->createWidget<MyGUI::EditBox>(resource,
                initial, MyGUI::Align::Default, "PG_RecruitmentStatus");
            g_rosterHeading = NativeUI::Label(client, initial,
                "PG_RecruitmentRosterHeading", "PRISONERS");
            g_rosterScroll = NativeUI::Scroll(client, initial,
                "PG_RecruitmentRoster");
            g_detailScroll = NativeUI::Scroll(client, initial,
                "PG_RecruitmentDetails");
            resource = "ImageBox";
            g_portrait = g_detailScroll->createWidget<MyGUI::ImageBox>(
                resource, initial, MyGUI::Align::Default,
                "PG_RecruitmentSelectedPortrait");
            Passive(g_portrait);
            resource = "Kenshi_WordWrapEmpty";
            g_name = NativeUI::WrappedLabel(g_detailScroll, initial,
                "PG_RecruitmentSelectedName", "No prisoner selected");
            g_stats = NativeUI::WrappedLabel(g_detailScroll, initial,
                "PG_RecruitmentSelectedStats", "");
            g_explanation = NativeUI::WrappedLabel(g_detailScroll, initial,
                "PG_RecruitmentExplanation", "");
            resource = "Kenshi_Button1";
            g_close = NativeUI::Button(client, initial,
                "PG_RecruitmentClose", "Back to Arena");
            g_close->eventMouseButtonClick +=
                MyGUI::newDelegate(OnCloseClicked);
            g_recruit = NativeUI::Button(client, initial,
                "PG_RecruitmentAction", "No Prisoner Selected");
            g_recruit->eventMouseButtonClick +=
                MyGUI::newDelegate(OnRecruitClicked);
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string(
                "Proving Grounds: prisoner recruitment UI creation failed at ") +
                resource + ": " + error.what());
            AbandonWindow();
        }
        catch (...)
        {
            PGLog::Error(std::string(
                "Proving Grounds: prisoner recruitment UI creation failed at ") +
                resource);
            AbandonWindow();
        }
    }

    void RefreshAll(bool preserveMessage)
    {
        if (!g_window)
            return;
        if (!preserveMessage)
            g_message.clear();
        try
        {
            const bool candidatesChanged = RefreshCandidates();
            UpdateDetails();
            if (candidatesChanged ||
                g_window->getClientWidget()->getSize() != g_clientSize ||
                BodyHeight() != g_fontHeight)
                LayoutWindow();
            else
            {
                LayoutRows();
                LayoutDetails();
            }
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: prisoner recruitment UI refresh failed");
            AbandonWindow();
        }
    }
}

namespace PrisonerRecruitmentUI
{
    void Show()
    {
        EnsureWindow();
        if (!g_window)
            return;
        g_message.clear();
        g_rosterScroll->setViewOffset(MyGUI::IntPoint());
        g_detailScroll->setViewOffset(MyGUI::IntPoint());
        RefreshAll(false);
        if (!g_window)
            return;
        FitWindowToScreen();
        LayoutWindow();
        g_window->setVisible(true);
        PGLog::Debug("Proving Grounds: prisoner recruitment UI shown");
    }

    void Close()
    {
        if (g_window)
            g_window->setVisible(false);
        Building* registry = ArenaIngress::GetBoundRegistry();
        if (registry && registry->isValid())
            ArenaUI::ShowFromRegistry(registry, false);
    }

    bool IsVisible()
    {
        return g_window && g_window->getVisible();
    }

    void Tick()
    {
        if (!IsVisible())
            return;
        try
        {
            FitWindowToScreen();
            RefreshAll(true);
        }
        catch (...)
        {
            PGLog::Error("Proving Grounds: prisoner recruitment UI tick failed");
            AbandonWindow();
        }
    }

    void AbandonWorldState()
    {
        if (g_window)
            g_window->setVisible(false);
        ClearRows();
        g_candidates.clear();
        g_selected = NULL;
        g_boundPortrait = NULL;
        g_message.clear();
    }
}
