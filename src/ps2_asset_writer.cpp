// Offline, bounded intermediate -> native SA RenderWare. Never linked to ASI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <rw.h>
#include <rwgta.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include "sa_map_lighting.hpp"
namespace fs=std::filesystem;
rw::Raster* sa_raster(rw::Image* image) {
    auto raster=rw::Raster::createFromImage(image);
    if(!raster)throw std::runtime_error("Cannot create SA raster");
    // D3D9 opaque C888 rasters use four bytes per pixel (X8R8G8B8).
    // librw writes the RGB bytes only; initialize X explicitly so neither
    // serialized TXDs nor shader reads contain allocator contents. Keep
    // the opaque format/flag; genuine source alpha remains in C8888.
    if((raster->format&0xF00)==rw::Raster::C888) {
        auto pixels=raster->lock(0,rw::Raster::LOCKWRITE);
        if(!pixels)throw std::runtime_error("Cannot lock SA raster");
        if(raster->stride<raster->width*4)throw std::runtime_error("Unexpected D3D9 raster stride");
        for(int y=0;y<raster->height;++y) {
            auto row=pixels+y*raster->stride;
            for(int x=0;x<raster->width;++x)row[x*4+3]=255;
            std::memset(row+raster->width*4,0,raster->stride-raster->width*4);
        }
        raster->unlock(0);
    }
    return raster;
}
struct Reader {
    std::ifstream file;
    size_t remaining;
    explicit Reader(const fs::path& path):file(path,std::ios::binary),remaining(size_t(fs::file_size(path))) {
        if(remaining>512*1024*1024 || !file)throw std::runtime_error("Invalid intermediate file");
    }
    void bytes(void* data,size_t n) {
        if(n>remaining)throw std::runtime_error("Truncated intermediate");
        file.read(static_cast<char*>(data),std::streamsize(n));
        if(!file)throw std::runtime_error("Cannot read intermediate");remaining-=n;
    }
    template<class T>T get(){T v;bytes(&v,sizeof(v));return v;}
    void magic(const char* expected){char actual[4];bytes(actual,4);if(std::memcmp(expected,actual,4))throw std::runtime_error("Invalid intermediate magic");}
    std::string name() {char value[32];bytes(value,32);if(!std::memchr(value,0,32))throw std::runtime_error("Unterminated name");return value;}
    void done(){if(remaining)throw std::runtime_error("Unparsed intermediate bytes");}
};
void write_mesh(const fs::path& input,const fs::path& output,const SaMapLighting& lighting) {
    Reader r(input);r.magic("PSM1");
    auto nv=r.get<uint32_t>(),nt=r.get<uint32_t>(),nm=r.get<uint32_t>();
    if(!nv || nv>65535 || nt>1000000 || !nm || nm>65535)throw std::runtime_error("Geometry exceeds SA capacity");
    auto g=rw::Geometry::create(int(nv),int(nt),rw::Geometry::POSITIONS|rw::Geometry::TEXTURED|rw::Geometry::PRELIT|rw::Geometry::NORMALS|rw::Geometry::LIGHT|rw::Geometry::MODULATE);
    for(uint32_t i=0;i<nm;++i) {
        auto name=r.name();auto mat=rw::Material::create();mat->color={255,255,255,255};
        mat->surfaceProps={1.f,0.f,1.f};
        if(!name.empty()) {auto tex=rw::Texture::create(nullptr);std::strncpy(tex->name,name.c_str(),31);tex->setFilter(rw::Texture::LINEAR);mat->setTexture(tex);tex->destroy();}
        g->matList.appendMaterial(mat);mat->destroy();
    }
    for(uint32_t i=0;i<nv;++i) {
        auto& v=g->morphTargets[0].vertices[i];r.bytes(&v,12);r.bytes(g->texCoords[0]+i,8);r.bytes(g->colors+i,4);
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||!std::isfinite(g->texCoords[0][i].u)||!std::isfinite(g->texCoords[0][i].v))throw std::runtime_error("Nonfinite mesh coordinate");
        g->morphTargets[0].normals[i]={0,0,0};
    }
    for(uint32_t i=0;i<nt;++i) {
        auto& t=g->triangles[i];r.bytes(&t,8);
        if(t.v[0]>=nv||t.v[1]>=nv||t.v[2]>=nv||t.matId>=nm)throw std::runtime_error("Triangle index outside geometry");
        auto a=g->morphTargets[0].vertices[t.v[0]],b=g->morphTargets[0].vertices[t.v[1]],c=g->morphTargets[0].vertices[t.v[2]];
        rw::V3d u={b.x-a.x,b.y-a.y,b.z-a.z},v={c.x-a.x,c.y-a.y,c.z-a.z};
        rw::V3d n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        for(int j=0;j<3;++j){auto& target=g->morphTargets[0].normals[t.v[j]];target.x+=n.x;target.y+=n.y;target.z+=n.z;}
    }
    r.done();
    for(uint32_t i=0;i<nv;++i){auto& n=g->morphTargets[0].normals[i];auto len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(len>0){n.x/=len;n.y/=len;n.z/=len;}else n={0,0,1};}
    g->calculateBoundingSphere();g->buildMeshes();
    auto c=rw::Clump::create();auto frame=rw::Frame::create();std::strncpy(gta::getNodeName(frame),output.stem().string().c_str(),23);
    c->setFrame(frame);auto a=rw::Atomic::create();a->setFrame(frame);a->setGeometry(g,0);g->destroy();c->addAtomic(a);
    lighting.apply(a);
    rw::StreamFile out;if(!out.open(output.string().c_str(),"wb"))throw std::runtime_error("Cannot open DFF output");
    if(!c->streamWrite(&out))throw std::runtime_error("Cannot write DFF");out.close();c->destroy();
}
void write_textures(const fs::path& input,const fs::path& output) {
    Reader r(input);r.magic("PST1");auto count=r.get<uint32_t>();if(count>4096)throw std::runtime_error("Too many textures");
    auto dict=rw::TexDictionary::create();
    for(uint32_t i=0;i<count;++i) {
        auto name=r.name();auto w=r.get<uint32_t>(),h=r.get<uint32_t>();if(!w||!h||w>2048||h>2048)throw std::runtime_error("Invalid raster shape");
        auto img=rw::Image::create(int(w),int(h),32);img->allocate();
        for(uint32_t y=0;y<h;++y)r.bytes(img->pixels+y*img->stride,w*4);
        auto ras=sa_raster(img);img->destroy();
        auto tex=rw::Texture::create(ras);std::strncpy(tex->name,name.c_str(),31);tex->setFilter(rw::Texture::LINEAR);dict->add(tex);
    }
    r.done();rw::StreamFile out;if(!out.open(output.string().c_str(),"wb"))throw std::runtime_error("Cannot open TXD output");
    dict->streamWrite(&out);out.close();dict->destroy();
}
void reference_bounds(const fs::path& directory) {
    std::cout<<"name,minx,miny,minz,maxx,maxy,maxz\n";
    for(const auto& e:fs::directory_iterator(directory)) {
        if(e.path().extension()!=".dff")continue;
        rw::StreamFile stream;if(!stream.open(e.path().string().c_str(),"rb")||!rw::findChunk(&stream,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Invalid reference DFF");
        auto c=rw::Clump::streamRead(&stream);stream.close();if(!c)throw std::runtime_error("Invalid reference clump");
        rw::V3d mn={INFINITY,INFINITY,INFINITY},mx={-INFINITY,-INFINITY,-INFINITY};
        FORLIST(link,c->atomics){auto a=rw::Atomic::fromClump(link);auto g=a->geometry;const std::string node=gta::getNodeName(a->getFrame());const auto suffix=node.find_last_of('_');if(suffix!=std::string::npos&&suffix+2<node.size()&&std::tolower(node[suffix+1])=='l'&&std::isdigit(node[suffix+2])&&node[suffix+2]!='0')continue;
            // Original entity loading replaces the atomic frame with the
            // placement matrix. Artist child-frame transforms must not be
            // applied again when comparing compiled PS2 world geometry.
            for(int i=0;i<g->numVertices;++i){auto v=g->morphTargets[0].vertices[i];mn.x=std::min(mn.x,v.x);mn.y=std::min(mn.y,v.y);mn.z=std::min(mn.z,v.z);mx.x=std::max(mx.x,v.x);mx.y=std::max(mx.y,v.y);mx.z=std::max(mx.z,v.z);}}
        c->destroy();if(std::isfinite(mn.x))std::cout<<e.path().stem().string()<<','<<mn.x<<','<<mn.y<<','<<mn.z<<','<<mx.x<<','<<mx.y<<','<<mx.z<<'\n';
    }
}
void reference_vertices(const fs::path& directory,const fs::path& output) {
    if(fs::exists(output))throw std::runtime_error("Reference output exists");
    std::ofstream file(output,std::ios::binary);file.write("RGV1",4);uint32_t count=0;file.write(reinterpret_cast<char*>(&count),4);
    for(const auto& e:fs::directory_iterator(directory)) {
        if(e.path().extension()!=".dff")continue;
        rw::StreamFile stream;if(!stream.open(e.path().string().c_str(),"rb")||!rw::findChunk(&stream,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Invalid reference DFF");
        auto c=rw::Clump::streamRead(&stream);stream.close();if(!c)throw std::runtime_error("Invalid reference clump");
        std::vector<rw::V3d> vertices;
        FORLIST(link,c->atomics){auto a=rw::Atomic::fromClump(link);auto g=a->geometry;std::string node=gta::getNodeName(a->getFrame());auto suffix=node.find_last_of('_');if(suffix!=std::string::npos&&suffix+2<node.size()&&std::tolower(node[suffix+1])=='l'&&std::isdigit(node[suffix+2])&&node[suffix+2]!='0')continue;vertices.insert(vertices.end(),g->morphTargets[0].vertices,g->morphTargets[0].vertices+g->numVertices);}
        c->destroy();char name[32]={};std::strncpy(name,e.path().stem().string().c_str(),31);file.write(name,32);auto nv=uint32_t(vertices.size());file.write(reinterpret_cast<char*>(&nv),4);file.write(reinterpret_cast<char*>(vertices.data()),nv*12);++count;
    }
    file.seekp(4);file.write(reinterpret_cast<char*>(&count),4);if(!file)throw std::runtime_error("Cannot write reference vertices");
    std::cout<<"reference_models="<<count<<" game_executed=false\n";
}
void texture_fingerprints(const fs::path& directory) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot open SHA256");
    for(const auto& entry:fs::directory_iterator(directory)) {
        auto extension=entry.path().extension().string();for(auto& c:extension)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(extension!=".txd")continue;
        rw::StreamFile in;if(!in.open(entry.path().string().c_str(),"rb")||!rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Invalid development reference TXD");
        auto dictionary=rw::TexDictionary::streamRead(&in);in.close();if(!dictionary)throw std::runtime_error("Invalid reference dictionary");
        FORLIST(link,dictionary->textures) {
            auto texture=rw::Texture::fromDict(link);auto image=texture->raster->toImage();
            if(!image)throw std::runtime_error("Cannot decode reference image");image->convertTo32();
            BCRYPT_HASH_HANDLE hash=nullptr;uint8_t digest[32];uint32_t dimensions[]={uint32_t(image->width),uint32_t(image->height)};
            if(image->depth!=32 || BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0 ||
               BCryptHashData(hash,reinterpret_cast<PUCHAR>(dimensions),8,0)<0)throw std::runtime_error("Cannot hash reference raster");
            for(int y=0;y<image->height;++y)if(BCryptHashData(hash,image->pixels+y*image->stride,image->width*4,0)<0)throw std::runtime_error("Cannot hash reference pixels");
            if(BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error("Cannot finish raster hash");BCryptDestroyHash(hash);
            std::cout<<entry.path().stem().string()<<'\t'<<texture->name<<'\t'<<image->width<<'\t'<<image->height<<'\t';
            static constexpr char digits[]="0123456789abcdef";for(auto value:digest)std::cout<<digits[value>>4]<<digits[value&15];std::cout<<'\n';
            image->destroy();
        }
        dictionary->destroy();
    }
    BCryptCloseAlgorithmProvider(algorithm,0);
}
void reference_material_names(const fs::path& directory) {
    for(const auto& entry:fs::directory_iterator(directory)) {
        if(entry.path().extension()!=".dff")continue;
        rw::StreamFile in;if(!in.open(entry.path().string().c_str(),"rb")||!rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("Invalid development reference DFF");
        auto clump=rw::Clump::streamRead(&in);in.close();if(!clump)throw std::runtime_error("Invalid reference clump");
        std::vector<std::string> names;
        FORLIST(link,clump->atomics) {
            auto geometry=rw::Atomic::fromClump(link)->geometry;
            for(int i=0;i<geometry->matList.numMaterials;++i) {
                auto texture=geometry->matList.materials[i]->texture;
                if(texture && std::find(names.begin(),names.end(),texture->name)==names.end())names.emplace_back(texture->name);
            }
        }
        for(const auto& name:names)std::cout<<entry.path().stem().string()<<'\t'<<name<<'\n';
        clump->destroy();
    }
}
void radar(const fs::path& input,const fs::path& output) {
    if(fs::exists(output))throw std::runtime_error("Radar output already exists");fs::create_directories(output);
    uint8_t ocean[4];
    {
        rw::StreamFile in;if(!in.open((input/"RADAR00.txd").string().c_str(),"rb")||!rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Missing source ocean color");
        auto dict=rw::TexDictionary::streamRead(&in);in.close();if(!dict||dict->textures.isEmpty())throw std::runtime_error("Source radar00 is empty");
        auto tex=rw::Texture::fromDict(dict->textures.link.next);auto img=tex->raster->toImage();if(!img)throw std::runtime_error("Cannot decode source ocean color");img->unpalettize();
        if(img->depth!=32)throw std::runtime_error("Unexpected source radar depth");std::memcpy(ocean,img->pixels,4);img->destroy();dict->destroy();
    }
    for(int y=0;y<12;++y)for(int x=0;x<12;++x) {
        const bool central=x>=2&&x<10&&y>=2&&y<10;
        const int source=central ? (y-2)*8+x-2 : 4;
        char filename[32];std::snprintf(filename,sizeof(filename),"RADAR%02d.txd",source);
        rw::StreamFile in;if(!in.open((input/filename).string().c_str(),"rb")||!rw::findChunk(&in,rw::ID_TEXDICTIONARY,nullptr,nullptr))throw std::runtime_error("Missing source radar dictionary");
        auto dict=rw::TexDictionary::streamRead(&in);in.close();if(!dict)throw std::runtime_error("Invalid radar dictionary");
        int textures=0;
        FORLIST(link,dict->textures) {
            auto tex=rw::Texture::fromDict(link);++textures;
            auto img=tex->raster->toImage();if(!img)throw std::runtime_error("Cannot read radar raster");img->unpalettize();
            auto raster=sa_raster(img);img->destroy();tex->raster->destroy();tex->raster=raster;
            std::snprintf(tex->name,32,"radar%02d",y*12+x);
        }
        if(textures==0) {
            // Empty original tiles contain no coastline; sample the ocean
            // pixel at the original map's northwest corner (RADAR00), which
            // the original water grid also classifies as sea level water.
            auto img=rw::Image::create(1,1,32);img->allocate();std::memcpy(img->pixels,ocean,4);auto raster=sa_raster(img);img->destroy();
            if(!raster)throw std::runtime_error("Cannot create ocean radar tile");auto tex=rw::Texture::create(raster);std::snprintf(tex->name,32,"radar%02d",y*12+x);tex->setFilter(rw::Texture::LINEAR);dict->add(tex);textures=1;
        }
        if(textures!=1)throw std::runtime_error("Expected one source radar texture");
        std::snprintf(filename,sizeof(filename),"radar%02d.txd",y*12+x);
        rw::StreamFile out;if(!out.open((output/filename).string().c_str(),"wb"))throw std::runtime_error("Cannot open radar output");dict->streamWrite(&out);out.close();dict->destroy();
    }
    std::cout<<"radar_tiles=144 original_tiles=64 offset_x=2 offset_y=2 game_executed=false\n";
}
int main(int argc,char** argv) {
    try {
        if(argc!=3&&!(argc==4&&(std::string(argv[1])=="--radar"||std::string(argv[1])=="--reference-vertices")))throw std::runtime_error("Usage: lcs-ps2-write INPUT_DIRECTORY OUTPUT_DIRECTORY; --reference-bounds INPUT_DIRECTORY; --texture-fingerprints INPUT_DIRECTORY; --reference-material-names INPUT_DIRECTORY; --radar INPUT OUTPUT; --reference-vertices INPUT OUTPUT");
        rw::Engine::init();gta::attachPlugins();rw::Engine::open(nullptr);rw::Engine::start();rw::Texture::setLoadTextures(false);rw::Texture::setCreateDummies(true);
        rw::platform=rw::PLATFORM_D3D9;rw::version=0x36003;rw::build=0xffff;
        if(std::string(argv[1])=="--reference-bounds"){reference_bounds(argv[2]);return 0;}
        if(std::string(argv[1])=="--texture-fingerprints"){texture_fingerprints(argv[2]);return 0;}
        if(std::string(argv[1])=="--reference-material-names"){reference_material_names(argv[2]);return 0;}
        if(std::string(argv[1])=="--radar"){radar(argv[2],argv[3]);return 0;}
        if(std::string(argv[1])=="--reference-vertices"){reference_vertices(argv[2],argv[3]);return 0;}
        fs::path input=argv[1],output=argv[2];if(fs::exists(output))throw std::runtime_error("Output directory already exists");fs::create_directories(output);
        SaMapLighting lighting(input/"map-lighting.psl");
        size_t meshes=0,txds=0;
        for(const auto& e:fs::directory_iterator(input)) {auto name=e.path().stem().string();if(e.path().extension()==".psm"){write_mesh(e.path(),output/(name+".dff"),lighting);++meshes;}else if(e.path().extension()==".pst"){write_textures(e.path(),output/(name+".txd"));++txds;}}
        rw::Engine::stop();rw::Engine::close();rw::Engine::term();std::cout<<"models="<<meshes<<" txds="<<txds<<" game_executed=false\n";return 0;
    } catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
