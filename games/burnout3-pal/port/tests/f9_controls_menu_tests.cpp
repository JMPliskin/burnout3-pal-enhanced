// Reuse the accepted menu's fake input fixtures. No renderer or guest simulation.
#define main UnusedFullSettingsSuite
#include "../../../../engine/host/tests/pc_settings_tests.cpp"
#undef main
int main(int argc,char** argv){using namespace ps2host;if(argc!=2)return 2;auto ini=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(ini.parent_path());File(ini,"antialiasing=smaa\nfullscreen=off\ncustom=keep\n");LoadSettings(ini.u8string());auto original=Read(ini);
 Key(VK_F9);Check(PcMenuOpen(),"keyboard F9 opens");Key(VK_F9);Check(!PcMenuOpen(),"keyboard F9 closes");
 for(int n=0;n<4;++n){pad.buttons=(1u<<1)|(1u<<2);UpdatePcMenu();Check(PcMenuOpen(),"combo opens on edge");for(int j=0;j<100;++j)UpdatePcMenu();Check(PcMenuOpen(),"held combo does not repeat");pad.buttons=0;UpdatePcMenu();pad.buttons=(1u<<1)|(1u<<2);UpdatePcMenu();Check(overlay.empty(),"release/repress closes visible panel");for(int j=0;j<100;++j)UpdatePcMenu();Check(overlay.empty(),"held combo keeps panel closed while input remains masked");pad.buttons=0;UpdatePcMenu();Check(!PcMenuOpen(),"release returns game input");}
 Check(Read(ini)==original,"menu-only preserves preferences");keys={VK_F12};UpdatePcMenu();keys.clear();UpdatePcMenu();Check(!PcMenuOpen(),"F12 does not toggle menu");
 for(const char* mode:{"off","fxaa","smaa"}){StepPcSetting(Row("ANTIALIASING"),1);Check(Setting("antialiasing")==mode && Value("ANTIALIASING").find("Restart")==std::string::npos,"AA still live");}LoadSettings(ini.u8string());Check(Setting("antialiasing")=="smaa","SMAA remains persisted");RunSetting("antialiasing","off");StepPcSetting(Row("ANTIALIASING"),1);Check(Value("ANTIALIASING")=="OFF [CLI]" && SavedSetting("antialiasing")=="smaa","AA CLI lock preserved");
 std::printf("Focused controls/menu: %s (%d checks)\n",failures?"FAIL":"PASS",checks);return failures?1:0;}
