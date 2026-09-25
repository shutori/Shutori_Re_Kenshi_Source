#include "DebugMenu.h"
#include "TownArena.h"
#include "TownDiagnosticRun.h"

#include "ArenaIngress.h"
#include "LeaderboardStore.h"
#include "PrisonerUtil.h"
#include "SquadUtil.h"
#include "NativeUI.h"

#include "PGLog.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#pragma warning(pop)

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <cstdio>
#include <cstdlib>
#include <Windows.h>
#include <exception>
#include <stdexcept>
#include <vector>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    const int kRowsPerPage = 8;
    const int kDiagnosticActions = 19;
    enum Tab { FightersTab, DiagnosticsTab };

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_selectedName = NULL;
    MyGUI::TextBox* g_rating = NULL;
    MyGUI::TextBox* g_marks = NULL;
    MyGUI::TextBox* g_status = NULL;
    MyGUI::TextBox* g_pageLabel = NULL;
    MyGUI::Button* g_rows[kRowsPerPage] = {};
    MyGUI::Button* g_prev = NULL;
    MyGUI::Button* g_next = NULL;
    MyGUI::TextBox* g_heading = NULL;
    MyGUI::TextBox* g_rowText[kRowsPerPage] = {};
    MyGUI::Button* g_actions[12] = {};
    MyGUI::TextBox* g_actionText[12] = {};
    MyGUI::ScrollView* g_scroll = NULL;
    MyGUI::Button* g_tabs[2] = {};
    MyGUI::TextBox* g_diagnosticSummary = NULL;
    MyGUI::Button* g_diagnosticActions[kDiagnosticActions] = {};
    MyGUI::TextBox* g_diagnosticText[kDiagnosticActions] = {};
    bool g_layoutReady = false;
    bool g_inLayout = false;
    std::vector<Character*> g_characters;
    Character* g_selected = NULL;
    int g_page = 0;
    Tab g_tab = FightersTab;
    int g_confirmFresh = 0;
    DWORD g_lastDiagnosticRefresh = 0;

    int Max(int a, int b) { return a > b ? a : b; }

    void DestroyWidgets()
    {
        g_layoutReady = false;
        g_inLayout = false;
        if (g_window) MyGUI::Gui::getInstance().destroyWidget(g_window);
        g_window = NULL;
        g_selectedName = g_rating = g_marks = g_status = g_pageLabel = g_heading = NULL;
        g_prev = g_next = NULL;
        g_scroll = NULL;
        g_tabs[0] = g_tabs[1] = NULL;
        g_diagnosticSummary = NULL;
        for (int i = 0; i < kRowsPerPage; ++i) { g_rows[i] = NULL; g_rowText[i] = NULL; }
        for (int i = 0; i < 12; ++i) { g_actions[i] = NULL; g_actionText[i] = NULL; }
        for (int i = 0; i < kDiagnosticActions; ++i) { g_diagnosticActions[i] = NULL; g_diagnosticText[i] = NULL; }
    }

    MyGUI::TextBox* MakeLabel(MyGUI::Widget* parent,
        const char* name, const char* caption, bool painted = false)
    {
        MyGUI::TextBox* label = painted
            ? parent->createWidget<MyGUI::EditBox>("Kenshi_PaintedWordWrapEmpty",
                MyGUI::IntCoord(0, 0, 100, 24), MyGUI::Align::Default, name)
            : NativeUI::WrappedLabel(parent, MyGUI::IntCoord(0, 0, 100, 24), name, caption);
        if (painted) label->setCaption(caption);
        // Keep the native parent button as the input target, including over
        // the EditBox's separate text client.
        for (MyGUI::Widget* widget = label; widget; )
        {
            widget->setNeedMouseFocus(false);
            widget->setNeedKeyFocus(false);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
        return label;
    }

    void OnWheel(MyGUI::Widget*, int relative)
    {
        if (!g_scroll || !relative) return;
        const MyGUI::IntPoint old = g_scroll->getViewOffset();
        const int limit = Max(0, g_scroll->getCanvasSize().height - g_scroll->getViewCoord().height);
        int offset = -old.top + (relative < 0 ? 1 : -1) * NativeUI::RowHeight(g_prev, 0) * 2;
        if (offset < 0) offset = 0;
        if (offset > limit) offset = limit;
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -offset));
    }

    void BindWheel(MyGUI::Widget* widget)
    {
        while (widget)
        {
            widget->setNeedMouseFocus(true);
            widget->setNeedKeyFocus(false);
            widget->eventMouseWheel += MyGUI::newDelegate(OnWheel);
            MyGUI::Widget* client = widget->getClientWidget();
            if (client == widget) break;
            widget = client;
        }
    }

    MyGUI::Button* MakeButton(MyGUI::Widget* parent,
        const char* name, const char* caption, void (*handler)(MyGUI::Widget*))
    {
        MyGUI::Button* button = NativeUI::Button(parent,
            MyGUI::IntCoord(0, 0, 100, 24), name, caption);
        button->eventMouseButtonClick += MyGUI::newDelegate(handler);
        if (parent == g_scroll) button->eventMouseWheel += MyGUI::newDelegate(OnWheel);
        return button;
    }

    int PlaceText(MyGUI::TextBox* label, int x, int y, int width, int body)
    {
        label->setCoord(x, y, Max(1, width), body);
        const int height = Max(body, label->getTextSize().height);
        label->setSize(Max(1, width), height);
        return height;
    }

    void Layout()
    {
        if (!g_layoutReady || g_inLayout) return;
        g_inLayout = true;
        const MyGUI::IntSize size = g_window->getClientWidget()->getSize();
        const int gap = NativeUI::Spacing(g_prev);
        const int body = Max(1, Max(g_prev->getFontHeight(), g_prev->getTextSize().height));
        const int width = Max(1, size.width - 2 * gap);
        const int pageHeight = NativeUI::RowHeight(g_prev, 0);
        const int tabWidth = Max(1,(width-gap)/2);
        g_tabs[0]->setCoord(gap,gap,tabWidth,pageHeight);
        g_tabs[1]->setCoord(gap+tabWidth+gap,gap,Max(1,width-tabWidth-gap),pageHeight);
        const int headingTop=gap+pageHeight+gap;
        const int headingHeight = PlaceText(g_heading, gap, headingTop, width, body);
        const int pageTop = headingTop + headingHeight + gap;
        const int pageWidth = Max(g_prev->getTextSize().width, g_next->getTextSize().width) + 4 * gap;
        if(g_tab==FightersTab){
            g_prev->setCoord(gap, pageTop, pageWidth, pageHeight);
            g_next->setCoord(Max(gap, size.width - gap - pageWidth), pageTop, pageWidth, pageHeight);
            g_pageLabel->setCoord(gap + pageWidth + gap, pageTop,Max(1, width - 2 * pageWidth - 2 * gap), pageHeight);
        }
        // Native scrollable status remains outside growing fighter/action content.
        const int statusHeight = body * 3 + 2 * gap;
        const int statusTop = Max(0, size.height - gap - statusHeight);
        g_status->setCoord(gap, statusTop, width, Max(0, size.height - gap - statusTop));
        const int scrollTop = pageTop + (g_tab==FightersTab?pageHeight+gap:0);
        const int scrollHeight = Max(0, statusTop - gap - scrollTop);
        const MyGUI::IntPoint oldOffset = g_scroll->getViewOffset();
        g_scroll->setCoord(gap, scrollTop, width, scrollHeight);
        for (int pass = 0; pass < 2; ++pass)
        {
            const MyGUI::IntCoord view = g_scroll->getViewCoord();
            const int contentWidth = Max(1, view.width - 2 * gap);
            const bool stacked = contentWidth < body * 36 + 3 * gap;
            const int listWidth = stacked ? contentWidth : (contentWidth - gap) / 2;
            const int detailX = stacked ? gap : gap + listWidth + gap;
            const int detailWidth = stacked ? contentWidth : contentWidth - listWidth - gap;
            int listBottom = gap;
            for (int i = 0; g_tab==FightersTab && i < kRowsPerPage; ++i)
            {
                if (!g_rows[i]->getVisible()) continue;
                const int textHeight = PlaceText(g_rowText[i], gap, gap,
                    listWidth - 2 * gap, body);
                g_rows[i]->setCoord(gap, listBottom, listWidth, textHeight + 2 * gap);
                listBottom += textHeight + 3 * gap;
            }
            int y = stacked ? listBottom + gap : gap;
            if(g_tab==FightersTab){
                y += PlaceText(g_selectedName, detailX, y, detailWidth, body) + gap;
                y += PlaceText(g_rating, detailX, y, detailWidth, body) + gap;
                y += PlaceText(g_marks, detailX, y, detailWidth, body) + gap;
                for (int i = 0; i < 12; ++i){
                    const int height = PlaceText(g_actionText[i], gap, gap,detailWidth - 2 * gap, body) + 2 * gap;
                    g_actions[i]->setCoord(detailX, y, detailWidth, height);y += height + gap;
                }
            } else {
                y=gap; y+=PlaceText(g_diagnosticSummary,gap,y,contentWidth,body)+2*gap;
                const int buttonWidth=stacked?contentWidth:(contentWidth-gap)/2;
                const int actionHeight=body*2+2*gap;
                int visibleIndex=0;
                for(int i=0;i<kDiagnosticActions;++i){
                    if(!g_diagnosticActions[i]->getVisible())continue;
                    const int column=stacked?0:visibleIndex%2;
                    const int x=gap+column*(buttonWidth+gap);
                    g_diagnosticActions[i]->setCoord(x,y,buttonWidth,actionHeight);
                    PlaceText(g_diagnosticText[i],gap,gap,buttonWidth-2*gap,body);
                    ++visibleIndex;
                    if(stacked || visibleIndex%2==0)y+=actionHeight+gap;
                }
                if(!stacked && visibleIndex%2)y+=actionHeight+gap;
                listBottom=0;
            }
            g_scroll->setCanvasSize(Max(1, view.width), Max(view.height, Max(y, listBottom) + gap));
            if (view.width == g_scroll->getViewCoord().width) break;
        }
        const int limit = Max(0, g_scroll->getCanvasSize().height - g_scroll->getViewCoord().height);
        const int offset = Max(0, -oldOffset.top);
        g_scroll->setViewOffset(MyGUI::IntPoint(0, -(offset < limit ? offset : limit)));
        g_inLayout = false;
    }

    void OnCoordChanged(MyGUI::Widget*)
    {
        try { Layout(); }
        catch (...)
        {
            PGLog::Error("Proving Grounds: native debug menu layout failed");
            DestroyWidgets();
        }
    }

    void CollectCharacters()
    {
        SquadUtil::CollectPlayerSquad(g_characters);
        std::vector<Character*> prisoners;
        if (ArenaIngress::GetBoundRegistry() && !PrisonerUtil::HasRuntimeActivity())
        {
            PrisonerUtil::CollectNearbyCagePrisoners(
                ArenaIngress::GetBoundRegistry(), prisoners);
        }
        else
        {
            prisoners = PrisonerUtil::GetRosterPrisoners();
        }
        for (size_t p = 0; p < prisoners.size(); ++p)
        {
            Character* prisoner = prisoners[p];
            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
                continue;
            bool duplicate = false;
            for (size_t i = 0; i < g_characters.size(); ++i)
            {
                if (g_characters[i] == prisoner)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                g_characters.push_back(prisoner);
        }
        if (g_selected && !g_selected->isValid())
            g_selected = NULL;

        const int pages = g_characters.empty()
            ? 1
            : static_cast<int>((g_characters.size() + kRowsPerPage - 1) / kRowsPerPage);
        if (g_page >= pages)
            g_page = pages - 1;
        if (g_page < 0)
            g_page = 0;
    }

    void Refresh()
    {
        if (!g_window)
            return;

        const bool fighters=g_tab==FightersTab;
        g_tabs[0]->setStateSelected(fighters);g_tabs[1]->setStateSelected(!fighters);
        g_heading->setCaption(fighters?"Fighter Ratings & Marks":"Controlled Matchmaking Diagnostics");
        g_prev->setVisible(fighters);g_next->setVisible(fighters);g_pageLabel->setVisible(fighters);
        g_selectedName->setVisible(fighters);g_rating->setVisible(fighters);g_marks->setVisible(fighters);
        for(int i=0;i<12;++i)g_actions[i]->setVisible(fighters);
        if(!fighters)for(int i=0;i<kRowsPerPage;++i)g_rows[i]->setVisible(false);
        g_diagnosticSummary->setVisible(!fighters);
        for(int i=0;i<kDiagnosticActions;++i)g_diagnosticActions[i]->setVisible(!fighters);

        if(!fighters){
            TownDiagnosticData::State& d=TownDiagnosticRun::Get();
            g_diagnosticSummary->setCaption(TownDiagnosticRun::StatusText());
            char labels[kDiagnosticActions][96];
            sprintf_s(labels[0],"%s",g_confirmFresh==1?"CONFIRM: reset town records + start 150":"Start Fresh 150 (resets town records)");
            sprintf_s(labels[1],"Start Current 150");
            sprintf_s(labels[2],"%s",g_confirmFresh==2?"CONFIRM: reset + start configured":"Start Configured Fresh");
            sprintf_s(labels[3],"Start Configured / Current Ratings");
            sprintf_s(labels[4],"%s",d.status==TownDiagnosticData::Paused?"Resume Diagnostic Run":"Pause Diagnostic Selection");
            sprintf_s(labels[5],"End Run");
            sprintf_s(labels[6],"Target preset: %d",d.config.target);
            sprintf_s(labels[7],"Target -10");sprintf_s(labels[8],"Target +10");
            sprintf_s(labels[9],"Stats-only model: %s",d.config.models[0]?"ON":"OFF");
            sprintf_s(labels[10],"Reduced-MMR model: %s",d.config.models[1]?"ON":"OFF");
            sprintf_s(labels[11],"Full-MMR model: %s",d.config.models[2]?"ON":"OFF");
            sprintf_s(labels[12],"Reduced MMR influence: %.0f%%",d.config.reducedInfluence*100);
            sprintf_s(labels[13],"1v1 coverage: %s",d.config.coverage[0]?"ON":"OFF");
            sprintf_s(labels[14],"Equal-group coverage: %s",d.config.coverage[1]?"ON":"OFF");
            sprintf_s(labels[15],"One-fighter difference: %s",d.config.coverage[2]?"ON":"OFF");
            sprintf_s(labels[16],"Large roster difference: %s",d.config.coverage[3]?"ON":"OFF");
            sprintf_s(labels[17],"Moderate-underdog share: %d%%",d.config.underdogPercent);
            sprintf_s(labels[18],"Style/tier balancing: %s",d.config.balanceStyleTier?"ON":"OFF");
            for(int i=0;i<kDiagnosticActions;++i)g_diagnosticText[i]->setCaption(labels[i]);
            const bool live=d.status==TownDiagnosticData::Active || d.status==TownDiagnosticData::Paused;
            for(int i=0;i<4;++i)g_diagnosticActions[i]->setEnabled(!live);
            g_diagnosticActions[4]->setEnabled((d.status==TownDiagnosticData::Active && !d.pending.active)||d.status==TownDiagnosticData::Paused);
            g_diagnosticActions[5]->setEnabled(live && !d.pending.active);
            for(int i=6;i<kDiagnosticActions;++i)g_diagnosticActions[i]->setEnabled(!live);
            Layout();return;
        }

        CollectCharacters();
        const int first = g_page * kRowsPerPage;
        for (int row = 0; row < kRowsPerPage; ++row)
        {
            const int index = first + row;
            MyGUI::Button* button = g_rows[row];
            if (index >= static_cast<int>(g_characters.size()))
            {
                button->setVisible(false);
                continue;
            }

            Character* character = g_characters[index];
            char stats[80];
            char indexText[24];
            const bool prisoner = PrisonerUtil::IsRosterPrisoner(character);
            sprintf_s(stats, "    R:%d  M:%d",
                static_cast<int>(LeaderboardStore::GetRating(character) + 0.5f),
                LeaderboardStore::GetMarks(character));
            sprintf_s(indexText, "%d", index);
            g_rowText[row]->setCaption(character->getName() +
                std::string(prisoner ? " [PRISONER]" : "") + stats);
            button->setUserString("index", indexText);
            button->setVisible(true);
            button->setStateSelected(character == g_selected);
        }

        const int pages = g_characters.empty()
            ? 1
            : static_cast<int>((g_characters.size() + kRowsPerPage - 1) / kRowsPerPage);
        char pageText[64];
        sprintf_s(pageText, "Page %d / %d", g_page + 1, pages);
        g_pageLabel->setCaption(pageText);
        g_prev->setEnabled(g_page > 0);
        g_next->setEnabled(g_page + 1 < pages);

        if (!g_selected)
        {
            g_selectedName->setCaption("Select a character");
            g_rating->setCaption("Rating: --");
            g_marks->setCaption("Arena Marks: --");
            Layout();
            return;
        }

        g_selectedName->setCaption(g_selected->getName());
        char ratingText[64];
        sprintf_s(ratingText, "Rating: %d",
            static_cast<int>(LeaderboardStore::GetRating(g_selected) + 0.5f));
        g_rating->setCaption(ratingText);
        char marksText[64];
        sprintf_s(marksText, "Arena Marks: %d",
            LeaderboardStore::GetMarks(g_selected));
        g_marks->setCaption(marksText);
        Layout();
    }

    void OnSelect(MyGUI::Widget* sender)
    {
        const int index = sender ? atoi(sender->getUserString("index").c_str()) : -1;
        if (index >= 0 && index < static_cast<int>(g_characters.size()))
        {
            g_selected = g_characters[index];
            g_status->setCaption("Changes are included in the next Kenshi save.");
            Refresh();
        }
    }

    void ChangeRating(float delta)
    {
        if (!g_selected)
        {
            g_status->setCaption("Select a character first.");
            return;
        }

        const float next = LeaderboardStore::GetRating(g_selected) + delta;
        g_status->setCaption(LeaderboardStore::SetRating(g_selected, next)
            ? "Rating updated for the active world."
            : "Could not save rating (load a game first).");
        Refresh();
    }

    void OnMinus100(MyGUI::Widget*) { ChangeRating(-100.0f); }
    void OnMinus10(MyGUI::Widget*) { ChangeRating(-10.0f); }
    void OnPlus10(MyGUI::Widget*) { ChangeRating(10.0f); }
    void OnPlus100(MyGUI::Widget*) { ChangeRating(100.0f); }

    void OnReset(MyGUI::Widget*)
    {
        if (!g_selected)
        {
            g_status->setCaption("Select a character first.");
            return;
        }
        g_status->setCaption(LeaderboardStore::SetRating(g_selected, 0.0f)
            ? "Rating reset for the active world."
            : "Could not save rating (load a game first).");
        Refresh();
    }

    void ChangeMarks(int delta)
    {
        if (!g_selected)
        {
            g_status->setCaption("Select a character or prisoner first.");
            return;
        }
        const int next = LeaderboardStore::GetMarks(g_selected) + delta;
        g_status->setCaption(LeaderboardStore::SetMarks(g_selected, next)
            ? "Arena Marks updated for the active world."
            : "Could not save Arena Marks (load a game first).");
        Refresh();
    }

    void OnMarksMinus10(MyGUI::Widget*) { ChangeMarks(-10); }
    void OnMarksPlus10(MyGUI::Widget*) { ChangeMarks(10); }
    void OnMarksPlus100(MyGUI::Widget*) { ChangeMarks(100); }

    void OnMarksReset(MyGUI::Widget*)
    {
        if (!g_selected)
        {
            g_status->setCaption("Select a character or prisoner first.");
            return;
        }
        g_status->setCaption(LeaderboardStore::SetMarks(g_selected, 0)
            ? "Arena Marks reset for the active world."
            : "Could not save Arena Marks (load a game first).");
        Refresh();
    }

    void OnTownTrial(MyGUI::Widget*)
    {
        TownArena::BeginTrial();
        if (g_status) g_status->setCaption(TownArena::GetStatus());
    }
    void OnTownSchedule(MyGUI::Widget*)
    {
        TownArena::SetScheduleEnabled(!TownArena::IsScheduleEnabled());
        if (g_status) g_status->setCaption(TownArena::GetStatus());
    }
    void OnStopTown(MyGUI::Widget*)
    {
        TownArena::Cancel();
        if (g_status) g_status->setCaption(TownArena::GetStatus());
    }
    void OnFightersTab(MyGUI::Widget*){g_tab=FightersTab;g_confirmFresh=0;Refresh();}
    void OnDiagnosticsTab(MyGUI::Widget*){g_tab=DiagnosticsTab;g_confirmFresh=0;Refresh();}
    void DiagnosticFeedback(bool ok,const char* success){
        if(g_status)g_status->setCaption(ok?success:TownDiagnosticRun::ValidationError().empty()?"Diagnostic action is unavailable in the current run state.":TownDiagnosticRun::ValidationError());
        if(ok)g_confirmFresh=0;Refresh();
    }
    void OnDiagnosticFresh150(MyGUI::Widget*){
        if(g_confirmFresh!=1){g_confirmFresh=1;if(g_status)g_status->setCaption("Click the red confirmation action again to reset all town ratings and records.");Refresh();return;}
        DiagnosticFeedback(TownDiagnosticRun::StartPreset(true),"Fresh 150-fight diagnostic run started.");
    }
    void OnDiagnosticCurrent150(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::StartPreset(false),"150-fight diagnostic run started with current ratings.");}
    void OnDiagnosticConfiguredFresh(MyGUI::Widget*){
        if(g_confirmFresh!=2){g_confirmFresh=2;if(g_status)g_status->setCaption("Click the confirmation action again to reset town records and use this configuration.");Refresh();return;}
        DiagnosticFeedback(TownDiagnosticRun::StartConfigured(true),"Configured fresh diagnostic run started.");
    }
    void OnDiagnosticConfiguredCurrent(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::StartConfigured(false),"Configured diagnostic run started with current ratings.");}
    void OnDiagnosticPause(MyGUI::Widget*){const bool paused=TownDiagnosticRun::Get().status==TownDiagnosticData::Paused;DiagnosticFeedback(paused?TownDiagnosticRun::Resume():TownDiagnosticRun::Pause(),paused?"Diagnostic run resumed.":"Diagnostic selection paused; ordinary matchmaking remains active.");}
    void OnDiagnosticEnd(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::End(),"Diagnostic run ended; ordinary matchmaking remains active.");}
    void OnDiagnosticTarget(MyGUI::Widget*){const int value=TownDiagnosticRun::Get().config.target;DiagnosticFeedback(TownDiagnosticRun::SetTarget(value<6?6:value<50?50:value<150?150:value<300?300:6),"Diagnostic target changed.");}
    void OnDiagnosticTargetMinus(MyGUI::Widget*){const int value=TownDiagnosticRun::Get().config.target;DiagnosticFeedback(TownDiagnosticRun::SetTarget(Max(1,value-10)),"Diagnostic target changed.");}
    void OnDiagnosticTargetPlus(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::SetTarget(TownDiagnosticRun::Get().config.target+10),"Diagnostic target changed.");}
    void OnDiagnosticStats(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleModel(0),"Stats-only model setting changed.");}
    void OnDiagnosticReduced(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleModel(1),"Reduced-MMR model setting changed.");}
    void OnDiagnosticFull(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleModel(2),"Full-MMR model setting changed.");}
    void OnDiagnosticInfluence(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::CycleReducedInfluence(),"Reduced-MMR influence changed.");}
    void OnDiagnosticDuel(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleCoverage(0),"1v1 coverage setting changed.");}
    void OnDiagnosticEqual(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleCoverage(1),"Equal-group coverage setting changed.");}
    void OnDiagnosticDifferenceOne(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleCoverage(2),"One-fighter-difference coverage setting changed.");}
    void OnDiagnosticDifferenceLarge(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleCoverage(3),"Large-difference coverage setting changed.");}
    void OnDiagnosticUnderdog(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::CycleUnderdogPercent(),"Underdog distribution changed.");}
    void OnDiagnosticBalance(MyGUI::Widget*){DiagnosticFeedback(TownDiagnosticRun::ToggleStyleTierBalance(),"Style/tier balancing changed.");}
    void OnPrevious(MyGUI::Widget*) { if (g_page > 0) --g_page; Refresh(); }
    void OnNext(MyGUI::Widget*) { ++g_page; Refresh(); }

    void OnWindowButtonPressed(MyGUI::Widget*, const std::string& name)
    {
        if ((name == "close" || name == "Close") && g_window)
            g_window->setVisible(false);
    }
}

namespace DebugMenu
{
    void AbandonWorldState()
    {
        g_characters.clear();
        g_selected = NULL;
        g_page = 0;
        if (g_window)
            g_window->setVisible(false);
        if (g_selectedName)
            g_selectedName->setCaption("Select a character");
        if (g_rating)
            g_rating->setCaption("Rating: --");
        if (g_marks)
            g_marks->setCaption("Arena Marks: --");
    }

    void Create()
    {
        if (g_window)
            return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        const char* resource = "Kenshi_WindowCX";
        try
        {
            g_window = gui->createWidgetReal<MyGUI::Window>(
                resource, 0.12f, 0.09f, 0.76f, 0.82f,
                MyGUI::Align::Center, "Window", "ProvingGroundsDebugWindow");
            g_window->setCaption("Proving Grounds - Debug Menu");
            g_window->setVisible(false);
            g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
            MyGUI::Widget* client = g_window->getClientWidget();
            if (!client) throw std::runtime_error("window has no client");
            resource = "Kenshi_Button1";
            g_tabs[0]=MakeButton(client,"PG_DebugFightersTab","Fighters",OnFightersTab);
            g_tabs[1]=MakeButton(client,"PG_DebugDiagnosticsTab","Diagnostics",OnDiagnosticsTab);
            resource = "Kenshi_WordWrapEmpty";
            g_heading = MakeLabel(client, "PG_DebugHeading", "Fighter Ratings & Marks");
            resource = "Kenshi_Button1";
            g_prev = MakeButton(client, "PG_DebugPrev", "< Prev", OnPrevious);
            g_next = MakeButton(client, "PG_DebugNext", "Next >", OnNext);
            resource = "Kenshi_TextboxStandardText";
            g_pageLabel = NativeUI::Label(client, MyGUI::IntCoord(0, 0, 100, 24),
                "PG_DebugPage", "Page 1 / 1");
            g_pageLabel->setTextAlign(MyGUI::Align::Center);
            g_pageLabel->setNeedMouseFocus(false);
            resource = "Kenshi_ScrollView";
            g_scroll = NativeUI::Scroll(client, MyGUI::IntCoord(0, 0, 300, 300), "PG_DebugScroll");
            resource = "Kenshi_WordWrapEmpty";
            g_selectedName = MakeLabel(g_scroll, "PG_DebugSelected", "Select a character");
            g_rating = MakeLabel(g_scroll, "PG_DebugRating", "Rating: --");
            g_marks = MakeLabel(g_scroll, "PG_DebugMarks", "Arena Marks: --");
            BindWheel(g_selectedName);
            BindWheel(g_rating);
            BindWheel(g_marks);
            for (int i = 0; i < kRowsPerPage; ++i)
            {
                char name[48], label[48];
                sprintf_s(name, "PG_DebugCharacter%d", i);
                sprintf_s(label, "PG_DebugCharacterText%d", i);
                resource = "Kenshi_Button1";
                g_rows[i] = MakeButton(g_scroll, name, "", OnSelect);
                resource = "Kenshi_PaintedWordWrapEmpty";
                g_rowText[i] = MakeLabel(g_rows[i], label, "", true);
            }
            const char* names[] = {"PG_DebugMinus100", "PG_DebugMinus10", "PG_DebugPlus10",
                "PG_DebugPlus100", "PG_DebugReset", "PG_DebugMarksMinus10", "PG_DebugMarksPlus10",
                "PG_DebugMarksPlus100", "PG_DebugMarksReset", "PG_DebugTownTrial",
                "PG_DebugStopTown", "PG_DebugTownSchedule"};
            const char* captions[] = {"Rating -100", "Rating -10", "Rating +10", "Rating +100",
                "Reset rating to 0", "Marks -10", "Marks +10", "Marks +100", "Marks 0",
                "Town 1v1", "Stop town fight", "Pause/resume ambient bouts"};
            void (*handlers[])(MyGUI::Widget*) = {OnMinus100, OnMinus10, OnPlus10, OnPlus100,
                OnReset, OnMarksMinus10, OnMarksPlus10, OnMarksPlus100, OnMarksReset,
                OnTownTrial, OnStopTown, OnTownSchedule};
            for (int i = 0; i < 12; ++i)
            {
                resource = "Kenshi_Button1";
                g_actions[i] = MakeButton(g_scroll, names[i], "", handlers[i]);
                resource = "Kenshi_PaintedWordWrapEmpty";
                const std::string labelName = std::string(names[i]) + "Text";
                g_actionText[i] = MakeLabel(g_actions[i], labelName.c_str(), captions[i], true);
            }
            resource="Kenshi_WordWrapEmpty";
            g_diagnosticSummary=MakeLabel(g_scroll,"PG_DebugDiagnosticSummary","Diagnostic mode is off.");
            BindWheel(g_diagnosticSummary);
            const char* diagnosticNames[kDiagnosticActions]={"PG_DiagFresh150","PG_DiagCurrent150","PG_DiagConfiguredFresh","PG_DiagConfiguredCurrent",
                "PG_DiagPause","PG_DiagEnd","PG_DiagTarget","PG_DiagTargetMinus","PG_DiagTargetPlus","PG_DiagStats","PG_DiagReduced","PG_DiagFull",
                "PG_DiagInfluence","PG_DiagDuel","PG_DiagEqual","PG_DiagDiffOne","PG_DiagDiffLarge","PG_DiagUnderdog","PG_DiagBalance"};
            void (*diagnosticHandlers[kDiagnosticActions])(MyGUI::Widget*)={OnDiagnosticFresh150,OnDiagnosticCurrent150,OnDiagnosticConfiguredFresh,OnDiagnosticConfiguredCurrent,
                OnDiagnosticPause,OnDiagnosticEnd,OnDiagnosticTarget,OnDiagnosticTargetMinus,OnDiagnosticTargetPlus,OnDiagnosticStats,OnDiagnosticReduced,OnDiagnosticFull,
                OnDiagnosticInfluence,OnDiagnosticDuel,OnDiagnosticEqual,OnDiagnosticDifferenceOne,OnDiagnosticDifferenceLarge,OnDiagnosticUnderdog,OnDiagnosticBalance};
            for(int i=0;i<kDiagnosticActions;++i){
                resource="Kenshi_Button1";g_diagnosticActions[i]=MakeButton(g_scroll,diagnosticNames[i],"",diagnosticHandlers[i]);
                resource="Kenshi_PaintedWordWrapEmpty";const std::string labelName=std::string(diagnosticNames[i])+"Text";
                g_diagnosticText[i]=MakeLabel(g_diagnosticActions[i],labelName.c_str(),"",true);
            }
            resource = "Kenshi_WordWrap";
            g_status = client->createWidget<MyGUI::EditBox>(resource,
                MyGUI::IntCoord(0, 0, 300, 100), MyGUI::Align::Default, "PG_DebugStatus");
            g_status->setCaption("F8 toggles this debug menu when enabled in Proving Grounds settings. When disabled, F8 does nothing.");
            g_layoutReady = true;
            client->eventChangeCoord += MyGUI::newDelegate(OnCoordChanged);
            Refresh();
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: native debug menu creation failed at ") +
                resource + ": " + error.what());
            DestroyWidgets();
            return;
        }
        catch (...)
        {
            PGLog::Error(std::string("Proving Grounds: native debug menu creation failed at ") + resource);
            DestroyWidgets();
            return;
        }
        PGLog::Debug("Proving Grounds: debug menu created");
    }

    void Toggle()
    {
        Create();
        if (!g_window)
            return;
        const bool show = !g_window->getVisible();
        g_window->setVisible(show);
        if (show)
        {
            try { Refresh(); }
            catch (...)
            {
                PGLog::Error("Proving Grounds: native debug menu refresh failed");
                DestroyWidgets();
            }
        }
    }

    void Close() { if (g_window) g_window->setVisible(false); }
    bool IsVisible() { return g_window && g_window->getVisible(); }
    void Tick(){
        if(!IsVisible() || g_tab!=DiagnosticsTab)return;
        const DWORD now=GetTickCount();if(g_lastDiagnosticRefresh && now-g_lastDiagnosticRefresh<1000)return;
        g_lastDiagnosticRefresh=now;try{Refresh();}catch(...){PGLog::Error("Proving Grounds: diagnostic debug refresh failed");DestroyWidgets();}
    }
}
