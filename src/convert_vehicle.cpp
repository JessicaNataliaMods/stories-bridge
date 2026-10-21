// Offline assets only. References: reLCS VehicleModelInfo::PreprocessHierarchy,
// gta-reversed CVehicleModelInfo::PreprocessHierarchy and the verified SA US
// component table at 0x8A6468. No replacement vehicle geometry is used.
#include "convert_vehicle.hpp"
#include <rw.h>
#include <rwgta.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <set>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <limits>
#include <utility>
namespace {
struct CollisionPluginData { rw::uint32 size; rw::uint8* data; };
rw::Clump* read_car(const std::filesystem::path& path) {
    rw::StreamFile in;
    if(!in.open(path.string().c_str(),"rb") || !rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Cannot open vehicle clump");
    auto c=rw::Clump::streamRead(&in);in.close();
    if(!c)throw std::runtime_error("Cannot read vehicle clump");
    return c;
}
uint32_t read_u32(const std::vector<uint8_t>& bytes,size_t offset,const char* error) {
    if(offset>bytes.size() || bytes.size()-offset<sizeof(uint32_t))throw std::runtime_error(error);
    uint32_t value;std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;
}
void write_u16(std::vector<uint8_t>& bytes,size_t offset,uint16_t value) {
    if(offset>bytes.size() || bytes.size()-offset<sizeof(value))throw std::runtime_error("COL3 output field overflow");
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
void write_u32(std::vector<uint8_t>& bytes,size_t offset,uint32_t value) {
    if(offset>bytes.size() || bytes.size()-offset<sizeof(value))throw std::runtime_error("COL3 output field overflow");
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
void write_i16(std::vector<uint8_t>& bytes,size_t offset,int16_t value) {
    if(offset>bytes.size() || bytes.size()-offset<sizeof(value))throw std::runtime_error("COL3 output field overflow");
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
uint16_t read_u16(const uint8_t* bytes,size_t size,size_t offset,const char* error) {
    if(offset>size || size-offset<sizeof(uint16_t))throw std::runtime_error(error);
    uint16_t value;std::memcpy(&value,bytes+offset,sizeof(value));return value;
}
struct OmittedFacePieces { uint32_t piece16=0,piece17=0,other=0; };
std::vector<uint8_t> convert_legacy_col_body_to_col3(const uint8_t* body,size_t size,const std::string& model_name,
                                                     OmittedFacePieces* omitted_pieces=nullptr) {
    if(!body || size<60)throw std::runtime_error("Truncated legacy collision model body");
    if(model_name.empty() || model_name.size()>24)throw std::runtime_error("COL3 model name must fit its 24-byte field");
    auto finite=[&](size_t offset) {
        if(offset>size || size-offset<sizeof(float))throw std::runtime_error("Truncated legacy collision float");
        float value;std::memcpy(&value,body+offset,sizeof(value));
        if(!std::isfinite(value))throw std::runtime_error("Nonfinite legacy collision value");
    };
    for(size_t i=0;i<10;++i)finite(i*sizeof(float));
    size_t cursor=40;
    auto array=[&](size_t stride) {
        if(cursor>size || size-cursor<4)throw std::runtime_error("Truncated legacy collision count");
        uint32_t count;std::memcpy(&count,body+cursor,4);cursor+=4;
        if(count>32767 || count>(size-cursor)/stride)throw std::runtime_error("Invalid legacy collision array size");
        const size_t start=cursor;cursor+=size_t(count)*stride;return std::pair<size_t,uint32_t>{start,count};
    };
    auto [spheres,num_spheres]=array(20);
    auto [lines,num_lines]=array(24);
    auto [boxes,num_boxes]=array(28);
    auto [vertices,num_vertices]=array(12);
    auto [triangles,num_triangles]=array(16);
    if(cursor!=size)throw std::runtime_error("Unparsed legacy collision bytes");
    if(!num_spheres && !num_boxes && !num_triangles)
        throw std::runtime_error("COLL model has no collision volumes");
    if(num_vertices && !num_triangles)
        throw std::runtime_error("COLL model has vertices without faces; SA COL3 cannot retain unused vertices");
    for(uint32_t i=0;i<num_spheres;++i)for(size_t j=0;j<4;++j)finite(spheres+20*i+4*j);
    for(uint32_t i=0;i<num_lines;++i)for(size_t j=0;j<6;++j)finite(lines+24*i+4*j);
    for(uint32_t i=0;i<num_boxes;++i)for(size_t j=0;j<6;++j)finite(boxes+28*i+4*j);
    for(uint32_t i=0;i<num_vertices;++i)for(size_t j=0;j<3;++j)finite(vertices+12*i+4*j);
    for(uint32_t i=0;i<num_triangles;++i) {
        for(size_t j=0;j<3;++j) {
            uint32_t index;std::memcpy(&index,body+triangles+16*i+4*j,sizeof(index));
            if(index>=num_vertices || index>static_cast<uint32_t>(std::numeric_limits<int16_t>::max()))
                throw std::runtime_error("COLL face index cannot be represented by SA COL3");
        }
        // LCS legacy faces carry a piece byte at +13. SA COL3 faces have only
        // a material and lighting byte, so this face-piece value is omitted.
        if(omitted_pieces) {
            const auto piece=body[triangles+16*i+13];
            if(piece==16)++omitted_pieces->piece16;
            else if(piece==17)++omitted_pieces->piece17;
            else if(piece)++omitted_pieces->other;
        }
    }

    // SA's Rockstar collision plugin carries native COL3 records. COL3 stores
    // vertices as signed 1/128-unit coordinates and faces as 16-bit indices;
    // All geometry and surface/material and lighting bytes are transferred.
    // Sphere and box piece bytes have SA fields. Legacy face piece bytes are
    // omitted because SA COL3 has no corresponding field.
    constexpr size_t record_header=8,model_name_size=24,col3_header_size=0x58;
    constexpr size_t data_offset=0x74;
    size_t array_cursor=data_offset;
    uint32_t offsets[5]={};
    const uint32_t counts[]={num_spheres,num_boxes,num_lines,num_vertices,num_triangles};
    const size_t strides[]={20,28,24,6,8};
    for(size_t i=0;i<5;++i)if(counts[i]) {
        if(array_cursor>std::numeric_limits<uint32_t>::max())throw std::runtime_error("COL3 array offset overflow");
        offsets[i]=static_cast<uint32_t>(array_cursor);
        array_cursor+=size_t(counts[i])*strides[i];
    }
    const size_t body_size=col3_header_size+(array_cursor-data_offset);
    if(body_size>static_cast<size_t>(std::numeric_limits<rw::int32>::max()) ||
       body_size>std::numeric_limits<uint32_t>::max()-record_header)
        throw std::runtime_error("COL3 collision model is too large");
    const size_t output_size=record_header+model_name_size+body_size;
    std::vector<uint8_t> col3(output_size,0);
    std::memcpy(col3.data(),"COL3",4);
    write_u32(col3,4,static_cast<uint32_t>(output_size-record_header));
    std::memcpy(col3.data()+record_header,model_name.data(),model_name.size());
    const size_t header=record_header+model_name_size;
    std::memcpy(col3.data()+header,body+16,24); // CBox min/max
    std::memcpy(col3.data()+header+24,body+4,12); // CSphere center
    std::memcpy(col3.data()+header+36,body,4); // CSphere radius
    write_u16(col3,header+40,static_cast<uint16_t>(num_spheres));
    write_u16(col3,header+42,static_cast<uint16_t>(num_boxes));
    write_u16(col3,header+44,static_cast<uint16_t>(num_triangles));
    write_u16(col3,header+46,static_cast<uint16_t>(num_lines));
    // SA 1.0 US 0x537D25..0x537D46 maps flags bit 1 to
    // CColModel::m_bHasCollisionVolumes; these volumes must be enabled.
    col3[header+48]=2;
    for(size_t i=0;i<5;++i)write_u32(col3,header+52+4*i,offsets[i]);
    const size_t arrays=header+col3_header_size;
    for(uint32_t i=0;i<num_spheres;++i) {
        const size_t source=spheres+20*i,target=arrays+20*i;
        std::memcpy(col3.data()+target,body+source+4,12);
        std::memcpy(col3.data()+target+12,body+source,4);
        col3[target+16]=body[source+16];col3[target+17]=body[source+17];col3[target+18]=body[source+19];
    }
    for(uint32_t i=0;i<num_boxes;++i) {
        const size_t source=boxes+28*i,target=arrays+20*num_spheres+28*i;
        std::memcpy(col3.data()+target,body+source,24);
        col3[target+24]=body[source+24];col3[target+25]=body[source+25];col3[target+26]=body[source+27];
    }
    const size_t lines_out=arrays+20*num_spheres+28*num_boxes;
    if(num_lines)std::memcpy(col3.data()+lines_out,body+lines,24*num_lines);
    const size_t vertices_out=lines_out+24*num_lines;
    for(uint32_t i=0;i<num_vertices;++i)for(size_t j=0;j<3;++j) {
        float value;std::memcpy(&value,body+vertices+12*i+4*j,sizeof(value));
        const double scaled=static_cast<double>(value)*128.0;
        if(scaled<=-32768.5 || scaled>=32767.5)
            throw std::runtime_error("COLL vertex coordinate exceeds SA COL3 compressed range");
        const auto quantized=static_cast<int16_t>(std::lround(scaled));
        write_i16(col3,vertices_out+6*i+2*j,quantized);
    }
    const size_t triangles_out=vertices_out+6*num_vertices;
    for(uint32_t i=0;i<num_triangles;++i) {
        const size_t source=triangles+16*i,target=triangles_out+8*i;
        for(size_t j=0;j<3;++j) {
            uint32_t index;std::memcpy(&index,body+source+4*j,sizeof(index));
            write_u16(col3,target+2*j,static_cast<uint16_t>(index));
        }
        // Preserve legacy surface (+12) and lighting (+15). The per-face LCS
        // piece (+13), including piece 16/17, has no slot in SA CColTriangle.
        col3[target+6]=body[source+12];col3[target+7]=body[source+15];
    }
    return col3;
}
void validate_col3_plugin(const uint8_t* data,size_t size,const std::string& expected_name) {
    if(!data || size<8+24+0x58 || std::memcmp(data,"COL3",4))
        throw std::runtime_error("SA vehicle collision plugin does not contain a COL3 record");
    uint32_t declared;std::memcpy(&declared,data+4,4);
    if(declared!=size-8)throw std::runtime_error("COL3 plugin record length mismatch");
    const char* name=reinterpret_cast<const char*>(data+8);size_t name_size=0;
    while(name_size<24 && name[name_size])++name_size;
    if(std::string(name,name_size)!=expected_name)throw std::runtime_error("COL3 collision model name mismatch");
    const size_t body_size=size-32;
    if(body_size>static_cast<size_t>(std::numeric_limits<rw::int32>::max()))throw std::runtime_error("COL3 collision model is too large");
    const auto* header=data+32;
    const uint16_t num_spheres=read_u16(header,body_size,40,"Truncated COL3 sphere count");
    const uint16_t num_boxes=read_u16(header,body_size,42,"Truncated COL3 box count");
    const uint16_t num_triangles=read_u16(header,body_size,44,"Truncated COL3 triangle count");
    const uint16_t num_lines=read_u16(header,body_size,46,"Truncated COL3 line count");
    if(header[48]!=2 || header[49] || header[50] || header[51])
        throw std::runtime_error("COL3 collision volumes are not enabled or header flags are unsupported");
    uint32_t offsets[5];for(size_t i=0;i<5;++i)std::memcpy(&offsets[i],header+52+4*i,4);
    if(!num_spheres && !num_boxes && !num_triangles)
        throw std::runtime_error("COL3 collision model has no collision volumes");
    size_t array_cursor=0x74;
    auto consume=[&](size_t index,size_t stride,uint32_t count) {
        if(!count) {if(offsets[index])throw std::runtime_error("Unexpected COL3 offset for an empty array");return;}
        if(offsets[index]!=array_cursor)throw std::runtime_error("COL3 arrays are not in the supported native layout");
        array_cursor+=size_t(count)*stride;
    };
    consume(0,20,num_spheres);consume(1,28,num_boxes);consume(2,24,num_lines);
    size_t num_vertices=0;
    if(offsets[3]) {
        if(!num_triangles || offsets[4]<=offsets[3] || (offsets[4]-offsets[3])%6)
            throw std::runtime_error("COL3 compressed vertex range is invalid");
        num_vertices=(offsets[4]-offsets[3])/6;
        if(!num_vertices || num_vertices>32767)throw std::runtime_error("COL3 vertex count is invalid");
        consume(3,6,static_cast<uint32_t>(num_vertices));
    } else if(num_triangles)throw std::runtime_error("COL3 faces have no compressed vertices");
    consume(4,8,num_triangles);
    if(body_size!=0x58+(array_cursor-0x74))throw std::runtime_error("COL3 array data length mismatch");
    gta::CColModel decoded;
    gta::readColModelVer3(&decoded,const_cast<rw::uint8*>(data+32),static_cast<rw::int32>(body_size));
    if(decoded.numSpheres!=num_spheres || decoded.numBoxes!=num_boxes || decoded.numLines!=num_lines ||
       decoded.numTriangles!=num_triangles || (num_spheres && !decoded.spheres) ||
       (num_boxes && !decoded.boxes) || (num_lines && !decoded.lines) ||
       (num_vertices && !decoded.compVertices) || (num_triangles && !decoded.triangles) || decoded.allocFlag!=3)
        throw std::runtime_error("Native COL3 decoder did not recover the embedded vehicle collision");
    auto finite=[](float value){if(!std::isfinite(value))throw std::runtime_error("Nonfinite decoded COL3 value");};
    finite(decoded.boundingSphere.radius);finite(decoded.boundingSphere.center.x);
    finite(decoded.boundingSphere.center.y);finite(decoded.boundingSphere.center.z);
    finite(decoded.boundingBox.min.x);finite(decoded.boundingBox.min.y);finite(decoded.boundingBox.min.z);
    finite(decoded.boundingBox.max.x);finite(decoded.boundingBox.max.y);finite(decoded.boundingBox.max.z);
    for(int i=0;i<decoded.numSpheres;++i)
        if(!std::isfinite(decoded.spheres[i].sph.radius) ||
           !std::isfinite(decoded.spheres[i].sph.center.x) || !std::isfinite(decoded.spheres[i].sph.center.y) ||
           !std::isfinite(decoded.spheres[i].sph.center.z))throw std::runtime_error("Invalid decoded COL3 sphere");
    for(int i=0;i<decoded.numBoxes;++i)
        if(!std::isfinite(decoded.boxes[i].box.min.x) || !std::isfinite(decoded.boxes[i].box.min.y) ||
           !std::isfinite(decoded.boxes[i].box.min.z) || !std::isfinite(decoded.boxes[i].box.max.x) ||
           !std::isfinite(decoded.boxes[i].box.max.y) || !std::isfinite(decoded.boxes[i].box.max.z))
             throw std::runtime_error("Invalid decoded COL3 box");
    for(int i=0;i<decoded.numLines;++i)
        if(!std::isfinite(decoded.lines[i].p0.x) || !std::isfinite(decoded.lines[i].p0.y) ||
           !std::isfinite(decoded.lines[i].p0.z) || !std::isfinite(decoded.lines[i].p1.x) ||
           !std::isfinite(decoded.lines[i].p1.y) || !std::isfinite(decoded.lines[i].p1.z))
            throw std::runtime_error("Invalid decoded COL3 line");
    for(size_t i=0;i<num_vertices;++i) {
        auto vertex=decoded.compVertices[i].Uncompress();
        finite(vertex.x);finite(vertex.y);finite(vertex.z);
    }
    for(int i=0;i<decoded.numTriangles;++i) {
        const auto& triangle=decoded.triangles[i];
        if(triangle.a<0 || triangle.b<0 || triangle.c<0 || size_t(triangle.a)>=num_vertices ||
           size_t(triangle.b)>=num_vertices || size_t(triangle.c)>=num_vertices)
            throw std::runtime_error("Invalid decoded COL3 face index");
    }
}
void validate_tropic_col3(const uint8_t* data,size_t size) {
    validate_col3_plugin(data,size,"tropic_col");
    const auto* header=data+32;
    if(read_u16(header,size-32,40,"Truncated ferry COL3 sphere count")!=11 ||
       read_u16(header,size-32,42,"Truncated ferry COL3 box count")!=4 ||
       read_u16(header,size-32,44,"Truncated ferry COL3 triangle count") ||
       read_u16(header,size-32,46,"Truncated ferry COL3 line count"))
        throw std::runtime_error("COL3 plugin is not the prepared ferry collision layout");
}
std::vector<uint8_t> read_tropic_collision(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size<48 || size>std::numeric_limits<rw::uint32>::max())throw std::runtime_error("Invalid ferry collision file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream in(path,std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!in)throw std::runtime_error("Cannot read ferry collision file");
    std::vector<uint8_t> selected;size_t cursor=0;
    while(cursor<bytes.size()) {
        if(bytes.size()-cursor<8 || std::memcmp(bytes.data()+cursor,"COLL",4))
            throw std::runtime_error("Only original SA legacy COLL ferry collision is supported");
        const uint64_t end64=uint64_t(cursor)+8+read_u32(bytes,cursor+4,"Truncated ferry COLL header");
        if(end64>bytes.size() || end64<cursor+32+40)throw std::runtime_error("Ferry COLL record bounds invalid");
        const size_t end=static_cast<size_t>(end64);
        const char* name=reinterpret_cast<const char*>(bytes.data()+cursor+8);
        size_t name_size=0;while(name_size<24 && name[name_size])++name_size;
        std::string model_name(name,name_size);
        for(auto& ch:model_name)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        if(model_name=="tropic" || model_name=="ferry") {
            if(!selected.empty())throw std::runtime_error("Duplicate ferry/TROPIC collision model in COL file");
            const size_t body_start=cursor+32,body_size=end-body_start;
            selected=convert_legacy_col_body_to_col3(bytes.data()+body_start,body_size,"tropic_col");
        }
        cursor=end;
    }
    if(selected.empty())throw std::runtime_error("COL file has no original ferry collision model named ferry or tropic");
    return selected;
}
std::vector<uint8_t> read_car_collision(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size<40 || size>std::numeric_limits<rw::uint32>::max())throw std::runtime_error("Invalid car collision file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream in(path,std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!in)throw std::runtime_error("Cannot read car collision file");
    size_t cursor=0;std::vector<uint8_t> selected;std::string selected_name;OmittedFacePieces omitted{};
    while(cursor<bytes.size()) {
        if(bytes.size()-cursor<8 || std::memcmp(bytes.data()+cursor,"COLL",4))
            throw std::runtime_error("Car collision input must contain legacy COLL records");
        const uint64_t end64=uint64_t(cursor)+8+read_u32(bytes,cursor+4,"Truncated car COLL header");
        if(end64>bytes.size() || end64<cursor+32+60)throw std::runtime_error("Car COLL record bounds invalid");
        const size_t end=static_cast<size_t>(end64);
        const char* name=reinterpret_cast<const char*>(bytes.data()+cursor+8);size_t name_size=0;
        while(name_size<24 && name[name_size])++name_size;
        if(!name_size)throw std::runtime_error("Car COLL record has an empty model name");
        std::string native_name(name,name_size);
        for(auto& ch:native_name)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        std::string plugin_name=native_name;
        if(plugin_name.size()<4 || plugin_name.compare(plugin_name.size()-4,4,"_col"))plugin_name+="_col";
        if(plugin_name.size()>24)throw std::runtime_error("Native car collision name plus _col exceeds the COL3 24-byte model name");
        if(!selected.empty())throw std::runtime_error("Car collision input must contain exactly one native COLL model");
        selected_name=plugin_name;
        selected=convert_legacy_col_body_to_col3(bytes.data()+cursor+32,end-(cursor+32),selected_name,&omitted);
        cursor=end;
    }
    if(selected.empty())throw std::runtime_error("Car collision file has no native COLL model");
    validate_col3_plugin(selected.data(),selected.size(),selected_name);
    std::cout<<"legacy_face_piece16_omitted="<<omitted.piece16
             <<" legacy_face_piece17_omitted="<<omitted.piece17
             <<" legacy_face_other_piece_omitted="<<omitted.other<<' ';
    return selected;
}
void validate_car_col3(const uint8_t* data,size_t size) {
    if(!data || size<32)throw std::runtime_error("SA car clump has no embedded collision plugin data");
    const char* raw_name=reinterpret_cast<const char*>(data+8);size_t name_size=0;
    while(name_size<24 && raw_name[name_size])++name_size;
    const std::string name(raw_name,name_size);
    if(name.size()<5 || name.compare(name.size()-4,4,"_col"))
        throw std::runtime_error("SA car COL3 collision name must end in _col");
    validate_col3_plugin(data,size,name);
}
CollisionPluginData* collision_plugin(rw::Clump* clump) {
    if(gta::collisionOffset<0)throw std::runtime_error("SA collision clump plugin is not registered");
    return PLUGINOFFSET(CollisionPluginData,clump,gta::collisionOffset);
}
void frames(rw::Frame* f,std::vector<rw::Frame*>& out) {
    out.push_back(f);for(auto p=f->child;p;p=p->next)frames(p,out);
}
rw::Frame* unique(rw::Clump* c,const char* name,bool required=true) {
    std::vector<rw::Frame*> list;frames(c->getFrame(),list);rw::Frame* found=nullptr;
    for(auto f:list)if(!std::strcmp(gta::getNodeName(f),name)) {
        if(found)throw std::runtime_error(std::string("Duplicate vehicle frame: ")+name);
        found=f;
    }
    if(!found && required)throw std::runtime_error(std::string("Missing vehicle frame: ")+name);
    return found;
}
void normalize_rcbandit_body_frame_names(rw::Clump* vehicle) {
    if(!vehicle || !vehicle->getFrame())throw std::runtime_error("Missing SA vehicle clump");
    auto root=vehicle->getFrame();
    if(std::strcmp(gta::getNodeName(root),"rcbandit"))return;

    // The original LCS RC Bandit uses these exact capitalized body-frame
    // names. Validate its known frame/atomic layout before spelling them as
    // the canonical SA names expected by the shared automobile adapter.
    auto high=unique(vehicle,"Chassis_hi",false);
    auto low=unique(vehicle,"Chassis_vlo",false);
    if(!high || !low || high==root || low==root || high==low ||
       unique(vehicle,"chassis_hi",false) || unique(vehicle,"chassis_vlo",false))
        throw std::runtime_error("RC Bandit does not match its original chassis frame contract");
    unique(vehicle,"chassis_dummy");
    for(const char* name:{"wheel_rf_dummy","wheel_lf_dummy","wheel_rb_dummy","wheel_lb_dummy"})
        unique(vehicle,name);

    size_t high_atomics=0,low_atomics=0;
    FORLIST(link,vehicle->atomics) {
        auto atomic=rw::Atomic::fromClump(link);
        if(atomic->getFrame()==high)++high_atomics;
        if(atomic->getFrame()==low)++low_atomics;
    }
    if(high_atomics!=1 || low_atomics!=1 || high->objectList.count()!=1 || low->objectList.count()!=1)
        throw std::runtime_error("RC Bandit chassis frames must each own exactly one atomic");

    std::strcpy(gta::getNodeName(high),"chassis_hi");
    std::strcpy(gta::getNodeName(low),"chassis_vlo");
}
bool inside(rw::Frame* frame,rw::Frame* parent) {
    for(auto f=frame;f;f=f->getParent())if(f==parent)return true;return false;
}
rw::Matrix relative_to_root(rw::Frame* f,rw::Frame* root) {
    rw::Matrix m=f->matrix;
    for(auto p=f->getParent();p && p!=root;p=p->getParent())rw::Matrix::mult(&m,&m,&p->matrix);
    return m;
}
void check(rw::Clump* car) {
    if(unique(car,"chassis")==car->getFrame())throw std::runtime_error("SA chassis must be a child frame");
    auto main=unique(car,"wheel_rf_dummy");
    for(const char* name:{"wheel_lf_dummy","wheel_rb_dummy","wheel_lb_dummy"})unique(car,name);
    unsigned wheels=0;
    FORLIST(link,car->atomics) {
        auto a=rw::Atomic::fromClump(link);
        if(!a->getFrame() || !a->geometry || rw::Skin::get(a->geometry))throw std::runtime_error("Invalid automobile atomic");
        if(inside(a->getFrame(),main)) {
            ++wheels;
            if(!a->geometry->numVertices || a->geometry->instData)throw std::runtime_error("Main wheel is not editable geometry");
        }
    }
    if(wheels!=1)throw std::runtime_error("SA main wheel subtree must contain exactly one original wheel atomic");
    for(const char* name:{"wheel_lf_dummy","wheel_rm_dummy","wheel_lm_dummy","wheel_rb_dummy","wheel_lb_dummy"}) {
        auto f=unique(car,name,false);if(!f)continue;
        FORLIST(link,car->atomics)if(inside(rw::Atomic::fromClump(link)->getFrame(),f))throw std::runtime_error("Secondary wheel geometry would be duplicated by SA's loader");
    }
    for(int i=1;i<=6;++i) {
        auto name="extra"+std::to_string(i);auto f=unique(car,name.c_str(),false);if(!f)continue;
        if(f->objectList.count()!=1)throw std::runtime_error("SA extra has no single directly attached atomic");
    }
}
struct CarRenderStats {
    size_t envmap_materials=0;
    size_t envmap_textures=0;
    size_t envmat_materials=0;
    size_t envmat_materials_created=0;
    size_t envmap_xvehicleenv128_textures=0;
    size_t envmap_xv_vehicleenv128_remapped=0;
    size_t envmap_xv_vehicleenv128_unresolved=0;
    size_t envmap_atomics=0;
    size_t envmap_pipelines_activated=0;
    size_t envmap_pipelines_enabled=0;
    size_t envmap_atomics_without_normals=0;
    size_t nonopaque_materials=0;
};
float clamped_env_shininess(float coefficient) {
    if(!std::isfinite(coefficient))
        throw std::runtime_error("SA car ENVMAP material has a nonfinite reflection coefficient");
    if(coefficient<0.0f)return 0.0f;
    if(coefficient>1.0f)return 1.0f;
    return coefficient;
}
float lcs_env_shininess(float coefficient) {
    // The LCS vehicle MatFX shader writes envMapCoefficient * 0.5 to its
    // shininess input. Retain the existing normalized SA coefficient domain
    // before scaling: over-range source values must not bypass the reduction
    // by saturating the target's byte (some source materials exceed 1.0).
    return clamped_env_shininess(coefficient)*0.5f;
}
void set_envmat_shininess(gta::EnvMat* env,float coefficient) {
    // librwgta exposes the decoded value as getShininess() but has no setter;
    // EnvMat stores the normalized value in one byte and streams five floats.
    const auto scaled=clamped_env_shininess(coefficient)*255.0f;
    env->shininess=static_cast<rw::uint8>(std::lround(scaled));
}
CarRenderStats car_render_contract(rw::Clump* car,bool enable_missing_pipelines) {
    CarRenderStats stats;
    FORLIST(link,car->atomics) {
        auto atomic=rw::Atomic::fromClump(link);
        auto geometry=atomic->geometry;
        if(!geometry || (geometry->matList.numMaterials && !geometry->matList.materials))
            throw std::runtime_error("SA car atomic has an invalid material list");
        bool has_envmap=false;
        for(int i=0;i<geometry->matList.numMaterials;++i) {
            auto material=geometry->matList.materials[i];
            if(!material)throw std::runtime_error("SA car geometry has a null material");
            if(material->color.alpha!=255)++stats.nonopaque_materials;
            if(rw::MatFX::getEffects(material)!=rw::MatFX::ENVMAP)continue;
            auto matfx=rw::MatFX::get(material);
            if(!matfx)throw std::runtime_error("SA car ENVMAP material has no MatFX payload");
            const float coefficient=lcs_env_shininess(matfx->getEnvCoefficient());
            ++stats.envmap_materials;
            auto envmat=gta::getEnvMat(material);
            if(enable_missing_pipelines) {
                if(!envmat) {
                    envmat=new gta::EnvMat{};
                    *PLUGINOFFSET(gta::EnvMat*,material,gta::envMatOffset)=envmat;
                    ++stats.envmat_materials_created;
                }
                envmat->scaleX=8;
                envmat->scaleY=8;
                envmat->transScaleX=8;
                envmat->transScaleY=8;
                set_envmat_shininess(envmat,coefficient);
                envmat->texture=matfx->getEnvTexture();
            } else if(!envmat) {
                throw std::runtime_error("SA car ENVMAP material has no EnvMat plugin");
            }
            ++stats.envmat_materials;
            const float shine=envmat->getShininess();
            const auto expected_byte=static_cast<int>(std::lround(coefficient*255.0f));
            if(!std::isfinite(shine) || envmat->scaleX!=8 || envmat->scaleY!=8 ||
               envmat->transScaleX!=8 || envmat->transScaleY!=8 ||
               std::abs(static_cast<int>(envmat->shininess)-expected_byte)>0)
                throw std::runtime_error("SA car ENVMAP EnvMat values do not match its MatFX reflection coefficient");
            if(auto texture=matfx->getEnvTexture()) {
                ++stats.envmap_textures;
                // The LCS source refers to xv_vehicleenv128, but that raster
                // is absent from its PS2 texture dictionaries. SA's vehicle
                // renderer resolves xvehicleenv128 from the native vehicle TXD.
                if(enable_missing_pipelines &&
                   std::strcmp(texture->name,"xv_vehicleenv128")==0) {
                    constexpr char target[]="xvehicleenv128";
                    std::memset(texture->name,0,sizeof(texture->name));
                    std::memcpy(texture->name,target,sizeof(target));
                    ++stats.envmap_xv_vehicleenv128_remapped;
                }
                if(std::strcmp(texture->name,"xvehicleenv128")==0)
                    ++stats.envmap_xvehicleenv128_textures;
                else if(std::strcmp(texture->name,"xv_vehicleenv128")==0)
                    ++stats.envmap_xv_vehicleenv128_unresolved;
            }
            has_envmap=true;
        }
        if(!has_envmap)continue;
        ++stats.envmap_atomics;
        if(!(geometry->flags&rw::Geometry::NORMALS))++stats.envmap_atomics_without_normals;
        if(enable_missing_pipelines && !rw::MatFX::getEffects(atomic)) {
            rw::MatFX::enableEffects(atomic);
            ++stats.envmap_pipelines_activated;
        }
        if(rw::MatFX::getEffects(atomic))++stats.envmap_pipelines_enabled;
        else throw std::runtime_error("SA car ENVMAP material is attached to an atomic without the MatFX pipeline");
    }
    if(stats.envmap_xv_vehicleenv128_unresolved)
        throw std::runtime_error("SA car ENVMAP still references missing LCS texture xv_vehicleenv128");
    return stats;
}
CarRenderStats apply_sa_vehicle_common(rw::Clump* vehicle,const std::vector<uint8_t>& collision_bytes) {
    if(!vehicle || !vehicle->getFrame())throw std::runtime_error("Missing SA vehicle clump");
    if(!collision_bytes.empty()) {
        if(collision_bytes.size()>std::numeric_limits<rw::uint32>::max())
            throw std::runtime_error("Embedded vehicle collision data exceeds SA plugin capacity");
        auto collision=collision_plugin(vehicle);
        if(collision->data || collision->size)
            throw std::runtime_error("Original vehicle clump already contains collision plugin data");
    }
    const auto render=car_render_contract(vehicle,true);
    if(!collision_bytes.empty()) {
        auto collision=collision_plugin(vehicle);
        collision->data=new rw::uint8[collision_bytes.size()];
        collision->size=static_cast<rw::uint32>(collision_bytes.size());
        std::memcpy(collision->data,collision_bytes.data(),collision_bytes.size());
        validate_car_col3(collision->data,collision->size);
    }
    return render;
}
void check_sa_boat(rw::Clump* boat) {
    auto root=boat->getFrame();
    if(!root || std::strcmp(gta::getNodeName(root),"tropic"))
        throw std::runtime_error("SA ferry clump root must be named tropic");
    if(root->objectList.count())throw std::runtime_error("SA ferry root must not own geometry");

    auto high=unique(boat,"boat_hi");
    auto low=unique(boat,"boat_vlo");
    auto seat=unique(boat,"ped_frontseat");
    if(high==root || low==root || high==low)throw std::runtime_error("Invalid SA boat component hierarchy");
    auto ped_point=unique(boat,"Ped_point");
    if(!ped_point->getParent() || seat->getParent()!=ped_point->getParent() || seat->objectList.count() || seat->child)
        throw std::runtime_error("SA ferry seat marker must alias the retained Ped_point dummy");
    unsigned high_atoms=0,low_atoms=0,atomics=0,door_atoms=0;
    // Optional original door/ramp elements: one atomic attached directly to each of the four
    // door/ramp dummy frames (retail COLLAPSE layout).  All four or none; SA's boat renderer
    // treats any atomic that is not "boat_hi" / "*_vlo" as a hi-detail boat atomic.
    rw::Frame* door_frames[4]={unique(boat,"door_front_dummy"),unique(boat,"door_back_dummy"),
                               unique(boat,"ramp_front_dummy"),unique(boat,"ramp_back_dummy")};
    unsigned door_seen[4]={0,0,0,0};
    FORLIST(link,boat->atomics) {
        auto atomic=rw::Atomic::fromClump(link);++atomics;
        if(!atomic->getFrame() || !atomic->geometry || rw::Skin::get(atomic->geometry))
            throw std::runtime_error("Invalid original ferry atomic");
        if(atomic->getFrame()==high)++high_atoms;
        else if(atomic->getFrame()==low)++low_atoms;
        else for(int i=0;i<4;++i)if(atomic->getFrame()==door_frames[i]){++door_atoms;++door_seen[i];}
        if(atomic->geometry->numMorphTargets!=1 || !atomic->geometry->numVertices || !atomic->geometry->numTriangles)
            throw std::runtime_error("Invalid SA ferry atomic geometry");
    }
    if(atomics!=2+door_atoms || high_atoms!=1 || low_atoms!=1 || (door_atoms!=0 && door_atoms!=4) ||
       door_seen[0]>1 || door_seen[1]>1 || door_seen[2]>1 || door_seen[3]>1)
        throw std::runtime_error("SA ferry must retain its original high/low atomics plus either no or exactly one atomic on each door/ramp dummy");
    auto collision=collision_plugin(boat);
    if(!collision->data || !collision->size)throw std::runtime_error("SA ferry clump has no embedded collision plugin data");
    validate_tropic_col3(collision->data,collision->size);
    for(const char* name:{"Chassis_dummy","Chim_left","Light_front","Light_rear","Ped_point",
                          "car1_dummy","car2_dummy","car3_dummy","car4_dummy",
                          "door_front_dummy","door_back_dummy","ramp_front_dummy","ramp_back_dummy"})
        unique(boat,name);
    if(unique(boat,"FERRY",false) || unique(boat,"chassis_hi",false) || unique(boat,"chassis_vlo",false))
        throw std::runtime_error("LCS ferry component names remain in SA boat hierarchy");
}
}
void adapt_sa_vehicle_common(rw::Clump* vehicle,const std::filesystem::path& collision_path) {
    if(!vehicle || !vehicle->getFrame())throw std::runtime_error("Missing SA vehicle clump");
    std::vector<uint8_t> collision_bytes;
    if(!collision_path.empty())collision_bytes=read_car_collision(collision_path);
    const auto render=apply_sa_vehicle_common(vehicle,collision_bytes);
    std::cout<<"sa_vehicle_common=true envmap_materials="<<render.envmap_materials
             <<" envmap_materials_with_texture="<<render.envmap_textures
             <<" envmat_materials="<<render.envmat_materials
             <<" envmat_materials_created="<<render.envmat_materials_created
             <<" envmap_remap_xv_vehicleenv128_to_xvehicleenv128="<<render.envmap_xv_vehicleenv128_remapped
             <<" envmap_textures_xvehicleenv128="<<render.envmap_xvehicleenv128_textures
             <<" envmap_atomics="<<render.envmap_atomics
             <<" envmap_atomic_pipelines_activated="<<render.envmap_pipelines_activated
             <<" envmap_pipelines_enabled="<<render.envmap_pipelines_enabled
             <<" envmap_atomics_without_normals="<<render.envmap_atomics_without_normals
             <<" nonopaque_materials_preserved="<<render.nonopaque_materials
             <<" embedded_col_plugin="<<(collision_bytes.empty()?"false":"true")<<' ';
}
void validate_sa_vehicle_common(rw::Clump* vehicle) {
    if(!vehicle || !vehicle->getFrame())throw std::runtime_error("Missing SA vehicle clump");
    const auto render=car_render_contract(vehicle,false);
    auto collision=collision_plugin(vehicle);
    if(!collision->data || !collision->size)
        throw std::runtime_error("SA vehicle clump has no embedded collision plugin data");
    validate_car_col3(collision->data,collision->size);
    std::cout<<"sa_vehicle_common_validated=true atomics="<<vehicle->countAtomics()
             <<" envmap_materials="<<render.envmap_materials
             <<" envmap_materials_with_texture="<<render.envmap_textures
             <<" envmat_materials="<<render.envmat_materials
             <<" envmap_textures_xvehicleenv128="<<render.envmap_xvehicleenv128_textures
             <<" envmap_atomics="<<render.envmap_atomics
             <<" envmap_atomic_pipelines_activated="<<render.envmap_pipelines_activated
             <<" envmap_pipelines_enabled="<<render.envmap_pipelines_enabled
             <<" envmap_atomics_without_normals="<<render.envmap_atomics_without_normals
             <<" nonopaque_materials="<<render.nonopaque_materials
             <<" embedded_col_plugin=true game_executed=false\n";
}
void adapt_sa_car(rw::Clump* car,const std::filesystem::path& wheel_path,float wheel_scale,
                  const std::filesystem::path& collision_path) {
    if(!std::isfinite(wheel_scale) || wheel_scale<=0 || wheel_scale>4)throw std::runtime_error("Invalid original wheel scale");
    normalize_rcbandit_body_frame_names(car);
    if(unique(car,"chassis",false))throw std::runtime_error("Vehicle is already adapted or has ambiguous chassis");
    std::vector<uint8_t> collision_bytes;
    if(!collision_path.empty()) {
        collision_bytes=read_car_collision(collision_path);
        if(collision_bytes.size()>std::numeric_limits<rw::uint32>::max())
            throw std::runtime_error("Embedded car collision data exceeds SA plugin capacity");
        auto plugin=collision_plugin(car);
        if(plugin->data || plugin->size)throw std::runtime_error("Original car clump already contains collision plugin data");
    }
    auto chassis=unique(car,"chassis_hi");
    auto main=unique(car,"wheel_rf_dummy");
    for(const char* name:{"wheel_lf_dummy","wheel_rb_dummy","wheel_lb_dummy"})unique(car,name);
    if(main->child || main->objectList.count())throw std::runtime_error("LCS wheel dummy already contains geometry");
    auto wheel=read_car(wheel_path);
    if(wheel->countAtomics()!=1){wheel->destroy();throw std::runtime_error("Wheel input must contain its single original near atomic");}
    auto source=rw::Atomic::fromClump(wheel->atomics.link.next);
    if(!source->geometry || source->geometry->instData || rw::Skin::get(source->geometry)) {
        wheel->destroy();throw std::runtime_error("Wheel must be converted to editable geometry first");
    }
    auto instance=source->clone();auto frame=rw::Frame::create();
    // CSimpleModelInfo::CreateInstance resets the modelling-layout frame.
    // SA CollapseFramesCB also discards the wheel child's matrix, so bake the
    // exact LCS uniform wheel scale into the isolated original geometry.
    frame->matrix.setIdentity();
    auto g=instance->geometry;auto& morph=g->morphTargets[0];
    for(int i=0;i<g->numVertices;++i) {
        morph.vertices[i].x*=wheel_scale;morph.vertices[i].y*=wheel_scale;morph.vertices[i].z*=wheel_scale;
    }
    morph.boundingSphere.center.x*=wheel_scale;morph.boundingSphere.center.y*=wheel_scale;morph.boundingSphere.center.z*=wheel_scale;
    morph.boundingSphere.radius*=wheel_scale;
    std::strcpy(gta::getNodeName(frame),"wheel");
    main->addChild(frame);instance->setFrame(frame);car->addAtomic(instance);
    gta::setPipelineID(instance,0);wheel->destroy();
    std::strcpy(gta::getNodeName(chassis),"chassis");
    // SA detaches an extra and copies only that frame's local matrix. Preserve
    // the real root-relative transform when LCS stores its mesh in a child.
    unsigned extras=0;
    for(int i=1;i<=6;++i) {
        auto name="extra"+std::to_string(i);auto f=unique(car,name.c_str(),false);if(!f)continue;
        std::vector<rw::Atomic*> atoms;
        FORLIST(link,car->atomics) {auto a=rw::Atomic::fromClump(link);if(inside(a->getFrame(),f))atoms.push_back(a);}
        if(atoms.size()!=1)throw std::runtime_error("Original extra needs a multi-atomic native component adapter: "+name);
        auto target=atoms[0]->getFrame();auto transform=relative_to_root(target,car->getFrame());
        if(target!=f)std::strcpy(gta::getNodeName(f),("lcs_extra"+std::to_string(i)).c_str());
        target->removeChild();car->getFrame()->addChild(target);target->matrix=transform;
        std::strcpy(gta::getNodeName(target),name.c_str());++extras;
    }
    const auto render=apply_sa_vehicle_common(car,collision_bytes);
    check(car);
    std::cout<<"sa_car_chassis=true original_main_wheel=true native_cloned_wheels=true original_extras="<<extras
             <<" envmap_materials="<<render.envmap_materials
             <<" envmap_materials_with_texture="<<render.envmap_textures
             <<" envmat_materials="<<render.envmat_materials
             <<" envmat_materials_created="<<render.envmat_materials_created
             <<" envmap_remap_xv_vehicleenv128_to_xvehicleenv128="<<render.envmap_xv_vehicleenv128_remapped
             <<" envmap_textures_xvehicleenv128="<<render.envmap_xvehicleenv128_textures
             <<" envmap_atomics="<<render.envmap_atomics
             <<" envmap_atomic_pipelines_activated="<<render.envmap_pipelines_activated
             <<" envmap_pipelines_enabled="<<render.envmap_pipelines_enabled
             <<" envmap_atomics_without_normals="<<render.envmap_atomics_without_normals
             <<" nonopaque_materials_preserved="<<render.nonopaque_materials
             <<" embedded_col_plugin="<<(collision_bytes.empty()?"false":"true")<<' ';
}
void validate_sa_car(const std::filesystem::path& path) {
    auto car=read_car(path);
    CarRenderStats render{};
    try {
        check(car);
        render=car_render_contract(car,false);
        auto collision=collision_plugin(car);
        if(!collision->data || !collision->size)throw std::runtime_error("SA car clump has no embedded collision plugin data");
        validate_car_col3(collision->data,collision->size);
    }catch(...){car->destroy();throw;}
    const auto atoms=car->countAtomics();car->destroy();
    std::cout<<"sa_car_hierarchy_validated=true atomics="<<atoms
             <<" envmap_materials="<<render.envmap_materials
             <<" envmap_materials_with_texture="<<render.envmap_textures
             <<" envmat_materials="<<render.envmat_materials
             <<" envmap_textures_xvehicleenv128="<<render.envmap_xvehicleenv128_textures
             <<" envmap_atomics="<<render.envmap_atomics
             <<" envmap_atomic_pipelines_activated="<<render.envmap_pipelines_activated
             <<" envmap_pipelines_enabled="<<render.envmap_pipelines_enabled
             <<" envmap_atomics_without_normals="<<render.envmap_atomics_without_normals
             <<" nonopaque_materials="<<render.nonopaque_materials
             <<" game_executed=false\n";
}
void adapt_sa_boat(rw::Clump* boat,const std::filesystem::path& collision_path) {
    if(!boat || !boat->getFrame())throw std::runtime_error("Missing original ferry clump");
    auto collision_bytes=read_tropic_collision(collision_path);
    if(collision_bytes.size()>std::numeric_limits<rw::uint32>::max())
        throw std::runtime_error("Embedded ferry collision data exceeds SA plugin capacity");
    auto root=boat->getFrame();
    if(std::strcmp(gta::getNodeName(root),"FERRY"))
        throw std::runtime_error("Input is not the original LCS ferry clump");
    if(unique(boat,"tropic",false) || unique(boat,"boat_hi",false) || unique(boat,"boat_vlo",false))
        throw std::runtime_error("Ferry is already adapted or has ambiguous SA boat components");
    auto high=unique(boat,"chassis_hi");
    auto low=unique(boat,"chassis_vlo");
    auto ped_point=unique(boat,"Ped_point");
    if(!ped_point->getParent())throw std::runtime_error("Ferry Ped_point must have a parent frame");
    for(const char* name:{"Chassis_dummy","Chim_left","Light_front","Light_rear","Ped_point",
                          "car1_dummy","car2_dummy","car3_dummy","car4_dummy",
                          "door_front_dummy","door_back_dummy","ramp_front_dummy","ramp_back_dummy"})
        unique(boat,name);

    // The original ferry already has the two LOD atomics consumed by SA's
    // boat renderer. Keep both geometries, every dummy and all local matrices.
    std::strcpy(gta::getNodeName(root),"tropic");
    std::strcpy(gta::getNodeName(high),"boat_hi");
    std::strcpy(gta::getNodeName(low),"boat_vlo");
    auto seat=rw::Frame::create();
    if(!seat)throw std::runtime_error("Cannot create SA boat seat marker");
    seat->matrix=ped_point->matrix;
    std::strcpy(gta::getNodeName(seat),"ped_frontseat");
    ped_point->getParent()->addChild(seat);
    auto collision=collision_plugin(boat);
    if(collision->data || collision->size)throw std::runtime_error("Original ferry clump already contains collision plugin data");
    collision->data=new rw::uint8[collision_bytes.size()];
    collision->size=static_cast<rw::uint32>(collision_bytes.size());
    std::memcpy(collision->data,collision_bytes.data(),collision_bytes.size());
    check_sa_boat(boat);
    std::cout<<"sa_boat_slot=tropic original_high_low_atomics=true ferry_dummies_preserved=true embedded_col_plugin=true sa_seat_alias=true ";
}
void validate_sa_boat(const std::filesystem::path& path) {
    auto boat=read_car(path);
    try {check_sa_boat(boat);}catch(...){boat->destroy();throw;}
    const auto atoms=boat->countAtomics();boat->destroy();
    std::cout<<"sa_boat_hierarchy_validated=true atomics="<<atoms<<" game_executed=false\n";
}
void merge_vehicle_textures(const std::filesystem::path& own,const std::filesystem::path& shared,const std::filesystem::path& output) {
    if(std::filesystem::exists(output))throw std::runtime_error("Merged texture dictionary output already exists");
    auto read=[](const std::filesystem::path& p) {
        rw::StreamFile in;if(!in.open(p.string().c_str(),"rb") || !rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Cannot open vehicle texture dictionary");
        auto d=rw::TexDictionary::streamRead(&in);in.close();if(!d)throw std::runtime_error("Cannot decode vehicle texture dictionary");return d;
    };
    auto dictionary=read(own);auto parent=read(shared);std::set<std::string> names;
    auto key=[](std::string name){for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));return name;};
    FORLIST(link,dictionary->textures)if(!names.insert(key(rw::Texture::fromDict(link)->name)).second)throw std::runtime_error("Duplicate vehicle texture name");
    std::vector<rw::Texture*> inherited;
    FORLIST(link,parent->textures)inherited.push_back(rw::Texture::fromDict(link));
    size_t added=0;
    for(auto t:inherited)if(names.insert(key(t->name)).second){dictionary->add(t);++added;}
    parent->destroy();
    rw::version=0x36003;rw::build=0xFFFF;std::filesystem::create_directories(output.parent_path());
    rw::StreamFile out;if(!out.open(output.string().c_str(),"wb"))throw std::runtime_error("Cannot write merged vehicle texture dictionary");
    dictionary->streamWrite(&out);out.close();dictionary->destroy();
    if(!std::filesystem::file_size(output))throw std::runtime_error("Vehicle texture dictionary write failed");
    std::cout<<"own_texture_precedence=true inherited_original_textures="<<added<<" game_executed=false\n";
}
