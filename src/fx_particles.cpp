#include "lcs/fx_particles.hpp"

#include <algorithm>
#include <cmath>

namespace lcs {

namespace {
bool finite3(const FxVec3& value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
constexpr float camera_removal_radius=100.0f; // UpdateClose: MagnitudeSqr2D() > SQR(100)
}

bool FxParticles::supports_single(int32_t particle_type)const {
    return particle_type==particle_blood || particle_type==particle_blood_small;
}

bool FxParticles::supports_object(int32_t object_type)const {
    switch(object_type) {
        case object_dark_smoke: case object_small_fire: case object_dry_ice:
        case object_smoke_trail: case object_rocket_trail: return true;
        default: return false;
    }
}

size_t FxParticles::live()const {
    return size_t(std::count_if(emitters_.begin(),emitters_.end(),[](const Emitter& e){return e.used;}));
}

uint32_t FxParticles::rand15() {
    if(!rng_)rng_=2463534242u;
    rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
    return (rng_>>8)&0x7FFFu;
}

float FxParticles::rand_range(float low,float high) {
    return low+(high-low)*float(rand15())/32767.0f;
}

FxStatus FxParticles::emit_one(Emitter& emitter,const FxVec3& position,const FxVec3& velocity,float size,
        FxSystemKind kind,bool force_color,uint8_t r,uint8_t g,uint8_t b,uint8_t a) {
    (void)emitter;
    FxParticleRequest request;
    request.kind=kind;request.position=position;request.velocity=velocity;request.size=size;
    request.has_color=force_color;request.red=r;request.green=g;request.blue=b;request.alpha=a;
    auto status=callbacks.emit(request);
    if(status.ok)++emitted_;
    return status;
}

FxStatus FxParticles::single(int32_t particle_type,const FxVec3& position,const FxVec3& velocity,float size) {
    FxStatus status;
    if(!callbacks.emit){status.diagnostic="SA particle backend is unavailable";return status;}
    if(!supports_single(particle_type)) {
        status.diagnostic="Particle type "+std::to_string(particle_type)+" has no verified SA particle system";
        return status;
    }
    if(!finite3(position) || !finite3(velocity) || !std::isfinite(size)) {
        status.diagnostic="Particle operands are not finite";return status;
    }
    Emitter scratch;
    // AddParticle(type,pos,vel,nil,size): the default colour (alpha 0) leaves the system colour.
    return emit_one(scratch,position,velocity,std::max(0.0f,size),FxSystemKind::blood,false,255,255,255,255);
}

FxStatus FxParticles::add_moving(int32_t object_type,const FxVec3& position,const FxVec3& target,float size,
        const std::array<int32_t,3>& rgb,uint32_t life_ms,uint32_t now,bool& added) {
    FxStatus status;added=false;
    if(!ready()){status.diagnostic="SA particle backend is unavailable";return status;}
    if(!supports_object(object_type)) {
        status.diagnostic="Particle effect type "+std::to_string(object_type)+" has no verified SA recipe";
        return status;
    }
    if(!finite3(position) || !finite3(target) || !std::isfinite(size)) {
        status.diagnostic="Particle effect operands are not finite";return status;
    }
    auto slot=std::find_if(emitters_.begin(),emitters_.end(),[](const Emitter& e){return !e.used;});
    if(slot==emitters_.end()) { // "Error: No particle objects available!" - the original returns nil
        status.ok=true;return status;
    }
    Emitter next;
    next.used=true;next.type=object_type;next.position=position;next.head=position;next.target=target;
    next.size=std::max(0.0f,size);
    next.remove_at=life_ms?uint32_t(now+life_ms):0u;
    const auto clamp=[](int32_t value){return uint8_t(std::min(255,std::max(0,value)));};
    const bool script_color=object_type==object_smoke_trail; // alpha 255 only for this type
    if(script_color){next.has_color=true;next.rgb={clamp(rgb[0]),clamp(rgb[1]),clamp(rgb[2])};next.alpha=255;}
    switch(object_type) {
        case object_dark_smoke:
            next.skip_frames=3;next.creation_chance=8;next.has_color=true;next.rgb={16,16,16};next.alpha=255;break;
        case object_small_fire:
            next.skip_frames=2;next.creation_chance=2;next.target={0,0,0};break;
        case object_dry_ice:
            next.skip_frames=1;next.creation_chance=0;next.target={0,0,0};break;
        case object_smoke_trail:
            next.skip_frames=1;next.creation_chance=2;next.rand_value=0.02f;break;
        case object_rocket_trail:
            next.skip_frames=2;next.creation_chance=8;next.rand_value=0.1f;break;
        default: break;
    }
    if(object_type==object_small_fire) {
        // The persistent SA fire system replaces the per-frame flame/smoke puffs of this object.
        if(!callbacks.fire_start){status.diagnostic="SA fire backend is unavailable";return status;}
        FxStatus fire;
        next.fire_token=callbacks.fire_start(next.position,next.size,fire);
        if(!next.fire_token){
            status=fire;if(status.diagnostic.empty())status.diagnostic="SA fire system could not be started";
            status.ok=false;return status;
        }
    }
    *slot=next;
    added=true;status.ok=true;return status;
}

void FxParticles::retire(Emitter& emitter) {
    if(emitter.fire_token) {
        const auto token=emitter.fire_token;
        bool stopped=false;
        if(callbacks.fire_stop) {
            try {stopped=callbacks.fire_stop(token);} catch(...) {stopped=false;}
        }
        if(!stopped)retired_fires_.push_back(token);
    }
    emitter=Emitter{};
}

size_t FxParticles::remove_in_area(const std::array<float,6>& box) {
    const float low[3]={std::min(box[0],box[3]),std::min(box[1],box[4]),std::min(box[2],box[5])};
    const float high[3]={std::max(box[0],box[3]),std::max(box[1],box[4]),std::max(box[2],box[5])};
    size_t removed=0;
    for(auto& emitter:emitters_) {
        if(!emitter.used)continue;
        bool inside=true;
        for(size_t axis=0;axis<3;++axis)
            inside=inside && emitter.position[axis]>=low[axis] && emitter.position[axis]<=high[axis];
        if(!inside)continue;
        retire(emitter);++removed;
    }
    return removed;
}

FxStatus FxParticles::emit_default_branch(Emitter& emitter,FxSystemKind kind) {
    FxStatus last;last.ok=true;
    for(int cycle=0;cycle<emitter.cycles;++cycle) {
        FxVec3 velocity=emitter.target;
        if(emitter.rand_value!=0.0f)
            for(auto& component:velocity)
                if(component!=0.0f)component+=rand_range(-emitter.rand_value,emitter.rand_value);
        last=emit_one(emitter,emitter.position,velocity,emitter.size,kind,emitter.has_color,
            emitter.rgb[0],emitter.rgb[1],emitter.rgb[2],emitter.alpha);
        if(!last.ok)return last;
    }
    return last;
}

void FxParticles::tick(uint32_t now) {
    // Fire systems whose stop was refused are retried until the native identity check agrees.
    for(auto it=retired_fires_.begin();it!=retired_fires_.end();) {
        bool stopped=false;
        if(callbacks.fire_stop) {
            try {stopped=callbacks.fire_stop(*it);} catch(...) {stopped=false;}
        }
        it=stopped?retired_fires_.erase(it):it+1;
    }
    if(!ready())return;
    const auto camera=callbacks.camera();
    for(auto& emitter:emitters_) {
        if(!emitter.used)continue;
        if(camera) {
            const float dx=emitter.position[0]-(*camera)[0],dy=emitter.position[1]-(*camera)[1];
            if(dx*dx+dy*dy>camera_removal_radius*camera_removal_radius){retire(emitter);continue;}
        }
        if(emitter.type==object_rocket_trail && emitter.head_started)
            for(size_t axis=0;axis<3;++axis)emitter.head[axis]+=emitter.target[axis];
        if(++emitter.frame_counter>=emitter.skip_frames) {
            emitter.frame_counter=0;
            bool gate=true;
            if(emitter.creation_chance!=0)gate=(rand15()%uint32_t(emitter.creation_chance))!=0;
            if(gate && emitter.type!=object_small_fire) {
                FxStatus status;status.ok=true;
                switch(emitter.type) {
                    case object_dark_smoke: status=emit_default_branch(emitter,FxSystemKind::smoke_dark);break;
                    case object_dry_ice: status=emit_default_branch(emitter,FxSystemKind::smoke_light);break;
                    case object_smoke_trail: status=emit_default_branch(emitter,FxSystemKind::smoke_dark);break;
                    case object_rocket_trail:
                        if(!emitter.head_started){emitter.head_started=true;emitter.head=emitter.position;}
                        else {
                            FxVec3 velocity{emitter.target[0]*-0.8f,emitter.target[1]*-0.8f,emitter.target[2]*-0.8f};
                            status=emit_one(emitter,emitter.head,velocity,emitter.size*1.5f,
                                FxSystemKind::smoke_dark,false,255,255,255,255);
                        }
                        break;
                    default: break;
                }
                if(!status.ok) {
                    if(diagnostic)diagnostic("SA particle emission failed for effect type "+std::to_string(emitter.type)+
                        "; the effect is ended: "+status.diagnostic);
                    retire(emitter);continue;
                }
            }
        }
        if(emitter.remove_at!=0 && emitter.remove_at<now)retire(emitter);
    }
}

void FxParticles::reset() {
    for(auto& emitter:emitters_)if(emitter.used)retire(emitter);
    if(callbacks.release)callbacks.release();
}

}
