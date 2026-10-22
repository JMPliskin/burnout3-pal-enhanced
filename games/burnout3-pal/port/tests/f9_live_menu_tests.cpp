// Only the changed F9/live-AA contract, reusing the existing input fixtures.
#define main UnusedFullSettingsSuite
#include "../../../../engine/host/tests/pc_settings_tests.cpp"
#undef main
int main(int argc,char** argv){
 using namespace ps2host;if(argc!=2)return 2;const auto ini=std::filesystem::u8path(argv[1]);
 if(!std::filesystem::create_directories(ini.parent_path()))return 3;
 File(ini,"antialiasing=off\ncustom=keep\n");LoadSettings(ini.u8string());const auto original=Read(ini);
 for(int n=0;n<12;++n){Key(VK_F9);Check(PcMenuOpen(),"F9 opens");Key(VK_F9);Check(!PcMenuOpen() && overlay.empty(),"F9 closes and releases input");}
 Check(Read(ini)==original,"F9-only does not write INI");
 const int aa=Row("ANTIALIASING");for(const char* mode:{"fxaa","smaa","off","fxaa","smaa","off"}){StepPcSetting(aa,1);Check(Setting("antialiasing")==mode,"live choice cycles");Check(Value("ANTIALIASING").find("Restart")==std::string::npos,"AA does not require restart");}
 StepPcSetting(aa,2);LoadSettings(ini.u8string());Check(Value("ANTIALIASING")=="SMAA" && Setting("custom")=="keep","SMAA persistence");
 RunSetting("antialiasing","fxaa");StepPcSetting(aa,1);Check(Value("ANTIALIASING")=="FXAA [CLI]" && SavedSetting("antialiasing")=="smaa","CLI lock leaves preference intact");
 std::printf("Focused live F9 tests: %s (%d checks)\n",failures?"FAIL":"PASS",checks);return failures?1:0;
}
