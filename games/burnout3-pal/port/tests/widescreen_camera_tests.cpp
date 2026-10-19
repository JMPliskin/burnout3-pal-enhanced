// Link with preserved PAL EE objects and the real host; no ISO/gameplay.
// The only test double is the frame-notification callback, not the camera.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>
#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include <windows.h>

void F_001E2100(lifted::Context&);
void RegisterGameOverrides();
extern const ps2host::FunctionEntry kGuestFunctions[];
extern const std::size_t kGuestFunctionCount;

namespace {
constexpr u32 camera = 0x10000, frame = 0x11000, window = 0x12000, stack = 0x40000;
unsigned notifications = 0, cases = 0;
bool fail_notification = false;
u32 observed_x = 0, observed_y = 0;
u32 Bits(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }
float Value(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void Near(float a, float b, const char* text) { Check(std::fabs(a-b) < 0.000002f, text); }
void Notification(lifted::Context& c) {
    Check(u32(c.r[4]) == frame, "Original callback argument changed");
    ++notifications;
    observed_x = generated::ReadU32(window); observed_y = generated::ReadU32(window+4);
    if (fail_notification) throw std::runtime_error("synthetic callback exception");
}
lifted::Context Prepare(float x, float y) {
    auto& m = ps2host::GuestMemory();
    std::memset(m.Translate(camera, 256), 0xA5, 256);
    std::memset(m.Translate(stack-64, 128), 0x5A, 128);
    generated::Write32(camera+4, frame);
    generated::Write32(window, Bits(x)); generated::Write32(window+4, Bits(y));
    notifications = 0;
    lifted::Context c{};
    c.r[4] = camera; c.r[5] = window; c.r[29] = stack; c.r[31] = 0x12345678;
    c.r[16] = 0x1122334455667788ull; c.rh[16] = 0x8877665544332211ull;
    c.budget = 10000;
    return c;
}
std::vector<u8> CameraAndStack() {
    auto& m = ps2host::GuestMemory();
    std::vector<u8> bytes(m.Translate(camera,256), m.Translate(camera,256)+256);
    bytes.insert(bytes.end(), m.Translate(stack-64,128), m.Translate(stack-64,128)+128);
    return bytes;
}
void OriginalEquivalent(const char* aspect, float x, float y) {
    ps2host::SetSetting("aspect", aspect);
    auto direct = Prepare(x,y); F_001E2100(direct);
    const auto expected = CameraAndStack();
    Check(notifications == 1, "Direct original did not notify once");
    auto hooked = Prepare(x,y); lifted::Call(hooked, 0x001E2100);
    Check(notifications == 1, "Hook did not invoke original exactly once");
    Check(std::memcmp(&direct,&hooked,sizeof direct)==0, "Pass-through register context changed");
    Check(expected == CameraAndStack(), "Pass-through camera/stack memory changed");
    Check(generated::ReadU32(window)==Bits(x) && generated::ReadU32(window+4)==Bits(y), "Pass-through input changed");
    ++cases;
}
void Horizontal(float x) {
    const float y=x*0.75f;
    ps2host::SetSetting("aspect","16:9");
    Near(ps2host::PictureAspect(),16.0f/9.0f,"Shared aspect setting did not reach hook");
    // Match USA's two float operations, not a rounded literal 4/3: those
    // differ by one ULP for 16:9. Check the mathematical factor as well.
    const float wide=x*(ps2host::PictureAspect()/(4.0f/3.0f));
    Near(wide,x*(4.0f/3.0f),"Horizontal factor is not 4/3");
    auto c=Prepare(x,y); const auto before=c; lifted::Call(c,0x001E2100);
    Check(notifications==1,"Widening recursed or skipped original notification");
    Check(observed_x==Bits(wide) && observed_y==Bits(y),"Original did not see horizontal-only window");
    Check(generated::ReadU32(camera+0x68)==Bits(wide),"Real original did not install wide x");
    Check(generated::ReadU32(camera+0x6C)==Bits(y),"Vertical window changed");
    Check(generated::ReadU32(camera+0x70)==generated::FloatDiv(Bits(1.0f),Bits(wide)),"Real inverse x wrong");
    Check(generated::ReadU32(camera+0x74)==generated::FloatDiv(Bits(1.0f),Bits(y)),"Real inverse y wrong");
    Check(generated::ReadU32(window)==Bits(x) && generated::ReadU32(window+4)==Bits(y),"Caller input not restored");
    Check(c.r[2]==camera && c.r[29]==before.r[29] && c.r[31]==before.r[31] && c.r[16]==before.r[16] && c.rh[16]==before.rh[16],"Original ABI return/preserved registers changed");
    // Reuse the same caller-owned vec2: expansion must not accumulate.
    c=before; notifications=0; lifted::Call(c,0x001E2100);
    Check(notifications==1 && generated::ReadU32(camera+0x68)==Bits(wide),"Repeated call compounded factor");
    ++cases;
}
} // namespace

int main(int argc,char** argv) {
    try {
        namespace fs=std::filesystem;
        Check(argc==2,"Pass an exclusive synthetic test directory");
        const auto dir=fs::u8path(argv[1]); Check(!fs::exists(dir),"Do not overwrite earlier tests");
        fs::create_directories(dir);
        ps2host::LoadSettings((dir/"pc_settings.ini").u8string());
        ps2host::SetSetting("sentinel","preserve");
        ps2host::RegisterFunctions(kGuestFunctions,kGuestFunctionCount);
        RegisterGameOverrides();
        ps2host::Override(0x001E3FC0,Notification,"synthetic frame notification spy");
        Check(std::strstr(ps2host::FunctionName(0x001E2100),"horizontal widescreen")!=nullptr,"Game registration missing camera hook");
        OriginalEquivalent("4:3",1.0f,.75f);
        OriginalEquivalent("4:3",.5f,.375f);
        Horizontal(1.0f); Horizontal(.5f); Horizontal(2.0f);
        OriginalEquivalent("16:9",4.0f/3.0f,.75f); // already wide
        OriginalEquivalent("16:9",1.0f,1.0f); // square/offscreen
        OriginalEquivalent("16:9",-1.0f,-.75f);
        OriginalEquivalent("16:9",0.0f,.75f);
        OriginalEquivalent("16:9",1.0f,std::numeric_limits<float>::quiet_NaN());
        OriginalEquivalent("16:9",std::numeric_limits<float>::infinity(),.75f);
        OriginalEquivalent("4:3",1.0f,.75f); // return after 16:9
        // Original callback throws: restore the shared caller vector on unwind.
        ps2host::SetSetting("aspect","16:9"); auto c=Prepare(1.0f,.75f);
        fail_notification=true;bool threw=false;
        try { lifted::Call(c,0x001E2100); } catch(const std::runtime_error&) { threw=true; }
        fail_notification=false;
        Check(threw && notifications==1,"Exception did not originate in original callback");
        Check(generated::ReadU32(window)==Bits(1.0f) && generated::ReadU32(window+4)==Bits(.75f),"Window not restored during exception");
        ++cases;
        // The exact setting consumed by PictureAspect also survives a reload.
        ps2host::LoadSettings((dir/"pc_settings.ini").u8string());
        Near(ps2host::PictureAspect(),16.0f/9.0f,"Aspect persistence failed");
        Check(ps2host::Setting("sentinel")=="preserve","Unrelated preference changed");++cases;
        std::printf("PAL camera tests: %u passed; real preserved F_001E2100, original frame callback spied; no ISO/gameplay.\n",cases);
        std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),0);return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"PAL camera test failed: %s\n",e.what());std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),1);return 1;
    }
}
