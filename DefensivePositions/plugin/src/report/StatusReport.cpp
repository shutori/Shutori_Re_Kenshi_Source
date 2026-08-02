#include "report/StatusReport.h"

#include <sstream>

namespace
{
    const char* unitResultLabel(UnitResult::Type result)
    {
        switch (result)
        {
        case UnitResult::Ok:
            return "OK";
        case UnitResult::NoAssignment:
            return "no assignment";
        case UnitResult::NotControllable:
            return "not controllable";
        case UnitResult::Dead:
            return "dead";
        case UnitResult::Ko:
            return "KO";
        case UnitResult::Imprisoned:
            return "imprisoned";
        case UnitResult::Unavailable:
            return "unavailable";
        case UnitResult::TurretMissing:
            return "turret missing";
        case UnitResult::TurretRemountFailed:
            return "turret remount failed";
        case UnitResult::Timeout:
            return "timeout";
        }
        return "unavailable";
    }
}

std::string formatSaveConfirm(size_t positions, size_t turrets)
{
    std::ostringstream out;
    out << "Saved " << positions << " positions (" << turrets << " turrets)";
    return out.str();
}

std::string formatDiscardConfirm(size_t positions)
{
    std::ostringstream out;
    out << "Discarded " << positions << " positions";
    return out.str();
}

std::string formatStatusReport(const std::vector<UnitStatus>& rows)
{
    std::ostringstream out;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i > 0)
        {
            out << '\n';
        }
        out << rows[i].displayName << ": " << unitResultLabel(rows[i].result);
        if (!rows[i].detail.empty())
        {
            out << " (" << rows[i].detail << ")";
        }
    }
    return out.str();
}
