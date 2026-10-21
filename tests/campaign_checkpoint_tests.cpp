#include "lcs/campaign_checkpoint.hpp"
#include <iostream>
#include <functional>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include "lcs/sa_campaign_save.hpp"
#endif
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void rejects(const std::function<void()>& f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}check(refused,"invalid checkpoint accepted");}
#ifdef _WIN32
namespace {
// Test the production wrapper, replacing only the unavailable game writer.
struct SaveCall {
    lcs::CampaignCheckpoint state;
    bool admit=true,writer_ok=true,commit_ok=true;
    unsigned captures=0,writes=0,results=0;
    std::filesystem::path path;
};
SaveCall* current_save=nullptr;
bool __cdecl fixture_writer(int flags){
    check(current_save&&flags==0,"wrapper passed invalid writer flags");
    ++current_save->writes;if(!current_save->writer_ok)return false;
    std::ofstream file(current_save->path,std::ios::binary|std::ios::trunc);
    file<<"Authorial chained native-writer fixture";file.flush();return bool(file);
}
bool __cdecl capture(std::uint8_t slot,const char* path,void* context){
    auto& call=*static_cast<SaveCall*>(context);++call.captures;
    check(slot==0&&std::filesystem::path(path)==call.path,"wrapper changed slot/path");return call.admit;
}
bool __cdecl commit(std::uint8_t,const char* path,bool saved,void* context){
    auto& call=*static_cast<SaveCall*>(context);++call.results;
    if(!saved||!call.commit_ok)return false;
    const auto data=call.state.encode(lcs::native_save_digest(path));
    const auto target=lcs::campaign_checkpoint_path(path);auto temporary=target;temporary+=L".tmp";
    {std::ofstream file(temporary,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());file.flush();if(!file)return false;}
    return MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
void check_writer_wrapper(const std::filesystem::path& path,const lcs::CampaignCheckpoint& state){
    // Reserve data scratch at the retail path-buffer address. No game code,
    // executable, assets or process are loaded or executed by this test.
    struct Buffer {
        void* memory=VirtualAlloc(reinterpret_cast<void*>(0x00C10000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        ~Buffer(){lcs::sa_campaign_save_bind_callbacks({});lcs::sa_campaign_save_bind_native_targets(nullptr,nullptr);current_save=nullptr;if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    } buffer;
    check(buffer.memory==reinterpret_cast<void*>(0x00C10000),"test path-buffer address unavailable");
    const auto name=path.string();check(name.size()<260,"test path exceeds native buffer");
    std::memcpy(reinterpret_cast<void*>(lcs::SACampaignSaveProfile::save_path_buffer),name.c_str(),name.size()+1);
    SaveCall call;call.state=state;call.path=path;current_save=&call;
    lcs::SACampaignSaveCallbacks callbacks;callbacks.before_save=capture;callbacks.save_result=commit;callbacks.context=&call;
    lcs::sa_campaign_save_bind_callbacks(callbacks);lcs::sa_campaign_save_bind_native_targets(fixture_writer,nullptr);
    const auto invoke=[](){return lcs::sa_campaign_save_writer_call(0,0,lcs::SACampaignSaveProfile::save_slot_return,0);};
    const auto unchanged=lcs::native_save_digest(path);call.admit=false;
    check(!invoke()&&call.captures==1&&call.writes==0&&call.results==0,"denied capture must not call writer/result");
    check(lcs::native_save_digest(path)==unchanged,"denied capture modified the existing save");
    call.admit=true;call.writer_ok=false;
    check(!invoke()&&call.writes==1&&call.results==1,"native writer failure must reach result and report failure");
    check(!lcs::sa_campaign_save_last_native_save_succeeded()&&!lcs::sa_campaign_save_last_sidecar_commit_succeeded(),"writer failure published success");
    check(lcs::native_save_digest(path)==unchanged,"failed writer fixture modified the prior save");
    call.writer_ok=true;call.commit_ok=false;
    check(!invoke()&&lcs::sa_campaign_save_last_native_save_succeeded()&&!lcs::sa_campaign_save_last_sidecar_commit_succeeded(),"native-only success must not report paired save success");
    call.commit_ok=true;
    check(invoke()&&lcs::sa_campaign_save_last_native_save_succeeded()&&lcs::sa_campaign_save_last_sidecar_commit_succeeded(),"accepted capture did not reach writer and paired commit");
    const auto disk=lcs::read_campaign_checkpoint(path);
    check(disk.runtime==state.runtime&&disk.adapter==state.adapter&&disk.services==state.services&&disk.outfit==state.outfit,"production wrapper changed the paired captured state");
}
}
#endif
int main(){try{
    lcs::CampaignCheckpoint in;in.time=123456;in.outfit="plr3";in.runtime={1,2,3};in.adapter={4,5};in.services={6,7,8};
    const auto bytes=in.encode(0x123456789abcdef0ull);const auto out=lcs::CampaignCheckpoint::decode(bytes,0x123456789abcdef0ull);
    check(out.time==in.time && out.outfit==in.outfit && out.runtime==in.runtime && out.adapter==in.adapter && out.services==in.services,"checkpoint roundtrip changed state");
    rejects([&]{lcs::CampaignCheckpoint::decode(bytes,1);});
    for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=0x80;rejects([&]{lcs::CampaignCheckpoint::decode(bad,0x123456789abcdef0ull);});}
    for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad.resize(i);rejects([&]{lcs::CampaignCheckpoint::decode(bad,0x123456789abcdef0ull);});}
    auto invalid=in;invalid.runtime.clear();rejects([&]{lcs::CampaignCheckpoint::decode(invalid.encode(9),9);});
    // Exercise the actual file pairing/read path without creating a game save.
    struct Files {
        std::filesystem::path folder,native,companion,temporary;
        Files(){folder=std::filesystem::temp_directory_path()/("lcs-checkpoint-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(folder);native=folder/"GTASAsf1.b";companion=lcs::campaign_checkpoint_path(native);temporary=companion;temporary+=".tmp";}
        ~Files(){std::error_code error;for(const auto& p:{temporary,companion,native,folder})std::filesystem::remove(p,error);}
    } files;
    {std::ofstream native(files.native,std::ios::binary);native<<"Authorial native-writer fixture";native.flush();check(bool(native),"native file write failed");}
    const auto paired=in.encode(lcs::native_save_digest(files.native));
    {std::ofstream companion(files.temporary,std::ios::binary);companion.write(reinterpret_cast<const char*>(paired.data()),paired.size());companion.flush();check(bool(companion),"companion file write failed");}
    std::filesystem::rename(files.temporary,files.companion);
    const auto disk=lcs::read_campaign_checkpoint(files.native);
    check(disk.runtime==in.runtime&&disk.adapter==in.adapter&&disk.services==in.services&&disk.outfit==in.outfit,"committed save pair changed captured state");
    {std::ofstream native(files.native,std::ios::binary|std::ios::app);native<<"changed";}
    rejects([&]{(void)lcs::read_campaign_checkpoint(files.native);});
#ifdef _WIN32
    check_writer_wrapper(files.native,in);
#endif
    std::cout<<"PASS: checkpoint files and production save wrapper; capture denial, writer/commit failure and paired success verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
