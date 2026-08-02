#include <boost/test/unit_test.hpp>
#include "report/StatusReport.h"

BOOST_AUTO_TEST_CASE(format_save_confirm)
{
    BOOST_CHECK_EQUAL(formatSaveConfirm(3, 1), "Saved 3 positions (1 turrets)");
}

BOOST_AUTO_TEST_CASE(format_discard_confirm)
{
    BOOST_CHECK_EQUAL(formatDiscardConfirm(2), "Discarded 2 positions");
    BOOST_CHECK_EQUAL(formatDiscardConfirm(0), "Discarded 0 positions");
}

BOOST_AUTO_TEST_CASE(format_report_includes_every_unit)
{
    std::vector<UnitStatus> rows;
    UnitStatus a; a.characterId = 1; a.displayName = "Rex"; a.result = UnitResult::Ok;
    UnitStatus b; b.characterId = 2; b.displayName = "Bob"; b.result = UnitResult::NoAssignment;
    rows.push_back(a); rows.push_back(b);
    const std::string text = formatStatusReport(rows);
    BOOST_CHECK(text.find("Rex") != std::string::npos);
    BOOST_CHECK(text.find("OK") != std::string::npos);
    BOOST_CHECK(text.find("Bob") != std::string::npos);
    BOOST_CHECK(text.find("no assignment") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(format_report_includes_detail_when_present)
{
    std::vector<UnitStatus> rows;
    UnitStatus a;
    a.characterId = 1;
    a.displayName = "Rex";
    a.result = UnitResult::TurretRemountFailed;
    a.detail = "occupied 1/1; couldIOperate=no";
    rows.push_back(a);
    const std::string text = formatStatusReport(rows);
    BOOST_CHECK(text.find("Rex: turret remount failed (occupied 1/1; couldIOperate=no)") != std::string::npos);
}
