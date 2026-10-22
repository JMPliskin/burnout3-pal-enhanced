// Targeted real SDL/Vulkan input and fullscreen tests. No guest or GS capture runs.
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2gpu/display.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace {int checks=0;void Check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
void Show(){static std::vector<unsigned> pixels(1280*896,0xff537a91);Check(ps2host::WindowDisplay()->Present(pixels.data(),1280,896),"native render");}
int Row(const char* label){auto r=ps2host::PcSettings();for(unsigned n=0;n<r.size();++n)if(r[n].label==label)return int(n);throw std::runtime_error(label);}
}
int main(int argc,char** argv){try{using namespace ps2host;Check(argc==3,"profile and mode");auto dir=std::filesystem::u8path(argv[1]);std::string mode=argv[2];auto ini=dir/"pc_settings.ini";
 if(mode!="reopen"){Check(std::filesystem::create_directories(dir),"exclusive fixture");if(mode=="existing")std::ofstream(ini)<<"fullscreen=off\ncustom=keep\n";if(mode=="cli")std::ofstream(ini)<<"fullscreen=on\ncustom=keep\n";}
 LoadSettings(ini.u8string());SetFullscreenDefault(true);if(mode=="cli")RunSetting("fullscreen","off");
 Check(OpenWindow(L"F9 controls/fullscreen focused test",640,448),"open native window");auto* display=WindowDisplay();Show();Check(WindowFullscreen()==(mode=="new"),"new default / saved off / CLI off / restart state");
 if(mode=="cli"){SetWindowFullscreen(true);Check(!WindowFullscreen() && SavedSetting("fullscreen")=="on","CLI window wins without overwriting saved on");}
 if(mode=="new"){
  StepPcSetting(Row("FULLSCREEN"),1);Check(!WindowFullscreen() && Setting("fullscreen")=="off","F9 live window persists");Show();
  StepPcSetting(Row("FULLSCREEN"),1);Check(WindowFullscreen(),"F9 live fullscreen");Show();StepPcSetting(Row("FULLSCREEN"),1);Show();
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.name="PAL synthetic controls";
  auto id=SDL_AttachVirtualJoystick(&desc);Check(id!=0,"attach virtual gamepad");auto joy=SDL_OpenJoystick(id);Check(joy!=nullptr,"open virtual joystick");for(int n=0;n<10;++n){display->Pump();SDL_Delay(10);}Check(SDL_IsGamepad(id),"virtual gamepad mapped");
  auto buttons=[&](bool on){Check(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_LEFT_STICK,on),"virtual L3");Check(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_RIGHT_STICK,on),"virtual R3");SDL_UpdateJoysticks();Check(display->Pump(),"real event pump");ps2gpu::Display::PadState pad;Check(display->PadOf(id,&pad),"read virtual pad");Check(bool(pad.buttons&(1u<<1))==on && bool(pad.buttons&(1u<<2))==on,"stick state reaches display");Check(!display->TakeCapture(),"L3+R3 never requests capture");};
  for(int n=0;n<3;++n){buttons(true);for(int j=0;j<20;++j){display->Pump();Check(!display->TakeCapture(),"held combo no capture");}buttons(false);}
  SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_F12;e.key.repeat=true;Check(SDL_PushEvent(&e),"push repeated F12");display->Pump();Check(!display->TakeCapture(),"repeated F12 ignored");e.key.repeat=false;SDL_PushEvent(&e);display->Pump();Check(display->TakeCapture() && !display->TakeCapture(),"F12 capture latched once");e.key.scancode=SDL_SCANCODE_F9;SDL_PushEvent(&e);display->Pump();Check(!display->TakeCapture(),"keyboard F9 no capture");SDL_CloseJoystick(joy);SDL_DetachVirtualJoystick(id);display->Pump();
  // One representative live AA cycle only; pipeline/resource regression is reused.
  for(const char* aa:{"fxaa","smaa","off"}){SetWindowAntiAliasing(aa);Show();Check(Setting("antialiasing")==aa,"live AA request preserved");}
 }
 std::printf("Native controls/fullscreen %s: PASS (%d checks)\n",mode.c_str(),checks);ReleaseController();std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),0);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"Native controls failed: %s\n",e.what());std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),1);return 1;}}
