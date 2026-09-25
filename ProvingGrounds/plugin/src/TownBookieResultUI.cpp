#include "TownBookieResultUI.h"
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
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#include <cstdio>
#include <exception>

namespace
{
    const int kMembers = 4;
    struct Card
    {
        MyGUI::Widget* root;
        MyGUI::ImageBox* portrait;
        MyGUI::TextBox* placeholder;
        MyGUI::EditBox* name;
        MyGUI::Widget* border[4];
        bool hasPortrait;
        Card() : root(NULL), portrait(NULL), placeholder(NULL), name(NULL), hasPortrait(false)
        { for (int i = 0; i < 4; ++i) border[i] = NULL; }
    };
    struct Column
    {
        MyGUI::Widget* root;
        MyGUI::EditBox* heading;
        MyGUI::Widget* rail;
        Card cards[kMembers];
        int count, side;
        Column() : root(NULL), heading(NULL), rail(NULL), count(0), side(-1) {}
    };

    MyGUI::Window* window = NULL;
    MyGUI::TextBox* measure = NULL;
    MyGUI::EditBox *title = NULL, *context = NULL, *payout = NULL;
    MyGUI::Widget* titleRails[2] = {};
    MyGUI::Button* closeButton = NULL;
    Column columns[2];
    TownBookieResultUI::Actions callbacks;
    MyGUI::IntSize screenSize;
    int tone = 0, winningSide = -1;

    int Max(int a, int b) { return a > b ? a : b; }
    int Min(int a, int b) { return a < b ? a : b; }
    int Body() { return Max(1, Max(measure->getFontHeight(), measure->getTextSize().height)); }
    MyGUI::Colour Gold() { return MyGUI::Colour(.86f, .66f, .22f); }
    MyGUI::Colour Winner() { return MyGUI::Colour(.92f, .72f, .24f); }
    MyGUI::Colour Loser() { return MyGUI::Colour(.58f, .24f, .20f); }
    MyGUI::Colour Neutral() { return MyGUI::Colour(.64f, .67f, .70f); }
    MyGUI::Colour OutcomeColour()
    {
        return tone == 1 ? MyGUI::Colour(.28f, .82f, .38f) :
            tone == 2 ? MyGUI::Colour(.90f, .27f, .20f) : Gold();
    }
    MyGUI::Widget* Accent(MyGUI::Widget* parent, const std::string& name)
    {
        try
        {
            MyGUI::Widget* result = parent->createWidget<MyGUI::Widget>("WhiteSkin",
                MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, name);
            result->setNeedMouseFocus(false);
            return result;
        }
        catch (...) { return NULL; }
    }
    void SetAccent(MyGUI::Widget* widget, const MyGUI::IntCoord& coord,
        const MyGUI::Colour& colour, float alpha = 1.0f)
    {
        if (!widget) return;
        widget->setCoord(coord); widget->setColour(colour); widget->setAlpha(alpha);
        widget->setVisible(true);
    }
    int Wrap(MyGUI::EditBox* label, int x, int y, int width)
    {
        label->setCoord(x, y, Max(1, width), Body());
        const int inset = Max(0, label->getHeight() - label->getTextRegion().height);
        const int height = Max(Body(), label->getTextSize().height + inset);
        label->setSize(Max(1, width), height);
        return height;
    }
    void CloseClicked(MyGUI::Widget*) { if (callbacks.close) callbacks.close(); }
    void WindowButton(MyGUI::Widget*, const std::string& name)
    {
        if ((name == "close" || name == "Close") && callbacks.close) callbacks.close();
    }
    void CreateCard(Column& column, int displayColumn, int slot)
    {
        char prefix[64]; sprintf_s(prefix, "PG_BookieResult_%d_%d", displayColumn, slot);
        Card& card = column.cards[slot];
        const MyGUI::IntCoord initial(0, 0, 100, 24);
        card.root = column.root->createWidget<MyGUI::Widget>("PanelEmpty", initial,
            MyGUI::Align::Default, prefix);
        card.portrait = card.root->createWidget<MyGUI::ImageBox>("ImageBox", initial,
            MyGUI::Align::Default, std::string(prefix) + "Portrait");
        card.placeholder = NativeUI::Label(card.root, initial,
            std::string(prefix) + "Placeholder", "?");
        card.placeholder->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
        card.name = NativeUI::WrappedLabel(card.root, initial,
            std::string(prefix) + "Name", "");
        card.name->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
        for (int edge = 0; edge < 4; ++edge)
            card.border[edge] = Accent(card.root,
                std::string(prefix) + "Border" + static_cast<char>('0' + edge));
    }
    bool EnsureWindow()
    {
        if (window) return true;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return false;
        try
        {
            window = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX",
                .18f, .15f, .64f, .70f, MyGUI::Align::Center,
                "Window", "PG_BookieWagerResults");
            window->setCaption("Scratch Bookie - Wager Results");
            window->setVisible(false);
            window->eventWindowButtonPressed += MyGUI::newDelegate(WindowButton);
            MyGUI::Widget* client = window->getClientWidget();
            const MyGUI::IntCoord initial(0, 0, 100, 24);
            measure = NativeUI::Label(client, initial, "PG_BookieResultMeasure", "WAGER RESULT");
            measure->setVisible(false);
            title = NativeUI::WrappedLabel(client, initial, "PG_BookieResultTitle", "");
            context = NativeUI::WrappedLabel(client, initial, "PG_BookieResultContext", "");
            payout = NativeUI::WrappedLabel(client, initial, "PG_BookieResultPayout", "");
            title->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
            context->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
            payout->setTextAlign(MyGUI::Align::Center | MyGUI::Align::Top);
            titleRails[0] = Accent(client, "PG_BookieResultTitleRailLeft");
            titleRails[1] = Accent(client, "PG_BookieResultTitleRailRight");
            closeButton = NativeUI::Button(client, initial, "PG_BookieResultClose", "Close");
            closeButton->eventMouseButtonClick += MyGUI::newDelegate(CloseClicked);
            for (int column = 0; column < 2; ++column)
            {
                char name[64]; sprintf_s(name, "PG_BookieResultColumn%d", column);
                columns[column].root = client->createWidget<MyGUI::Widget>("PanelEmpty", initial,
                    MyGUI::Align::Default, name);
                columns[column].heading = NativeUI::WrappedLabel(columns[column].root, initial,
                    std::string(name) + "Heading", "");
                columns[column].heading->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
                columns[column].rail = Accent(columns[column].root, std::string(name) + "Rail");
                for (int slot = 0; slot < kMembers; ++slot) CreateCard(columns[column], column, slot);
            }
            return true;
        }
        catch (const std::exception& error)
        {
            PGLog::Error(std::string("Proving Grounds: wager results UI creation failed: ") + error.what());
        }
        catch (...) { PGLog::Error("Proving Grounds: wager results UI creation failed"); }
        TownBookieResultUI::Destroy();
        return false;
    }
    void BindCard(Card& card, const TownBookieUI::FighterView& fighter)
    {
        card.name->setCaption(fighter.name + "\n" + fighter.style);
        Character* actor = fighter.actor.getCharacter();
        PortraitManager* manager = PortraitManager::getInstance();
        card.hasPortrait = actor && actor->isValid() && manager;
        if (card.hasPortrait) manager->setImageWidget(actor->getHandle(), card.portrait, true);
        card.portrait->setVisible(card.hasPortrait);
        card.placeholder->setVisible(!card.hasPortrait);
    }
    int LayoutCard(Card& card, int x, int y, int width, int portrait,
        const MyGUI::Colour& colour, int gap, int edge)
    {
        const int portraitX = (width - portrait) / 2;
        card.portrait->setCoord(portraitX, gap, portrait, portrait);
        card.placeholder->setCoord(portraitX, gap, portrait, portrait);
        const int nameTop = gap + portrait + gap;
        const int nameHeight = Wrap(card.name, gap, nameTop, Max(1, width - 2 * gap));
        const int height = nameTop + nameHeight + gap;
        card.root->setCoord(x, y, width, height);
        const MyGUI::IntCoord borders[4] = {
            MyGUI::IntCoord(portraitX, gap, edge, portrait),
            MyGUI::IntCoord(portraitX, gap, portrait, edge),
            MyGUI::IntCoord(portraitX, gap + portrait - edge, portrait, edge),
            MyGUI::IntCoord(portraitX + portrait - edge, gap, edge, portrait)
        };
        for (int i = 0; i < 4; ++i) SetAccent(card.border[i], borders[i], colour);
        return height;
    }
    void Layout()
    {
        MyGUI::RenderManager* render = MyGUI::RenderManager::getInstancePtr();
        if (!render) return;
        const MyGUI::IntSize screen = render->getViewSize();
        if (screen.width <= 0 || screen.height <= 0) return;
        screenSize = screen;
        const int body = Body();
        const int gap = NativeUI::Spacing(measure);
        const int edge = Max(1, gap / 3);
        int maximumCount = Max(columns[0].count, columns[1].count);
        const int cardColumns = maximumCount > 1 ? 2 : 1;
        const int cardRows = Max(1, (maximumCount + cardColumns - 1) / cardColumns);
        const int naturalCardWidth = body * 12;
        const int naturalColumnWidth = cardColumns * naturalCardWidth + (cardColumns + 1) * gap;
        int contentWidth = 2 * naturalColumnWidth + 3 * gap;
        contentWidth = Max(contentWidth, title->getTextSize().width + 10 * gap);
        contentWidth = Max(contentWidth, payout->getTextSize().width + 8 * gap);
        const MyGUI::IntSize outerBefore = window->getSize();
        const MyGUI::IntSize clientBefore = window->getClientWidget()->getSize();
        const int chromeWidth = Max(0, outerBefore.width - clientBefore.width);
        const int chromeHeight = Max(0, outerBefore.height - clientBefore.height);
        const int outerWidth = Min(screen.width * 90 / 100,
            Max(520, contentWidth + chromeWidth));
        const int clientWidth = Max(1, outerWidth - chromeWidth);
        const int innerWidth = Max(1, clientWidth - 3 * gap);
        const int columnWidth = Max(1, (innerWidth - gap) / 2);
        const int cardWidth = Max(1, (columnWidth - (cardColumns + 1) * gap) / cardColumns);
        const int portrait = Min(body * 9, Max(body * 5, cardWidth - 2 * gap));
        const int cardHeight = portrait + 4 * gap + 2 * body;
        const int headingHeight = 2 * body;
        const int teamsHeight = headingHeight + 2 * gap + cardRows * cardHeight + (cardRows - 1) * gap;
        const int titleHeight = 2 * body;
        const int contextHeight = 2 * body;
        const int payoutHeight = 5 * body;
        const int closeHeight = NativeUI::RowHeight(closeButton, 0);
        const int desiredClientHeight = titleHeight + contextHeight + teamsHeight + payoutHeight + closeHeight + 10 * gap;
        const int outerHeight = Min(screen.height * 90 / 100,
            Max(360, desiredClientHeight + chromeHeight));
        window->setCoord((screen.width - outerWidth) / 2, (screen.height - outerHeight) / 2,
            outerWidth, outerHeight);

        const MyGUI::IntSize size = window->getClientWidget()->getSize();
        const int width = Max(1, size.width - 2 * gap);
        int y = gap;
        const int actualTitleHeight = Wrap(title, 4 * gap, y, Max(1, size.width - 8 * gap));
        const int railWidth = Max(0, (size.width - title->getTextSize().width - 12 * gap) / 2);
        const int railTop = y + actualTitleHeight / 2;
        SetAccent(titleRails[0], MyGUI::IntCoord(gap, railTop, railWidth, edge), OutcomeColour(), .85f);
        SetAccent(titleRails[1], MyGUI::IntCoord(size.width - gap - railWidth, railTop, railWidth, edge), OutcomeColour(), .85f);
        y += actualTitleHeight + gap;
        y += Wrap(context, gap, y, width) + 2 * gap;
        const int actualColumnWidth = Max(1, (width - gap) / 2);
        int columnBottom = y;
        for (int column = 0; column < 2; ++column)
        {
            Column& item = columns[column];
            const int left = gap + column * (actualColumnWidth + gap);
            item.root->setCoord(left, y, actualColumnWidth, teamsHeight);
            const MyGUI::Colour colour = winningSide < 0 ? Neutral() :
                (column == 0 ? Winner() : Loser());
            item.heading->setTextColour(colour);
            const int headingH = Wrap(item.heading, gap, gap, Max(1, actualColumnWidth - 2 * gap));
            SetAccent(item.rail, MyGUI::IntCoord(gap, gap + headingH + gap / 2,
                Max(1, actualColumnWidth - 2 * gap), edge), colour, .8f);
            const int gridTop = gap + headingH + 2 * gap;
            const int localCardWidth = Max(1,
                (actualColumnWidth - (cardColumns + 1) * gap) / cardColumns);
            const int localPortrait = Min(body * 9, Max(body * 5, localCardWidth - 2 * gap));
            int tallestBottom = gridTop;
            for (int slot = 0; slot < item.count; ++slot)
            {
                Card& card = item.cards[slot];
                const int cardX = gap + (slot % cardColumns) * (localCardWidth + gap);
                const int cardY = gridTop + (slot / cardColumns) * (cardHeight + gap);
                const int height = LayoutCard(card, cardX, cardY, localCardWidth,
                    localPortrait, colour, gap, edge);
                tallestBottom = Max(tallestBottom, cardY + height);
            }
            item.root->setSize(actualColumnWidth, tallestBottom + gap);
            columnBottom = Max(columnBottom, y + tallestBottom + gap);
        }
        y = columnBottom + gap;
        payout->setTextColour(OutcomeColour());
        y += Wrap(payout, gap, y, width) + 2 * gap;
        const int buttonWidth = Min(width, Max(body * 12, closeButton->getTextSize().width + 6 * gap));
        closeButton->setCoord((size.width - buttonWidth) / 2, y, buttonWidth, closeHeight);
    }
}

namespace TownBookieResultUI
{
    bool IsVisible() { return window && window->getVisible(); }
    void Close() { if (window) window->setVisible(false); }
    void Destroy()
    {
        if (window && MyGUI::Gui::getInstancePtr()) MyGUI::Gui::getInstance().destroyWidget(window);
        window = NULL; measure = NULL; title = context = payout = NULL; closeButton = NULL;
        titleRails[0] = titleRails[1] = NULL;
        columns[0] = Column(); columns[1] = Column(); callbacks = Actions();
        screenSize = MyGUI::IntSize(); tone = 0; winningSide = -1;
    }
    bool Show(const View& view, const Actions& actions)
    {
        if (!EnsureWindow()) return false;
        callbacks = actions; tone = view.tone; winningSide = view.winningSide;
        title->setCaption(view.title); context->setCaption(view.context); payout->setCaption(view.payout);
        title->setTextColour(OutcomeColour());
        const int displaySides[2] = { view.winningSide >= 0 ? view.winningSide : 0,
            view.winningSide >= 0 ? 1 - view.winningSide : 1 };
        for (int column = 0; column < 2; ++column)
        {
            Column& item = columns[column];
            item.side = displaySides[column];
            item.count = Min(kMembers, static_cast<int>(view.fighters[item.side].size()));
            std::string heading = view.winningSide < 0 ?
                std::string(item.side == 0 ? "TEAM A" : "TEAM B") :
                std::string(column == 0 ? "VICTOR  -  TEAM " : "DEFEATED  -  TEAM ") +
                    (item.side == 0 ? "A" : "B");
            if (item.side == view.selectedSide) heading += "  [YOUR PICK]";
            item.heading->setCaption(heading);
            for (int slot = 0; slot < kMembers; ++slot)
            {
                item.cards[slot].root->setVisible(slot < item.count);
                if (slot < item.count) BindCard(item.cards[slot], view.fighters[item.side][slot]);
            }
        }
        Layout();
        window->setVisible(true);
        window->upLayerItem();
        return IsVisible();
    }
}
