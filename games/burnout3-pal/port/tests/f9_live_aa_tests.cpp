// Existing native AA forwarding test, with aggregate cold/warm operation timings.
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2gpu/display.h"
#include <volk.h>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
struct Metrics {unsigned pipelines=0,idles=0,images=0,dispatches=0;double pipeline_ms=0,idle_ms=0;} m;
PFN_vkCreateComputePipelines real_pipeline;PFN_vkDeviceWaitIdle real_idle;PFN_vkCreateImage real_image;PFN_vkCmdDispatch real_dispatch;
double Ms(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
VKAPI_ATTR VkResult VKAPI_CALL Pipeline(VkDevice d,VkPipelineCache c,uint32_t n,const VkComputePipelineCreateInfo* i,const VkAllocationCallbacks* a,VkPipeline* o){auto from=Clock::now();auto r=real_pipeline(d,c,n,i,a,o);m.pipeline_ms+=Ms(from);m.pipelines+=n;return r;}
VKAPI_ATTR VkResult VKAPI_CALL Idle(VkDevice d){auto from=Clock::now();auto r=real_idle(d);m.idle_ms+=Ms(from);++m.idles;return r;}
VKAPI_ATTR VkResult VKAPI_CALL Image(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* o){++m.images;return real_image(d,i,a,o);}
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z){++m.dispatches;real_dispatch(c,x,y,z);}
void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
void Show(unsigned k){static std::vector<u32> pixels;pixels.resize(size_t(640*k)*448*k);for(size_t i=0;i<pixels.size();++i)pixels[i]=(i/13%2)?0xffffffff:0xff000000;Require(ps2host::WindowDisplay()->Present(pixels.data(),640*k,448*k),"real presentation");}
void Print(const char* event,const char* mode,double request,double present,const Metrics& before){std::printf("phase=%s mode=%s request_ms=%.3f present_ms=%.3f pipelines=%u pipeline_ms=%.3f idles=%u idle_ms=%.3f images=%u dispatches=%u\n",event,mode,request,present,m.pipelines-before.pipelines,m.pipeline_ms-before.pipeline_ms,m.idles-before.idles,m.idle_ms-before.idle_ms,m.images-before.images,m.dispatches-before.dispatches);}
}
int main(int argc,char** argv){
 try{
  Require(argc>=3,"exclusive profile and baseline/prepared mode required");const bool prepared=std::string(argv[2])=="prepared";const bool reopen=std::string(argv[2])=="reopen-smaa";
  const auto folder=std::filesystem::u8path(argv[1]);if(!reopen)Require(std::filesystem::create_directories(folder),"exclusive fixture");auto ini=folder/"pc_settings.ini";
  if(!reopen)std::ofstream(ini)<<"antialiasing=off\nvsync=off\nfps_counter=off\ncustom=keep\n";
  ps2host::LoadSettings(ini.u8string());auto from=Clock::now();Require(ps2host::OpenWindow(L"PAL F9 live AA timing",640,448),"native window");
  std::printf("window_init_ms=%.3f\n",Ms(from));
  if(argc>3){ps2host::RunSetting("gpu_thread","on");std::string error;Require(ps2host::StartGpuRenderer(&error,ps2host::GpuMode::Primary,false),error.c_str());Require(ps2host::EffectiveSetting("gpu_thread")=="on","real GPU wrapper selected");}
  real_pipeline=vkCreateComputePipelines;real_idle=vkDeviceWaitIdle;real_image=vkCreateImage;real_dispatch=vkCmdDispatch;
  vkCreateComputePipelines=Pipeline;vkDeviceWaitIdle=Idle;vkCreateImage=Image;vkCmdDispatch=Dispatch;
  if(reopen){
   Require(ps2host::Setting("antialiasing")=="smaa","actual saved SMAA input");Show(2);
   Require(m.dispatches==3 && m.pipelines==0,"SMAA restored and rendered without compiling pipelines");
   Require(m.idles==1,"only the existing CPU-picture first-allocation wait remains; no AA replacement wait");
   std::printf("SMAA new-process persistence: PASS; 3 dispatches, 0 live pipelines, 1 existing CPU-picture initial allocation wait\n");
   ps2host::ReleaseController();std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),0);return 0;
  }
  Show(2);int row=-1;auto rows=ps2host::PcSettings();for(size_t n=0;n<rows.size();++n)if(rows[n].label=="ANTIALIASING")row=int(n);Require(row>=0,"AA row");
  auto before=m;from=Clock::now();ps2host::OpenPcMenu();double open=Ms(from);from=Clock::now();Show(2);Print("menu_open","off",open,Ms(from),before);
  before=m;from=Clock::now();ps2host::SetWindowOverlay({});double close=Ms(from);from=Clock::now();Show(2);Print("overlay_close","off",close,Ms(from),before);
  const char* modes[]={"fxaa","smaa","off","fxaa","smaa","off"};
  for(unsigned n=0;n<6;++n){
   before=m;from=Clock::now();ps2host::StepPcSetting(row,1);double request=Ms(from);Require(ps2host::Setting("antialiasing")==modes[n],"selection order");
   from=Clock::now();Show(2);double elapsed=Ms(from);Print(n<3?"first":"repeat",modes[n],request,elapsed,before);
   unsigned expected=n%3==0?1:n%3==1?3:0;Require(m.dispatches-before.dispatches==expected,"effective filter dispatches");
   if(prepared){Require(m.pipelines==before.pipelines,"no pipeline compilation during selection");Require(m.idles==before.idles,"no device-wide idle on fixed-size selection");}
   if(n>=3){Require(m.images==before.images,"warm filter resources reused");Require(m.pipelines==before.pipelines,"warm pipelines reused");}
   ps2host::LoadSettings(ini.u8string());Require(ps2host::Setting("antialiasing")==modes[n] && ps2host::Setting("custom")=="keep","persisted mode and unrelated key");
  }
  if(prepared){ // Shared post-processing consumers after a real size change.
   ps2host::StepPcSetting(row,1);Show(3);ps2host::StepPcSetting(row,1);Show(3);Show(2);Require(!ps2host::WindowClosed(),"resize keeps post resources valid");
   ps2host::RunSetting("antialiasing","fxaa");ps2host::SetWindowAntiAliasing("off");auto d=m.dispatches;Show(2);Require(m.dispatches-d==1,"CLI reaches actual display");
   Require(ps2host::PcSettings()[row].value.find("[CLI]")!=std::string::npos,"CLI row lock");
   ps2host::SetSetting("custom","saved");ps2host::LoadSettings(ini.u8string());Require(ps2host::Setting("antialiasing")=="smaa","CLI did not replace saved SMAA");
  }
  if(prepared && ps2host::OpenAudio()) {
   std::vector<short> silence(4800*2,0);ps2host::AudioFrames(silence.data(),4800);
   const auto a=ps2host::GetAudioTimingSnapshot();std::this_thread::sleep_for(std::chrono::milliseconds(40));
   const auto b=ps2host::GetAudioTimingSnapshot();
   Require(a.open && b.written==a.written && b.read>a.read,"snapshot observes independent WASAPI consumption without changing the queue");
   std::printf("Audio observation only: written=%llu read_before=%llu read_after=%llu; no recovery policy tested\n",b.written,a.read,b.read);
   ps2host::CloseAudio();
  }
  std::printf("Live AA targeted %s: PASS\n",prepared?"prepared":"baseline");
  vkCreateComputePipelines=real_pipeline;vkDeviceWaitIdle=real_idle;vkCreateImage=real_image;vkCmdDispatch=real_dispatch;
  ps2host::ReleaseController();std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),0);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"Live AA test failed: %s\n",e.what());std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),1);return 1;}
}
