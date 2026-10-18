// Synthetic startup tests: no guest code or original disc assets executed.
#include "../player_startup.h"
#include "ps2host/settings.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
 assert(argc==2);assert(GetACP()==CP_UTF8);auto root=std::filesystem::u8path(argv[1]);int tests=0;
 const std::string pal=(root/L"PAL espa\u00f1ol \u65e5.iso").u8string();
 const std::string wrong=(root/L"USA.iso").u8string();
 assert(pal_player::CompatibleDisc(pal));assert(!pal_player::CompatibleDisc(wrong));assert(!pal_player::CompatibleDisc((root/L"invalid.iso").u8string()));++tests;
 assert(pal_player::Utf8(pal_player::Wide(pal))==pal);++tests;
 int picks=0,rejects=0,writes=0;
 auto valid=[&](const std::string& s){return pal_player::CompatibleDisc(s);};
 auto pick=[&](){++picks;return pal;};auto reject=[&](const std::string&){++rejects;};auto persist=[&](const std::string& s){++writes;ps2host::SetSetting("disc",s);};
 auto ini=root/L"pc_settings.ini";ps2host::LoadSettings(ini.u8string());
 assert(pal_player::Select("",valid,pick,reject,persist)==pal && picks==1 && writes==1);++tests;
 ps2host::LoadSettings(ini.u8string());assert(ps2host::Setting("disc")==pal);++tests;
 picks=0;writes=0;assert(pal_player::Select(ps2host::Setting("disc"),valid,pick,reject,persist)==pal && picks==0 && writes==0);++tests;
 assert(pal_player::Select((root/L"renamed-away.iso").u8string(),valid,pick,reject,persist)==pal && picks==1);++tests;
 picks=0;rejects=0;assert(pal_player::Select(wrong,valid,pick,reject,persist)==pal && rejects==1 && picks==1);++tests;
 writes=0;assert(pal_player::Select("",valid,[]{return std::string();},reject,persist).empty() && writes==0);++tests;
 bool failed=false;try{pal_player::Select("",valid,pick,reject,[](const std::string&){throw std::runtime_error("access denied");});}catch(const std::exception&){failed=true;}assert(failed);++tests;
 auto card=root/L"memcard";std::filesystem::create_directory(card);std::ofstream(card/L"synthetic-save.txt")<<"keep";
 const auto run=pal_player::NewRun(root);pal_player::EnsureWritable(run);_putenv_s("PAL_RELOCATION_AUDIT_DIR",run.u8string().c_str());assert(std::getenv("PAL_RELOCATION_AUDIT_DIR"));std::ifstream in(card/L"synthetic-save.txt");std::string save;in>>save;assert(save=="keep");++tests;
 auto occupied=root/L"occupied";std::ofstream(occupied)<<"file";failed=false;try{pal_player::EnsureWritable(occupied);}catch(const std::exception&){failed=true;}assert(failed);++tests;
 std::cout<<"player startup tests: "<<tests<<" passed\n";
}
