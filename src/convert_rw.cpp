// Offline conversion. This library is never linked into the ASI or SA renderer.
#include <rw.h>
#include <rwgta.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <cmath>
#include <fstream>
#include <cstring>
#include "convert_vehicle.hpp"
#include "convert_vehicle_classes.hpp"
#include "convert_vehicle_aircraft.hpp"
#include "convert_vehicle_special.hpp"
#include "convert_vehicle_ambient.hpp"
#include "convert_ped.hpp"
#include "sa_map_lighting.hpp"

namespace fs=std::filesystem;
bool atomic_only=false;
bool white_map=false;
fs::path map_lighting;
fs::path vehicle_wheel;
fs::path vehicle_car_collision;
bool vehicle_dodo_plane=false;
bool vehicle_boat=false;
std::filesystem::path vehicle_boat_collision;
std::string vehicle_class;
fs::path vehicle_class_collision;
float vehicle_class_wheel_size=0.0f;
fs::path ped_reference;
float vehicle_wheel_scale=0;
std::string selected_atomic;
void map_inventory(const fs::path& input) {
    for(const auto& e:fs::directory_iterator(input)) {
        auto ext=e.path().extension().string();if(ext!=".dff" && ext!=".txd")continue;
        rw::StreamFile in;if(!in.open(e.path().string().c_str(),"rb"))throw std::runtime_error("Cannot open map asset");
        const auto name=e.path().stem().string();
        if(ext==".dff") {
            if(!rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Missing map clump");
            auto c=rw::Clump::streamRead(&in);if(!c)throw std::runtime_error("Cannot read map clump");
            FORLIST(link,c->atomics) {
                auto g=rw::Atomic::fromClump(link)->geometry;
                if(!g || (g->numVertices && !g->colors))throw std::runtime_error(name+": missing vertex prelight");
                for(int i=0;i<g->matList.numMaterials;++i) {
                    auto m=g->matList.materials[i];
                    if(!m || m->color.red!=255 || m->color.green!=255 || m->color.blue!=255)throw std::runtime_error(name+": nonwhite map material");
                    if(m->texture)std::cout<<"D\t"<<name<<'\t'<<m->texture->name<<'\n';
                }
                std::cout<<"G\t"<<name<<'\t'<<g->numVertices<<'\t'<<g->numTriangles<<'\n';
            }
            c->destroy();
        }else{
            if(!rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Missing map dictionary");
            auto d=rw::TexDictionary::streamRead(&in);if(!d)throw std::runtime_error("Cannot read map dictionary");
            FORLIST(link,d->textures)std::cout<<"T\t"<<name<<'\t'<<rw::Texture::fromDict(link)->name<<'\n';
            d->destroy();
        }
        in.close();
    }
}
void validate_col(const fs::path& path,const fs::path& mapped_output={}) {
    if(!mapped_output.empty() && fs::exists(mapped_output))throw std::runtime_error("Mapped collision output already exists");
    std::ifstream in(path,std::ios::binary);
    std::vector<uint8_t> bytes(size_t(fs::file_size(path)));in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!in)throw std::runtime_error("Cannot read collision file");
    size_t cursor=0,models=0,vertices=0,triangles=0,mapped_surfaces=0,defaulted=0;unsigned max_surface=0;
    // LCS SurfaceTable.h -> SA eSurfaceType.h. Grass/sand/water select the
    // corresponding SA category. Unrecognized LCS values use its default road
    // behavior; they must not index SA's surface array with values such as 255.
    constexpr uint8_t surfaces[]={0,1,9,6,25,4,63,45,66,50,64,51,52,53,57,56,55,62,29,39,42,70,43,67,68,41,18,58,59,60,61,69,54,33,34};
    auto surface=[&](size_t p){
        if(!mapped_output.empty()) {
            const auto old=bytes[p];const auto value=old<sizeof(surfaces) ? surfaces[old] : uint8_t(0);
            if(old>=sizeof(surfaces))++defaulted;
            if(old!=value)++mapped_surfaces;
            bytes[p]=value;
        }
        max_surface=std::max(max_surface,unsigned(bytes[p]));
    };
    auto u32=[&](size_t p){if(p>bytes.size() || bytes.size()-p<4)throw std::runtime_error("Truncated collision field");uint32_t v;std::memcpy(&v,bytes.data()+p,4);return v;};
    auto finite=[&](size_t p){float v;std::memcpy(&v,bytes.data()+p,4);if(!std::isfinite(v))throw std::runtime_error("Nonfinite collision coordinate");};
    while(cursor<bytes.size()) {
        if(u32(cursor)==0x324C4F43) {
            if(!mapped_output.empty())throw std::runtime_error("COL2 already uses SA surfaces; refusing to remap it as LCS");
            const uint64_t limit=uint64_t(cursor)+8+u32(cursor+4);
            if(limit>bytes.size() || limit<cursor+108)throw std::runtime_error("COL2 chunk bounds invalid");
            const size_t end=size_t(limit);
            auto u16=[&](size_t p){if(p+2>end)throw std::runtime_error("Truncated COL2 field");uint16_t v;std::memcpy(&v,bytes.data()+p,2);return v;};
            for(unsigned i=0;i<10;++i)finite(cursor+32+4*i);
            const auto ns=u16(cursor+72),nb=u16(cursor+74),nt=u16(cursor+76);
            if(ns>32767 || nb>32767 || nt>32767 || bytes[cursor+78] || bytes[cursor+79] ||
               u32(cursor+80)!=(ns||nb||nt ? 2u:0u) || u32(cursor+104))throw std::runtime_error("Unsupported COL2 flags/counts");
            auto array=[&](size_t field,size_t length){
                auto offset=u32(cursor+field);
                if(!length){if(offset)throw std::runtime_error("Unexpected empty COL2 offset");return size_t(0);}
                const uint64_t start=uint64_t(cursor)+4+offset;
                if(!offset || start<cursor+108 || start>end || length>end-start || start%4)
                    throw std::runtime_error("COL2 array outside/alignment of chunk");
                return size_t(start);
            };
            auto spheres=array(84,20*ns),boxes=array(88,28*nb);array(92,0);
            auto faces=array(100,8*nt);size_t nv=0;
            for(unsigned i=0;i<nt;++i)for(unsigned j=0;j<3;++j)nv=std::max(nv,size_t(u16(faces+8*i+2*j))+1);
            auto verts=array(96,6*nv);
            for(unsigned i=0;i<ns;++i){for(unsigned j=0;j<4;++j)finite(spheres+20*i+4*j);surface(spheres+20*i+16);if(bytes[spheres+20*i+19]!=255)throw std::runtime_error("Nonwhite COL2 sphere lighting");}
            for(unsigned i=0;i<nb;++i){for(unsigned j=0;j<6;++j)finite(boxes+28*i+4*j);surface(boxes+28*i+24);if(bytes[boxes+28*i+27]!=255)throw std::runtime_error("Nonwhite COL2 box lighting");}
            for(unsigned i=0;i<nt;++i){surface(faces+8*i+6);if(bytes[faces+8*i+7]!=255)throw std::runtime_error("Nonwhite COL2 face lighting");}
            // Reread through the native library's SA COL2 decoder as well.
            gta::CColModel reread;
            gta::readColModelVer2(&reread,bytes.data()+cursor+32,int(end-cursor-32));
            if(reread.numSpheres!=ns || reread.numBoxes!=nb || reread.numTriangles!=nt ||
               (nv && !reread.compVertices) || (nt && !reread.triangles))throw std::runtime_error("Native COL2 reread mismatch");
            for(unsigned i=0;i<nt;++i)if(reread.triangles[i].surface!=bytes[faces+8*i+6] || reread.triangles[i].lighting!=255)
                throw std::runtime_error("Native COL2 surface/light mismatch");
            ++models;vertices+=nv;triangles+=nt;cursor=end;continue;
        }
        if(u32(cursor)!=0x4C4C4F43)throw std::runtime_error("Expected legacy COLL chunk");
        const uint64_t end64=uint64_t(cursor)+8+u32(cursor+4);
        if(end64>bytes.size() || end64<cursor+92)throw std::runtime_error("Collision chunk bounds invalid");
        const size_t end=size_t(end64);size_t p=cursor+32;
        auto take=[&](size_t n){if(p>end || n>end-p)throw std::runtime_error("Collision data exceeds chunk");auto start=p;p+=n;return start;};
        auto count=[&](){auto v=u32(take(4));if(v>32767)throw std::runtime_error("Collision count exceeds legacy signed 16-bit capacity");return v;};
        auto bounds=take(40);for(unsigned i=0;i<10;++i)finite(bounds+4*i);
        auto ns=count();auto spheres=take(20*ns);
        for(unsigned i=0;i<ns;++i){for(unsigned j=0;j<4;++j)finite(spheres+20*i+4*j);surface(spheres+20*i+16);}
        auto nl=count();auto lines=take(24*nl);for(unsigned i=0;i<6*nl;++i)finite(lines+4*i);
        auto nb=count();auto boxes=take(28*nb);
        for(unsigned i=0;i<nb;++i){for(unsigned j=0;j<6;++j)finite(boxes+28*i+4*j);surface(boxes+28*i+24);}
        auto nv=count();auto verts=take(12*nv);for(unsigned i=0;i<3*nv;++i)finite(verts+4*i);
        auto nt=count();auto faces=take(16*nt);
        for(unsigned i=0;i<nt;++i){for(unsigned j=0;j<3;++j)if(u32(faces+16*i+4*j)>=nv)throw std::runtime_error("Collision triangle vertex index outside model");surface(faces+16*i+12);}
        if(p!=end)throw std::runtime_error("Unparsed legacy collision bytes");
        ++models;vertices+=nv;triangles+=nt;cursor=end;
    }
    if(!mapped_output.empty()) {
        fs::create_directories(mapped_output.parent_path());std::ofstream out(mapped_output,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!out)throw std::runtime_error("Cannot write mapped collision");
    }
    std::cout<<"collision_models="<<models<<" vertices="<<vertices<<" triangles="<<triangles<<" maximum_surface_id="<<max_surface<<" mapped_surfaces="<<mapped_surfaces<<" defaulted_unknown_surfaces="<<defaulted<<" collision_validation_passed=true game_executed=false\n";
}
void validate_assets(const fs::path& input,bool actors=false) {
    size_t models=0,dictionaries=0,vertices=0,triangles=0,textures=0;
    auto finite=[](const rw::V3d& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    for(const auto& e:fs::recursive_directory_iterator(input)) {
        auto ext=e.path().extension().string();
        if(!e.is_regular_file() || (ext!=".dff" && ext!=".txd"))continue;
        try {
            rw::StreamFile in;
            if(!in.open(e.path().string().c_str(),"rb"))throw std::runtime_error("Cannot open asset");
            if(ext==".dff") {
                if(!rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Missing clump");
                auto clump=rw::Clump::streamRead(&in);if(!clump)throw std::runtime_error("Cannot reread clump");
                size_t atomics=0;
                FORLIST(link,clump->atomics) {
                    ++atomics;auto a=rw::Atomic::fromClump(link);auto g=a->geometry;
                    if(!a->getFrame() || !g || g->instData || (g->flags&rw::Geometry::NATIVE) ||
                       g->numVertices<0 || g->numVertices>65535 || g->numTriangles<0 ||
                       g->numMorphTargets!=1 || g->numTexCoordSets<0 || g->numTexCoordSets>8)
                        throw std::runtime_error("Invalid map geometry layout");
                    if(auto skin=rw::Skin::get(g)) {
                        if(!actors)throw std::runtime_error("Unexpected skinned map geometry");
                        const auto hierarchy=rw::HAnimHierarchy::get(clump);
                        if(!hierarchy || skin->numBones<=0 || skin->numBones>256 || hierarchy->numNodes!=skin->numBones ||
                           !skin->inverseMatrices || (g->numVertices && (!skin->indices || !skin->weights)))
                            throw std::runtime_error("Skin does not match its animation hierarchy");
                        std::set<int> bone_ids;
                        for(int i=0;i<hierarchy->numNodes;++i)if(!bone_ids.insert(hierarchy->nodeInfo[i].id).second)throw std::runtime_error("Duplicate actor bone ID");
                        // RW matrices contain 12 floats plus flag/padding words;
                        // the fourth word of each row is not a float coefficient.
                        for(int i=0;i<16*skin->numBones;++i)if(i%4!=3 && !std::isfinite(skin->inverseMatrices[i]))throw std::runtime_error("Nonfinite inverse bone matrix coefficient");
                        for(int i=0;i<g->numVertices;++i) {
                            float sum=0;
                            for(int k=0;k<4;++k) {
                                const auto w=skin->weights[4*i+k];
                                if(!std::isfinite(w) || w<0 || w>1.00001f || (w>0 && skin->indices[4*i+k]>=skin->numBones))
                                    throw std::runtime_error("Invalid actor skin influence");
                                sum+=w;
                            }
                            if(std::abs(sum-1.f)>0.002f)throw std::runtime_error("Actor skin weights are not normalized");
                        }
                    }
                    if(g->numVertices && !g->morphTargets[0].vertices)throw std::runtime_error("Missing map vertices");
                    for(int i=0;i<g->numVertices;++i) {
                        if(!finite(g->morphTargets[0].vertices[i]))throw std::runtime_error("Nonfinite map vertex");
                        if(g->morphTargets[0].normals && !finite(g->morphTargets[0].normals[i]))throw std::runtime_error("Nonfinite normal");
                        for(int uv=0;uv<g->numTexCoordSets;++uv)
                            if(!g->texCoords[uv] || !std::isfinite(g->texCoords[uv][i].u) || !std::isfinite(g->texCoords[uv][i].v))throw std::runtime_error("Invalid texture coordinates");
                    }
                    for(int i=0;i<g->numTriangles;++i) {
                        const auto& tri=g->triangles[i];
                        if(tri.matId>=g->matList.numMaterials || tri.v[0]>=g->numVertices || tri.v[1]>=g->numVertices || tri.v[2]>=g->numVertices)throw std::runtime_error("Triangle index outside geometry");
                    }
                    if(g->meshHeader) {
                        uint64_t total=0;auto meshes=g->meshHeader->getMeshes();
                        for(unsigned i=0;i<g->meshHeader->numMeshes;++i) {
                            const auto& mesh=meshes[i];total+=mesh.numIndices;
                            if(!mesh.material || (mesh.numIndices && !mesh.indices))throw std::runtime_error("Invalid binmesh layout");
                            for(unsigned j=0;j<mesh.numIndices;++j)if(mesh.indices[j]>=g->numVertices)throw std::runtime_error("Binmesh index outside geometry");
                        }
                        if(total!=g->meshHeader->totalIndices)throw std::runtime_error("Binmesh index count mismatch");
                    }
                    for(int i=0;i<g->matList.numMaterials;++i) {
                        auto material=g->matList.materials[i];if(!material)throw std::runtime_error("Missing material");
                        if(material->texture && !g->numTexCoordSets)throw std::runtime_error("Textured material has no UVs");
                    }
                    vertices+=g->numVertices;triangles+=g->numTriangles;
                }
                if(!atomics || (!actors && atomics!=1))throw std::runtime_error("Unexpected converted DFF atomic count");
                clump->destroy();++models;
            }else{
                if(!rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Missing texture dictionary");
                auto dict=rw::TexDictionary::streamRead(&in);if(!dict)throw std::runtime_error("Cannot reread native texture dictionary");
                FORLIST(link,dict->textures) {
                    auto tex=rw::Texture::fromDict(link);auto raster=tex->raster;
                    if(!raster || raster->platform!=rw::PLATFORM_D3D9 || raster->width<=0 || raster->height<=0 || raster->getNumLevels()<=0)throw std::runtime_error("Invalid native D3D9 raster");
                    ++textures;
                }
                dict->destroy();++dictionaries;
            }
            in.close();
        }catch(const std::exception& ex){throw std::runtime_error(e.path().string()+": "+ex.what());}
    }
    std::cout<<"models="<<models<<" dictionaries="<<dictionaries<<" textures="<<textures<<" vertices="<<vertices<<" triangles="<<triangles<<" reread_validation_passed=true game_executed=false\n";
}
void inspect_frames(rw::Frame* frame) {
    std::cout<<"frame="<<gta::getNodeName(frame)<<'\n';
    std::cout<<"frame_bone name="<<gta::getNodeName(frame)<<" bone_tag="<<rw::HAnimData::get(frame)->id<<'\n';
    const auto& m=frame->matrix;
    std::cout<<"frame_matrix name="<<gta::getNodeName(frame)<<" right="<<m.right.x<<','<<m.right.y<<','<<m.right.z<<" up="<<m.up.x<<','<<m.up.y<<','<<m.up.z<<" at="<<m.at.x<<','<<m.at.y<<','<<m.at.z<<" pos="<<m.pos.x<<','<<m.pos.y<<','<<m.pos.z<<'\n';
    for(auto child=frame->child;child;child=child->next)inspect_frames(child);
}
void inspect_dff(const fs::path& input) {
    rw::StreamFile in;
    if(!in.open(input.string().c_str(),"rb") || !rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr)) throw std::runtime_error("Cannot open clump");
    auto clump=rw::Clump::streamRead(&in);if(!clump) throw std::runtime_error("Cannot read clump");
    std::set<std::string> textures;
    bool skinned=false;
    FORLIST(link,clump->atomics) {
        auto a=rw::Atomic::fromClump(link);
        skinned=skinned || rw::Skin::get(a->geometry)!=nullptr;
        std::cout<<"atomic="<<gta::getNodeName(a->getFrame())<<" vertices="<<a->geometry->numVertices<<'\n';
        if(!a->geometry->instData && a->geometry->numVertices && a->geometry->morphTargets[0].vertices) {
            rw::V3d low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
            for(int i=0;i<a->geometry->numVertices;++i) {
                const auto& v=a->geometry->morphTargets[0].vertices[i];
                low.x=std::min(low.x,v.x);low.y=std::min(low.y,v.y);low.z=std::min(low.z,v.z);
                high.x=std::max(high.x,v.x);high.y=std::max(high.y,v.y);high.z=std::max(high.z,v.z);
            }
            std::cout<<"atomic_bounds name="<<gta::getNodeName(a->getFrame())<<" min="<<low.x<<','<<low.y<<','<<low.z<<" max="<<high.x<<','<<high.y<<','<<high.z<<'\n';
        }
        for(int i=0;i<a->geometry->matList.numMaterials;++i) {
            auto tex=a->geometry->matList.materials[i]->texture;if(tex) textures.insert(tex->name);
        }
    }
    for(const auto& name:textures) std::cout<<"texture="<<name<<'\n';
    std::cout<<"skinned="<<(skinned ? "true":"false")<<'\n';
    inspect_frames(clump->getFrame());
    if(auto h=rw::HAnimHierarchy::get(clump)) {
        std::cout<<"bones="<<h->numNodes<<'\n';
        for(int i=0;i<h->numNodes;++i) std::cout<<"bone="<<h->nodeInfo[i].id<<" flags="<<h->nodeInfo[i].flags<<'\n';
    }
    clump->destroy();in.close();
}
void texture_index(const fs::path& input) {
    for(const auto& e:fs::recursive_directory_iterator(input)) {
        auto ext=e.path().extension().string();for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(!e.is_regular_file() || ext!=".txd") continue;
        rw::StreamFile in;
        if(!in.open(e.path().string().c_str(),"rb") || !rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr)) throw std::runtime_error("Cannot open texture dictionary");
        auto dict=rw::TexDictionary::streamRead(&in);if(!dict) throw std::runtime_error("Cannot decode texture dictionary");
        FORLIST(link,dict->textures) {auto tex=rw::Texture::fromDict(link);std::cout<<tex->name<<'\t'<<e.path().string()<<'\n';}
        dict->destroy();in.close();
    }
}
void convert_dff(rw::StreamFile& in,rw::StreamFile& out) {
    rw::ChunkHeaderInfo header{};
    if(!rw::findChunk(&in,rw::ID_CLUMP,&header.length,&header.version)) throw std::runtime_error("No RenderWare clump");
    auto clump=rw::Clump::streamRead(&in);
    if(!clump) throw std::runtime_error("Cannot decode RenderWare clump");
    if(atomic_only || !selected_atomic.empty()) {
        std::vector<rw::Atomic*> all;
        rw::Atomic* selected=nullptr;int best=INT_MAX;
        FORLIST(link,clump->atomics) {
            auto a=rw::Atomic::fromClump(link); all.push_back(a);
            std::string name=gta::getNodeName(a->getFrame());
            if(!selected_atomic.empty()) {
                if(name==selected_atomic) {
                    if(selected)throw std::runtime_error("Duplicate requested atomic");
                    selected=a;best=0;
                }
                continue;
            }
            auto suffix=name.find_last_of('_'); int lod=0;
            if(suffix!=std::string::npos && suffix+2<name.size() &&
               (name[suffix+1]=='l' || name[suffix+1]=='L') && std::isdigit(static_cast<unsigned char>(name[suffix+2]))) lod=std::stoi(name.substr(suffix+2));
            if(lod<best) {best=lod;selected=a;}
        }
        if(!selected || best!=0) {clump->destroy();throw std::runtime_error("Map model has no near atomic");}
        for(auto a:all) if(a!=selected) {clump->removeAtomic(a);a->destroy();}
        // SA interprets _dam suffixes as damage atomics. Map data uses the LCS
        // _l0/_l1 convention; its selected near geometry is a regular atomic.
        std::strcpy(gta::getNodeName(selected->getFrame()),"lcs_near");
    }
    size_t atomics=0,vertices=0,skins=0;
    FORLIST(link,clump->atomics) {
        auto a=rw::Atomic::fromClump(link); auto g=a->geometry;
        if(g->instData) {
            rw::platform=g->instData->platform;
            rw::switchPipes(a,rw::platform);
            a->uninstance();
            if(g->instData) { clump->destroy(); throw std::runtime_error("Cannot uninstance native geometry"); }
        }
        ++atomics; vertices+=g->numVertices;
        if(white_map) {
            for(int i=0;i<g->matList.numMaterials;++i) {
                auto mat=g->matList.materials[i];mat->color.red=mat->color.green=mat->color.blue=255;
            }
            if(!g->colors && g->numVertices) {
                g->colors=rwNewT(rw::RGBA,g->numVertices,0);g->flags|=rw::Geometry::PRELIT;
                for(int i=0;i<g->numVertices;++i)g->colors[i]={255,255,255,255};
            }
            // Keep the source vertex prelight; only material tint is neutralized.
        }
        if(rw::Skin::get(g)) ++skins;
        // SA will select its native object/ped pipeline after loading.
        gta::setPipelineID(a,0);
        if(white_map)SaMapLighting(map_lighting).apply(a);
    }
    if(!vehicle_wheel.empty()) {
        if(vehicle_dodo_plane)adapt_sa_dodo_plane(clump,vehicle_wheel,vehicle_wheel_scale,vehicle_car_collision);
        else adapt_sa_car(clump,vehicle_wheel,vehicle_wheel_scale,vehicle_car_collision);
    }
    if(vehicle_boat)adapt_sa_boat(clump, vehicle_boat_collision);
    if(vehicle_class=="bike")adapt_sa_bike(clump,vehicle_class_collision);
    if(vehicle_class=="generic-boat")adapt_sa_generic_boat(clump,vehicle_class_collision);
    if(vehicle_class=="heli")adapt_sa_heli(clump,vehicle_class_collision,vehicle_class_wheel_size);
    if(vehicle_class=="train")adapt_sa_train(clump,vehicle_class_collision);
    if(vehicle_class=="ambient-plane")adapt_sa_ambient_plane(clump,vehicle_class_collision,vehicle_class_wheel_size);
    if(!ped_reference.empty())clump=adapt_sa_ped(clump,ped_reference);
    rw::platform=rw::PLATFORM_D3D9;
    rw::switchPipes(clump,rw::platform);
    rw::version=0x36003; rw::build=0xFFFF;
    clump->streamWrite(&out);
    clump->destroy();
    std::cout<<"atomics="<<atomics<<" vertices="<<vertices<<" skinned="<<skins;
}
void convert_txd(rw::StreamFile& in,rw::StreamFile& out) {
    rw::ChunkHeaderInfo header{};
    if(!rw::findChunk(&in,rw::ID_TEXDICTIONARY,&header.length,&header.version)) throw std::runtime_error("No texture dictionary");
    auto dict=rw::TexDictionary::streamRead(&in);
    if(!dict) throw std::runtime_error("Cannot decode texture dictionary");
    size_t count=0;
    FORLIST(link,dict->textures) {
        auto tex=rw::Texture::fromDict(link); auto raster=tex->raster;
        if(!raster) { dict->destroy(); throw std::runtime_error("Texture has no raster"); }
        if(raster->platform==rw::PLATFORM_D3D8) {
            // The d3d8/d3d9 offline raster storage is shared; the writer emits
            // each format's correct native header without changing mip data.
            raster->platform=rw::PLATFORM_D3D9;
        } else if(raster->platform!=rw::PLATFORM_D3D9) {
            auto image=raster->toImage();
            if(!image) { dict->destroy(); throw std::runtime_error("Cannot decode texture pixels"); }
            rw::platform=rw::PLATFORM_D3D9;
            auto converted=rw::Raster::createFromImage(image);
            image->destroy();
            if(!converted) { dict->destroy(); throw std::runtime_error("Cannot create D3D9 raster"); }
            tex->raster=converted; raster->destroy();
        }
        ++count;
    }
    rw::platform=rw::PLATFORM_D3D9; rw::version=0x36003; rw::build=0xFFFF;
    dict->streamWrite(&out); dict->destroy();
    std::cout<<"textures="<<count;
}
void convert(const fs::path& input,const fs::path& output) {
        if(fs::exists(output)) throw std::runtime_error("Output already exists");
        fs::create_directories(output.parent_path());
        rw::platform=rw::PLATFORM_D3D8;
        rw::StreamFile in,out;
        if(!in.open(input.string().c_str(),"rb")) throw std::runtime_error("Cannot open input");
        const auto temporary=output.string()+".converting";
        if(!out.open(temporary.c_str(),"wb")) throw std::runtime_error("Cannot open output");
        try {
            auto ext=input.extension().string();
            for(auto& c:ext) c=char(std::tolower(static_cast<unsigned char>(c)));
            if(ext==".dff") convert_dff(in,out);
            else if(ext==".txd") convert_txd(in,out);
            else throw std::runtime_error("Expected dff or txd");
        } catch(...) { out.close(); fs::remove(temporary); throw; }
        out.close(); in.close();
        fs::rename(temporary,output);
        std::cout<<" rw=3.6.0.3 output="<<output.string()<<'\n';
}
int main(int argc,char** argv) {
    try {
        if(argc==4 && std::string(argv[2])=="--map-col"){validate_col(argv[1],argv[3]);return 0;}
        const bool car=(argc==6 || argc==7) && std::string(argv[3])=="--sa-car";
        const bool dodo_plane=(argc==6 || argc==7) && std::string(argv[3])=="--sa-dodo-plane";
        const bool boat=argc==5 && std::string(argv[3])=="--sa-boat";
        const bool heli=argc==6 && std::string(argv[3])=="--sa-heli";
        const bool ambient_plane=argc==6 && std::string(argv[3])=="--sa-ambient-plane";
        const bool class_vehicle=heli || ambient_plane || (argc==5 && (std::string(argv[3])=="--sa-bike" || std::string(argv[3])=="--sa-generic-boat" || std::string(argv[3])=="--sa-train"));
        const bool ped=argc==5 && std::string(argv[3])=="--sa-ped";
        const bool merge=argc==5 && std::string(argv[2])=="--merge-txd";
        const bool named=argc==5 && std::string(argv[3])=="--atomic-name";
        const bool lit_map=argc==5 && std::string(argv[3])=="--atomic-white";
        if(argc!=3 && !(argc==4 && (std::string(argv[3])=="--atomic"||std::string(argv[3])=="--atomic-white")) && !car && !dodo_plane && !boat && !class_vehicle && !ped && !merge && !named && !lit_map) throw std::runtime_error("Usage: lcs-convert-rw INPUT OUTPUT [--atomic | --atomic-white [LIGHTING.psl] | --atomic-name NAME | --sa-boat COLLISION.col | --sa-bike COLLISION.col | --sa-generic-boat COLLISION.col | --sa-heli COLLISION.col WHEEL_SIZE | --sa-ambient-plane COLLISION.col WHEEL_SIZE | --sa-train COLLISION.col | --sa-car WHEEL.dff SCALE [COLLISION.col] | --sa-dodo-plane WHEEL.dff SCALE COLLISION.col | --sa-ped NATIVE_PED.dff]; OWN.txd --merge-txd SHARED.txd OUTPUT.txd");
        vehicle_dodo_plane=dodo_plane;
        if(class_vehicle){vehicle_class=std::string(argv[3]).substr(5);vehicle_class_collision=argv[4];}
        if(heli||ambient_plane)vehicle_class_wheel_size=std::stof(argv[5]);
        if(ped)ped_reference=argv[4];
        white_map=(argc==4||lit_map)&&std::string(argv[3])=="--atomic-white";
        vehicle_boat=boat;
        if(boat)vehicle_boat_collision=argv[4];
        atomic_only=(argc==4 && (std::string(argv[3])=="--atomic" || std::string(argv[3])=="--atomic-white"))||lit_map;if(lit_map)map_lighting=argv[4];if(named)selected_atomic=argv[4];if(car||dodo_plane){vehicle_wheel=argv[4];vehicle_wheel_scale=std::stof(argv[5]);if(argc==7)vehicle_car_collision=argv[6];}
        fs::path input=argv[1],output=argv[2];
        bool info=std::string(argv[2])=="--info" || std::string(argv[2])=="--texture-index" || std::string(argv[2])=="--validate" || std::string(argv[2])=="--validate-actors" || std::string(argv[2])=="--validate-col" || std::string(argv[2])=="--validate-sa-car" || std::string(argv[2])=="--validate-sa-boat" || std::string(argv[2])=="--validate-sa-bike" || std::string(argv[2])=="--validate-sa-generic-boat" || std::string(argv[2])=="--validate-sa-heli" || std::string(argv[2])=="--map-inventory";
        info=info || std::string(argv[2])=="--validate-sa-dodo-plane" || std::string(argv[2])=="--validate-sa-train" || std::string(argv[2])=="--validate-sa-ambient-plane" || std::string(argv[2])=="--validate-sa-rhino-muzzle";
        if(!info && fs::exists(output)) throw std::runtime_error("Output already exists");
        rw::Engine::init(); gta::attachPlugins(); rw::Engine::open(nullptr); rw::Engine::start();
        rw::Texture::setLoadTextures(false);
        rw::Texture::setCreateDummies(true);
        if(merge) {
            merge_vehicle_textures(input,argv[3],argv[4]);
            rw::Engine::stop();rw::Engine::close();rw::Engine::term();return 0;
        }
        if(info) {
            if(std::string(argv[2])=="--map-inventory")map_inventory(input);
            else if(std::string(argv[2])=="--texture-index") texture_index(input);
            else if(std::string(argv[2])=="--validate")validate_assets(input);
            else if(std::string(argv[2])=="--validate-actors")validate_assets(input,true);
            else if(std::string(argv[2])=="--validate-col")validate_col(input);
            else if(std::string(argv[2])=="--validate-sa-car")validate_sa_car(input);
            else if(std::string(argv[2])=="--validate-sa-rhino-muzzle")validate_sa_rhino_muzzle(input);
            else if(std::string(argv[2])=="--validate-sa-boat")validate_sa_boat(input);
            else if(std::string(argv[2])=="--validate-sa-bike")validate_sa_bike(input);
            else if(std::string(argv[2])=="--validate-sa-generic-boat")validate_sa_generic_boat(input);
            else if(std::string(argv[2])=="--validate-sa-heli")validate_sa_heli(input);
            else if(std::string(argv[2])=="--validate-sa-dodo-plane")validate_sa_dodo_plane(input);
            else if(std::string(argv[2])=="--validate-sa-train")validate_sa_train(input);
            else if(std::string(argv[2])=="--validate-sa-ambient-plane")validate_sa_ambient_plane(input);
            else inspect_dff(input);
            rw::Engine::stop();rw::Engine::close();rw::Engine::term();return 0;
        }
        size_t converted=0;
        if(fs::is_directory(input)) {
            std::vector<fs::path> files;
            for(const auto& e:fs::recursive_directory_iterator(input)) {
                auto ext=e.path().extension().string();
                for(auto& c:ext) c=char(std::tolower(static_cast<unsigned char>(c)));
                if(e.is_regular_file() && (ext==".dff" || ext==".txd")) files.push_back(e.path());
            }
            for(const auto& file:files) {
                std::cout<<"input="<<file.string()<<' '<<std::flush;
                convert(file,output/fs::relative(file,input)); ++converted;
            }
        } else { convert(input,output); ++converted; }
        rw::Engine::stop(); rw::Engine::close(); rw::Engine::term();
        std::cout<<"converted="<<converted<<" game_executed=false\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
