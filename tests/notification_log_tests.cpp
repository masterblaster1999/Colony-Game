#include <doctest/doctest.h>

#include "game/util/NotificationLog.h"

using namespace colony::game::util;

TEST_CASE("NotificationLog: bounded log drops oldest entries")
{
    NotificationLog log;
    log.setMaxLogEntries(3);

    log.push("A", NotifySeverity::Info, 1.0);
    log.push("B", NotifySeverity::Info, 2.0);
    log.push("C", NotifySeverity::Info, 3.0);
    log.push("D", NotifySeverity::Info, 4.0);
    log.push("E", NotifySeverity::Info, 5.0);

    REQUIRE(log.log().size() == 3);
    CHECK(log.log()[0].text == "C");
    CHECK(log.log()[1].text == "D");
    CHECK(log.log()[2].text == "E");
}

TEST_CASE("NotificationLog: toasts expire via tick")
{
    NotificationLog log;
    log.setMaxToasts(4);

    log.push("Hello", NotifySeverity::Info, 0.0, /*toastTtlSeconds=*/1.0f, NotifyTarget::None(), /*pushToast=*/true);
    REQUIRE(log.toasts().size() == 1);

    log.tick(0.5f);
    CHECK(log.toasts().size() == 1);
    CHECK(log.toasts()[0].ttlSeconds == doctest::Approx(0.5f));

    log.tick(0.6f);
    CHECK(log.toasts().empty());
}

TEST_CASE("NotificationLog: pushToast=false logs without creating a toast")
{
    NotificationLog log;
    log.push("Silent", NotifySeverity::Warning, 0.0, /*toastTtlSeconds=*/5.0f, NotifyTarget::None(), /*pushToast=*/false);

    REQUIRE(log.log().size() == 1);
    CHECK(log.toasts().empty());
}
