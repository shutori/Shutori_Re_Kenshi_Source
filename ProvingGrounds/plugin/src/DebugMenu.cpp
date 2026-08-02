#include "DebugMenu.h"

#include "LeaderboardStore.h"
#include "SquadUtil.h"

#include <Debug.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#pragma warning(pop)

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Colour.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    const int kRowsPerPage = 8;

    MyGUI::Window* g_window = NULL;
    MyGUI::TextBox* g_selectedName = NULL;
    MyGUI::TextBox* g_rating = NULL;
    MyGUI::TextBox* g_status = NULL;
    MyGUI::TextBox* g_pageLabel = NULL;
    MyGUI::Button* g_rows[kRowsPerPage] = {};
    MyGUI::Button* g_prev = NULL;
    MyGUI::Button* g_next = NULL;
    std::vector<Character*> g_characters;
    Character* g_selected = NULL;
    int g_page = 0;

    const MyGUI::Colour kBackground(26.f / 255.f, 22.f / 255.f, 18.f / 255.f, 1.f);
    const MyGUI::Colour kText(232.f / 255.f, 220.f / 255.f, 200.f / 255.f, 1.f);
    const MyGUI::Colour kMuted(154.f / 255.f, 139.f / 255.f, 114.f / 255.f, 1.f);
    const MyGUI::Colour kBrass(196.f / 255.f, 165.f / 255.f, 116.f / 255.f, 1.f);

    void Tint(MyGUI::Widget* widget, const MyGUI::Colour& colour)
    {
        if (widget)
            widget->setColour(colour);
    }

    MyGUI::TextBox* MakeLabel(MyGUI::Widget* parent, float x, float y, float w, float h,
        const char* name, const char* caption)
    {
        MyGUI::TextBox* label = parent->createWidgetReal<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText", x, y, w, h, MyGUI::Align::Default, name);
        label->setCaption(caption);
        label->setFontHeight(16);
        label->setTextColour(kText);
        Tint(label, kText);
        return label;
    }

    MyGUI::Button* MakeButton(MyGUI::Widget* parent, float x, float y, float w, float h,
        const char* name, const char* caption, void (*handler)(MyGUI::Widget*))
    {
        MyGUI::Button* button = parent->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", x, y, w, h, MyGUI::Align::Default, name);
        button->setCaption(caption);
        button->setFontHeight(16);
        button->setTextColour(kText);
        button->setTextShadow(true);
        button->setTextShadowColour(MyGUI::Colour::Black);
        button->eventMouseButtonClick += MyGUI::newDelegate(handler);
        return button;
    }

    void CollectCharacters()
    {
        SquadUtil::CollectPlayerSquad(g_characters);
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
            char caption[160];
            char indexText[24];
            sprintf_s(caption, "%s    Rating: %d", character->getName().c_str(),
                static_cast<int>(LeaderboardStore::GetRating(character) + 0.5f));
            sprintf_s(indexText, "%d", index);
            button->setCaption(caption);
            button->setUserString("index", indexText);
            button->setVisible(true);
            Tint(button, character == g_selected ? kBrass : kBackground);
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
            return;
        }

        g_selectedName->setCaption(g_selected->getName());
        char ratingText[64];
        sprintf_s(ratingText, "Rating: %d",
            static_cast<int>(LeaderboardStore::GetRating(g_selected) + 0.5f));
        g_rating->setCaption(ratingText);
    }

    void OnSelect(MyGUI::Widget* sender)
    {
        const int index = sender ? atoi(sender->getUserString("index").c_str()) : -1;
        if (index >= 0 && index < static_cast<int>(g_characters.size()))
        {
            g_selected = g_characters[index];
            g_status->setCaption("Changes are written to this save immediately.");
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
            ? "Rating saved."
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
            ? "Rating reset and saved."
            : "Could not save rating (load a game first).");
        Refresh();
    }

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
    void Create()
    {
        if (g_window)
            return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;

        g_window = gui->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX", 0.28f, 0.18f, 0.44f, 0.58f,
            MyGUI::Align::Center, "Window", "ProvingGroundsDebugWindow");
        g_window->setCaption("Proving Grounds - Debug Menu");
        g_window->setVisible(false);
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowButtonPressed);
        Tint(g_window, kBackground);

        MyGUI::Widget* client = g_window->getClientWidget();
        MyGUI::TextBox* heading = MakeLabel(client, 0.04f, 0.03f, 0.55f, 0.07f,
            "PG_DebugHeading", "Character Ratings");
        heading->setFontHeight(20);
        Tint(heading, kBrass);
        g_selectedName = MakeLabel(client, 0.62f, 0.03f, 0.34f, 0.06f,
            "PG_DebugSelected", "Select a character");
        g_selectedName->setTextAlign(MyGUI::Align::Right);
        g_rating = MakeLabel(client, 0.62f, 0.09f, 0.34f, 0.05f,
            "PG_DebugRating", "Rating: --");
        g_rating->setTextAlign(MyGUI::Align::Right);

        for (int i = 0; i < kRowsPerPage; ++i)
        {
            char name[48];
            sprintf_s(name, "PG_DebugCharacter%d", i);
            g_rows[i] = MakeButton(client, 0.04f, 0.15f + i * 0.075f, 0.56f, 0.062f,
                name, "", OnSelect);
            g_rows[i]->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        }

        MakeButton(client, 0.64f, 0.20f, 0.13f, 0.09f, "PG_DebugMinus100", "-100", OnMinus100);
        MakeButton(client, 0.80f, 0.20f, 0.13f, 0.09f, "PG_DebugMinus10", "-10", OnMinus10);
        MakeButton(client, 0.64f, 0.32f, 0.13f, 0.09f, "PG_DebugPlus10", "+10", OnPlus10);
        MakeButton(client, 0.80f, 0.32f, 0.13f, 0.09f, "PG_DebugPlus100", "+100", OnPlus100);
        MakeButton(client, 0.64f, 0.45f, 0.29f, 0.08f, "PG_DebugReset", "Reset to 0", OnReset);
        g_prev = MakeButton(client, 0.04f, 0.78f, 0.13f, 0.07f, "PG_DebugPrev", "< Prev", OnPrevious);
        g_pageLabel = MakeLabel(client, 0.19f, 0.79f, 0.26f, 0.06f, "PG_DebugPage", "Page 1 / 1");
        g_pageLabel->setTextAlign(MyGUI::Align::Center);
        g_next = MakeButton(client, 0.47f, 0.78f, 0.13f, 0.07f, "PG_DebugNext", "Next >", OnNext);
        g_status = MakeLabel(client, 0.04f, 0.88f, 0.89f, 0.07f,
            "PG_DebugStatus", "F8 closes this menu. Changes save immediately.");
        g_status->setFontHeight(14);
        Tint(g_status, kMuted);
        Refresh();
        DebugLog("Proving Grounds: debug menu created");
    }

    void Toggle()
    {
        Create();
        if (!g_window)
            return;
        const bool show = !g_window->getVisible();
        g_window->setVisible(show);
        if (show)
            Refresh();
    }

    void Close() { if (g_window) g_window->setVisible(false); }
    bool IsVisible() { return g_window && g_window->getVisible(); }
}
