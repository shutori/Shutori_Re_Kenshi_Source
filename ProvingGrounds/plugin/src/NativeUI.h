#pragma once

#include <string>
#include <mygui/MyGUI_Types.h>

namespace MyGUI { class Widget; class TextBox; class Button; class ScrollView; class EditBox; }

// Construction only. The caller owns widgets, callbacks and game state.
// These helpers deliberately leave the loaded skin's typography/palette intact.
namespace NativeUI
{
    MyGUI::TextBox* Label(MyGUI::Widget* parent, const MyGUI::IntCoord& bounds,
        const std::string& name, const std::string& caption);
    MyGUI::Button* Button(MyGUI::Widget* parent, const MyGUI::IntCoord& bounds,
        const std::string& name, const std::string& caption);
    MyGUI::ScrollView* Scroll(MyGUI::Widget* parent,
        const MyGUI::IntCoord& bounds, const std::string& name);
    // Read-only native word wrapping for names/status that must not be clipped.
    MyGUI::EditBox* WrappedLabel(MyGUI::Widget* parent,
        const MyGUI::IntCoord& bounds, const std::string& name, const std::string& caption);
    int Spacing(MyGUI::TextBox* sample);
    int RowHeight(MyGUI::TextBox* sample, int imageHeight);
}
