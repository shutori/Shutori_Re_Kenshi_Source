#include "NativeUI.h"
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_TextBox.h>

namespace NativeUI
{
    MyGUI::TextBox* Label(MyGUI::Widget* parent, const MyGUI::IntCoord& bounds,
        const std::string& name, const std::string& caption)
    {
        MyGUI::TextBox* result = parent->createWidget<MyGUI::TextBox>(
            "Kenshi_TextboxStandardText", bounds, MyGUI::Align::Default, name);
        result->setCaption(caption);
        return result;
    }

    MyGUI::Button* Button(MyGUI::Widget* parent, const MyGUI::IntCoord& bounds,
        const std::string& name, const std::string& caption)
    {
        MyGUI::Button* result = parent->createWidget<MyGUI::Button>(
            "Kenshi_Button1", bounds, MyGUI::Align::Default, name);
        result->setCaption(caption);
        return result;
    }

    MyGUI::ScrollView* Scroll(MyGUI::Widget* parent,
        const MyGUI::IntCoord& bounds, const std::string& name)
    {
        // Both vanilla and Dark UI define this template with native scrollbars.
        MyGUI::ScrollView* result = parent->createWidget<MyGUI::ScrollView>(
            "Kenshi_ScrollView", bounds, MyGUI::Align::Default, name);
        result->setVisibleHScroll(false);
        result->setVisibleVScroll(true);
        result->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        return result;
    }

    MyGUI::EditBox* WrappedLabel(MyGUI::Widget* parent,
        const MyGUI::IntCoord& bounds, const std::string& name, const std::string& caption)
    {
        // This native static EditBox uses Kenshi_TextboxStandardText as its client.
        // A plain TextBox has no word-wrap API in Kenshi's bundled MyGUI.
        MyGUI::EditBox* result = parent->createWidget<MyGUI::EditBox>(
            "Kenshi_WordWrapEmpty", bounds, MyGUI::Align::Default, name);
        result->setCaption(caption);
        return result;
    }

    int Spacing(MyGUI::TextBox* sample)
    {
        const int height = sample->getFontHeight();
        return height / 2 > 4 ? height / 2 : 4;
    }

    int RowHeight(MyGUI::TextBox* sample, int imageHeight)
    {
        int height = sample->getTextSize().height;
        if (height < sample->getFontHeight()) height = sample->getFontHeight();
        if (height < imageHeight) height = imageHeight;
        return height + 2 * Spacing(sample);
    }
}
