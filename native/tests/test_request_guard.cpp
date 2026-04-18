#include <QtTest/QtTest>

#include "app/request_guard.h"

using namespace ais::app;

class RequestGuardTests final : public QObject {
    Q_OBJECT

private slots:
    void entersAndLeavesBusyStates();
    void rejectsSecondEntryUntilReleased();
    void rejectsIdleEntry();
    void ignoresMismatchedLeaveState();
};

void RequestGuardTests::entersAndLeavesBusyStates() {
    RequestGuard guard;

    QVERIFY(guard.tryEnter(BusyState::Capturing));
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Capturing));

    guard.leave(BusyState::Capturing);

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void RequestGuardTests::rejectsSecondEntryUntilReleased() {
    RequestGuard guard;

    QVERIFY(guard.tryEnter(BusyState::RequestInFlight));
    QVERIFY(!guard.tryEnter(BusyState::Capturing));

    guard.leave(BusyState::RequestInFlight);

    QVERIFY(guard.tryEnter(BusyState::TestingProvider));
}

void RequestGuardTests::rejectsIdleEntry() {
    RequestGuard guard;

    QVERIFY(!guard.tryEnter(BusyState::Idle));
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}

void RequestGuardTests::ignoresMismatchedLeaveState() {
    RequestGuard guard;

    QVERIFY(guard.tryEnter(BusyState::Capturing));

    guard.leave(BusyState::RequestInFlight);

    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Capturing));
}

QTEST_APPLESS_MAIN(RequestGuardTests)

#include "test_request_guard.moc"
