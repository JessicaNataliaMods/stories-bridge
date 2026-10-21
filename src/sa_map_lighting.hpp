#pragma once
// Include after rw.h/rwgta.h (the bundled headers have no include guards).
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <algorithm>

// Offline only. Leeds' final light is prelight*ambient + emissive*0.5,
// with PS2's 255/128 texture modulation. A native SA DN atomic interpolates
// two bounded source endpoints without applying a second directional light.
struct SaMapLighting {
    unsigned char parameters[12]{};
    bool enabled=false;
    explicit SaMapLighting(const std::filesystem::path& path) {
        std::ifstream input(path,std::ios::binary);char magic[4];
        if(!input)return;
        input.read(magic,4);input.read(reinterpret_cast<char*>(parameters),12);
        if(!input || std::string(magic,4)!="PSL1" || input.peek()!=EOF)
            throw std::runtime_error("Invalid map lighting input");
        enabled=true;
    }
    void apply(rw::Atomic* atomic) const {
        if(!enabled)return;
        auto g=atomic->geometry;
        if(!g->colors)throw std::runtime_error("Map lighting requires source prelight");
        std::vector<bool> textured(g->numVertices,false);
        for(int i=0;i<g->numTriangles;++i) {
            const auto& t=g->triangles[i];
            if(t.matId>=g->matList.numMaterials)throw std::runtime_error("Lighting material index invalid");
            if(g->matList.materials[t.matId]->texture)
                for(auto v:t.v)textured[v]=true;
        }
        gta::allocateExtraVertColors(g);
        auto night=gta::getExtraVertColors(atomic);
        if(!night)throw std::runtime_error("Cannot allocate SA night colors");
        for(int i=0;i<g->numVertices;++i) {
            const auto original=g->colors[i];
            for(int endpoint=0;endpoint<2;++endpoint) {
                rw::RGBA result=original;
                auto src=reinterpret_cast<const unsigned char*>(&original);
                auto dst=reinterpret_cast<unsigned char*>(&result);
                for(int k=0;k<3;++k) {
                    double value=src[k]*parameters[6*endpoint+k]/255.0+parameters[6*endpoint+3+k]*.5;
                    if(textured[i])value*=255.0/128.0;
                    dst[k]=static_cast<unsigned char>(std::min(255.0,value+.5));
                }
                if(endpoint==0)g->colors[i]=result;else night[i]=result;
            }
        }
        g->flags&=~rw::Geometry::LIGHT;
        for(int i=0;i<g->matList.numMaterials;++i)g->matList.materials[i]->surfaceProps={0.f,0.f,0.f};
        gta::setPipelineID(atomic,gta::RSPIPE_PC_CustomBuildingDN_PipeID);
    }
};
