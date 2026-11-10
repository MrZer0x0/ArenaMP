#include "../../apps/openmw/mwgui/notificationhistory.hpp"
#include <cassert>
#include <iostream>
int main()
{
    MWGui::NotificationHistory h;
    assert(h.accept("Repair succeeded"));
    assert(!h.accept("#AAAAAARepair succeeded\n"));
    assert(!h.accept(" Repair   succeeded "));
    assert(h.accept("Repair failed"));
    assert(h.accept("Навык повышен до 21"));
    assert(h.accept("Навык повышен до 22"));
    assert(!h.accept("#FFFFFFНавык повышен до 21"));
    // Continuous repeats never extend the display/queue; no new entry is accepted.
    for (int i=0; i<100; ++i) { h.advance(1.f); assert(!h.accept("Repair succeeded")); }
    h.advance(5.1f);
    assert(h.accept("Repair succeeded"));
    // Long visible/queued messages remain deduplicated beyond the grace period.
    for (int i=0; i<30; ++i) { h.advance(1.f); h.keepVisible("Repair succeeded"); }
    assert(!h.accept("Repair succeeded"));
    h.clear();
    assert(h.accept("Repair succeeded"));
    assert(!h.accept("#FFFFFF \n"));
    assert(MWGui::NotificationHistory::key("##123456") == "#123456");
    // A burst is bounded and does not prevent subsequent new messages.
    for (int i=0; i<3000; ++i) assert(h.accept("burst " + std::to_string(i)));
    h.advance(6.f);
    assert(h.accept("burst 2999"));
    std::cout << "PASS: notification duplicates, UTF-8, colours, queue lifetime, cooldown, reset, burst\n";
}
