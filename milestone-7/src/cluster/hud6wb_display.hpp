// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_DISPLAY_HPP
#define CLUSTER_HUD6WB_DISPLAY_HPP
#include "cluster/display.hpp"
#include "cluster/hud6wb_kit.hpp"
namespace nbt {
namespace kombinav {
class Hud6wbDisplay : public ClusterDisplay {
  public:
    enum { kMaxBatch = 128 };
    Hud6wbDisplay(hudterm::Port& port, const hudterm::Bmw6wbProfile& profile, bool shadow,
                  const hudterm::GrafiteKit& kit = hudterm::GrafiteKit());
    const char* name() const { return "6wb"; }
    void setSettings(const Settings& s) { settings_ = s; }
    void setLog(LogFn f, void* u) {
        log_ = f;
        user_ = u;
    }
    bool sameManeuver(const NavView& v, double now);
    Result showManeuver(const NavView& v, double now, bool isNew, bool first);
    size_t roadNameLimit(size_t) const { return p_.base.streetChars; }
    Result updateDistance(const NavView& v, double now);
    Result keepUp(const NavView& v, double now);
    void guidanceGap() {}
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_; }
    bool factoryActive() { return !shadow_ && port_.status() == hudterm::kNavActive; }
    bool ready() { return !shadow_ && port_.status() == hudterm::kReady; }
    hudterm::Batch clearBatch() const;

  private:
    Result health();
    bool canDraw();
    bool submit(const hudterm::Batch& b);
    hudterm::Batch render(const NavView& v, bool scrolling, bool& signVisible) const;
    std::vector<uint8_t> streetText(const std::string& road, bool signVisible, bool scrolling, size_t offset,
                                    bool* atEnd = NULL) const;
    Result scrollStreet(double now);
    void log(const std::string& s) const;
    hudterm::GrafiteKit kit_;
    hudterm::Port& port_;
    const hudterm::Bmw6wbProfile p_;
    void remember(const NavView& v, const hudterm::Batch& b);
    bool mainChanged(const NavView& v) const;
    bool iconChanged(const NavView& v) const;
    bool shadow_, sent_, fresh_, dirty_, shownLanes_;
    uint32_t epoch_;
    uint16_t mainVisible_, laneVisible_, iconVisible_;
    NavView shown_;
    Settings settings_;
    LogFn log_;
    void* user_;
    hudterm::Batch queuedPicture_;
    NavView queuedView_;
    size_t queuedCall_;
    bool kitErrorLogged_;
    bool queuedScroll_, queuedSign_, streetScrolling_, streetSign_, streetAtEnd_;
    size_t streetOffset_;
    double streetDue_;
    std::vector<uint8_t> streetShown_;
};
} // namespace kombinav
} // namespace nbt
#endif
