// Synthetic F9 -> real host setter -> real Vulkan presentation; no game assets.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <volk.h>
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include <windows.h>
namespace {
std::atomic<unsigned> dispatches{0},presents{0},failures{0};
PFN_vkCmdDispatch original_dispatch;
PFN_vkQueuePresentKHR original_present;
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z){++dispatches;original_dispatch(c,x,y,z);}
VKAPI_ATTR VkResult VKAPI_CALL Present(VkQueue q,const VkPresentInfoKHR* p){++presents;auto r=original_present(q,p);if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR)++failures;return r;}
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 try{
  Require(argc==2,"Pass an exclusive temporary directory");
  auto root=std::filesystem::u8path(argv[1]);Require(!std::filesystem::exists(root),"Do not overwrite a prior test");std::filesystem::create_directories(root);
  auto ini=root/"pc_settings.ini";ps2host::LoadSettings(ini.u8string());
  ps2host::SetSetting("aspect","16:9");ps2host::SetSetting("frame_pacing","on");ps2host::SetSetting("vsync","off");ps2host::SetSetting("sentinel","keep");
  std::ofstream(root/"synthetic-card.txt")<<"unchanged";
  auto choices=ps2host::PcSettings();int row=-1;
  for(int i=0;i<int(choices.size());++i)if(choices[i].label=="ANTIALIASING")row=i;
  Require(row>=0 && choices[row].value=="OFF","Missing F9 AA row/default Off");
  Require(ps2host::Setting("antialiasing").empty(),"Reading F9 reset an absent preference");
  Require(ps2host::OpenWindow(L"PAL F9 AA synthetic test",1280,896),"Window failed");
  ps2host::OpenPcMenu();Require(ps2host::PcMenuOpen(),"F9 panel not open");
  original_dispatch=vkCmdDispatch;original_present=vkQueuePresentKHR;vkCmdDispatch=Dispatch;vkQueuePresentKHR=Present;
  unsigned scale=2;std::vector<uint32_t> pixels;
  ps2host::SetPictureSource([&](std::vector<uint32_t>* out,uint32_t* w,uint32_t* h){*w=640*scale;*h=448*scale;*out=pixels;return true;});
  const char* modes[]={"off","fxaa","smaa","off"};const unsigned expected[]={0,1,3,0};unsigned tests=0;
  for(unsigned k:{2u,3u,2u}){
   scale=k;pixels.resize(size_t(640*k)*448*k);
   for(size_t i=0;i<pixels.size();++i)pixels[i]=(i/13%2)?0xffffffffu:0xff000000u;
   ps2host::SetSetting("resolution",k==2?"2x":"3x");
   for(unsigned n=0;n<4;++n){
    if(n)ps2host::StepPcSetting(row,1);
    Require(ps2host::Setting("antialiasing","off")==modes[n],"Selector order incorrect");
    auto before=dispatches.load(),p=presents.load();ps2host::PresentFrame();
    Require(!ps2host::WindowClosed(),"Window closed/presentation failed");
    unsigned count=dispatches.load()-before;Require(count==expected[n],"F9 change did not reach real shader pipeline");
    Require(presents.load()>p && !failures.load(),"No successful Vulkan presentation");
    ps2host::LoadSettings(ini.u8string());Require(ps2host::Setting("antialiasing","off")==modes[n],"AA did not survive reload");
    Require(ps2host::Setting("resolution")== (k==2?"2x":"3x") && ps2host::Setting("aspect")=="16:9" && ps2host::Setting("frame_pacing")=="on" && ps2host::Setting("vsync")=="off" && ps2host::Setting("sentinel")=="keep","Other settings changed");
    std::printf("scale=%u AA=%s dispatches=%u presents=%u persisted=yes\n",k,modes[n],count,presents.load()-p);++tests;
   }
  }
  ps2host::RunSetting("antialiasing","smaa");
  ps2host::StepPcSetting(row,1);
  Require(ps2host::Setting("antialiasing")=="smaa" && ps2host::PcSettings()[row].value.find("[CLI]")!=std::string::npos,"CLI AA must be locked and labelled");
  ps2host::SetWindowAntiAliasing("fxaa");
  auto d=dispatches.load(); ps2host::PresentFrame();
  Require(dispatches.load()-d==3,"CLI must win in real display setter");
  ps2host::SetSetting("sentinel","still-kept");
  ps2host::LoadSettings(ini.u8string());
  Require(ps2host::Setting("antialiasing")=="off","CLI override must not leak into saved preferences");
  Require(ps2host::Setting("sentinel")=="still-kept","unrelated preference saved with override active");
  std::ifstream card(root/"synthetic-card.txt");std::string value;card>>value;Require(value=="unchanged","Card changed");
  ps2host::SetPictureSource({});vkCmdDispatch=original_dispatch;vkQueuePresentKHR=original_present;
  std::printf("F9 AA tests: %u passed; default, hot switching, Off, persistence and unrelated settings/card preserved\n",tests);
  // Same host-window lifetime policy as windows_main.cpp: finish reporting,
  // release input, then let the OS release the Vulkan device at process exit.
  // A prior diagnostic run timed out after storing the final preference;
  // this test follows the player's established lifetime policy instead.
  ps2host::ReleaseController();std::fflush(nullptr);
  TerminateProcess(GetCurrentProcess(),0);
  return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"F9 AA test failed: %s\n",e.what());std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),1);return 1;}
}
