// Simulated HID replies for the production JBL driver; no hardware access.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>

namespace {
std::vector<BYTE> linkReport, batteryReport;
std::vector<BYTE> requests;
int featureOpens = 0, v3Opens = 0;
bool allowFeatureOpen = true, allowV3Open = true, allowLink = true, allowBattery = true;

HidColl Receiver360X() {
    HidColl c;
    c.vid = 0x0ECB;
    c.pid = 0x20A5;
    c.iface = 5;
    c.caps.UsagePage = 0x0001;
    c.caps.Usage = 0;
    c.caps.InputReportByteLength = 2;
    c.caps.OutputReportByteLength = 64;
    c.caps.FeatureReportByteLength = 64;
    return c;
}
HidColl Receiver650() {
    HidColl c;
    c.vid = 0x0ECB;
    c.pid = 0x2125;
    c.caps.UsagePage = 0xFF12;
    c.caps.InputReportByteLength = 64;
    c.caps.FeatureReportByteLength = 64;
    return c;
}
std::vector<BYTE> Feature(BYTE id, BYTE value) {
    std::vector<BYTE> r(64, 0);
    r[0] = id;
    r[1] = value;
    return r;
}
PollContext Run(const HidColl& c, BYTE percent = 95, BYTE connected = 1) {
    requests.clear();
    linkReport = Feature(0x68, connected);
    batteryReport = Feature(0x49, percent);
    PollContext ctx;
    ctx.colls = {c};
    PollJbl(ctx);
    return ctx;
}
}  // namespace

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    ++featureOpens;
    c_ = c;
    if (!allowFeatureOpen) return false;
#ifdef _WIN32
    h_ = reinterpret_cast<HANDLE>(1);
#else
    fd_ = 1;
#endif
    return true;
}
bool HidDev::Open(const HidColl& c) {
    ++v3Opens;
    assert(c.pid == 0x2125);  // The 360X uses only the feature-report handle.
    c_ = c;
    if (!allowV3Open) return false;
#ifdef _WIN32
    h_ = reinterpret_cast<HANDLE>(1);
#else
    fd_ = 1;
#endif
    return true;
}
void HidDev::Close() {
#ifdef _WIN32
    h_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}
void HidDev::Flush() {}
bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    assert(c_.pid == 0x20A5 && (id == 0x68 || id == 0x49));
    requests.push_back(id);
    if ((id == 0x68 && !allowLink) || (id == 0x49 && !allowBattery)) return false;
    out = id == 0x68 ? linkReport : batteryReport;
    return true;
}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    assert(c_.pid == 0x2125);
    assert((data == std::vector<BYTE>{0x88, 0x00, 0xDD, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0D, 0x00}));
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int) {
    assert(c_.pid == 0x2125);
    out.assign(16, 0);
    out[0] = 0x86; out[2] = 0xDD; out[3] = 0x01; out[5] = 0x01;
    out[11] = 0x0D; out[13] = 0x01; out[15] = 0x80 | 69;
    return static_cast<int>(out.size());
}
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    auto c = Receiver360X();
    auto ctx = Run(c);
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x0ECB, 0x20A5));
    const auto& s = ctx.out[0];
    assert(s.percent == 95 && s.name == L"JBL Quantum 360X" && s.key == L"jbl360x:20A5");
    assert(s.kind == DeviceKind::Headset && s.via == L"2.4 GHz receiver" && !s.charging);
    assert((requests == std::vector<BYTE>{0x68, 0x49}));
    int before = featureOpens;
    PollJbl(ctx);
    assert(featureOpens == before);  // Claimed devices are not reopened.

    assert(Run(c, 0).out[0].percent == 0);
    assert(Run(c, 100).out[0].percent == 100);
    assert(Run(c, 101).out.empty());
    assert(Run(c, 95, 0).out.empty());
    assert((requests == std::vector<BYTE>{0x68}));  // Never display a stale off-headset level.
    assert(Run(c, 95, 2).out.empty());
    allowLink = false;
    assert(Run(c).out.empty());
    allowLink = true;
    allowBattery = false;
    assert(Run(c).out.empty());
    allowBattery = true;
    allowFeatureOpen = false;
    before = featureOpens;
    assert(Run(c).out.empty() && featureOpens == before + 1 && requests.empty());
    allowFeatureOpen = true;

    ctx = {};
    ctx.colls = { c };
    linkReport = Feature(0x67, 1); batteryReport = Feature(0x49, 95);
    PollJbl(ctx);
    assert(ctx.out.empty());
    linkReport = Feature(0x68, 1); batteryReport = Feature(0x48, 95);
    PollJbl(ctx);
    assert(ctx.out.empty());
    batteryReport = {0x49};
    PollJbl(ctx);
    assert(ctx.out.empty());

    ctx = {};
    for (int i = 0; i < 8; ++i) {
        auto wrong = c;
        if (i == 0) wrong.vid = 0x1234;
        if (i == 1) wrong.pid = 0x20A6;
        if (i == 2) wrong.iface = 4;
        if (i == 3) wrong.caps.UsagePage = 2;
        if (i == 4) wrong.caps.Usage = 1;
        if (i == 5) wrong.caps.InputReportByteLength = 3;
        if (i == 6) wrong.caps.OutputReportByteLength = 63;
        if (i == 7) wrong.caps.FeatureReportByteLength = 63;
        ctx.colls.push_back(wrong);
    }
    before = featureOpens;
    PollJbl(ctx);
    assert(ctx.out.empty() && featureOpens == before);

    // The existing HIDV3 JBL support remains available alongside the new HIDV1 path.
    ctx = {};
    ctx.colls = {Receiver650(), c};
    linkReport = Feature(0x68, 1); batteryReport = Feature(0x49, 95);
    PollJbl(ctx);
    assert(ctx.out.size() == 2 && ctx.IsClaimed(0x0ECB, 0x2125));
    assert(ctx.out[0].percent == 95 && ctx.out[1].percent == 69 && ctx.out[1].charging);
    assert(v3Opens > 0);
    std::puts("JBL driver tests passed");
}
