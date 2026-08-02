#include "BodySilhouette.h"
#include "PartLayerResolve.h"

#include <Debug.h>

#include <ogre/OgreColourValue.h>
#include <ogre/OgreImage.h>
#include <ogre/OgreResourceGroupManager.h>

#include <Windows.h>

#include <string>

namespace
{
    bool g_resourcesReady = false;
    bool g_hitmapReady = false;
    Ogre::Image g_hitmap;

    const Ogre::String kGroup = "CharacterInspectorGui";

    std::string GetModDirectory()
    {
        HMODULE module = NULL;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)&GetModDirectory,
                &module) ||
            !module)
        {
            return std::string();
        }

        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(module, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return std::string();

        std::string full(path, path + len);
        size_t slash = full.find_last_of("\\/");
        if (slash == std::string::npos)
            return std::string();
        return full.substr(0, slash);
    }

    std::string LayerFromHitId(int id)
    {
        switch (id)
        {
        case 1: return "head";
        case 2: return "chest";
        case 3: return "stomach";
        case 4: return "left_arm";
        case 5: return "right_arm";
        case 6: return "left_leg";
        case 7: return "right_leg";
        default: return std::string();
        }
    }
}

namespace BodySilhouette
{
    bool EnsureResources()
    {
        if (g_resourcesReady)
            return true;

        std::string modDir = GetModDirectory();
        if (modDir.empty())
        {
            ErrorLog("Character Inspector: could not resolve mod directory");
            return false;
        }

        std::string silDir = modDir + "\\gui\\silhouette";
        try
        {
            Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
            if (!rgm.resourceGroupExists(kGroup))
                rgm.createResourceGroup(kGroup);
            rgm.addResourceLocation(silDir, "FileSystem", kGroup, false);
            rgm.initialiseResourceGroup(kGroup);
            g_resourcesReady = true;
            DebugLog(("Character Inspector: silhouette resources from " + silDir).c_str());
            return true;
        }
        catch (...)
        {
            ErrorLog("Character Inspector: failed to register silhouette resources");
            return false;
        }
    }

    bool EnsureHitmap()
    {
        if (g_hitmapReady)
            return true;
        if (!EnsureResources())
            return false;

        try
        {
            g_hitmap.load("body_hitmap.png", kGroup);
            g_hitmapReady = true;
            DebugLog("Character Inspector: hitmap loaded");
            return true;
        }
        catch (...)
        {
            ErrorLog("Character Inspector: failed to load body_hitmap.png");
            return false;
        }
    }

    std::string LayerIdForPartName(const std::string& partName)
    {
        return PartLayer::FromName(partName);
    }

    std::string SampleLayerAt(float u, float v)
    {
        if (!EnsureHitmap())
            return std::string();
        if (u < 0.f || v < 0.f || u > 1.f || v > 1.f)
            return std::string();

        size_t w = g_hitmap.getWidth();
        size_t h = g_hitmap.getHeight();
        if (w == 0 || h == 0)
            return std::string();

        size_t x = (size_t)(u * (float)(w - 1));
        size_t y = (size_t)(v * (float)(h - 1));
        Ogre::ColourValue c = g_hitmap.getColourAt(x, y, 0);
        if (c.a < 0.05f)
            return std::string();

        // R channel stores part id 1..7 (as 1/255 .. 7/255 if float, or raw depending on format).
        // Prefer reconstructing from 0..255.
        int id = (int)(c.r * 255.0f + 0.5f);
        if (id < 1 || id > 7)
        {
            // Some codecs keep small ints oddly; try rounding differently
            id = (int)(c.r + 0.5f);
        }
        return LayerFromHitId(id);
    }
}
