#include "ThermalRecovery.h"
#include <cassert>
#include <limits>

static void cool(ThermalRecovery& p, uint32_t start = 0) {
    for (uint32_t t = 0; t <= ThermalRecovery::COOL_MS; t += 5000) p.sample(start + t, true, 70);
}

int main() {
    ThermalRecovery p;
    const uint32_t epoch = 1800000000;
    for (unsigned i = 0; i < 3; ++i) {
        p.trip(epoch + i * 600, i * 600, 80, false, true);
        assert(p.state.count == i + 1 && p.state.notice);
        assert(!p.acknowledge(0)); // Direct API must not bypass cooling.
        cool(p);
        assert(!p.canResume(300000, false, true, true)); // Default OFF.
        assert(!p.canResume(300000, true, false, true));
        assert(!p.canResume(300000, true, true, false)); // Schedule.
        if (i < 2) {
            assert(p.canResume(300000, true, true, true));
            p.resume();
            assert(p.state.notice && p.state.resumed && !p.state.latched);
            assert(p.acknowledge(300000));
            assert(p.state.count == i + 1); // Warning dismissal preserves budget.
        } else {
            assert(p.state.manualBlocked && !p.canResume(300000, true, true, true));
        }
    }
    auto saved = p.state;
    ThermalRecovery rebooted;
    rebooted.restore(saved, true, 0);
    assert(rebooted.state.latched && rebooted.state.manualBlocked && rebooted.state.count == 3);
    assert(!rebooted.acknowledge(300000)); // No sensor samples since reboot.
    assert(rebooted.updateHistory(epoch + 100000, 1));
    assert(rebooted.state.count == 0 && rebooted.state.manualBlocked); // Sticky third trip.
    cool(rebooted);
    assert(rebooted.acknowledge(300000));
    assert(!rebooted.state.latched && !rebooted.state.manualBlocked && !rebooted.state.notice);

    // Sliding window crosses midnight, expires each event separately, and does
    // not silently erase history when NTP is absent or the clock moves back.
    p = ThermalRecovery{};
    p.trip(epoch, 0, 80, false, true);
    p.resume();
    p.trip(epoch + 3600, 3600, 80, false, true);
    assert(p.state.count == 2);
    p.updateHistory(epoch + 86400, 86400);
    assert(p.state.count == 1 && p.state.tripEpoch[0] == epoch + 3600);
    saved = p.state;
    rebooted.restore(saved, true, 0);
    rebooted.updateHistory(0, 500);
    assert(rebooted.state.count == 1);
    rebooted.updateHistory(epoch - 1000, 1000);
    assert(rebooted.state.count == 1);

    // Unsynced events have monotonic ages; across reboot, unknown ages are
    // conservatively retained for a full day instead of granting new retries.
    p = ThermalRecovery{};
    p.trip(0, 100, 80, false, true);
    assert(p.updateHistory(epoch, 700));
    assert(p.state.tripEpoch[0] == epoch - 600);
    p = ThermalRecovery{};
    p.trip(0, 0, 80, false, true);
    rebooted.restore(p.state, true, 0);
    rebooted.updateHistory(0, 86399);
    assert(rebooted.state.count == 1);
    rebooted.updateHistory(0, 86400);
    assert(rebooted.state.count == 0);

    // A hot, invalid or missing sample resets all five continuous minutes.
    p = ThermalRecovery{};
    p.trip(epoch, 0, 80, false, true);
    cool(p);
    assert(p.cooled(300000));
    assert(!p.cooled(315001));
    p.sample(305000, true, 70.1f);
    assert(!p.cooled(305000) && p.remainingSec(305000) == 300);
    p.sample(310000, true, 70);
    p.sample(315000, false, 0);
    assert(!p.cooled(315000));
    p.sample(320000, true, std::numeric_limits<float>::quiet_NaN());
    assert(!p.cooled(320000));
    p.sample(325000, true, 70);
    p.sample(345001, true, 70); // Long unobserved interval restarts the timer.
    assert(p.remainingSec(345001) == 300);
    cool(p, UINT32_MAX - 100000); // millis wrap during continuous cooling.
    assert(p.cooled(uint32_t(UINT32_MAX - 100000 + 300000)));

    p.trip(epoch, 0, 80, true, true);
    cool(p);
    assert(p.state.emergency && p.state.manualBlocked);
    assert(!p.canResume(300000, true, true, true));
    p = ThermalRecovery{};
    p.trip(epoch, 0, 80, false, false);
    cool(p);
    assert(!p.canResume(300000, true, true, true)); // Never start a stopped server.
    p.state.magic = 0;
    rebooted.restore(p.state, true, 0, 30);
    assert(rebooted.state.latched && rebooted.state.manualBlocked && rebooted.state.notice);
    assert(rebooted.state.coolLimitC == 20); // Migration respects a nondefault limit.
    // An OTA from THR1 changes the margin once and retains every safety block.
    saved = ThermalRecoveryState{};
    saved.magic = 0x54485231;
    saved.coolLimitC = 65;
    saved.latched = saved.notice = saved.manualBlocked = saved.emergency = 1;
    saved.count = 3;
    saved.tripEpoch[0] = epoch;
    rebooted.restore(saved, true, 0);
    assert(rebooted.state.coolLimitC == 70 && rebooted.state.count == 3);
    assert(rebooted.state.emergency && rebooted.state.manualBlocked && rebooted.state.latched);
    assert(rebooted.state.tripEpoch[0] == epoch);
    saved = rebooted.state;
    rebooted.restore(saved, true, 0);
    assert(rebooted.state.coolLimitC == 70); // No repeated relaxation on reboot.
    rebooted.restore(saved, true, 0, 75);
    assert(rebooted.state.coolLimitC == 65); // Respect a lower configured limit.
    for (uint32_t t = 0; t <= ThermalRecovery::COOL_MS; t += 5000) rebooted.sample(t, true, 65);
    assert(rebooted.acknowledge(300000)); // Migrated emergency/third-trip latch unlocks after cooling.
    assert(!rebooted.state.latched && rebooted.state.count == 0);
}
