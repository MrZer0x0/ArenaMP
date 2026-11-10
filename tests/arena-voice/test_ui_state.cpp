#include "apps/openmw/mwmp/VoiceUiState.hpp"
#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    using mwmp::VoiceUiState;
    VoiceUiState ui;
    // A touch before focus/after pause must never open capture.
    ui.press(true);
    assert(!ui.pressed());
    ui.foreground(true);
    ui.press(true);
    assert(ui.pressed());
    ui.publish(VoiceUiState::Enabled | VoiceUiState::Transmitting, 0.5f, "Игрок");
    assert((ui.state() & VoiceUiState::Transmitting) != 0);
    assert((ui.state() >> 8) == 127);
    assert(ui.speakers() == "Игрок");
    ui.foreground(false);
    assert(!ui.pressed() && ui.state() == 0 && ui.speakers().empty());
    ui.foreground(true);
    assert(!ui.pressed() && ui.state() == 0); // Return needs a fresh touch and engine snapshot.
    ui.press(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    assert(!ui.pressed()); // Lost ACTION_UP / stopped UI renewal fails closed.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    assert(ui.state() == 0 && ui.speakers().empty()); // No stale "speaking" if engine stalls.
    // JNI/UI reads can overlap the native update thread without accessing players.
    std::thread writer([&]() {
        for (int i = 0; i < 10000; ++i) ui.publish(VoiceUiState::Ready, 1.f, "Player");
    });
    for (int i = 0; i < 10000; ++i) { (void)ui.state(); (void)ui.speakers(); }
    writer.join();
    ui.reset();
    assert(!ui.pressed() && ui.state() == 0 && ui.speakers().empty());
    std::cout << "Voice UI lifecycle, lease, stale state and concurrent snapshot checks: PASS\n";
}
