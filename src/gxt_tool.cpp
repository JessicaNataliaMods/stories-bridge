#include "lcs/text.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: lcs-gxt LCS.GXT native-SA.GXT output.GXT");
        if(std::filesystem::exists(argv[3]))throw std::runtime_error("Text output already exists");
        auto lcs=lcs::TextArchive::load(argv[1],true);const auto native=lcs::TextArchive::load(argv[2],false);
        lcs.merge_native_frontend(native);const auto bytes=lcs.native_bytes();
        const auto reread=lcs::TextArchive::parse(bytes,false);
        if(reread.banks!=lcs.banks)throw std::runtime_error("Native text round trip mismatch");
        std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        if(!out)throw std::runtime_error("Cannot save native text");
        size_t count=0;for(const auto& bank:lcs.banks)count+=bank.second.size();
        std::cout<<"native_gxt_validated=true banks="<<lcs.banks.size()<<" labels="<<count<<" bytes="<<bytes.size()<<" game_executed=false\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
