// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Updates the maneuver, lanes, text and distance bar on their respective surfaces.
// Обновляет манёвр, полосы, текст и шкалу расстояния в соответствующих областях.
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "cluster/grafite_oro/miniature_raster.hpp"
#include "cluster/hud6wa_display.hpp"
#include "nbt_env.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
namespace nbt {
namespace kombinav {
using namespace hudterm;
namespace {
const double kStreetHoldSeconds = 1.5;
const double kStreetStepSeconds = 0.5;

bool streetScrollEnabled() {
    const int forced = envFlag("NBT_6WB_STREET_SCROLL");
    return forced < 0 ? flagSet(k6wbStreetScrollFlagName) : forced == 1;
}

bool continuesStreetCharacter(uint16_t u) {
    return (u >= 0xdc00 && u <= 0xdfff) || (u >= 0x0300 && u <= 0x036f) ||
           (u >= 0x0483 && u <= 0x0489) || (u >= 0x1ab0 && u <= 0x1aff) ||
           (u >= 0x1dc0 && u <= 0x1dff) || (u >= 0x20d0 && u <= 0x20ff) ||
           (u >= 0xfe00 && u <= 0xfe0f) || (u >= 0xfe20 && u <= 0xfe2f) || u == 0x200d;
}
uint16_t other(uint16_t v, const Surface& s) {
    return v == s.a ? s.b : s.a;
}
Batch errorBatch(const char* why) {
    Batch b(why, true);
    return b;
}

} // namespace
Hud6wbDisplay::Hud6wbDisplay(Port& port, const Bmw6wbProfile& p, bool shadow, const GrafiteKit& kit)
    : kit_(kit), port_(port), p_(p), shadow_(shadow), sent_(false), fresh_(true), dirty_(false), shownLanes_(true),
      epoch_(port.epoch()), mainVisible_(p.main.b), laneVisible_(p.lanes.b), iconVisible_(p.icons.b), log_(NULL),
      user_(NULL), queuedCall_(0), kitErrorLogged_(false), queuedScroll_(false), queuedSign_(false),
      streetScrolling_(false), streetSign_(false), streetAtEnd_(false), streetOffset_(0), streetDue_(0) {}
void Hud6wbDisplay::log(const std::string& s) const {
    if (log_) log_(s.c_str(), user_);
}
ClusterDisplay::Result Hud6wbDisplay::health() {
    if (shadow_) return kOk;
    if (port_.status() == kFailed) return kRejected;
    if (port_.epoch() != epoch_) {
        epoch_ = port_.epoch();
        fresh_ = true;
        dirty_ = true;
        queuedPicture_ = Batch();
        queuedCall_ = 0;
        streetShown_.clear();
        mainVisible_ = p_.main.b;
        laneVisible_ = p_.lanes.b;
        iconVisible_ = p_.icons.b;
    }
    // After a disconnect, redraw when the link becomes available again.
    return kOk;
}
bool Hud6wbDisplay::canDraw() {
    if (shadow_) return true;
    // Never replace an unexecuted full batch: the buffer state advances only with an ordered batch.
    return port_.waitIdle(0);
}
bool Hud6wbDisplay::submit(const Batch& b) {
    if (shadow_) {
        for (size_t i = 0; i < b.calls.size(); ++i) {
            const Call& c = b.calls[i];
            char header[128];
            snprintf(header, sizeof(header),
                     "[6wb shadow] fkt=%03X h=%u tx=%u var=1/0 op=6 tag=registered-client payload:", c.fkt, c.handle(),
                     c.fkt == kAddBitmap ? 0 : p_.vectorTx);
            std::string line = header;
            for (size_t j = 0; j < c.payload.size(); ++j) {
                char h[4];
                snprintf(h, sizeof(h), " %02x", c.payload[j]);
                line += h;
            }
            log(line);
        }
        return true;
    }
    if (port_.status() != kReady) return false;
    SubmitResult r = port_.submit(b);
    return r == kQueued;
}
bool Hud6wbDisplay::mainChanged(const NavView& v) const {
    return fresh_ || v.aa_type != shown_.aa_type || v.has_exit_angle != shown_.has_exit_angle ||
           (v.has_exit_angle && v.exit_angle != shown_.exit_angle) || v.has_exit_number != shown_.has_exit_number ||
           (v.has_exit_number && v.exit_number != shown_.exit_number);
}
bool Hud6wbDisplay::iconChanged(const NavView& v) const {
    if (fresh_ || v.has_next != shown_.has_next) return true;
    if (v.has_next &&
        (v.next_type != shown_.next_type || v.next_has_angle != shown_.next_has_angle ||
         (v.next_has_angle && v.next_angle != shown_.next_angle) || v.next_exit_number != shown_.next_exit_number))
        return true;
    if (p_.roadShields) {
        if (road_sign::route(v.road) != road_sign::route(shown_.road)) return true;
    }
    return false;
}
bool Hud6wbDisplay::sameManeuver(const NavView& v, double) {
    health();
    return !queuedPicture_.calls.empty() || dirty_ || v.road != shown_.road || mainChanged(v) || iconChanged(v) ||
           (settings_.lanes && v.lanes != shown_.lanes) || shownLanes_ != settings_.lanes;
}
std::vector<uint8_t> Hud6wbDisplay::streetText(const std::string& road, bool signVisible, bool scrolling,
                                              size_t offset, bool* atEnd) const {
    const size_t cap = p_.base.streetChars;
    const size_t spaces = signVisible ? std::min<size_t>(p_.signLayout.spaces, cap - 1) : 0;
    const std::vector<uint8_t> text =
        unicodeText(p_.base.street, road_sign::street(road, signVisible, p_.signLayout, cap), cap);
    if (atEnd) *atEnd = true;
    if (!scrolling || p_.streetScrollWidth <= spaces) return text;

    // Skip the handle, encoding and fixed shield padding. Keep UTF-16 pairs and combining marks together.
    const size_t first = 3 + spaces * 2;
    const size_t end = text.size() - 2;
    std::vector<size_t> boundaries;
    uint16_t previous = 0;
    for (size_t i = first; i < end; i += 2) {
        const uint16_t unit = rdBe16(&text[i]);
        if (i == first || (previous != 0x200d && !continuesStreetCharacter(unit))) boundaries.push_back(i);
        previous = unit;
    }
    const size_t width = p_.streetScrollWidth - spaces;
    if (boundaries.size() <= width) return text;
    const size_t last = boundaries.size() - width;
    offset = std::min(offset, last);
    if (atEnd) *atEnd = offset == last;
    boundaries.push_back(end);
    std::vector<uint8_t> frame(text.begin(), text.begin() + first);
    frame.insert(frame.end(), text.begin() + boundaries[offset], text.begin() + boundaries[offset + width]);
    putBe16(frame, 0);
    return frame;
}

Batch Hud6wbDisplay::render(const NavView& v, bool scrolling, bool& signVisible) const {
    Batch b("6wb-guidance", false);
    KitBarState bar = kitBarState(v, p_.barRangeMetres);
    bar.visible = bar.visible && settings_.hud_bar;
    sixwbkit::Maneuver::Value m = GrafiteKit::maneuver(v.aa_type);
    sixwbkit::PlanePolicy::Value policy = sixwbkit::describe(m).policy;
    // Pass the configured drawing rectangle to the central maneuver renderer.
    // Передаём настроенную область отрисовщику большого манёвра.
    const Rect& useful = p_.base.glyph;
    uint16_t target = other(mainVisible_, p_.main);
    uint16_t lane = other(laneVisible_, p_.lanes);
    const uint16_t icon = p_.iconHandle ? p_.iconHandle : other(iconVisible_, p_.icons);
    const uint16_t otherIcon = other(icon, p_.icons);
    // Preflight the whole picture before any buffer is cleared or hidden.
    if (mainChanged(v)) {
        if (policy == sixwbkit::PlanePolicy::Hidden) {
            b.add(kSetVisibility, setVisibility(mainVisible_, false));
        } else {
            Batch drawing;
            if (!kit_.central(v, target, useful, drawing))
                return errorBatch("6wb-kit-error: central palette/geometry not representable");
            b.add(kRemoveAllItems, removeAllItems(target));
            b.calls.insert(b.calls.end(), drawing.calls.begin(), drawing.calls.end());
            b.add(kSwitchVisibility, switchVisibility(mainVisible_, target));
            b.add(kSetVisibility, setVisibility(target, true));
        }
    }
    if (fresh_ || (settings_.lanes && v.lanes != shown_.lanes) || shownLanes_ != settings_.lanes) {
        Batch drawing;
        bool visible = settings_.lanes && !v.lanes.empty();
        if (visible && !kit_.lanes(v, lane, p_.lanes.width, p_.lanes.height, drawing)) {
            visible = false;
            drawing = Batch();
            log("6wb: lane geometry omitted; central maneuver continues");
        }
        b.add(kRemoveAllItems, removeAllItems(lane));
        b.calls.insert(b.calls.end(), drawing.calls.begin(), drawing.calls.end());
        b.add(kSwitchVisibility, switchVisibility(laneVisible_, lane));
        b.add(kSetVisibility, setVisibility(lane, visible));
    }
    std::vector<uint8_t> signPayload;
    if (p_.roadShields && p_.iconBitmaps) {
        std::vector<uint32_t> pixels;
        if (road_sign::draw(road_sign::route(v.road), p_.icons.width, p_.icons.height, p_.signLayout, pixels))
            signPayload = bitmap(icon, 0, pixels, p_.icons.width, p_.icons.height, p_.maxBitmapBytes);
    }
    signVisible = !signPayload.empty();
    const bool refreshIcon = iconChanged(v) || (p_.roadShields && dirty_);
    if (refreshIcon) {
        const sixwbkit::Maneuver::Value nextType = GrafiteKit::maneuver(v.next_type);
        const bool nextDrawable = v.has_next && sixwbkit::describe(nextType).policy != sixwbkit::PlanePolicy::Hidden;
        bool picture = p_.iconBitmaps && (signVisible || nextDrawable);
        if (picture) {
            std::vector<uint8_t> payload;
            payload.swap(signPayload);
            // Both images use the same bitmap surface. A road sign takes precedence.
            if (!signVisible && nextDrawable) {
                const Rect& r = p_.nextGlyph;
                std::vector<uint32_t> px((size_t)p_.icons.width * p_.icons.height, 0);
                sixwbkit::NavigationEvent next;
                next.maneuver = nextType;
                next.hasExitAngle = v.next_has_angle;
                next.exitAngle = (double)v.next_angle;
                next.hasExitNumber = v.next_exit_number > 0;
                next.exitNumber = (unsigned)v.next_exit_number;
                sixwbkit::rasterKit(
                    px, p_.icons.width, p_.icons.height,
                    sixwbkit::renderMiniature(next, sixwbkit::Rect(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1)));
                payload = bitmap(icon, 0, px, p_.icons.width, p_.icons.height, p_.maxBitmapBytes);
            }
            if (!payload.empty()) {
                // A fixed handle may already be visible. Hide it before replacing
                // its contents, keeping the other handle out of this draw path.
                if (p_.iconHandle) b.add(kSetVisibility, setVisibility(icon, false));
                b.add(kRemoveAllItems, removeAllItems(icon));
                b.add(kAddBitmap, payload);
                if (!p_.iconHandle) b.add(kSwitchVisibility, switchVisibility(iconVisible_, icon));
            } else {
                picture = false;
                log("6wb icons: bitmap exceeds the C05 payload limit; surfaces hidden");
            }
        }
        b.add(kSetVisibility, setVisibility(otherIcon, false));
        b.add(kSetVisibility, setVisibility(icon, picture));
    }
    if (fresh_ || v.road != shown_.road || signVisible != streetSign_)
        b.add(kTextareaSetText, streetText(v.road, signVisible, scrolling, 0));
    std::string dist = v.dist_text == "-" ? "" : v.dist_text, oldDist = shown_.dist_text == "-" ? "" : shown_.dist_text;
    for (size_t i = 0; i < dist.size(); ++i)
        if (dist[i] == ',') dist[i] = '.';
    for (size_t i = 0; i < oldDist.size(); ++i)
        if (oldDist[i] == ',') oldDist[i] = '.';
    std::vector<uint8_t> distance = unicodeText(p_.base.distance, dist, p_.base.distanceChars);
    if (fresh_ || distance != unicodeText(p_.base.distance, oldDist, p_.base.distanceChars))
        b.add(kTextareaSetText, distance);
    std::string unit = dist.empty() ? "" : hudUnitText(v.aa_unit),
                oldUnit = oldDist.empty() ? "" : hudUnitText(shown_.aa_unit);
    if (fresh_ || unit != oldUnit) b.add(kTextareaSetText, unicodeText(p_.base.unit, unit, 3));
    if (fresh_) {
        b.add(kSetVisibility, setVisibility(p_.base.street, true));
        b.add(kSetVisibility, setVisibility(p_.base.distance, true));
        b.add(kSetVisibility, setVisibility(p_.base.unit, true));
    }
    KitBarState oldBar = kitBarState(shown_, p_.barRangeMetres);
    oldBar.visible = oldBar.visible && settings_.hud_bar;
    if (bar.visible && (fresh_ || !oldBar.visible || bar.value != oldBar.value))
        b.add(kPotentiometerSetValue, setBarValue(p_.bar, bar.value));
    if (fresh_ || bar.visible != oldBar.visible) b.add(kSetVisibility, setVisibility(p_.bar, bar.visible));
    if (fresh_) b.add(kSetVisibility, setVisibility(p_.base.frame, true));
    if (b.calls.size() > 4096) return errorBatch("6wb-kit-error: bounded frame call budget exceeded");
    return b;
}
void Hud6wbDisplay::remember(const NavView& v, const Batch& b) {
    // Commit predicted buffer state only after the batch is accepted. The next
    // batch waits for local dispatch completion; acknowledgement order is irrelevant.
    for (size_t i = 0; i < b.calls.size(); ++i)
        if (b.calls[i].fkt == kSwitchVisibility) {
            const std::vector<uint8_t>& p = b.calls[i].payload;
            uint16_t from = rdBe16(&p[0]), to = rdBe16(&p[2]);
            if (from == mainVisible_)
                mainVisible_ = to;
            else if (from == laneVisible_)
                laneVisible_ = to;
            else if (from == iconVisible_)
                iconVisible_ = to;
        }
    for (size_t i = 0; i < b.calls.size(); ++i) {
        const Call& c = b.calls[i];
        if (c.fkt == kTextareaSetText && c.handle() == p_.base.street) {
            streetShown_ = c.payload;
            streetSign_ = queuedSign_;
            streetScrolling_ = queuedScroll_;
            streetOffset_ = 0;
            streetDue_ = 0;
        }
        if (c.fkt == kSetVisibility && c.payload.size() == 3 && c.payload[2] &&
            (c.handle() == p_.icons.a || c.handle() == p_.icons.b))
            iconVisible_ = c.handle();
    }
    if (!b.calls.empty()) sent_ = !shadow_;
    fresh_ = false;
    dirty_ = false;
    shown_ = v;
    shownLanes_ = settings_.lanes;
}
ClusterDisplay::Result Hud6wbDisplay::showManeuver(const NavView& v, double, bool, bool) {
    Result r = health();
    if (r != kOk) return r;
    if (!canDraw()) {
        dirty_ = true;
        return kOk;
    }
    if (queuedPicture_.calls.empty()) {
        queuedScroll_ = streetScrollEnabled();
        queuedSign_ = false;
        Batch b = render(v, queuedScroll_, queuedSign_);
        if (b.label.find("6wb-kit-error:") == 0) {
            if (!kitErrorLogged_) {
                char detail[192];
                snprintf(detail, sizeof(detail), " | AA type %d, angle %s%lld, exit %s%lld, lanes %u", v.aa_type,
                         v.has_exit_angle ? "" : "absent/", (long long)v.exit_angle, v.has_exit_number ? "" : "absent/",
                         (long long)v.exit_number, (unsigned)v.lanes.size());
                log(b.label + detail);
                kitErrorLogged_ = true;
            }
            b = Batch("6wb-unrepresentable", false);
            b.add(kSetVisibility, setVisibility(mainVisible_, false));
            b.add(kSetVisibility, setVisibility(laneVisible_, false));
            b.add(kSetVisibility, setVisibility(p_.bar, false));
            b.add(kSetVisibility, setVisibility(p_.icons.a, false));
            b.add(kSetVisibility, setVisibility(p_.icons.b, false));
            queuedSign_ = false;
            b.add(kTextareaSetText, streetText(v.road, false, queuedScroll_, 0));
            b.add(kTextareaSetText,
                  unicodeText(p_.base.distance, v.dist_text == "-" ? "" : v.dist_text, p_.base.distanceChars));
            b.add(kTextareaSetText, unicodeText(p_.base.unit, v.dist_text == "-" ? "" : hudUnitText(v.aa_unit), 3));
            b.add(kSetVisibility, setVisibility(p_.base.street, true));
            b.add(kSetVisibility, setVisibility(p_.base.distance, true));
            b.add(kSetVisibility, setVisibility(p_.base.unit, true));
            b.add(kSetVisibility, setVisibility(p_.base.frame, true));
        }
        for (size_t i = 0; i < b.calls.size(); ++i)
            if (!callOk(b.calls[i], handlesOf(p_), true)) return kInternal;
        if (b.calls.empty()) {
            remember(v, b);
            return kOk;
        }
        queuedPicture_ = b;
        queuedView_ = v;
        queuedCall_ = 0;
    }
    // Chunking is an adapter concern, not a new MOST queue or retry strategy.
    // One bounded chunk is dispatched at a time. The visible buffer flips only
    // after its ordered geometry commands; never replace a partial picture.
    Batch chunk("6wb-guidance", false);
    size_t end = std::min(queuedCall_ + kMaxBatch, queuedPicture_.calls.size());
    chunk.calls.assign(queuedPicture_.calls.begin() + queuedCall_, queuedPicture_.calls.begin() + end);
    if (!submit(chunk)) {
        dirty_ = true;
        return health();
    }
    sent_ = !shadow_;
    queuedCall_ = end;
    if (queuedCall_ == queuedPicture_.calls.size()) {
        remember(queuedView_, queuedPicture_);
        queuedPicture_ = Batch();
        queuedCall_ = 0;
    } else
        dirty_ = true;
    return kOk;
}
ClusterDisplay::Result Hud6wbDisplay::updateDistance(const NavView& v, double now) {
    return showManeuver(v, now, false, false);
}
ClusterDisplay::Result Hud6wbDisplay::keepUp(const NavView& v, double now) {
    const Result r = health();
    if (r != kOk) return r;
    if (!queuedPicture_.calls.empty() || dirty_ || fresh_ || !v.sameDistance(shown_) ||
        v.has_step_m != shown_.has_step_m || (settings_.hud_bar && v.has_step_m && v.step_m != shown_.step_m))
        return showManeuver(v, now, false, false);
    return scrollStreet(now);
}

ClusterDisplay::Result Hud6wbDisplay::scrollStreet(double now) {
    if (streetShown_.empty()) return kOk;
    const bool scrolling = streetScrollEnabled();
    if (!scrolling && !streetScrolling_) return kOk;
    if (!canDraw()) return kOk;

    size_t offset = 0;
    bool atEnd = false;
    if (scrolling == streetScrolling_) {
        if (!streetDue_) {
            streetText(shown_.road, streetSign_, true, 0, &streetAtEnd_);
            streetDue_ = now + kStreetHoldSeconds;
        }
        if ((streetOffset_ == 0 && streetAtEnd_) || now < streetDue_) return kOk;
        if (!streetAtEnd_) offset = streetOffset_ + 1;
    }

    const std::vector<uint8_t> text = streetText(shown_.road, streetSign_, scrolling, offset, &atEnd);
    if (text != streetShown_) {
        Batch b("6wb-street-scroll");
        b.add(kTextareaSetText, text);
        if (!submit(b)) return health();
        streetShown_ = text;
    }
    streetScrolling_ = scrolling;
    streetAtEnd_ = atEnd;
    streetOffset_ = offset;
    streetDue_ = now + ((offset == 0 || atEnd) ? kStreetHoldSeconds : kStreetStepSeconds);
    return kOk;
}
Batch Hud6wbDisplay::clearBatch() const {
    Batch b("6wb-clear", true, kSlotAll);
    b.add(kSetVisibility, setVisibility(p_.base.frame, false));
    const uint16_t hs[] = {p_.main.a, p_.main.b, p_.lanes.a, p_.lanes.b};
    for (size_t i = 0; i < 4; ++i)
        b.add(kRemoveAllItems, removeAllItems(hs[i]));
    b.add(kSetVisibility, setVisibility(p_.icons.a, false));
    b.add(kSetVisibility, setVisibility(p_.icons.b, false));
    b.add(kSetVisibility, setVisibility(p_.bar, false));
    b.add(kTextareaSetText, unicodeText(p_.base.street, "", p_.base.streetChars));
    b.add(kTextareaSetText, unicodeText(p_.base.distance, "", p_.base.distanceChars));
    b.add(kTextareaSetText, unicodeText(p_.base.unit, "", 3));
    return b;
}
bool Hud6wbDisplay::clear(std::string* detail) {
    streetShown_.clear();
    queuedPicture_ = Batch();
    queuedCall_ = 0;
    if (shadow_) return submit(clearBatch());
    if (port_.status() == kNavActive) return true;
    // Never reset a failed terminal just to send cleanup; no writes to unverified resources.
    if (!ready() || !port_.waitIdle(3000) || !submit(clearBatch()) || !port_.waitIdle(3000)) {
        if (detail) *detail = "6wb: cleanup dispatch did not complete";
        return false;
    }
    return true;
}
void Hud6wbDisplay::forget() {
    streetShown_.clear();
    sent_ = false;
    fresh_ = true;
    dirty_ = true;
    shown_ = NavView();
    queuedPicture_ = Batch();
    queuedCall_ = 0;
    kitErrorLogged_ = false;
    mainVisible_ = p_.main.b;
    laneVisible_ = p_.lanes.b;
    iconVisible_ = p_.icons.b;
}
} // namespace kombinav
} // namespace nbt
