#include "lcs/text.hpp"
#include "lcs/audio.hpp"
#include "lcs/cutscene_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <chrono>
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){bool caught=false;try{fn();}catch(...){caught=true;}check(caught,message);}
void u32(std::vector<uint8_t>& b,uint32_t value){for(int i=0;i<4;++i)b.push_back(uint8_t(value>>(8*i)));}
void tag(std::vector<uint8_t>& b,const char* value){b.insert(b.end(),value,value+4);}
void name(std::vector<uint8_t>& b,const std::string& value){for(size_t i=0;i<8;++i)b.push_back(i<value.size()?uint8_t(value[i]):0);}
std::vector<uint8_t> duplicate_lcs_text() {
    std::vector<uint8_t> b;tag(b,"TABL");u32(b,12);name(b,"MAIN");u32(b,20);
    tag(b,"TKEY");u32(b,36);
    u32(b,0);name(b,"AAA");u32(b,12);name(b,"AAA");u32(b,26);name(b,"BBB");
    std::vector<uint8_t> data;for(const auto& s:{"first","second","label"}){for(const char* c=s;*c;++c){data.push_back(uint8_t(*c));data.push_back(0);}data.insert(data.end(),{0,0});}
    tag(b,"TDAT");u32(b,uint32_t(data.size()));b.insert(b.end(),data.begin(),data.end());return b;
}
void text_tests() {
    check(lcs::text_key("123456789")==0x340BC6D9,"native CRC matches independent standard vector before final XOR");
    const auto source=duplicate_lcs_text();auto archive=lcs::TextArchive::parse(source,true);
    check(archive.get("MAIN","AAA")=="second" && archive.get("MAIN","BBB")=="label","duplicate LCS label uses original binary-search row");
    archive.banks["VIC1"][lcs::text_key("VIC1_1")]="Drive to the marker";
    lcs::TextArchive native;native.banks["MAIN"][lcs::text_key("AAA")]="unrelated";native.banks["MAIN"][lcs::text_key("MENU") ]="New Game";
    archive.merge_native_frontend(native);
    check(archive.get("MAIN","AAA")=="second" && archive.get("MAIN","MENU")=="New Game","mission texts retain native menu-only keys without replacing LCS keys");
    const auto bytes=archive.native_bytes();
    check(lcs::read16(bytes,0)==4 && lcs::read16(bytes,2)==8 && lcs::read32(bytes,8)==24,"native SA header specifies version 4 and single-byte text");
    const auto main_offset=lcs::read32(bytes,20);
    check(std::string(bytes.begin()+main_offset,bytes.begin()+main_offset+4)=="TKEY","native MAIN offset points directly to TKEY");
    auto read=lcs::TextArchive::parse(bytes,false);check(read.banks==archive.banks,"all mission and frontend text values round trip");read.require_campaign(archive);
    read.banks.erase("VIC1");rejects([&]{read.require_campaign(archive);},"missing original mission bank fails mounting");
    auto malformed=source;malformed[24]=1;rejects([&]{lcs::TextArchive::parse(malformed,true);},"misaligned TKEY size is rejected");
    malformed=source;malformed.back()=1;rejects([&]{lcs::TextArchive::parse(malformed,true);},"unterminated UTF16 text is rejected");
    malformed=source;malformed.resize(19);rejects([&]{lcs::TextArchive::parse(malformed,true);},"truncated text bank directory is rejected");
}
void pcm_bounds() {
    std::vector<uint8_t> b;u32(b,4);u32(b,4);u32(b,2000);u32(b,0);u32(b,0xFFFFFFFF);
    check(lcs::AudioArchive::parse_index(b,8)[0].frequency==2000,"original low-frequency sound effects are valid PCM");
    rejects([&]{lcs::AudioArchive::parse_index(b,7);},"PCM sample cannot read beyond RAW archive");
    b[4]=3;rejects([&]{lcs::AudioArchive::parse_index(b,8);},"odd PCM16 sample size is rejected");
    b[4]=4;b[8]=0;b[9]=0;rejects([&]{lcs::AudioArchive::parse_index(b,8);},"nonempty PCM with zero frequency is rejected");
}
void stereo_cutscene() {
    std::vector<uint8_t> vb(4*0x2000);
    for(size_t offset=0;offset<0x2000;offset+=16) {
        vb[offset]=12;vb[0x2000+offset]=12;
        std::fill(vb.begin()+offset+2,vb.begin()+offset+16,0x11);
        std::fill(vb.begin()+0x2000+offset+2,vb.begin()+0x2000+offset+16,0xEE);
    }
    vb[0x2000-16]=0;vb[0x4000-16]=0;
    vb[0x4000]=0x1C;vb[0x6000]=0x1C;
    const auto decoded=lcs::decode_cutscene_vb(vb);
    lcs::Ps2MusicStream incremental(vb,0);
    auto music_first=incremental.next_block(),music_second=incremental.next_block();
    check(int16_t(lcs::read16(music_first.pcm,0))==1 && int16_t(lcs::read16(music_first.pcm,2))==-2 &&
          int16_t(lcs::read16(music_second.pcm,0))==3840 && int16_t(lcs::read16(music_second.pcm,2))==-7680 &&
          incremental.encoded_position()==0,
          "Music uses SPU2 signed integer residual/rounded prediction and carries saturated history across stereo blocks");
    auto seek_bytes=vb;seek_bytes.insert(seek_bytes.end(),vb.begin(),vb.begin()+0x4000);
    lcs::Ps2MusicStream seek_music(seek_bytes,0x4000);
    const auto sought_music=seek_music.next_block();
    check(lcs::read16(sought_music.pcm,0)==0 && lcs::read16(sought_music.pcm,2)==0,
          "Encoded music seek begins with fresh decoder history at an authored stereo block");
    std::vector<uint8_t> wrapping(0x8000);
    wrapping[0]=0x1C;wrapping[0x2000]=0x1C;
    for(size_t start:{size_t(0x5FF0),size_t(0x7FF0)})
        std::fill(wrapping.begin()+start+2,wrapping.begin()+start+16,0x11);
    lcs::Ps2MusicStream wrapping_music(wrapping,0);
    check(lcs::read16(wrapping_music.next_block().pcm,0)==0,"Initial music key-on history starts clear");
    wrapping_music.next_block();
    check(lcs::read16(wrapping_music.next_block().pcm,0)==3840,"File wrap retains predictor history instead of restarting a decoded PCM loop");
    auto flagged=wrapping;flagged[1]=7;flagged[0]=12;flagged[2]=0x11;
    lcs::Ps2MusicStream flagged_music(flagged,0);
    check(lcs::read16(flagged_music.next_block().pcm,0)==1,"ADPCM flag7 does not fabricate silence in music ring buffers");
    std::vector<uint8_t> clipping(0x8000);
    std::fill(clipping.begin()+2,clipping.begin()+16,0x77);
    clipping[16]=0x10;clipping[18]=0x87;
    lcs::Ps2MusicStream clipping_music(clipping,0);
    const auto clipped_music=clipping_music.next_block();
    check(int16_t(lcs::read16(clipped_music.pcm,28*4))==32767 &&
          int16_t(lcs::read16(clipped_music.pcm,29*4))==-2049,
          "SPU2 saturation is applied before predictor history; following negative nibble uses clipped32767");
    rejects([&]{lcs::Ps2MusicStream bad(vb,1);},"Unaligned encoded seek rejected");
    rejects([&]{lcs::Ps2MusicStream bad(vb,0x4000);},"Seek beyond retail two-block end margin rejected");
    check(lcs::Ps2MusicStream::retail_seek_offset(21954560,8640)==278528 &&
          lcs::Ps2MusicStream::retail_seek_offset(21954560,286720)==10452992 &&
          lcs::Ps2MusicStream::retail_seek_offset(21954560,509120)==18612224 &&
          lcs::Ps2MusicStream::retail_seek_offset(21954560,320200)==11698176 &&
          lcs::Ps2MusicStream::retail_seek_offset(21954560,672000)==0 &&
          lcs::Ps2MusicStream::retail_seek_offset(21954560,600000)==0,
          "Retail CITY positions use integer seconds, eighth interpolation, stereo block alignment and EOF margin");
    auto positioned=lcs::Ps2MusicStream::open_at_position(vb,1000000);
    check(positioned->encoded_position()==0 && positioned->next_block().pcm.size()==57344,
          "Out-of-range source music position resets to encoded start and reaches real decoder");
    auto city_positioned=lcs::Ps2MusicStream::open_source_city_track(vb,18,509120,100);
    check(city_positioned->encoded_position()==0 && city_positioned->next_block().pcm.size()==57344,
          "Real source CITY17/18 start consumer honors retail non-radio zero result, not reconstruction position jump");
    lcs::ContinuousMusicTrack strip{11,22,600320},club{33,44,600320};
    check(lcs::configure_continuous_music(41,255,18,67,strip,club)==lcs::ContinuousMusicConfiguration::playing_gated && club.position==33 && club.service_age==44,
          "Observed playing track gates configuration before state mutation");
    check(lcs::configure_continuous_music(41,255,67,18,strip,club)==lcs::ContinuousMusicConfiguration::next_gated && club.position==33,
          "Observed pending track gates configuration before mutation");
    check(lcs::configure_continuous_music(41,255,67,67,strip,club)==lcs::ContinuousMusicConfiguration::configured &&
          club.position==286847 && club.service_age==0 && strip.position==11,
          "Retail configuration uses real random low7 bits and resets only target service age");
    auto configured_city=lcs::Ps2MusicStream::open_source_city_track(vb,18,club.position,club.service_age);
    check(configured_city->encoded_position()==0 && configured_city->next_block().pcm.size()==57344,
          "Configured position state reaches actual retail non-radio start consumer and decoder");
    const auto path=std::filesystem::temp_directory_path()/("lcs-wave-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".wav");
    try {
        lcs::write_pcm_wave(path,decoded);const auto mounted=lcs::read_pcm_wave(path);
        check(mounted.frequency==decoded.frequency && mounted.channels==decoded.channels && mounted.pcm==decoded.pcm,"mounted PCM reader preserves every original stereo sample");
        {std::fstream broken(path,std::ios::binary|std::ios::in|std::ios::out);broken.seekp(40);broken.put(1);}
        rejects([&]{lcs::read_pcm_wave(path);},"mounted wave with false PCM length cannot reach the audio backend");
    }catch(...){std::filesystem::remove(path);throw;}
    std::filesystem::remove(path);
    auto signed_pcm=[&](size_t at){return int16_t(lcs::read16(decoded.pcm,at));};
    check(decoded.frequency==32000 && decoded.channels==2 && decoded.pcm.size()==vb.size()/16*28*2,"original cutscene stereo format and duration are retained");
    check(signed_pcm(0)==2 && signed_pcm(2)==-1 && signed_pcm(4)==2,"0x2000-byte channel blocks become interleaved PCM frames");
    check(signed_pcm(0x2000/16*28*4)==3841 && signed_pcm(0x2000/16*28*4+2)==-7679,"predictor state continues across channel blocks independently");
    vb[1]=7;const auto padded=lcs::decode_cutscene_vb(vb);
    check(lcs::read16(padded.pcm,0)==0 && lcs::read16(padded.pcm,4)==0 && int16_t(lcs::read16(padded.pcm,2))==-1,"padding is silent without muting the other stereo channel");
    vb[0]=0x50;rejects([&]{lcs::decode_cutscene_vb(vb);},"invalid predictor cannot index outside the coefficient table");
    vb[0]=12;vb[1]=8;rejects([&]{lcs::decode_cutscene_vb(vb);},"invalid ADPCM flags are rejected");
    vb.pop_back();rejects([&]{lcs::decode_cutscene_vb(vb);},"truncated channel group is rejected before decoding");
    rejects([&]{lcs::decode_cutscene_vb({});},"empty cutscene audio is rejected");
}
void original_resources(const std::filesystem::path& directory) {
    const auto text=lcs::TextArchive::load(directory/"TEXT"/"AMERICAN.GXT",true);
    check(text.get("MAIN","VIC1")=="Home Sweet Home","first story mission is VIC1, not a bike side activity");
    const auto native=lcs::TextArchive::parse(text.native_bytes(),false);native.require_campaign(text);
    lcs::AudioArchive archive(directory/"audio");
    const auto& door=archive.record(183);const auto& target=archive.record(290);
    check(door.loop_start==9240 && door.loop_end==17022 && door.size==17024 &&
        target.loop_start==5768 && target.loop_end==11310 && target.size==11312,
        "Mounted door/target retain authored nonzero loop offsets, never full-sample loop substitutes");
    check(archive.record(4).loop_end==-1 && archive.record(5607).frequency==12000,
        "Original loop sentinel and separate alarm frequency donor preserved without decoding unrelated PCM");
    rejects([&]{archive.record(uint32_t(archive.sample_count()));},"Metadata query must remain within actual sample table");
    const auto sample=archive.sample("vic1_da");
    check(sample.frequency==12000 && sample.pcm.size()==59752,"first mission's original PCM is addressed through the reLCS sample enum");
    lcs::AudioAdapter adapter(archive);bool done=false;int plays=0,stops=0;
    adapter.play=[&](size_t n,const lcs::AudioSample& s){check(n==0 && s.pcm==sample.pcm,"speech backend receives original PCM unchanged");++plays;return true;};
    adapter.stop=[&](size_t n){check(n==0,"LCS slot one translates to native backend zero");++stops;};adapter.finished=[&](size_t){return done;};
    std::vector<uint8_t> code(64);
    auto op=[&](uint16_t v){code.push_back(uint8_t(v));code.push_back(uint8_t(v>>8));};
    auto slot=[&]{code.insert(code.end(),{7,1});};
    op(COMMAND_LOAD_MISSION_AUDIO);slot();name(code,"VIC1_DA");
    op(COMMAND_HAS_MISSION_AUDIO_LOADED);slot();op(COMMAND_PLAY_MISSION_AUDIO);slot();
    op(COMMAND_HAS_MISSION_AUDIO_FINISHED);slot();op(COMMAND_HAS_MISSION_AUDIO_FINISHED);slot();
    op(COMMAND_CLEAR_MISSION_AUDIO);slot();op(COMMAND_HAS_MISSION_AUDIO_LOADED);slot();
    lcs::Vm vm(code,64,64);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t id){return adapter.dispatch(v,t,id);};lcs::Thread t;t.ip=64;
    check(vm.step(t,0)==lcs::StepResult::continued && stops==1,"loading an original speech slot resets previous playback");
    check(vm.step(t,0)==lcs::StepResult::continued && t.condition,"HAS_LOADED reflects actual sample availability");
    check(vm.step(t,0)==lcs::StepResult::continued && plays==1,"PLAY submits speech exactly once");
    check(vm.step(t,0)==lcs::StepResult::continued && !t.condition,"speech cannot finish merely because it was loaded");done=true;
    check(vm.step(t,0)==lcs::StepResult::continued && t.condition,"speech completion comes from the playback backend");
    check(vm.step(t,0)==lcs::StepResult::continued && stops==2,"CLEAR releases the original audio slot");
    check(vm.step(t,0)==lcs::StepResult::continued && !t.condition,"cleared speech is no longer reported loaded");
    rejects([&]{archive.sample("NO_SUCH");},"unknown mission audio labels do not select unrelated SA sounds");
}
void original_news(const std::filesystem::path& directory,const std::filesystem::path& source) {
    lcs::AudioArchive archive(directory/"audio");
    for(int32_t id=0;id<14;++id) {
        const auto path=source/(std::string("NEWS_")+char('A'+id)+".VB");
        std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"Every original PS2 news source must exist");
        const auto length=file.tellg();std::vector<uint8_t> bytes(static_cast<size_t>(length));file.seekg(0);
        check(bool(file.read(reinterpret_cast<char*>(bytes.data()),length)),"Original news source is complete");
        const auto original=lcs::decode_cutscene_vb(bytes),mounted=archive.announcement(id);
        check(mounted.pcm==original.pcm && mounted.frequency==original.frequency && mounted.channels==original.channels,"All 14 mounted broadcasts preserve every original stereo PCM sample");
    }
    rejects([&]{archive.announcement(-1);},"Negative news ID is rejected");
    rejects([&]{archive.announcement(14);},"News ID cannot select an unrelated SA sound");
    lcs::AudioAdapter adapter(archive);bool ended=false;size_t plays=0,stops=0;std::vector<uint8_t> expected=archive.announcement(0).pcm;
    adapter.play=[&](size_t slot,const lcs::AudioSample& sample){check(slot==2 && sample.pcm==expected,"Queued news uses its own playback slot and the selected original asset");++plays;return true;};
    adapter.stop=[&](size_t slot){check(slot==2,"News lifecycle cannot stop a mission dialogue slot");++stops;};
    adapter.finished=[&](size_t slot){check(slot==2,"News completion queries its own playback slot");return ended;};
    const auto queue=[&](int32_t id){std::vector<uint8_t> code(64);code.push_back(uint8_t(COMMAND_PLAY_ANNOUNCEMENT));code.push_back(uint8_t(COMMAND_PLAY_ANNOUNCEMENT>>8));code.push_back(6);u32(code,uint32_t(id));lcs::Vm vm(code,64,64);lcs::Thread t;t.ip=64;vm.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t op){return adapter.dispatch(v,th,op);};return vm.step(t,0);};
    check(queue(0)==lcs::StepResult::continued && plays==0,"Original script only queues news; it does not play on foot");
    check(adapter.tick_announcements(false) && plays==0,"Pending announcement waits for eligible radio playback");
    check(adapter.tick_announcements(true) && plays==1,"Entering eligible radio playback starts original news once");
    check(queue(1)==lcs::StepResult::continued && adapter.tick_announcements(true) && plays==1,"An in-progress broadcast cannot be overwritten by another script request");
    ended=true;check(adapter.tick_announcements(true) && stops==1 && plays==1,"Playback backend completion releases the news queue");
    ended=false;expected=archive.announcement(1).pcm;
    check(queue(1)==lcs::StepResult::continued && adapter.tick_announcements(true) && plays==2,"A later request plays its own original broadcast");
    check(adapter.tick_announcements(false) && stops==2 && adapter.tick_announcements(true) && plays==2,"Leaving the vehicle cancels an in-progress broadcast instead of restarting it later");
    adapter.play=[](size_t,const lcs::AudioSample&){return false;};
    check(queue(2)==lcs::StepResult::continued && !adapter.tick_announcements(true),"Failed news submission cannot be reported as successful playback");
}
int main(int argc,char** argv) {
    try{text_tests();pcm_bounds();stereo_cutscene();if(argc>=2)original_resources(argv[1]);if(argc==3)original_news(argv[1],argv[2]);
        std::cout<<"PASS: native mission texts, original label selection, PCM bounds and speech slot lifecycle; game/audio playback not executed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
