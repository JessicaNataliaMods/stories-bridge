#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <string>
namespace fs=std::filesystem;
struct Entry { uint32_t offset,size; std::string name; };
uint32_t read32(std::istream& f) { std::array<uint8_t,4> b{}; f.read(reinterpret_cast<char*>(b.data()),4); if(!f) throw std::runtime_error("Truncated directory"); return b[0]|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24; }
void write32(std::ostream& f,uint32_t v) { for(int i=0;i<4;++i) f.put(char(v>>(8*i))); }
void unpack(const fs::path& img,const fs::path& directory,const fs::path& output) {
    if(fs::exists(output)) throw std::runtime_error("Output exists");
    const auto length=fs::file_size(img),dir_length=fs::file_size(directory);
    if(dir_length%32) throw std::runtime_error("Invalid IMG v1 directory length");
    std::ifstream index(directory,std::ios::binary),data(img,std::ios::binary);
    if(!index || !data) throw std::runtime_error("Cannot open IMG/DIR");
    std::vector<Entry> entries; std::set<std::string> names;
    for(uint64_t i=0;i<dir_length/32;++i) {
        Entry e{read32(index),read32(index),{}}; char name[24]; index.read(name,24);
        auto end=std::find(name,name+24,'\0');
        if(end==name+24) throw std::runtime_error("Unterminated IMG name");
        e.name.assign(name,end);
        if(e.name.empty() || e.name.find_first_of("/\\:")!=std::string::npos || e.name=="." || e.name=="..") throw std::runtime_error("Unsafe IMG name");
        auto key=e.name; for(auto& c:key) c=char(std::tolower(static_cast<unsigned char>(c)));
        if(!names.insert(key).second) throw std::runtime_error("Duplicate IMG name");
        if(!e.size || uint64_t(e.offset)*2048>=length ||
           uint64_t(e.offset)*2048+uint64_t(e.size)*2048>((length+2047)/2048)*2048) throw std::runtime_error("IMG entry outside archive");
        entries.push_back(e);
    }
    // Entries in reLCS's archive are not required to end at a sector boundary.
    fs::create_directories(output);
    std::array<char,65536> buffer{};
    for(const auto& e:entries) {
        data.seekg(uint64_t(e.offset)*2048);
        std::ofstream out(output/e.name,std::ios::binary);
        if(!out) throw std::runtime_error("Cannot create extracted file");
        uint64_t left=uint64_t(e.size)*2048;
        // The final file may have less than a full sector of padding.
        left=std::min(left,length-uint64_t(e.offset)*2048);
        while(left) { auto n=std::min(left,uint64_t(buffer.size())); data.read(buffer.data(),std::streamsize(n)); if(!data) throw std::runtime_error("Truncated IMG data"); out.write(buffer.data(),std::streamsize(n)); left-=n; }
        if(!out) throw std::runtime_error("Cannot write extracted file");
    }
    std::cout<<"entries="<<entries.size()<<" extracted=true\n";
}
void pack(const fs::path& input,const fs::path& output) {
    if(fs::exists(output)) throw std::runtime_error("Output exists");
    std::vector<fs::path> files;
    for(const auto& e:fs::directory_iterator(input)) if(e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(),files.end());
    std::vector<Entry> entries;
    uint64_t offset=(8+files.size()*32+2047)/2048;
    for(const auto& f:files) {
        auto name=f.filename().string(); auto sectors=(fs::file_size(f)+2047)/2048;
        if(name.size()>23 || !sectors || sectors>65535 || offset+sectors>UINT32_MAX) throw std::runtime_error("IMG v2 capacity exceeded");
        entries.push_back({uint32_t(offset),uint32_t(sectors),name}); offset+=sectors;
    }
    fs::create_directories(output.parent_path());
    auto temporary=output.string()+".packing";
    std::ofstream out(temporary,std::ios::binary); if(!out) throw std::runtime_error("Cannot create IMG");
    out.write("VER2",4); write32(out,uint32_t(entries.size()));
    for(const auto& e:entries) { write32(out,e.offset); write32(out,e.size); std::array<char,24> name{}; std::memcpy(name.data(),e.name.data(),e.name.size()); out.write(name.data(),24); }
    std::array<char,65536> buffer{};
    for(size_t i=0;i<files.size();++i) {
        auto target=uint64_t(entries[i].offset)*2048;
        while(uint64_t(out.tellp())<target) out.put(0);
        std::ifstream in(files[i],std::ios::binary); if(!in) throw std::runtime_error("Cannot read input");
        while(in) { in.read(buffer.data(),buffer.size()); out.write(buffer.data(),in.gcount()); }
        if(!in.eof() || !out) throw std::runtime_error("IMG input/output failure");
    }
    while(uint64_t(out.tellp())<offset*2048) out.put(0);
    out.close(); if(!out) throw std::runtime_error("Cannot finish IMG"); fs::rename(temporary,output);
    std::cout<<"entries="<<entries.size()<<" format=IMGv2 sectors="<<offset<<'\n';
}
void unpack_native(const fs::path& input,const fs::path& output) {
    if(fs::exists(output))throw std::runtime_error("Output exists");
    std::ifstream in(input,std::ios::binary);char magic[4]{};in.read(magic,4);
    if(std::memcmp(magic,"VER2",4))throw std::runtime_error("Expected SA IMG v2");
    const auto count=read32(in);const auto length=fs::file_size(input);
    if(count>100000 || 8ull+32ull*count>length)throw std::runtime_error("Invalid native IMG directory");
    const auto directory_sectors=(8ull+32ull*count+2047)/2048;
    std::vector<Entry> entries;std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        const auto offset=read32(in),sizes=read32(in);
        const auto sectors=(sizes>>16)?sizes>>16:sizes&65535;
        char name[24]{};in.read(name,24);auto end=std::find(name,name+24,'\0');
        if(!in || end==name || end==name+24 || !sectors || offset<directory_sectors || uint64_t(offset+uint64_t(sectors))*2048>length)throw std::runtime_error("Invalid native IMG entry bounds");
        std::string value(name,end),key=value;
        if(value.find_first_of("/\\:")!=std::string::npos || value=="." || value=="..")throw std::runtime_error("Unsafe native IMG name");
        for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(!names.insert(key).second)throw std::runtime_error("Duplicate native IMG name");
        entries.push_back({offset,sectors,value});
    }
    auto ranges=entries;std::sort(ranges.begin(),ranges.end(),[](const Entry& a,const Entry& b){return a.offset<b.offset;});
    for(size_t i=1;i<ranges.size();++i)if(uint64_t(ranges[i-1].offset)+ranges[i-1].size>ranges[i].offset)throw std::runtime_error("Overlapping native IMG entries");
    fs::create_directories(output);std::array<char,65536> buffer{};
    for(const auto& entry:entries) {
        in.seekg(uint64_t(entry.offset)*2048);std::ofstream out(output/entry.name,std::ios::binary);
        uint64_t left=uint64_t(entry.size)*2048;
        while(left){const auto n=std::min(left,uint64_t(buffer.size()));in.read(buffer.data(),std::streamsize(n));out.write(buffer.data(),std::streamsize(n));if(!in || !out)throw std::runtime_error("Native IMG extraction failed");left-=n;}
    }
    std::cout<<"entries="<<entries.size()<<" extracted_native=true source_modified=false\n";
}
void filter_native(const fs::path& input,const fs::path& output,const fs::path& excluded_models={}) {
    if(fs::exists(output)) throw std::runtime_error("Output exists");
    std::set<std::string> unwanted;
    if(!excluded_models.empty()) {
        std::ifstream names(excluded_models); if(!names)throw std::runtime_error("Cannot read excluded model list");
        std::string name;
        while(std::getline(names,name)) {
            if(!name.empty() && name.back()=='\r')name.pop_back();
            if(name.empty())continue;
            if(name.find_first_of("/\\:")!=std::string::npos)throw std::runtime_error("Invalid excluded model name");
            for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            unwanted.insert(name+".dff");
        }
    }
    std::ifstream in(input,std::ios::binary);char magic[4]{};in.read(magic,4);
    if(std::memcmp(magic,"VER2",4)) throw std::runtime_error("Expected SA IMG v2");
    const auto count=read32(in);const auto length=fs::file_size(input);
    if(count>100000 || 8ull+32ull*count>length) throw std::runtime_error("Invalid IMG v2 directory");
    std::vector<Entry> entries;size_t excluded=0;
    for(uint32_t i=0;i<count;++i) {
        auto offset=read32(in),sizes=read32(in);auto sectors=(sizes>>16) ? sizes>>16 : sizes&65535;
        char name[24]{};in.read(name,24);auto end=std::find(name,name+24,'\0');
        if(!in || end==name+24 || !sectors || uint64_t(offset)*2048+uint64_t(sectors)*2048>length) throw std::runtime_error("Invalid SA IMG entry");
        std::string key(name,end);for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));
        auto ext=fs::path(key).extension().string();
        if(ext==".ipl" || ext==".col" || (ext==".dat" && key.compare(0,5,"nodes")==0) || unwanted.count(key)) {++excluded;continue;}
        entries.push_back({offset,sectors,std::string(name,end)});
    }
    fs::create_directories(output.parent_path());auto temporary=output.string()+".filtering";
    std::ofstream out(temporary,std::ios::binary);if(!out) throw std::runtime_error("Cannot create filtered IMG");
    uint64_t offset=(8+entries.size()*32+2047)/2048;
    out.write("VER2",4);write32(out,uint32_t(entries.size()));
    for(const auto& e:entries) {write32(out,uint32_t(offset));write32(out,e.size);std::array<char,24> name{};std::memcpy(name.data(),e.name.data(),e.name.size());out.write(name.data(),24);offset+=e.size;}
    const auto data_start=((8+entries.size()*32+2047)/2048)*2048;
    while(uint64_t(out.tellp())<data_start)out.put(0);
    std::array<char,65536> buffer{};
    for(const auto& e:entries) {
        in.seekg(uint64_t(e.offset)*2048);uint64_t left=uint64_t(e.size)*2048;
        while(left) {auto n=std::min(left,uint64_t(buffer.size()));in.read(buffer.data(),std::streamsize(n));out.write(buffer.data(),std::streamsize(n));if(!in || !out)throw std::runtime_error("Filtered IMG IO failed");left-=n;}
    }
    out.close();if(!out)throw std::runtime_error("Cannot finish filtered IMG");fs::rename(temporary,output);
    std::cout<<"retained="<<entries.size()<<" removed_sa_world_entries="<<excluded<<" source_modified=false\n";
}
int main(int argc,char** argv) {
    try {
        if(argc==5 && std::string(argv[1])=="unpack") unpack(argv[2],argv[3],argv[4]);
        else if(argc==4 && std::string(argv[1])=="pack") pack(argv[2],argv[3]);
        else if(argc==4 && std::string(argv[1])=="unpack-native") unpack_native(argv[2],argv[3]);
        else if((argc==4 || argc==5) && std::string(argv[1])=="filter-native") filter_native(argv[2],argv[3],argc==5 ? fs::path(argv[4]) : fs::path{});
        else throw std::runtime_error("Usage: lcs-img unpack ARCHIVE.img ARCHIVE.dir DIRECTORY | unpack-native SA.img DIRECTORY | pack DIRECTORY OUTPUT.img | filter-native SA.img OUTPUT.img [EXCLUDED_MODEL_NAMES.txt]");
        return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
