#pragma once

// Force-included ahead of everything else (see DefensivePositions.vcxproj
// <ForcedIncludeFiles>) so KenshiLib / Ogre / Boost 1.60 compile cleanly.
//
// Primary build toolset is VS2010 (v100). The guards below also keep the
// headers usable if you temporarily compile with a newer MSVC for IDE
// IntelliSense / experiments — but the shipping DLL must be linked with v100
// against the vc100 Boost libs from KenshiLib_Examples_deps.

// ---------------------------------------------------------------------------
// Windows / Ogre / Boost coexistence
// ---------------------------------------------------------------------------
// 1) NOMINMAX: Windows.h otherwise defines min/max macros that break
//    OgreFastArray.h's `std::max(...)` into `std::( ...` (C2589).
// 2) Include Windows.h BEFORE any Boost header, and set BOOST_USE_WINDOWS_H
//    so Boost 1.60 does not declare its own CreateMutexA/CreateEventA/...
//    prototypes that conflict with the modern Windows SDK (C2116/C2733).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#ifndef BOOST_USE_WINDOWS_H
#define BOOST_USE_WINDOWS_H
#endif

// ---------------------------------------------------------------------------
// Boost.tuple (PhysicsActual.h expects it transitively)
// ---------------------------------------------------------------------------
#include <boost/tuple/tuple.hpp>

// ---------------------------------------------------------------------------
// std::tr1 resurfacing for OgrePrerequisites.h / OgreString.h
// ---------------------------------------------------------------------------
// Only needed on MSVC 2017+ where std::tr1 was removed. VS2010 (v100) still
// has the real TR1 namespace, so this is a no-op there.

#if defined(_MSC_VER) && _MSC_VER >= 1910 && !defined(_HAS_TR1_NAMESPACE)

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace std
{
    namespace tr1
    {
        using ::std::hash;
        using ::std::unordered_map;
        using ::std::unordered_multimap;
        using ::std::unordered_set;
        using ::std::unordered_multiset;
    }
}

#endif
