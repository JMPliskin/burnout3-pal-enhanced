// Synthetic GS packets exercise the adopted USA 2D code directly. Only draw/flush
// sinks are spies: this is not a dynamic equivalence test of the game functions.
#define F_0021A4E0 HudFlushSpy
#define F_00364B00 HudSpriteSpy
#include "../widescreen_hud.cpp"
#undef F_0021A4E0
#undef F_00364B00
#include <filesystem>
#include <stdexcept>
#include <windows.h>
#include "ps2host/memory.h"

namespace {
unsigned tests=0,flush_calls=0,sprite_calls=0;
float sprite_seen=0;
constexpr u32 mem=0x10000,gp=0x20000,verts=0x30000;
u32 Bits(float x){u32 b;std::memcpy(&b,&x,4);return b;}
float Float(u32 b){float x;std::memcpy(&x,&b,4);return x;}
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Near(float x,float y){Check(std::fabs(x-y)<.0001f,"Unexpected transformed coordinate");}
void Reset(){g_batch=Batch2D{};g_pieces.clear();g_building.clear();g_anchors.clear();std::memset(ps2host::GuestMemory().Translate(mem,0x400),0,0x400);}
void W64(u32 at,u64 x){generated::Write32(at,u32(x));generated::Write32(at+4,u32(x>>32));}
void Xyz(u32 at,int x,int y){generated::Write32(at,u32(x)|(u32(y)<<16));}
u32 Tag(u32 at,u32 flg,u32 loops,u32 regs,u64 desc,u32 prim=6){W64(at,u64(loops)|(u64(1)<<46)|(u64(prim)<<47)|(u64(flg)<<58)|(u64(regs)<<60));W64(at+8,desc);return at+16;}
ElementId Id(u32 context,u32 object,const char* cell){ElementId x;x.context=context;x.object=object;std::memcpy(x.cell,cell,3);return x;}
void Apply(u32 begin,u32 end,float k){std::vector<Vertex2D> all;for(u32 at=begin;at+16<=end && end-at<0x10000;)at=TagVertices(at,end,&all,k);std::vector<Vertex2D> picture;for(auto v:all)if(v.picture)picture.push_back(v);if(k<1)PlaceVertices(picture,k);}
void Append(lifted::Context&){generated::Write32(gp-0x5D98,mem+48);}
void MidFlush(lifted::Context& c){generated::Write32(gp-0x5D98,mem+48);Flush2D(c);generated::Write32(gp-0x5D98,mem+32);}
}
void HudFlushSpy(lifted::Context& c){++flush_calls;generated::Write32(u32(c.r[28])-0x5D98,mem);c.r[2]=0x1234;}
void HudSpriteSpy(lifted::Context& c){++sprite_calls;sprite_seen=Float(generated::ReadU32(u32(c.r[4])));c.r[2]=0x4321;}
int main(int argc,char**argv){try{
 Check(argc==2,"Exclusive test data directory required");const auto dir=std::filesystem::u8path(argv[1]);Check(!std::filesystem::exists(dir),"Existing output rejected");std::filesystem::create_directories(dir);ps2host::LoadSettings((dir/"pc_settings.ini").u8string());
 for(auto a:{Anchor::kCentre,Anchor::kLeft,Anchor::kRight,Anchor::kStretch}){Near(Place(123,320,320,1,a),123);++tests;}
 Near(Place(100,320,320,.75f,Anchor::kLeft),75);++tests;
 Near(Place(540,320,320,.75f,Anchor::kRight),565);++tests;
 Near(Place(100,320,320,.75f,Anchor::kCentre),155);++tests;
 Near(Place(100,320,320,.75f,Anchor::kStretch),100);++tests;
 Check(AnchorOf(Id(0x1A0780,0x381380,"BR"))==Anchor::kRight,"Speed anchor mapping");Check(AnchorOf(Id(0x1D75B0,0x1A8170,"BL"))==Anchor::kLeft,"Boost anchor mapping");Check(AnchorOf(Id(0x1A0780,0x3D7EA0,"BC"))==Anchor::kStretch,"Overlay stretch mapping");++tests;
 for(auto aspect:{"4:3","16:9","4:3"}){ps2host::SetSetting("aspect",aspect);Near(Squeeze(),std::string(aspect)=="16:9"?.75f:1.f);}++tests;
 ps2host::SetSetting("aspect","auto");Near(Squeeze(),1); // No host window: documented fallback, not a window-aspect test.
 ps2host::SetSetting("aspect","21:9");Near(Squeeze(),4.f/7.f);++tests;
 // Packed XYZ values: preserve y/z/colour; full backdrop spans retain edges.
 Reset();Tag(mem,0,2,1,5);Xyz(mem+16,0x8000-1600,0x8000);Xyz(mem+32,0x8000-800,0x8100);generated::Write32(mem+20,0xAABBCCDD);
 Apply(mem,mem+48,.75f);Check((generated::ReadU32(mem+16)&65535)==0x8000-1200,"Packed x");Check((generated::ReadU32(mem+16)>>16)==0x8000 && generated::ReadU32(mem+20)==0xAABBCCDD,"y/z altered");++tests;
 Reset();Tag(mem,0,2,1,5);Xyz(mem+16,0x8000-5120,0x8000);Xyz(mem+32,0x8000+5120,0x8000);Apply(mem,mem+48,.75f);Check((generated::ReadU32(mem+16)&65535)==0x8000-5120 && (generated::ReadU32(mem+32)&65535)==0x8000+5120,"Backdrop edges");++tests;
 Reset();Tag(mem,1,2,1,5);Xyz(mem+16,0x8000-1600,0x8000);Xyz(mem+24,0x8000-800,0x8100);Apply(mem,mem+32,.75f);Check((generated::ReadU32(mem+16)&65535)==0x8000-1200,"Reglist x");++tests;
 Reset();Tag(mem,0,3,1,0xE);W64(mem+16,u64(1)<<16);generated::Write32(mem+24,0x4C);Xyz(mem+32,0x8000-1600,0x8000);generated::Write32(mem+40,5);Xyz(mem+48,0x8000-800,0x8000);generated::Write32(mem+56,5);Apply(mem,mem+64,.75f);Check((generated::ReadU32(mem+32)&65535)==0x8000-1600,"Offscreen target transformed");++tests;
 Reset();W64(mem,u64(100)|(u64(300)<<16)|(u64(447)<<48));const auto old=Read64(mem);SqueezeScissor(mem,.75f);Check((Read64(mem)&0x7ff)==155 && ((Read64(mem)>>16)&0x7ff)==305 && (Read64(mem)>>32)==(old>>32),"Narrow scissor");++tests;
 W64(mem,u64(639)<<16);SqueezeScissor(mem,.75f);Check(Read64(mem)==(u64(639)<<16),"Full scissor");++tests;
 Reset();Tag(mem,0,2,1,5);Xyz(mem+16,0x8000-3520,0x8B00);Xyz(mem+32,0x8000-2720,0x8B00);g_pieces.push_back({mem,mem+48,Id(0x1D75B0,0x1A8170,"BL")});Apply(mem,mem+48,.75f);Check((generated::ReadU32(mem+16)&65535)==0x8000-3920,"Left anchored packet");++tests;
 // Exact 4:3 packet contents unchanged, including narrow scissor.
 Reset();Tag(mem,0,1,1,0xE);W64(mem+16,u64(100)|(u64(300)<<16));generated::Write32(mem+24,0x40);std::vector<u8> before(ps2host::GuestMemory().Translate(mem,32),ps2host::GuestMemory().Translate(mem,32)+32);Apply(mem,mem+32,1);Check(!std::memcmp(before.data(),ps2host::GuestMemory().Translate(mem,32),32),"4:3 packet modified");++tests;
 // Persistent sprite inputs restored after each draw; original draw sink invoked once.
 for(auto aspect:{"4:3","16:9","16:9","4:3"}){ps2host::SetSetting("aspect",aspect);generated::Write32(verts,Bits(100));generated::Write32(verts+4,Bits(20));lifted::Context c{};c.r[4]=verts;c.r[5]=1;sprite_calls=0;Sprites2D(c);Near(sprite_seen,std::string(aspect)=="16:9"?75:100);Check(sprite_calls==1 && c.r[2]==0x4321 && generated::ReadU32(verts)==Bits(100)&&generated::ReadU32(verts+4)==Bits(20),"Sprite restore/original sink");++tests;}
 ps2host::SetSetting("aspect","16:9");{lifted::Context c{};c.r[4]=verts;c.r[5]=0;sprite_calls=0;Sprites2D(c);Check(sprite_calls==1,"Empty sprite original sink");++tests;}
 Reset();generated::Write32(gp-0x5D98,mem);lifted::Context c{};c.r[28]=gp;Builder<Append>(c);Check(g_pieces.size()==1 && g_pieces[0].begin==mem && g_pieces[0].end==mem+48,"Builder tracking");++tests;
 Reset();generated::Write32(gp-0x6D20,mem);generated::Write32(gp-0x5D98,mem);flush_calls=0;Builder<MidFlush>(c);Check(flush_calls==1 && g_building.empty() && g_pieces.size()==1 && g_pieces[0].begin==mem && g_pieces[0].end==mem+32 && c.r[2]==0x1234,"Mid-builder flush tracking");++tests;
 char cell[3];CellOf(10,430,cell);Check(std::string(cell)=="BL","Cell classification");++tests;
 std::printf("PAL HUD: %u synthetic regression cases passed. Real adopted transforms; draw/flush sinks spied. No ISO/gameplay.\n",tests);std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),0);return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"HUD regression failure: %s\n",e.what());std::fflush(nullptr);TerminateProcess(GetCurrentProcess(),1);return 1;}}
