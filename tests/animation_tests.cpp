#include "lcs/animation.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
void check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
void word(Bytes& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(i*8)));}
Bytes chunk(const char* tag,Bytes payload){Bytes b(tag,tag+4);word(b,uint32_t(payload.size()));b.insert(b.end(),payload.begin(),payload.end());while(b.size()%4)b.push_back(0);return b;}
void append(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
void rejects(const Bytes& b,const char* message){bool rejected=false;try{lcs::AnimationArchive::parse_anpk(b);}catch(...){rejected=true;}check(rejected,message);}
int main(){try {
    Bytes info;word(info,1);info.insert(info.end(),{'h','o','m','s','w','e','t',0});
    Bytes body=chunk("INFO",info);append(body,chunk("NAME",{'c','s','t','o','n','i',0}));
    Bytes anim(28);std::memcpy(anim.data(),"Root",5);word(anim,1);word(anim,0);word(anim,0);word(anim,5);
    Bytes cpan=chunk("ANIM",anim),frames;
    for(float f:{0.f,0.f,0.f,1.f,2.f,3.f,4.f,.5f}){uint32_t bits;std::memcpy(&bits,&f,4);word(frames,bits);}
    append(cpan,chunk("KRT0",frames));
    Bytes dgan_info;word(dgan_info,1);word(dgan_info,0);
    auto dgan=chunk("INFO",dgan_info);append(dgan,chunk("CPAN",cpan));append(body,chunk("DGAN",dgan));
    const auto file=chunk("ANPK",body);const auto result=lcs::AnimationArchive::parse_anpk(file);
    check(result.name=="homswet" && result.animations.size()==1 && result.animations[0].sequences[0].bone_tag==5 && result.animations[0].sequences[0].duration==.5f,"ANPK hierarchy, bone tag and time match independent fixture");
    auto damaged=file;damaged.pop_back();rejects(damaged,"truncated keyframe rejected");
    damaged=file;damaged[4]=0xff;rejects(damaged,"oversized parent rejected");
    damaged=file;const char* tag="ANIM";auto where=std::search(damaged.begin(),damaged.end(),tag,tag+4);
    const auto at=size_t(where-damaged.begin());damaged[at+8+28]=2;rejects(damaged,"keyframe count mismatch rejected");
    damaged=file;const char* frame_tag="KRT0";where=std::search(damaged.begin(),damaged.end(),frame_tag,frame_tag+4);
    const auto frame_at=size_t(where-damaged.begin());damaged[frame_at+8+2]=0xC0;damaged[frame_at+8+3]=0x7F;rejects(damaged,"NaN animation value rejected");
    damaged=file;damaged.push_back(1);rejects(damaged,"nonzero archive padding rejected");
    check(lcs::adapt_anpk(file).bytes==file,"valid original keyframe bytes stay unchanged");
    const auto aliased=lcs::adapt_anpk(file,{},{},"L0001");
    check(lcs::AnimationArchive::parse_anpk(aliased.bytes).name=="L0001" && aliased.bytes.size()==file.size(),"native block alias preserves all section sizes");
    check(std::equal(file.begin()+28,file.end(),aliased.bytes.begin()+28),"block alias leaves animation/sequence/keyframe records unchanged");
    bool oversized_alias=false;try{lcs::adapt_anpk(file,{},{},"TOO_LONG");}catch(...){oversized_alias=true;}
    check(oversized_alias,"alias cannot overrun original block name storage");
    const auto renamed=lcs::adapt_anpk(file,{{"Root","DummyRoot"}});
    check(renamed.renamed_sequences==1 && lcs::AnimationArchive::parse_anpk(renamed.bytes).animations[0].sequences[0].name=="DummyRoot","bounded explicit frame remapping changes only the named sequence");
    // Independent three-record fixture. Each rotation/translation belongs to
    // its own time; sorting timestamps alone would corrupt the animation.
    Bytes records;
    for(float marker:{30.f,20.f,21.f})for(float f:{0.f,0.f,0.f,1.f,marker,0.f,0.f,marker==30.f ? 3.f:2.f}){uint32_t bits;std::memcpy(&bits,&f,4);word(records,bits);}
    auto three_anim=anim;three_anim[28]=3;
    auto three_cpan=chunk("ANIM",three_anim);append(three_cpan,chunk("KRT0",records));
    auto three_dgan=chunk("INFO",dgan_info);append(three_dgan,chunk("CPAN",three_cpan));
    auto three_body=chunk("INFO",info);append(three_body,chunk("NAME",{'c','s','t','o','n','i',0}));append(three_body,chunk("DGAN",three_dgan));
    const auto unordered=chunk("ANPK",three_body);rejects(unordered,"negative native interpolation intervals are detected");
    const auto ordered=lcs::adapt_anpk(unordered);check(ordered.reordered_sequences==1,"unsorted original sequence is recorded");
    const auto sorted_where=std::search(ordered.bytes.begin(),ordered.bytes.end(),frame_tag,frame_tag+4);const auto sorted_at=size_t(sorted_where-ordered.bytes.begin())+8;
    float first_marker,second_marker,third_marker;
    std::memcpy(&first_marker,ordered.bytes.data()+sorted_at+16,4);std::memcpy(&second_marker,ordered.bytes.data()+sorted_at+32+16,4);std::memcpy(&third_marker,ordered.bytes.data()+sorted_at+64+16,4);
    check(first_marker==20 && second_marker==21 && third_marker==30,"complete records follow their times with stable equal-time order");
    check(lcs::AnimationArchive::parse_anpk(ordered.bytes).animations[0].sequences[0].duration==3.f,"duration comes from last chronological frame");
    check(lcs::adapt_anpk(ordered.bytes).bytes==ordered.bytes,"normalization is idempotent");
    auto named=file;
    std::fill(named.begin()+at+8+40,named.begin()+at+8+44,0xFF);
    const auto tagged=lcs::adapt_anpk(named,{},{{{"cstoni","Root"},2000}});
    check(tagged.tagged_sequences==1 && lcs::AnimationArchive::parse_anpk(tagged.bytes).animations[0].sequences[0].bone_tag==2000,"named skinned sequence is bound to its verified DFF bone tag");
    bool rejected=false;try{lcs::adapt_anpk(file,{},{{{"cstoni","Root"},2000}});}catch(...){rejected=true;}
    check(rejected,"existing original bone tag cannot be overwritten by remapping");
    rejected=false;try{lcs::adapt_anpk(named,{},{{{"missing","Root"},2000}});}catch(...){rejected=true;}
    check(rejected,"absent mapping cannot be reported as a successful native binding");
    std::cout<<"Bounded original ANPK format tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
