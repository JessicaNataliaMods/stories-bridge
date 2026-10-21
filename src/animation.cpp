#include "lcs/animation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <numeric>
#include <stdexcept>
namespace lcs {
namespace {
void require(bool valid,const char* message){if(!valid)throw std::runtime_error(message);}
struct Reader {
    const std::vector<uint8_t>& data;size_t position=0;
    struct Section {size_t begin,end,padded_end;};
    uint32_t word(size_t at)const {
        require(at<=data.size() && data.size()-at>=4,"Truncated IFP field");
        return uint32_t(data[at])|uint32_t(data[at+1])<<8|uint32_t(data[at+2])<<16|uint32_t(data[at+3])<<24;
    }
    std::string label(size_t at,size_t length)const {
        require(at<=data.size() && length<=data.size()-at,"Truncated IFP label");
        const auto first=data.begin()+at,last=first+length,end=std::find(first,last,0);
        require(end!=first && end!=last,"Invalid IFP label termination");
        require(std::all_of(first,end,[](uint8_t c){return c>=32 && c<=126;}),"Invalid IFP label characters");
        return std::string(first,end);
    }
    Section section(const char* tag,size_t parent) {
        require(position<=parent && parent-position>=8,"Truncated IFP section");
        require(std::equal(tag,tag+4,data.begin()+position),"Unexpected IFP section tag");
        const auto size=word(position+4);position+=8;
        const uint64_t end=uint64_t(position)+size,padded=uint64_t(position)+((uint64_t(size)+3)&~3ull);
        require(end<=parent && padded<=parent,"IFP section exceeds parent");
        return {position,size_t(end),size_t(padded)};
    }
    void finish(Section s) {require(position==s.end,"Unparsed IFP section bytes");position=s.padded_end;}
    float number(size_t at)const {
        const auto bits=word(at);float value;std::memcpy(&value,&bits,4);
        require(std::isfinite(value),"Nonfinite IFP keyframe");return value;
    }
};
}
AnimationArchive AnimationArchive::load_anpk(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    require(bool(file),"Cannot read IFP file");const auto size=file.tellg();
    require(size>=8 && size<=64*1024*1024,"Invalid IFP file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
    require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)),"Truncated IFP file");
    return parse_anpk(bytes);
}
static AnimationArchive parse_archive(const std::vector<uint8_t>& bytes,AnimationAdaptation* adapted=nullptr,
                                     const std::map<std::string,std::string>& frame_names={},const AnimationBoneMap& bones={}) {
    Reader r{bytes};AnimationArchive result;
    const auto root=r.section("ANPK",bytes.size());
    auto info=r.section("INFO",root.end);
    require(info.end-info.begin>=5 && info.end-info.begin<=28,"Invalid IFP block information");
    const auto count=r.word(info.begin);require(count>0 && count<=256,"Invalid IFP animation count");
    result.name=r.label(info.begin+4,info.end-info.begin-4);r.position=info.end;r.finish(info);
    std::set<std::string> animation_names;
    for(uint32_t i=0;i<count;++i) {
        Animation animation;
        auto name=r.section("NAME",root.end);
        require(name.end-name.begin<=24,"IFP animation name exceeds native capacity");
        animation.name=r.label(name.begin,name.end-name.begin);
        require(animation_names.insert(animation.name).second,"Duplicate IFP animation name");
        r.position=name.end;r.finish(name);
        const auto dgan=r.section("DGAN",root.end);
        info=r.section("INFO",dgan.end);
        require(info.end-info.begin==8,"Invalid IFP animation information");
        const auto sequences=r.word(info.begin);require(sequences>0 && sequences<=256,"Invalid IFP sequence count");
        r.position=info.end;r.finish(info);
        std::set<int32_t> tags;
        for(uint32_t j=0;j<sequences;++j) {
            const auto cpan=r.section("CPAN",dgan.end),anim=r.section("ANIM",cpan.end);
            require(anim.end-anim.begin==44,"IFP sequence requires verified native bone-tag layout");
            AnimationSequence sequence;sequence.name=r.label(anim.begin,28);
            const auto rename=frame_names.find(sequence.name);
            if(adapted && rename!=frame_names.end() && rename->second!=sequence.name) {
                require(!rename->second.empty() && rename->second.size()<28,"Remapped IFP frame name exceeds capacity");
                require(std::all_of(rename->second.begin(),rename->second.end(),[](char c){return c>=32 && c<=126;}),"Invalid remapped IFP frame name");
                std::fill(adapted->bytes.begin()+anim.begin,adapted->bytes.begin()+anim.begin+28,0);
                std::copy(rename->second.begin(),rename->second.end(),adapted->bytes.begin()+anim.begin);
                ++adapted->renamed_sequences;
            }
            sequence.keyframes=r.word(anim.begin+28);sequence.bone_tag=int32_t(r.word(anim.begin+40));
            const auto mapped=bones.find({animation.name,sequence.name});
            if(adapted && mapped!=bones.end()) {
                require(sequence.bone_tag<0,"Refusing to replace an original IFP bone tag");
                require(mapped->second>=0 && mapped->second<65535,"Mapped IFP bone tag exceeds native capacity");
                for(size_t q=0;q<4;++q)adapted->bytes[anim.begin+40+q]=uint8_t(uint32_t(mapped->second)>>(8*q));
                ++adapted->tagged_sequences;
            }
            require(sequence.keyframes<=65535,"IFP keyframe count exceeds native sequence capacity");
            require(sequence.bone_tag<0 || tags.insert(sequence.bone_tag).second,"Duplicate IFP sequence bone tag");
            r.position=anim.end;r.finish(anim);
            if(sequence.keyframes) {
                require(r.position<=cpan.end && cpan.end-r.position>=8,"Missing IFP keyframes");
                const auto tag=std::string(bytes.begin()+r.position,bytes.begin()+r.position+4);
                require(tag=="KR00" || tag=="KRT0" || tag=="KRTS","Unsupported IFP keyframe layout");
                const auto frames=r.section(tag.c_str(),cpan.end);
                const size_t stride=tag=="KR00" ? 20 : tag=="KRT0" ? 32 : 44;
                require(frames.end-frames.begin==uint64_t(sequence.keyframes)*stride,"IFP keyframe count disagrees with section length");
                float previous=0;bool decreasing=false;std::vector<float> times;
                for(uint32_t k=0;k<sequence.keyframes;++k) {
                    const auto at=frames.begin+k*stride;
                    for(size_t q=0;q<stride;q+=4)(void)r.number(at+q);
                    const auto time=r.number(at+stride-4);
                    require(time>=0,"Negative IFP keyframe time");times.push_back(time);
                    if(time<previous && !adapted)throw std::runtime_error("IFP keyframe times decrease: "+animation.name+"/"+sequence.name+
                        " frame="+std::to_string(k)+" offset="+std::to_string(at+stride-4)+
                        " previous="+std::to_string(previous)+" current="+std::to_string(time));
                    decreasing=decreasing || time<previous;
                    previous=time;
                }
                if(decreasing && adapted) {
                    std::vector<size_t> order(sequence.keyframes);std::iota(order.begin(),order.end(),0);
                    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return times[a]<times[b];});
                    for(size_t k=0;k<order.size();++k)std::copy_n(bytes.begin()+frames.begin+order[k]*stride,stride,adapted->bytes.begin()+frames.begin+k*stride);
                    ++adapted->reordered_sequences;previous=times[order.back()];
                }
                sequence.duration=previous;r.position=frames.end;r.finish(frames);
            }
            r.finish(cpan);animation.sequences.push_back(std::move(sequence));
        }
        r.finish(dgan);result.animations.push_back(std::move(animation));
    }
    r.finish(root);
    require(bytes.size()-r.position<2048 && std::all_of(bytes.begin()+r.position,bytes.end(),[](uint8_t v){return v==0;}),"Unexpected IFP trailing data");
    return result;
}
AnimationArchive AnimationArchive::parse_anpk(const std::vector<uint8_t>& bytes) {return parse_archive(bytes);}
AnimationAdaptation adapt_anpk(const std::vector<uint8_t>& bytes,const std::map<std::string,std::string>& frame_names,const AnimationBoneMap& bones,const std::string& block_name) {
    AnimationAdaptation result;result.bytes=bytes;
    (void)parse_archive(bytes,&result,frame_names,bones);
    require(result.tagged_sequences==bones.size(),"Animation bone mapping references an absent sequence");
    if(!block_name.empty()) {
        Reader reader{bytes};const auto root=reader.section("ANPK",bytes.size());
        const auto info=reader.section("INFO",root.end);
        require(block_name.size()<info.end-info.begin-4,"Aliased IFP block exceeds original string capacity");
        require(std::all_of(block_name.begin(),block_name.end(),[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}),"Invalid aliased animation block name");
        std::fill(result.bytes.begin()+info.begin+4,result.bytes.begin()+info.end,0);
        std::copy(block_name.begin(),block_name.end(),result.bytes.begin()+info.begin+4);
    }
    (void)AnimationArchive::parse_anpk(result.bytes);
    return result;
}
}
