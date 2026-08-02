#include "store/LayoutStore.h"

#include <cstring>

namespace
{
    // CF02: full ObjectRef for turrets (type + container), not just u64 index|serial.
    const char kMagic[4] = { 'C', 'F', '0', '2' };

    template <typename T>
    void appendPod(std::string& out, const T& value)
    {
        out.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    // Reads sizeof(T) bytes from blob starting at offset into value, advancing
    // offset. Returns false (leaving offset unchanged) if not enough bytes remain.
    template <typename T>
    bool readPod(const std::string& blob, size_t& offset, T& value)
    {
        if (blob.size() - offset < sizeof(T))
        {
            return false;
        }
        std::memcpy(&value, blob.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }
}

void LayoutStore::upsert(const DefenseAssignment& a)
{
    m_assignments[a.characterId] = a;
}

bool LayoutStore::tryGet(uint64_t characterId, DefenseAssignment& out) const
{
    std::map<uint64_t, DefenseAssignment>::const_iterator it = m_assignments.find(characterId);
    if (it == m_assignments.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

bool LayoutStore::erase(uint64_t characterId)
{
    return m_assignments.erase(characterId) > 0;
}

size_t LayoutStore::size() const
{
    return m_assignments.size();
}

void LayoutStore::clear()
{
    m_assignments.clear();
}

std::vector<DefenseAssignment> LayoutStore::all() const
{
    std::vector<DefenseAssignment> result;
    result.reserve(m_assignments.size());
    for (std::map<uint64_t, DefenseAssignment>::const_iterator it = m_assignments.begin();
         it != m_assignments.end(); ++it)
    {
        result.push_back(it->second);
    }
    return result;
}

std::string LayoutStore::serialize() const
{
    std::string blob;
    blob.append(kMagic, sizeof(kMagic));

    const uint32_t count = static_cast<uint32_t>(m_assignments.size());
    appendPod(blob, count);

    for (std::map<uint64_t, DefenseAssignment>::const_iterator it = m_assignments.begin();
         it != m_assignments.end(); ++it)
    {
        const DefenseAssignment& a = it->second;
        appendPod(blob, a.characterId);
        appendPod(blob, a.position.x);
        appendPod(blob, a.position.y);
        appendPod(blob, a.position.z);
        appendPod(blob, a.facingYaw);
        const uint8_t hasTurret = a.hasTurret ? 1 : 0;
        appendPod(blob, hasTurret);
        appendPod(blob, a.turret);
    }

    return blob;
}

bool LayoutStore::deserialize(const std::string& blob)
{
    if (blob.size() < sizeof(kMagic) || std::memcmp(blob.data(), kMagic, sizeof(kMagic)) != 0)
    {
        return false;
    }

    size_t offset = sizeof(kMagic);

    uint32_t count = 0;
    if (!readPod(blob, offset, count))
    {
        return false;
    }

    std::map<uint64_t, DefenseAssignment> parsed;
    for (uint32_t i = 0; i < count; ++i)
    {
        DefenseAssignment a = {};
        uint8_t hasTurret = 0;
        if (!readPod(blob, offset, a.characterId) ||
            !readPod(blob, offset, a.position.x) ||
            !readPod(blob, offset, a.position.y) ||
            !readPod(blob, offset, a.position.z) ||
            !readPod(blob, offset, a.facingYaw) ||
            !readPod(blob, offset, hasTurret) ||
            !readPod(blob, offset, a.turret))
        {
            return false;
        }
        a.hasTurret = (hasTurret != 0);
        parsed[a.characterId] = a;
    }

    m_assignments.swap(parsed);
    return true;
}
