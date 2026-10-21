#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace lcs {
// Private, versioned companion to a native SA save. No native address is a receipt.
struct CampaignSaveWriter {
    std::vector<uint8_t> data;
    void u32(uint32_t v){for(unsigned i=0;i<4;++i)data.push_back(uint8_t(v>>(i*8)));}
    void u64(uint64_t v){u32(uint32_t(v));u32(uint32_t(v>>32));}
    void bytes(const std::vector<uint8_t>& b){if(b.size()>32*1024*1024)throw std::runtime_error("Campaign save section too large");u32(uint32_t(b.size()));data.insert(data.end(),b.begin(),b.end());}
    void text(const std::string& s){if(s.size()>1024)throw std::runtime_error("Campaign save label too long");bytes({s.begin(),s.end()});}
    template<class T> void pod(const T& value){static_assert(std::is_trivially_copyable_v<T>);const auto* b=reinterpret_cast<const uint8_t*>(&value);data.insert(data.end(),b,b+sizeof(T));}
};
struct CampaignSaveReader {
    const std::vector<uint8_t>& data;size_t cursor=0;
    void require(size_t n)const{if(n>data.size()-cursor)throw std::runtime_error("Truncated campaign save");}
    uint32_t u32(){require(4);uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(data[cursor++])<<(8*i);return v;}
    uint64_t u64(){const auto low=u32();return low|(uint64_t(u32())<<32);}
    std::vector<uint8_t> bytes(){const auto n=u32();if(n>32*1024*1024)throw std::runtime_error("Campaign section exceeds limit");require(n);std::vector<uint8_t> r(data.begin()+cursor,data.begin()+cursor+n);cursor+=n;return r;}
    std::string text(){const auto b=bytes();if(b.size()>1024)throw std::runtime_error("Campaign label exceeds limit");return {b.begin(),b.end()};}
    template<class T> T pod(){static_assert(std::is_trivially_copyable_v<T>);require(sizeof(T));T v{};std::memcpy(&v,data.data()+cursor,sizeof(T));cursor+=sizeof(T);return v;}
    void end()const{if(cursor!=data.size())throw std::runtime_error("Trailing campaign save data");}
};
inline uint64_t campaign_save_digest(const uint8_t* bytes,size_t size,uint64_t hash=14695981039346656037ull){for(size_t i=0;i<size;++i){hash^=bytes[i];hash*=1099511628211ull;}return hash;}
inline uint64_t native_save_digest(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read native save for campaign pairing");
    std::array<uint8_t,65536> block{};uint64_t hash=14695981039346656037ull,total=0;
    while(file){file.read(reinterpret_cast<char*>(block.data()),block.size());const auto n=size_t(file.gcount());total+=n;if(total>64*1024*1024)throw std::runtime_error("Native save exceeds supported size");hash=campaign_save_digest(block.data(),n,hash);}
    if(!file.eof() || !total)throw std::runtime_error("Incomplete native save read");return hash;
}
struct CampaignCheckpoint {
    std::vector<uint8_t> runtime,adapter,services;
    std::string outfit;
    uint32_t time=0;
    std::vector<uint8_t> encode(uint64_t native_hash)const{
        CampaignSaveWriter w;w.u32(0x5343534c);w.u32(1);w.u64(native_hash);w.u32(time);w.text(outfit);w.bytes(runtime);w.bytes(adapter);w.bytes(services);
        w.u64(campaign_save_digest(w.data.data(),w.data.size()));return w.data;
    }
    static CampaignCheckpoint decode(const std::vector<uint8_t>& data,uint64_t native_hash){
        if(data.size()<8 || data.size()>64*1024*1024)throw std::runtime_error("Invalid campaign companion size");
        CampaignSaveReader checksum{data};checksum.cursor=data.size()-8;
        if(checksum.u64()!=campaign_save_digest(data.data(),data.size()-8))throw std::runtime_error("Campaign companion checksum mismatch");
        CampaignSaveReader r{data};if(r.u32()!=0x5343534c || r.u32()!=1)throw std::runtime_error("Unsupported campaign companion version");
        if(r.u64()!=native_hash)throw std::runtime_error("Campaign companion belongs to a different native save");
        CampaignCheckpoint s;s.time=r.u32();s.outfit=r.text();s.runtime=r.bytes();s.adapter=r.bytes();s.services=r.bytes();r.u64();r.end();
        if(s.runtime.empty() || s.adapter.empty() || s.outfit.empty())throw std::runtime_error("Incomplete campaign companion");return s;
    }
};
inline std::filesystem::path campaign_checkpoint_path(const std::filesystem::path& native){auto path=native;path+=L".lcs";return path;}
inline CampaignCheckpoint read_campaign_checkpoint(const std::filesystem::path& native){
    const auto path=campaign_checkpoint_path(native);std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Native save has no LCS companion; old progression cannot be reconstructed");
    const auto size=file.tellg();if(size<0 || size>64*1024*1024)throw std::runtime_error("Campaign companion size invalid");
    std::vector<uint8_t> data(static_cast<size_t>(size));file.seekg(0);if(!file.read(reinterpret_cast<char*>(data.data()),size))throw std::runtime_error("Cannot read campaign companion");
    return CampaignCheckpoint::decode(data,native_save_digest(native));
}
}
