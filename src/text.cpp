#include "lcs/text.hpp"
#include "lcs/scm.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <set>

namespace lcs {
uint32_t text_key(const std::string& name) {
    uint32_t crc=0xFFFFFFFF;
    for(unsigned char c:name) {
        if(c>='a' && c<='z')c-=32;
        crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xEDB88320u:0u);
    }
    return crc;
}
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static std::string label(const std::vector<uint8_t>& bytes,size_t at) {
    require(at<=bytes.size() && bytes.size()-at>=8,"Truncated GXT label");
    std::string text;
    for(size_t i=0;i<8 && bytes[at+i];++i) {
        if(bytes[at+i]<32 || bytes[at+i]>126)throw std::runtime_error("Invalid GXT label character at "+std::to_string(at+i));
        text+=char(bytes[at+i]);
    }
    require(!text.empty(),"Empty GXT label");return text;
}
static bool magic(const std::vector<uint8_t>& bytes,size_t at,const char* name) {
    return at<=bytes.size() && bytes.size()-at>=4 && std::equal(name,name+4,bytes.begin()+at);
}
TextArchive TextArchive::load(const std::filesystem::path& path,bool lcs_format) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    require(bool(in),"Cannot read text archive");const auto size=in.tellg();
    require(size>=8 && size<=16*1024*1024,"Invalid text archive size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));in.seekg(0);
    require(bool(in.read(reinterpret_cast<char*>(bytes.data()),size)),"Truncated text archive");
    return parse(bytes,lcs_format);
}
TextArchive TextArchive::parse(const std::vector<uint8_t>& bytes,bool lcs_format) {
    TextArchive result;const size_t start=lcs_format ? 0 : 4;
    if(!lcs_format)require(read16(bytes,0)==4 && read16(bytes,2)==8,"Expected native SA GXT version 4, encoding 8");
    require(magic(bytes,start,"TABL"),"Text archive has no TABL");
    const auto table_size=read32(bytes,start+4);
    require(table_size && table_size%12==0 && table_size/12<=200 && start+8ull+table_size<=bytes.size(),"Invalid text bank directory");
    std::set<uint32_t> offsets;
    for(size_t entry=start+8;entry<start+8+table_size;entry+=12) {
        const auto name=label(bytes,entry);const auto offset=read32(bytes,entry+8);
        require(offset>=start+8+table_size && offsets.insert(offset).second,"Invalid or duplicate text bank offset");
        size_t cursor=offset;
        if(name!="MAIN") {
            require(label(bytes,cursor)==name,"Text bank name disagrees with directory");cursor+=8;
        }
        require(magic(bytes,cursor,"TKEY"),"Text bank has no TKEY");
        const auto key_size=read32(bytes,cursor+4);const size_t key_start=cursor+8;
        const size_t row_size=lcs_format ? 12 : 8;
        require(key_size%row_size==0 && key_start+uint64_t(key_size)+8<=bytes.size(),"Invalid TKEY size");
        cursor=key_start+key_size;require(magic(bytes,cursor,"TDAT"),"Text bank has no TDAT");
        const auto data_size=read32(bytes,cursor+4);const size_t data_start=cursor+8;
        require(data_start+uint64_t(data_size)<=bytes.size(),"Truncated TDAT");
        TextBank bank;
        std::map<uint32_t,std::string> original_labels;
        for(size_t row=key_start;row<key_start+key_size;row+=row_size) {
            size_t source_row=row;
            std::string source_label;
            if(lcs_format) {
                source_label=label(bytes,row+4);
                // Original LCS contains duplicate frontend names with different
                // values. Reproduce CKeyArray::BinarySearch's selected row.
                int low=0,high=int(key_size/row_size)-1;
                while(low<=high) {
                    const int middle=(low+high)/2;
                    const auto candidate=key_start+size_t(middle)*row_size;
                    const auto compared=source_label.compare(label(bytes,candidate+4));
                    if(!compared){source_row=candidate;break;}
                    if(compared<0)high=middle-1;else low=middle+1;
                }
                require(low<=high,"LCS text keys are not searchable in original order");
            }
            const auto value_offset=read32(bytes,source_row);
            require(value_offset<data_size && (!lcs_format || value_offset%2==0),"Invalid text value offset");
            const auto key=lcs_format ? text_key(label(bytes,row+4)) : read32(bytes,row+4);
            if(lcs_format) {
                const auto old_label=original_labels.find(key);
                require(old_label==original_labels.end() || old_label->second==source_label,"Distinct LCS labels collide in native CRC");
                original_labels.emplace(key,source_label);
            }
            std::string value;size_t p=data_start+value_offset;bool terminated=false;
            while(p<data_start+data_size) {
                uint16_t c=0;
                if(lcs_format) {require(p+2<=data_start+data_size,"Truncated UTF16 text");c=read16(bytes,p);p+=2;}
                else c=bytes[p++];
                if(!c){terminated=true;break;}
                require(c<=255,"LCS glyph needs an explicit SA font mapping");value+=char(c);
            }
            require(terminated,"Text value has no terminator");
            const auto old=bank.find(key);
            if(old!=bank.end() && old->second!=value)
                throw std::runtime_error("Conflicting text key in "+name+": "+(lcs_format ? label(bytes,row+4) : std::to_string(key)));
            bank.emplace(key,std::move(value));
        }
        require(result.banks.emplace(name,std::move(bank)).second,"Duplicate text bank");
    }
    require(result.banks.count("MAIN")!=0,"Text archive has no MAIN bank");return result;
}
const std::string& TextArchive::get(const std::string& bank,const std::string& key) const {
    return banks.at(bank).at(text_key(key));
}
void TextArchive::merge_native_frontend(const TextArchive& native) {
    auto& main=banks.at("MAIN");
    for(const auto& entry:native.banks.at("MAIN"))main.emplace(entry);
}
void TextArchive::require_campaign(const TextArchive& original)const {
    for(const auto& bank:original.banks) {
        const auto native=banks.find(bank.first);
        if(native==banks.end())throw std::runtime_error("Native mission text bank is missing: "+bank.first);
        for(const auto& label:bank.second) {
            const auto value=native->second.find(label.first);
            if(value==native->second.end() || value->second!=label.second)
                throw std::runtime_error("Native LCS mission text does not match the original: "+bank.first);
        }
    }
}
std::vector<uint8_t> TextArchive::native_bytes() const {
    require(!banks.empty() && banks.size()<=200 && banks.count("MAIN"),"Native text bank capacity exceeded");
    std::vector<uint8_t> bytes{4,0,8,0};
    auto u32=[&](uint32_t value){for(int i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i)));};
    auto tag=[&](const char* value){bytes.insert(bytes.end(),value,value+4);};
    auto name=[&](const std::string& value){require(!value.empty() && value.size()<=8,"Invalid bank name");for(size_t i=0;i<8;++i)bytes.push_back(i<value.size() ? uint8_t(value[i]) : 0);};
    tag("TABL");u32(uint32_t(banks.size()*12));const auto table=bytes.size();bytes.resize(table+banks.size()*12);
    std::vector<std::string> order{"MAIN"};for(const auto& bank:banks)if(bank.first!="MAIN")order.push_back(bank.first);
    for(size_t n=0;n<order.size();++n) {
        const auto& bank=banks.at(order[n]);const auto offset=uint32_t(bytes.size());
        for(size_t i=0;i<order[n].size();++i)bytes[table+12*n+i]=uint8_t(order[n][i]);
        for(int i=0;i<4;++i)bytes[table+12*n+8+i]=uint8_t(offset>>(8*i));
        if(order[n]!="MAIN")name(order[n]);
        tag("TKEY");u32(uint32_t(bank.size()*8));
        std::vector<uint8_t> data;
        for(const auto& entry:bank) {
            u32(uint32_t(data.size()));u32(entry.first);
            data.insert(data.end(),entry.second.begin(),entry.second.end());data.push_back(0);
        }
        tag("TDAT");u32(uint32_t(data.size()));bytes.insert(bytes.end(),data.begin(),data.end());
    }
    return bytes;
}
}
