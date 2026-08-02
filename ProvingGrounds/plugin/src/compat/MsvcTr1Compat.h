#pragma once

// Force-included ahead of everything else so KenshiLib / Ogre / Boost 1.60
// compile cleanly with the VS2010 (v100) toolset required for KenshiLib plugins.

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

#include <boost/tuple/tuple.hpp>

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
