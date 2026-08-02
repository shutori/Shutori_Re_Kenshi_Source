#include "StatsOverviewUI.h"
#include "GameProtectionAdapter.h"
#include "BodySilhouette.h"
#include "UiText.h"

#include <Debug.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/Globals.h>
#include <kenshi/InputHandler.h>
#pragma warning(pop)

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_KeyCode.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Window.h>

#include <ois/OISKeyboard.h>

#include <cstdio>
#include <map>
#include <string>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    enum ViewMode
    {
        ModeOverview = 0,
        ModePart = 1
    };

    MyGUI::Window* g_window = NULL;
    MyGUI::Widget* g_content = NULL;
    MyGUI::Widget* g_bodyHit = NULL;
    MyGUI::Button* g_backButton = NULL;
    Character* g_character = NULL;
    CharacterProtectionView g_cachedView;
    std::map<std::string, MyGUI::ImageBox*> g_fillLayers;
    std::map<std::string, MyGUI::ImageBox*> g_highlightLayers;
    std::string g_hoveredLayer;
    std::string g_selectedLayer;
    ViewMode g_mode = ModeOverview;
    int g_partIndex = -1;
    bool g_frameHooked = false;
    bool g_escWasDown = false;

    const float kBodyX = 0.02f;
    const float kBodyY = 0.04f;
    const float kBodyW = 0.40f;
    const float kBodyH = 0.92f;

    const float kContentX = 0.44f;
    const float kContentY = 0.02f;
    const float kContentW = 0.54f;
    const float kContentH = 0.82f;

    const char* kLayerIds[] = {
        "head", "chest", "stomach", "left_arm", "right_arm", "left_leg", "right_leg"
    };

    void ShowOverviewMode();
    void ShowPartMode(size_t partIndex);
    void RefreshCaption();
    void RefreshPartFills();
    int FindPartIndexForLayer(const std::string& layerId);

    void SetPartFillTexture(const std::string& layerId, const char* band)
    {
        std::map<std::string, MyGUI::ImageBox*>::iterator it = g_fillLayers.find(layerId);
        if (it == g_fillLayers.end() || !it->second)
            return;

        char tex[64];
        sprintf_s(tex, "body_%s_%s.png", layerId.c_str(), band);
        it->second->setImageTexture(tex);
        it->second->setVisible(true);
    }

    void RefreshPartFills()
    {
        for (size_t i = 0; i < sizeof(kLayerIds) / sizeof(kLayerIds[0]); ++i)
        {
            std::string id = kLayerIds[i];
            int partIndex = FindPartIndexForLayer(id);
            if (partIndex < 0)
            {
                // Missing from anatomy — grey only.
                SetPartFillTexture(id, "grey");
                continue;
            }

            float mit = estimateMitigationPercent(
                g_cachedView.parts[(size_t)partIndex].weighted.cut,
                g_cachedView.parts[(size_t)partIndex].weighted.blunt,
                g_cachedView.parts[(size_t)partIndex].weighted.pierce);
            SetPartFillTexture(id, MitigationBandName(mit));
        }
    }

    void HideAllHighlights()
    {
        for (std::map<std::string, MyGUI::ImageBox*>::iterator it = g_highlightLayers.begin();
             it != g_highlightLayers.end();
             ++it)
        {
            if (it->second)
                it->second->setVisible(false);
        }
        g_hoveredLayer.clear();
    }

    void ShowHighlight(const std::string& layerId)
    {
        // In part mode keep the selected part lit even when hover moves away.
        std::string active = layerId;
        if (active.empty() && g_mode == ModePart)
            active = g_selectedLayer;

        if (active == g_hoveredLayer)
            return;

        for (std::map<std::string, MyGUI::ImageBox*>::iterator it = g_highlightLayers.begin();
             it != g_highlightLayers.end();
             ++it)
        {
            if (it->second)
                it->second->setVisible(it->first == active);
        }
        g_hoveredLayer = active;
    }

    int FindPartIndexForLayer(const std::string& layerId)
    {
        if (layerId.empty())
            return -1;
        for (size_t i = 0; i < g_cachedView.parts.size(); ++i)
        {
            if (g_cachedView.parts[i].layerId == layerId)
                return (int)i;
        }
        return -1;
    }

    bool MouseToBodyUV(MyGUI::Widget* hitWidget, float& u, float& v)
    {
        if (!hitWidget)
            return false;

        MyGUI::InputManager* input = MyGUI::InputManager::getInstancePtr();
        if (!input)
            return false;

        const MyGUI::IntPoint mouse = input->getMousePosition();
        const MyGUI::IntCoord abs = hitWidget->getAbsoluteCoord();
        if (abs.width <= 0 || abs.height <= 0)
            return false;

        u = (float)(mouse.left - abs.left) / (float)abs.width;
        v = (float)(mouse.top - abs.top) / (float)abs.height;
        return true;
    }

    void OnBodyMouseMove(MyGUI::Widget* sender, int /*left*/, int /*top*/)
    {
        float u = 0.f;
        float v = 0.f;
        if (!MouseToBodyUV(sender, u, v))
        {
            ShowHighlight("");
            return;
        }

        std::string layer = BodySilhouette::SampleLayerAt(u, v);
        ShowHighlight(layer);
    }

    void OnBodyClick(MyGUI::Widget* sender)
    {
        float u = 0.f;
        float v = 0.f;
        if (!MouseToBodyUV(sender, u, v) || !g_character)
            return;

        std::string layer = BodySilhouette::SampleLayerAt(u, v);
        int partIndex = FindPartIndexForLayer(layer);
        if (partIndex < 0)
            return;

        ShowPartMode((size_t)partIndex);
    }

    void OnBodyLostFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*newFocus*/)
    {
        ShowHighlight("");
    }

    void OnBackClicked(MyGUI::Widget* /*sender*/)
    {
        ShowOverviewMode();
    }

    void OnWindowClosed(MyGUI::Window* /*widget*/, const std::string& name)
    {
        // Kenshi_WindowCX chrome uses MyGUI's "Close" button name.
        if (name == "close" || name == "Close")
            StatsOverviewUI::close();
    }

    void OnKeyPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        if (key == MyGUI::KeyCode::Escape)
            StatsOverviewUI::handleEscape();
    }

    void SuppressGameEscape()
    {
        if (!key)
            return;
        key->escape = false;
        key->escape_msg = false;
    }

    bool IsEscapeDown()
    {
        if (key && key->keyboard)
            return key->keyboard->isKeyDown(OIS::KC_ESCAPE);
        if (key)
            return key->escape || key->escape_msg;
        return false;
    }

    void OnFrameStart(float /*dt*/)
    {
        if (!StatsOverviewUI::isOpen())
        {
            g_escWasDown = false;
            return;
        }

        // Do not steal key focus every frame — inventory/other UI must stay usable.
        const bool escDown = IsEscapeDown();
        if (escDown)
            SuppressGameEscape();

        if (escDown && !g_escWasDown)
            StatsOverviewUI::handleEscape();

        g_escWasDown = escDown;
    }

    void EnsureFrameHook()
    {
        if (g_frameHooked)
            return;
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui)
            return;
        gui->eventFrameStart += MyGUI::newDelegate(OnFrameStart);
        g_frameHooked = true;
    }

    void BuildSilhouette(MyGUI::Widget* parent)
    {
        g_fillLayers.clear();
        g_highlightLayers.clear();
        g_bodyHit = NULL;
        if (!BodySilhouette::EnsureResources())
            return;
        BodySilhouette::EnsureHitmap();

        // Colour fills per part (red/orange/green/grey) — no flat grey base.
        for (size_t i = 0; i < sizeof(kLayerIds) / sizeof(kLayerIds[0]); ++i)
        {
            std::string id = kLayerIds[i];
            char widgetName[64];
            sprintf_s(widgetName, "BodyFill_%s", id.c_str());

            MyGUI::ImageBox* fill = parent->createWidgetReal<MyGUI::ImageBox>(
                "ImageBox", kBodyX, kBodyY, kBodyW, kBodyH, MyGUI::Align::Default, widgetName);
            if (!fill)
                continue;
            fill->setImageTexture(std::string("body_") + id + "_grey.png");
            fill->setNeedMouseFocus(false);
            fill->setVisible(true);
            g_fillLayers[id] = fill;
        }

        // Hover / selection highlight on top of fills.
        for (size_t i = 0; i < sizeof(kLayerIds) / sizeof(kLayerIds[0]); ++i)
        {
            std::string id = kLayerIds[i];
            std::string tex = std::string("body_") + id + "_hl.png";
            char widgetName[64];
            sprintf_s(widgetName, "BodyHl_%s", id.c_str());

            MyGUI::ImageBox* hl = parent->createWidgetReal<MyGUI::ImageBox>(
                "ImageBox", kBodyX, kBodyY, kBodyW, kBodyH, MyGUI::Align::Default, widgetName);
            if (!hl)
                continue;
            hl->setImageTexture(tex);
            hl->setVisible(false);
            hl->setNeedMouseFocus(false);
            g_highlightLayers[id] = hl;
        }

        g_bodyHit = parent->createWidgetReal<MyGUI::Widget>(
            "PanelEmpty", kBodyX, kBodyY, kBodyW, kBodyH, MyGUI::Align::Default, "BodyHit");
        if (g_bodyHit)
        {
            g_bodyHit->setNeedMouseFocus(true);
            g_bodyHit->eventMouseMove += MyGUI::newDelegate(OnBodyMouseMove);
            g_bodyHit->eventMouseButtonClick += MyGUI::newDelegate(OnBodyClick);
            g_bodyHit->eventMouseLostFocus += MyGUI::newDelegate(OnBodyLostFocus);
        }

        RefreshPartFills();
    }

    void ClearContent()
    {
        if (!g_window)
            return;

        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client)
            return;

        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (g_content && gui)
        {
            gui->destroyWidget(g_content);
            g_content = NULL;
        }

        g_content = client->createWidgetReal<MyGUI::Widget>(
            "PanelEmpty", kContentX, kContentY, kContentW, kContentH,
            MyGUI::Align::Default, "CIContent");
        if (g_content)
            g_content->setNeedMouseFocus(false);
    }

    void RefreshCaption()
    {
        if (!g_window)
            return;

        if (g_mode == ModePart && g_partIndex >= 0 && (size_t)g_partIndex < g_cachedView.parts.size())
        {
            std::string caption = g_cachedView.characterName + " - " +
                g_cachedView.parts[(size_t)g_partIndex].partName;
            g_window->setCaption(caption);
        }
        else
        {
            g_window->setCaption(g_cachedView.characterName);
        }
    }

    void EnsureBackButton(bool visible)
    {
        if (!g_window)
            return;
        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client)
            return;

        if (!g_backButton)
        {
            g_backButton = client->createWidgetReal<MyGUI::Button>(
                "Kenshi_Button1", 0.56f, 0.88f, 0.30f, 0.09f,
                MyGUI::Align::Default, "CIBack");
            if (g_backButton)
            {
                g_backButton->setCaption("Back");
                g_backButton->eventMouseButtonClick += MyGUI::newDelegate(OnBackClicked);
            }
        }

        if (g_backButton)
            g_backButton->setVisible(visible);
    }

    void ShowOverviewMode()
    {
        g_mode = ModeOverview;
        g_partIndex = -1;
        g_selectedLayer.clear();
        HideAllHighlights();
        ClearContent();
        RefreshCaption();
        EnsureBackButton(false);
        RefreshPartFills();

        if (!g_content)
            return;

        // Content root is already at right-panel coords; children use local 0..1.
        const float x = 0.0f;
        const float w = 1.0f;
        const float lineH = 0.072f;
        const float gap = 0.012f;
        float y = 0.02f;

        AddMitigationBanner(
            g_content, "OvMitigation", x, y, lineH, gap,
            g_cachedView.totalsApprox.cut,
            g_cachedView.totalsApprox.blunt,
            g_cachedView.totalsApprox.pierce,
            w);

        AddResistLine(
            g_content, "OvResists", x, y, lineH, gap + 0.008f,
            g_cachedView.totalsApprox.cut,
            g_cachedView.totalsApprox.blunt,
            g_cachedView.totalsApprox.pierce,
            w);

        AddHeaderLine(g_content, "OvHint", x, y, w, lineH, "Click a body part");
        y += lineH + 0.024f;

        if (g_cachedView.parts.empty())
        {
            AddTextLine(g_content, "EmptyParts", x, y, w, lineH, "No body parts.");
            return;
        }

        const int columns = 2;
        const float colW = 0.50f;
        const float startY = y;
        for (size_t i = 0; i < g_cachedView.parts.size(); ++i)
        {
            int col = (int)(i % columns);
            int row = (int)(i / columns);
            float px = x + col * colW;
            float by = startY + row * (lineH + gap);
            if (by > 0.95f)
                break;

            float mit = estimateMitigationPercent(
                g_cachedView.parts[i].weighted.cut,
                g_cachedView.parts[i].weighted.blunt,
                g_cachedView.parts[i].weighted.pierce);

            char widgetName[64];
            sprintf_s(widgetName, "PartLine%u", (unsigned)i);
            AddPartSummaryLine(
                g_content, widgetName, px, by, colW - 0.02f, lineH,
                g_cachedView.parts[i].partName, mit);
        }
    }

    void ShowPartMode(size_t partIndex)
    {
        if (partIndex >= g_cachedView.parts.size())
            return;

        // Refresh live gear in case the player swapped armour while open.
        if (g_character)
            g_cachedView = buildFromCharacter(g_character);
        if (partIndex >= g_cachedView.parts.size())
            return;

        const PartProtectionView& part = g_cachedView.parts[partIndex];
        g_mode = ModePart;
        g_partIndex = (int)partIndex;
        g_selectedLayer = part.layerId;
        ShowHighlight(g_selectedLayer);
        ClearContent();
        RefreshCaption();
        EnsureBackButton(true);
        RefreshPartFills();

        if (!g_content)
            return;

        const float x = 0.0f;
        const float w = 1.0f;
        const float lineH = 0.072f;
        const float gap = 0.014f;
        float y = 0.02f;

        AddMitigationBanner(
            g_content, "PtMitigation", x, y, lineH, gap,
            part.weighted.cut,
            part.weighted.blunt,
            part.weighted.pierce,
            w);

        AddResistLine(
            g_content, "PtResists", x, y, lineH, gap + 0.01f,
            part.weighted.cut,
            part.weighted.blunt,
            part.weighted.pierce,
            w);

        AddHeaderLine(g_content, "GearHead", x, y, w, lineH, "Armour on this part");
        y += lineH + 0.018f;

        if (part.pieces.empty())
        {
            AddColoredLine(
                g_content, "NoGear", x, y, w, lineH,
                "None - unprotected", MutedColour());
            return;
        }

        for (size_t i = 0; i < part.pieces.size() && y < 0.95f; ++i)
        {
            const ArmourPieceInput& piece = part.pieces[i];
            AddPieceRowWithIcon(
                g_content, (unsigned)i, x, y, w, lineH, gap + 0.01f,
                piece.name,
                piece.iconTexture,
                piece.coverage,
                piece.cutResistance,
                piece.bluntResistance,
                piece.pierceResistance);
        }
    }
}

namespace StatsOverviewUI
{
    void close()
    {
        SuppressGameEscape();
        g_escWasDown = false;

        g_fillLayers.clear();
        g_highlightLayers.clear();
        g_bodyHit = NULL;
        g_content = NULL;
        g_backButton = NULL;
        g_hoveredLayer.clear();
        g_selectedLayer.clear();
        g_mode = ModeOverview;
        g_partIndex = -1;

        if (g_window)
        {
            MyGUI::Gui* guiMgr = MyGUI::Gui::getInstancePtr();
            if (guiMgr)
                guiMgr->destroyWidget(g_window);
            g_window = NULL;
        }
        g_character = NULL;
    }

    bool isOpen()
    {
        return g_window != NULL;
    }

    Character* currentCharacter()
    {
        return g_character;
    }

    void handleEscape()
    {
        close();
    }

    void open(Character* character)
    {
        close();

        if (!character)
        {
            ErrorLog("Character Inspector: no character for overview");
            return;
        }

        g_cachedView = buildFromCharacter(character);
        g_character = character;

        MyGUI::Gui* guiMgr = MyGUI::Gui::getInstancePtr();
        if (!guiMgr)
            return;

        EnsureFrameHook();

        // Non-modal so inventory / portraits / gear stay interactive.
        g_window = guiMgr->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX", 0.30f, 0.20f, 0.34f, 0.48f,
            MyGUI::Align::Default, "Window", "CharacterInspectorOverview");
        g_window->setCaption(g_cachedView.characterName);
        g_window->eventWindowButtonPressed += MyGUI::newDelegate(OnWindowClosed);
        g_window->setNeedKeyFocus(true);
        g_window->eventKeyButtonPressed += MyGUI::newDelegate(OnKeyPressed);

        MyGUI::Widget* client = g_window->getClientWidget();
        if (!client)
            return;

        BuildSilhouette(client);
        ShowOverviewMode();
    }
}

