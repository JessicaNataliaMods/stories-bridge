#include "fastlz.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
class Reader {
    std::ifstream stream;
public:
    uint64_t size;
    explicit Reader(const fs::path& path) : stream(path,std::ios::binary|std::ios::ate) {
        require(bool(stream),"Cannot read asset converter package");
        size=uint64_t(stream.tellg());
    }
    uint64_t position() { return uint64_t(stream.tellg()); }
    void seek(uint64_t p) { require(p<=size,"Archive seek outside package"); stream.seekg(std::streamoff(p)); }
    void read(void* data,size_t bytes) {
        require(position()<=size && bytes<=size-position(),"Truncated converter package");
        require(bool(stream.read(static_cast<char*>(data),std::streamsize(bytes))),"Cannot read package data");
    }
    uint32_t u32() { uint8_t b[4]; read(b,4); return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24; }
    uint64_t u64() { const auto lo=u32(); return uint64_t(lo)|uint64_t(u32())<<32; }
    std::wstring text() {
        const auto count=u32(); require(count<=32768,"Unreasonable package string length");
        std::wstring value;
        for (uint32_t i=0;i<count;++i) {
            uint8_t b[2]; read(b,2); const auto c=uint16_t(b[0])|uint16_t(b[1])<<8;
            require(c!=0,"Embedded NUL in package string"); value.push_back(wchar_t(c));
        }
        return value;
    }
};
struct Entry {
    fs::path path;
    uint64_t size, compressed, offset;
    uint32_t blocks, crc;
};
std::vector<Entry> table(Reader& r) {
    require(r.size>=32,"Converter package too small");
    r.seek(r.size-8); const auto initial=r.u64(); r.seek(initial);
    // The published reLCS converter uses the 2024 sfxPackager FastLZ format.
    // Read data only: no embedded executable, script or installer is launched.
    require(r.u32()==0x4d414749,"Unsupported converter archive signature");
    require(r.u32()==0x4653544c,"Converter archive is not FastLZ");
    require(r.u64()==0,"Encrypted or unsupported converter package");
    const auto table_position=r.u64(); require(table_position>initial+24 && table_position<r.size-8,"Invalid asset table offset");
    r.seek(table_position); const auto count=r.u64();
    require(count>0 && count<=100000,"Invalid asset count");
    std::vector<Entry> entries;
    std::set<std::wstring> names;
    for (uint64_t i=0;i<count;++i) {
        const auto flags=r.u32(); require((flags&3)==0,"Spanned or downloaded assets are unsupported");
        const auto filename=r.text(), directory=r.text();
        Entry e; e.path=(fs::path(directory)/filename).lexically_normal();
        require(!filename.empty() && !e.path.has_root_path(),"Absolute or empty asset path");
        for (const auto& part:e.path) require(part!=L".." && part!=L"." && part.native().find(L':')==std::wstring::npos,"Unsafe asset path");
        auto key=e.path.wstring();
        std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c) { return c>=L'A' && c<=L'Z' ? c+32 : c; });
        require(names.insert(key).second,"Duplicate asset destination");
        e.size=r.u64(); e.compressed=r.u64(); e.crc=r.u32();
        r.u64(); r.u64(); e.blocks=r.u32(); e.offset=r.u64();
        const auto pre=r.text(), post=r.text(); require(pre.empty() && post.empty(),"Package has per-file scripts; refusing to execute them");
        require(e.size<=4ull*1024*1024*1024 && e.blocks<=1000000,"Unreasonable asset size");
        require(e.offset>=initial+24 && e.offset<table_position,"Asset starts outside package payload");
        require(uint64_t(e.blocks)*16<=table_position-e.offset,"Asset headers exceed payload");
        entries.push_back(std::move(e));
    }
    require(r.position()==r.size-8,"Unexpected converter table trailing data");
    return entries;
}
void extract(Reader& r,const Entry& entry,const fs::path& root) {
    const auto target=root/entry.path;
    const auto parent=fs::weakly_canonical(target.parent_path());
    const auto relative=parent.lexically_relative(root);
    require(!relative.empty() || parent==root,"Cannot resolve asset destination");
    for (const auto& part:relative) require(part!=L"..","Asset destination escapes output directory");
    require(!fs::exists(target),"Asset destination already exists; refusing to overwrite");
    fs::create_directories(parent);
    auto temporary=target; temporary+=L".extracting";
    require(!fs::exists(temporary),"Temporary asset destination already exists");
    std::ofstream output(temporary,std::ios::binary);
    require(bool(output),"Cannot create extracted asset");
    uint64_t total=0, compressed=0;
    uint32_t crc=0xffffffff;
    try {
        r.seek(entry.offset);
        for (uint32_t block=0;block<entry.blocks;++block) {
            r.u64(); const auto packed=r.u32(), unpacked=r.u32();
            require(unpacked>0 && unpacked<=256*1024,"Invalid FastLZ output block size");
            require(total<=entry.size && unpacked<=entry.size-total,"Asset block exceeds expected size");
            std::vector<uint8_t> bytes(unpacked);
            if (packed==UINT32_MAX) { r.read(bytes.data(),unpacked); compressed+=unpacked; }
            else {
                require(packed>0 && packed<=512*1024,"Invalid FastLZ input block size");
                std::vector<uint8_t> input(packed); r.read(input.data(),packed); compressed+=packed;
                require(fastlz_decompress(input.data(),int(packed),bytes.data(),int(unpacked))==int(unpacked),"FastLZ block decode failed");
            }
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            require(bool(output),"Cannot write extracted asset");
            if (entry.crc) for (const auto byte:bytes) {
                crc^=byte;
                for (int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & uint32_t(-int32_t(crc&1)));
            }
            total+=unpacked;
        }
        require(total==entry.size,"Extracted asset size differs from table");
        require(entry.compressed==UINT64_MAX || compressed==entry.compressed,"Compressed asset size differs from table");
        require(!entry.crc || (crc^0xffffffff)==entry.crc,"Extracted asset CRC differs from table");
        output.close(); require(bool(output),"Cannot finish extracted asset");
        fs::rename(temporary,target);
    } catch (...) {
        output.close(); std::error_code ignored; fs::remove(temporary,ignored); throw;
    }
}
}
int main(int argc,char** argv) {
    if (argc!=3) { std::cerr<<"Usage: lcs-unpack-assets reLCSAssetConverter.exe OUTPUT_DIRECTORY|--list\n"; return 2; }
    try {
        Reader reader(argv[1]); const auto entries=table(reader);
        const bool list=std::string(argv[2])=="--list";
        fs::path output;
        if (!list) { output=fs::weakly_canonical(fs::absolute(argv[2])); fs::create_directories(output); }
        uint64_t bytes=0;
        for (const auto& entry:entries) {
            if (!list) extract(reader,entry,output);
            bytes+=entry.size;
            std::cout<<(list ? "asset=" : "extracted=")<<entry.path.generic_string()<<" bytes="<<entry.size<<'\n';
        }
        std::cout<<"assets="<<entries.size()<<" total_bytes="<<bytes<<" installer_executed=false\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
