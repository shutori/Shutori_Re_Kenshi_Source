#include "monitor/ArrivalMonitor.h"

#include <cmath>

namespace
{
    const UnitSnap* findSnap(const GameSnapshot& snap, uint64_t characterId)
    {
        for (size_t i = 0; i < snap.units.size(); ++i)
        {
            if (snap.units[i].characterId == characterId)
            {
                return &snap.units[i];
            }
        }
        return nullptr;
    }

    float distance(const Vec3& a, const Vec3& b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

void ArrivalMonitor::begin(const std::vector<TrackedUnit>& units)
{
    m_units = units;
}

bool ArrivalMonitor::isActive() const
{
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        if (!m_units[i].done)
        {
            return true;
        }
    }
    return false;
}

void ArrivalMonitor::tick(float dtSec, const GameSnapshot& snap)
{
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        TrackedUnit& unit = m_units[i];
        if (unit.done)
        {
            continue;
        }

        unit.elapsedSec += dtSec;

        const UnitSnap* found = findSnap(snap, unit.characterId);
        if (!found)
        {
            unit.result = UnitResult::Unavailable;
            unit.done = true;
            continue;
        }
        if (!found->controllable)
        {
            unit.result = UnitResult::NotControllable;
            unit.done = true;
            continue;
        }
        if (!found->alive)
        {
            unit.result = UnitResult::Dead;
            unit.done = true;
            continue;
        }
        if (found->ko)
        {
            unit.result = UnitResult::Ko;
            unit.done = true;
            continue;
        }
        if (found->imprisoned)
        {
            unit.result = UnitResult::Imprisoned;
            unit.done = true;
            continue;
        }

        if (unit.assignment.hasTurret)
        {
            if (!found->turretExists)
            {
                unit.result = UnitResult::TurretMissing;
                unit.done = true;
            }
            else if (found->mounted && found->mountedTurret == unit.assignment.turret)
            {
                unit.result = UnitResult::Ok;
                unit.done = true;
            }
            else if (unit.elapsedSec >= kDefaultTimeoutSec)
            {
                unit.result = UnitResult::Timeout;
                unit.done = true;
            }
            // else: still waiting for mount, keep ticking.
        }
        else
        {
            if (distance(found->position, unit.assignment.position) <= kPositionTolerance)
            {
                unit.result = UnitResult::Ok;
                unit.done = true;
            }
            else if (unit.elapsedSec >= kDefaultTimeoutSec)
            {
                unit.result = UnitResult::Timeout;
                unit.done = true;
            }
            // else: still walking to position, keep ticking.
        }
    }
}

void ArrivalMonitor::markTerminal(uint64_t characterId, UnitResult::Type result, const std::string& detail)
{
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        TrackedUnit& unit = m_units[i];
        if (unit.characterId == characterId && !unit.done)
        {
            unit.result = result;
            unit.detail = detail;
            unit.done = true;
            return;
        }
    }
}

void ArrivalMonitor::markRemountFailed(uint64_t characterId, const std::string& detail)
{
    markTerminal(characterId, UnitResult::TurretRemountFailed, detail);
}

void ArrivalMonitor::markUnavailable(uint64_t characterId)
{
    markTerminal(characterId, UnitResult::Unavailable, std::string());
}

void ArrivalMonitor::setDetail(uint64_t characterId, const std::string& detail)
{
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        if (m_units[i].characterId == characterId)
        {
            m_units[i].detail = detail;
            return;
        }
    }
}

void ArrivalMonitor::reset()
{
    m_units.clear();
}

std::vector<UnitStatus> ArrivalMonitor::results() const
{
    std::vector<UnitStatus> out;
    out.reserve(m_units.size());
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        UnitStatus status;
        status.characterId = m_units[i].characterId;
        status.displayName = m_units[i].displayName;
        status.result = m_units[i].result;
        status.detail = m_units[i].detail;
        out.push_back(status);
    }
    return out;
}
