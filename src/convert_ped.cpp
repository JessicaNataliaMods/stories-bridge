// Offline skin retargeting. All source and reference media are supplied by
// the user. Bone-space rebasing avoids guessed rotations or renamed weights.
#include "convert_ped.hpp"
#include <rw.h>
#include <rwgta.h>
#include <stdexcept>
#include <map>
#include <vector>
#include <cstring>
#include <cmath>
#include <iostream>
#include <functional>
namespace {
rw::Matrix matrix(const float* values) {
    rw::Matrix result;std::memcpy(&result,values,64);result.update();return result;
}
int tag(int source) {
    return source==54 || source==2000 ? 44 : source==55 || source==2001 ? 54 : source;
}
void verify_alternate_toes(rw::Frame* frame,std::map<int,int>& counts) {
    const int id=rw::HAnimData::get(frame)->id;
    if(id==2000 || id==2001) {
        auto parent=frame->getParent();
        const char* expected=id==2000 ? "L Toe0" : "R Toe0";
        if(!parent || rw::HAnimData::get(parent)->id!=(id==2000 ? 43 : 53) ||
           std::strcmp(gta::getNodeName(frame),expected)!=0 || ++counts[id]!=1)
            throw std::runtime_error("Unverified alternate LCS toe identity");
    }
    for(auto child=frame->child;child;child=child->next)verify_alternate_toes(child,counts);
}
}
rw::Clump* adapt_sa_ped(rw::Clump* source,const std::filesystem::path& reference) {
    rw::StreamFile in;
    if(!in.open(reference.string().c_str(),"rb") || !rw::findChunk(&in,rw::ID_CLUMP,nullptr,nullptr))throw std::runtime_error("SA ped reference unavailable");
    auto target=rw::Clump::streamRead(&in);in.close();
    auto old_h=rw::HAnimHierarchy::get(source);
    auto new_h=target?rw::HAnimHierarchy::get(target):nullptr;
    if(!old_h || !new_h || source->countAtomics()!=1 || target->countAtomics()!=1 || old_h->numNodes!=24 || new_h->numNodes!=32)
        throw std::runtime_error("Expected original LCS 24-bone ped and native SA 32-bone reference");
    auto src=rw::Atomic::fromClump(source->atomics.link.next);
    // Original vice1..vice8 use 2000/2001 for left/right toes. Accept these
    // only when frame names AND foot-parent IDs establish the identity.
    std::map<int,int> alternate_toes;
    verify_alternate_toes(source->getFrame(),alternate_toes);
    for(int i=0;i<old_h->numNodes;++i)
        if((old_h->nodeInfo[i].id==2000 || old_h->nodeInfo[i].id==2001) && !alternate_toes.count(old_h->nodeInfo[i].id))
            throw std::runtime_error("Alternate toe has no verified source frame");
    auto ref=rw::Atomic::fromClump(target->atomics.link.next);
    auto g=src->geometry;auto old_skin=rw::Skin::get(g);auto ref_skin=rw::Skin::get(ref->geometry);
    if(!old_skin || !ref_skin || old_skin->numBones!=24 || ref_skin->numBones!=32 || g->instData || g->numMorphTargets!=1)
        throw std::runtime_error("Ped skin/bind matrices are not editable or inconsistent");
    std::map<int,int> indices;
    for(int i=0;i<32;++i)if(!indices.emplace(new_h->nodeInfo[i].id,i).second)throw std::runtime_error("Duplicate SA bone");
    // Keep the original character's proportions. Replacing every pivot with
    // male01/bfyst stretches short torsos/necks to the donor's bone lengths.
    // The donor supplies SA axes, helper bones and topology, not body dimensions.
    std::vector<rw::Matrix> binds(32),source_binds(32);
    for(int i=0;i<32;++i) {
        auto inverse=matrix(ref_skin->inverseMatrices+16*i);
        binds[i].setIdentity();rw::Matrix::invert(&binds[i],&inverse);binds[i].update();
    }
    auto source_root=old_h->getIndex(0),native_root=new_h->getIndex(0);
    if(source_root<0 || native_root<0)throw std::runtime_error("Missing ped root bone");
    std::vector<int> remap;
    std::vector<bool> source_pivot(32,false);
    for(int i=0;i<24;++i) {
        auto found=indices.find(tag(old_h->nodeInfo[i].id));
        if(found==indices.end())throw std::runtime_error("LCS bone has no native SA counterpart");
        remap.push_back(found->second);
        auto original=matrix(old_skin->inverseMatrices+16*i);
        rw::Matrix source_bind;source_bind.setIdentity();rw::Matrix::invert(&source_bind,&original);
        source_binds[found->second]=source_bind;source_pivot[found->second]=true;
    }
    // Keep native parent-local directions, using source lengths on mapped
    // edges and native offsets on SA-only facial/helper edges.
    std::function<void(rw::Frame*)> helpers=[&](rw::Frame* f) {
        const int i=new_h->getIndex(rw::HAnimData::get(f)->id);
        auto parent=f->getParent();const int pi=parent?new_h->getIndex(rw::HAnimData::get(parent)->id):-1;
        if(i>=0 && pi>=0) {
            auto inverse=matrix(ref_skin->inverseMatrices+16*pi);
            auto child_inverse=matrix(ref_skin->inverseMatrices+16*i);
            rw::Matrix child_bind;child_bind.setIdentity();rw::Matrix::invert(&child_bind,&child_inverse);
            rw::V3d local;rw::V3d::transformPoints(&local,&child_bind.pos,1,&inverse);
            if(source_pivot[i] && source_pivot[pi]) {
                auto a=source_binds[i].pos,b=source_binds[pi].pos;
                const float length=std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
                const float donor=std::sqrt(local.x*local.x+local.y*local.y+local.z*local.z);
                if(donor<0.0001f || !std::isfinite(length))throw std::runtime_error("Invalid ped bone length");
                const float scale=length/donor;local.x*=scale;local.y*=scale;local.z*=scale;
            }
            rw::V3d::transformPoints(&binds[i].pos,&local,1,&binds[pi]);binds[i].update();
        }
        for(auto c=f->child;c;c=c->next)helpers(c);
    };
    helpers(target->getFrame());
    std::vector<rw::Matrix> rebases;
    for(int i=0;i<24;++i) {
        auto inverse=matrix(old_skin->inverseMatrices+16*i);
        rw::Matrix transform;transform.setIdentity();rw::Matrix::mult(&transform,&inverse,&binds[remap[i]]);
        rebases.push_back(transform);
    }
    auto skin=rwNewT(rw::Skin,1,0);skin->init(32,32,g->numVertices);
    for(int i=0;i<32;++i) {
        rw::Matrix inverse;inverse.setIdentity();rw::Matrix::invert(&inverse,&binds[i]);
        std::memcpy(skin->inverseMatrices+16*i,&inverse,64);
    }
    auto& morph=g->morphTargets[0];
    for(int v=0;v<g->numVertices;++v) {
        rw::V3d position{},normal{};float total=0;
        for(int w=0;w<4;++w) {
            const auto weight=old_skin->weights[4*v+w];const auto index=old_skin->indices[4*v+w];
            if(!std::isfinite(weight) || weight<0 || (weight>0 && index>=24))throw std::runtime_error("Invalid source skin weight");
            skin->weights[4*v+w]=weight;skin->indices[4*v+w]=weight>0?uint8_t(remap[index]):0;
            if(weight==0)continue;
            rw::V3d p,n;rw::V3d::transformPoints(&p,&morph.vertices[v],1,&rebases[index]);
            position.x+=weight*p.x;position.y+=weight*p.y;position.z+=weight*p.z;
            if(morph.normals) {
                auto rotation=rebases[index];rotation.pos={0,0,0};rotation.update();
                rw::V3d::transformPoints(&n,&morph.normals[v],1,&rotation);
                normal.x+=weight*n.x;normal.y+=weight*n.y;normal.z+=weight*n.z;
            }
            total+=weight;
        }
        if(std::fabs(total-1)>0.01f || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))throw std::runtime_error("Invalid retargeted vertex");
        morph.vertices[v]=position;
        if(morph.normals) {
            const float length=std::sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
            if(length<0.001f)throw std::runtime_error("Degenerate retargeted normal");
            morph.normals[v]={normal.x/length,normal.y/length,normal.z/length};
        }
    }
    std::function<void(rw::Frame*)> frames=[&](rw::Frame* f) {
        const int i=new_h->getIndex(rw::HAnimData::get(f)->id);
        auto parent=f->getParent();const int pi=parent?new_h->getIndex(rw::HAnimData::get(parent)->id):-1;
        if(i>=0 && pi>=0) {
            rw::Matrix inverse;inverse.setIdentity();rw::Matrix::invert(&inverse,&binds[pi]);
            rw::Matrix::mult(&f->matrix,&binds[i],&inverse);f->matrix.update();
        }
        for(auto c=f->child;c;c=c->next)frames(c);
    };
    frames(target->getFrame());target->getFrame()->updateObjects();
    // Attach the source geometry to the adapted native frame tree. The
    // frame transforms and skin palette describe the same new bind pose.
    auto frame=ref->getFrame();source->removeAtomic(src);src->setFrame(frame);
    target->removeAtomic(ref);ref->destroy();target->addAtomic(src);
    // The old skin allocation belongs to the geometry plugin. Free its data
    // before replacing the plugin pointer; source clump no longer owns src.
    rwFree(old_skin->data);rwFree(old_skin);
    rw::Skin::set(g,skin);skin->findNumWeights(g->numVertices);skin->findUsedBones(g->numVertices);
    rw::Skin::setHierarchy(src,new_h);g->calculateBoundingSphere();gta::setPipelineID(src,0);
    source->destroy();
    std::cout<<" sa_ped_bones=32 source_bones=24 source_proportions=true native_bone_axes=true";
    return target;
}
