#pragma once
#include <string>
#include <map>
#include <set>

namespace FighterIdentityPolicy
{
    typedef unsigned long long Lifetime;
    class Registry
    {
        struct Entry
        {
            Lifetime lifetime;
            std::string id;
            Entry() : lifetime(0) {}
        };
        typedef std::map<const void*, Entry> Entries;
        Entries entries;
        // Sticky within a world: disappearance of a duplicate is not an
        // authorized decision about which fighter owns its saved career.
        std::set<std::string> conflicts;
        Lifetime sequence;
    public:
        Registry() : sequence(0) {}
        Lifetime Begin(const void* object)
        {
            if (!object || sequence == ~Lifetime(0)) return 0;
            Entry entry;
            entry.lifetime = ++sequence;
            entries[object] = entry;
            return entry.lifetime;
        }
        Lifetime Current(const void* object) const
        {
            Entries::const_iterator it = entries.find(object);
            return it == entries.end() ? 0 : it->second.lifetime;
        }
        void Load(const void* object, bool hasSaveState, const std::string& id, bool& conflict)
        {
            conflict = false;
            // Native loadFromSerialise(NULL) is a no-op, not a new lifetime.
            // A real legacy save lacking our field still creates a binding.
            if (!hasSaveState) return;
            const Lifetime lifetime = Begin(object);
            if (!id.empty()) Bind(object, lifetime, id, conflict);
        }
        bool Bind(const void* object, Lifetime lifetime, const std::string& id, bool& conflict)
        {
            conflict = false;
            Entries::iterator it = entries.find(object);
            if (it == entries.end() || it->second.lifetime != lifetime || id.empty()) return false;
            if (!it->second.id.empty() && it->second.id != id) return false;
            it->second.id = id;
            for (Entries::const_iterator other = entries.begin(); other != entries.end(); ++other)
                if (other->first != object && other->second.id == id)
                    conflict = conflicts.insert(id).second || conflict;
            return conflicts.find(id) == conflicts.end();
        }
        bool Find(const void* object, Lifetime lifetime, std::string& id) const
        {
            if (!SavedId(object, lifetime, id)) return false;
            if (conflicts.find(id) == conflicts.end()) return true;
            id.clear();
            return false;
        }
        // Serialization only. A conflicted token still belongs in native
        // metadata, but must never authorize a ledger read or mutation.
        bool SavedId(const void* object, Lifetime lifetime, std::string& id) const
        {
            id.clear();
            Entries::const_iterator it = entries.find(object);
            if (it == entries.end() || it->second.lifetime != lifetime || it->second.id.empty()) return false;
            id = it->second.id;
            return true;
        }
        void End(const void* object, Lifetime lifetime)
        {
            Entries::iterator it = entries.find(object);
            if (it != entries.end() && it->second.lifetime == lifetime) entries.erase(it);
        }
        void Clear() { entries.clear(); conflicts.clear(); }
    };
}
