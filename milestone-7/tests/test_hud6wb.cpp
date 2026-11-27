// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Exercises 6WB guidance rendering and HUDTerminal command dispatch.
// Проверяет отрисовку подсказок 6WB и отправку команд HUDTerminal.
#include "cluster/hud6wb_codec.hpp"
#include "cluster/hud6wb_display.hpp"
#include "cluster/nav_core.hpp"
#include "fake_dsi_client.hpp"
#include "kombi_nav_test_util.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace nbt;
using namespace nbt::hudterm;

namespace {

const uint16_t kHost = 0x0123;
const uint16_t kSelf = 0x2077;
const uint32_t kTag = 77;

class RecordingPort : public Port {
public:
    std::vector<Batch> batches;
    bool idle;
    Status state;
    uint32_t generation;

    RecordingPort() : idle(true), state(kReady), generation(0) {}
    SubmitResult submit(const Batch& b) {
        assert(idle && state == kReady);
        batches.push_back(b);
        return kQueued;
    }
    Status status() const { return state; }
    uint32_t epoch() const { return generation; }
    bool waiting(int) const { return false; }
    void reset() {}
    bool waitIdle(uint32_t) { return idle && state == kReady; }
};

std::vector<uint8_t> streetOnWire(const Batch& b, uint16_t handle) {
    for (size_t i = 0; i < b.calls.size(); ++i)
        if (b.calls[i].fkt == kTextareaSetText && b.calls[i].handle() == handle)
            return b.calls[i].payload;
    return std::vector<uint8_t>();
}

void streetScroll() {
    setenv("NBT_6WB_STREET_SCROLL", "1", 1);
    RecordingPort port;
    Bmw6wbProfile profile;
    profile.streetScrollWidth = 4;
    const uint16_t h = profile.base.street;
    kombinav::Hud6wbDisplay display(port, profile, false);
    kombinav::NavView v;
    v.aa_type = 8;
    v.road = "А😀е́БВГ";
    v.dist_text = "350";
    v.aa_unit = 1;
    assert(display.showManeuver(v, 0, true, true) == kombinav::ClusterDisplay::kOk);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, "А😀е́Б", 255));
    assert(display.keepUp(v, 0.25) == kombinav::ClusterDisplay::kOk);
    assert(display.keepUp(v, 1.5) == kombinav::ClusterDisplay::kOk && port.batches.size() == 1);
    port.idle = false;
    assert(display.keepUp(v, 2) == kombinav::ClusterDisplay::kOk && port.batches.size() == 1);
    assert(display.keepUp(v, 20) == kombinav::ClusterDisplay::kOk && port.batches.size() == 1);
    port.idle = true;
    assert(display.keepUp(v, 21) == kombinav::ClusterDisplay::kOk && port.batches.size() == 2);
    assert(port.batches.back().calls.size() == 1);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, "😀е́БВ", 255));
    assert(display.keepUp(v, 21.25) == kombinav::ClusterDisplay::kOk && port.batches.size() == 2);
    assert(display.keepUp(v, 21.5) == kombinav::ClusterDisplay::kOk && port.batches.size() == 3);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, "е́БВГ", 255));
    assert(display.keepUp(v, 22) == kombinav::ClusterDisplay::kOk && port.batches.size() == 3);
    assert(display.keepUp(v, 23) == kombinav::ClusterDisplay::kOk && port.batches.size() == 4);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, "А😀е́Б", 255));

    setenv("NBT_6WB_STREET_SCROLL", "0", 1);
    assert(display.keepUp(v, 23.25) == kombinav::ClusterDisplay::kOk && port.batches.size() == 5);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, v.road, 255));
    assert(display.keepUp(v, 26) == kombinav::ClusterDisplay::kOk && port.batches.size() == 5);
    setenv("NBT_6WB_STREET_SCROLL", "1", 1);
    assert(display.keepUp(v, 26.25) == kombinav::ClusterDisplay::kOk && port.batches.size() == 6);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, "А😀е́Б", 255));

    v.road = "Лес";
    assert(display.showManeuver(v, 26.5, true, false) == kombinav::ClusterDisplay::kOk);
    assert(streetOnWire(port.batches.back(), h) == unicodeText(h, v.road, 255));
    assert(display.keepUp(v, 27) == kombinav::ClusterDisplay::kOk);
    assert(display.keepUp(v, 30) == kombinav::ClusterDisplay::kOk && port.batches.size() == 7);
    assert(display.clear(NULL));
    assert(display.keepUp(v, 31) == kombinav::ClusterDisplay::kOk && port.batches.size() == 8);

    profile.roadShields = profile.iconBitmaps = true;
    profile.signLayout.spaces = 2;
    profile.streetScrollWidth = 6;
    RecordingPort shieldPort;
    kombinav::Hud6wbDisplay shieldDisplay(shieldPort, profile, false);
    v.road = "A1 Проспект";
    assert(shieldDisplay.showManeuver(v, 0, true, true) == kombinav::ClusterDisplay::kOk);
    assert(streetOnWire(shieldPort.batches.back(), h) == unicodeText(h, "  A1 П", 255));
    assert(shieldDisplay.keepUp(v, 0.25) == kombinav::ClusterDisplay::kOk);
    assert(shieldDisplay.keepUp(v, 1.75) == kombinav::ClusterDisplay::kOk);
    assert(streetOnWire(shieldPort.batches.back(), h) == unicodeText(h, "  1 Пр", 255));
    ++shieldPort.generation;
    assert(shieldDisplay.keepUp(v, 2) == kombinav::ClusterDisplay::kOk);
    assert(streetOnWire(shieldPort.batches.back(), h) == unicodeText(h, "  A1 П", 255));
    unsetenv("NBT_6WB_STREET_SCROLL");
}

void transport() {
    Bmw6wbProfile profile;
    assert(validProfile(profile));
    Config config;
    config.sixwb = true;
    config.handles = handlesOf(profile);
    config.navFrame = profile.base.frame;
    assert(!config.handles.has(1) && !config.handles.has(6));
    Core core(config);
    core.setHost(kHost, 0);
    core.setLink(true, kSelf, 0, kTag);
    assert(core.status() == kReady);

    Batch b("6wb-test");
    b.add(kTextareaSetText, unicodeText(profile.base.street, std::string(70, 'a'), profile.base.streetChars));
    b.add(kAddBitmap, bitmap(profile.icons.a, 0, std::vector<uint32_t>(16, 0), 4, 4, profile.maxBitmapBytes));
    b.add(kSetVisibility, setVisibility(profile.base.frame, true));
    assert(!callOk(b.calls[0], config.handles) && callOk(b.calls[0], config.handles, true));
    assert(!callOk(b.calls[1], config.handles) && callOk(b.calls[1], config.handles, true));
    assert(core.submit(b, 0) == kQueued);

    most::Msg outgoing;
    assert(core.next(0, outgoing) && outgoing.txType == 4 && outgoing.hdrTag == kTag);
    core.sent(1);
    assert(!core.next(15, outgoing));
    assert(core.next(16, outgoing) && outgoing.txType == 0 && outgoing.fkt == kAddBitmap);
    core.sent(16);
    assert(core.next(31, outgoing) && outgoing.fkt == kSetVisibility);
    assert(core.busy());
    core.sent(31);
    core.tick(11000);
    assert(!core.busy() && core.status() == kReady && core.stats().batchesDone == 1);

    const std::vector<uint8_t> on = setVisibility(profile.base.frame, true);
    const std::vector<uint8_t> off = setVisibility(profile.base.frame, false);
    most::View message = {};
    message.fblock = kFBlock;
    message.inst = kInst;
    message.src = kHost;
    message.dst = 0x2100;
    message.fkt = kSetVisibility;
    message.op = most::kOpResultAck;
    message.hdrTag = kTag;
    message.data = &on[0];
    message.len = on.size();
    core.onMessage(message, 11000); // some terminals mirror replies to another app address
    assert(core.status() == kReady && core.stats().answers == 1);

    assert(core.submit(b, 11000) == kQueued);
    message.src = 0x2100;
    message.dst = kHost;
    message.hdrTag = kTag + 1;
    message.op = most::kOpStartResultAck;
    core.onMessage(message, 11001);
    assert(core.status() == kNavActive && !core.busy() && !core.next(11001, outgoing));
    message.data = &off[0];
    core.onMessage(message, 11002);
    core.tick(11002 + config.navQuietMs);
    assert(core.status() == kReady);
}

void navigation(bool shadow) {
    char dir[] = "/tmp/hud6wb_testXXXXXX";
    assert(mkdtemp(dir));
    const std::string marker = std::string(dir) + "/shown";
    const std::string marker6wb = marker + ".6wb";
    kombinav::KombiNav::kShownFile = marker.c_str();
    setenv("NBT_CLUSTER", "6wb", 1);
    setenv("NBT_KOMBI_NAV", "1", 1);
    setenv("NBT_KOMBI_NAV_YIELD", "0", 1);
    if (shadow)
        setenv("NBT_HUDTERM", "log", 1);
    else
        unsetenv("NBT_HUDTERM");
    fake_dsi::reset();
    {
        most::Client client;
        kombinav::KombiNav nav;
        nav.start(&client);
        assert(strcmp(nav.clusterName(), "6wb") == 0);
        assert(client.start("T"));
        fake_dsi::attach();
        std::vector<uint8_t> response;
        most::putLe32(response, kTag);
        most::putLe16(response, kSelf);
        fake_dsi::data(most::kInfoClientResponse, &response[0], response.size());
        std::vector<uint8_t> registry;
        most::putLe32(registry, 1);
        most::putLe16(registry, kHost);
        most::putLe16(registry, most::registryKey(kFBlock, kInst));
        fake_dsi::data(most::kAttrRegistry, &registry[0], registry.size());

        const kntest::Bytes active = kntest::status(1);
        const std::string road = "Straße 123";
        const kntest::Bytes step = kntest::navState(8, road);
        const kntest::Bytes dist = kntest::curPos(350, "350", 1, 90);
        kombinav::KombiNav::navStatusCallback(0x8006, &step[0], step.size(), &nav);
        kombinav::KombiNav::navStatusCallback(0x8007, &dist[0], dist.size(), &nav);
        kombinav::KombiNav::navStatusCallback(0x8003, &active[0], active.size(), &nav);
        if (!shadow) {
            for (int i = 0; i < 100 && access(marker6wb.c_str(), F_OK) != 0; ++i)
                usleep(20000);
            assert(access(marker6wb.c_str(), F_OK) == 0);
        } else {
            usleep(500000);
            assert(access(marker6wb.c_str(), F_OK) != 0);
        }
        nav.stop(); // Queued graphics are dispatched before the clear, even without acknowledgements.
        fake_dsi::detach();
    }
    Bmw6wbProfile p;
    const std::vector<uint8_t> road = unicodeText(p.base.street, "Straße 123", p.base.streetChars);
    bool arrow = false, frameOn = false, frameOff = false, street = false, bar = false;
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i) {
        const fake_dsi::Call& call = fake_dsi::calls[i];
        if (call.kind != fake_dsi::Call::kRequest || call.id != most::kReqProcessMessage) continue;
        assert(!shadow && call.payload.size() > 4 && call.payload.size() % 4 == 0);
        most::View view;
        assert(most::parseView(&call.payload[4], call.payload.size() - 4, view));
        assert(view.dst == kHost && view.src == kSelf && view.hdrTag == kTag && view.txType == 4);
        assert(view.fblock == kFBlock && view.inst == kInst && view.fkt != kGetSize && view.len >= 2);
        const uint16_t handle = rdBe16(view.data);
        if (view.fkt == kAddPolygon && handle == p.main.a) arrow = true;
        if (view.fkt == kPotentiometerSetValue && handle == p.bar) bar = true;
        if (view.fkt == kTextareaSetText && view.len == road.size() && memcmp(view.data, &road[0], view.len) == 0)
            street = true;
        if (view.fkt == kSetVisibility && handle == p.base.frame && view.len == 3) {
            if (view.data[2]) {
                assert(arrow && street && bar);
                frameOn = true;
            } else {
                assert(frameOn);
                frameOff = true;
            }
        }
    }
    assert(shadow || (frameOn && frameOff));
    assert(access(marker.c_str(), F_OK) != 0 && access(marker6wb.c_str(), F_OK) != 0);
    assert(rmdir(dir) == 0);
}

} // namespace

int main() {
    transport();
    streetScroll();
    navigation(false);
    navigation(true);
    puts("BMW 6WB navigation and HUDTerminal dispatch passed");
}
