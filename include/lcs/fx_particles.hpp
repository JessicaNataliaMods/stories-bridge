#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lcs {

using FxVec3=std::array<float,3>;

// SA particle blueprints the bridge feeds through FxSystem_c::AddParticle. Each kind is one
// SA system that SA itself drives with AddParticle (prt_* blueprints in effects.fxp).
enum class FxSystemKind : uint8_t { blood, smoke_light, smoke_dark };

struct FxParticleRequest {
    FxSystemKind kind=FxSystemKind::blood;
    FxVec3 position{},velocity{}; // LCS units: metres, metres per 30 Hz frame
    float size=0;                 // LCS size operand; 0 selects the blueprint default
    bool has_color=false;
    uint8_t red=255,green=255,blue=255,alpha=255;
};

struct FxStatus {
    bool ok=false;
    // True once a native mutation may have happened; a failure then must not be replayed.
    bool native_entered=false;
    std::string diagnostic;
};

struct FxCallbacks {
    // One native AddParticle.
    std::function<FxStatus(const FxParticleRequest&)> emit;
    // Starts an SA fire system; token 0 means failure. Strength follows SA's script-fire sizes.
    std::function<uint32_t(const FxVec3&,float,FxStatus&)> fire_start;
    // True when the token's system is stopped or no longer exists (identity checked natively).
    std::function<bool(uint32_t)> fire_stop;
    // Releases every native system the bridge created.
    std::function<void()> release;
    std::function<std::optional<FxVec3>()> camera;
};

// Production owner of LCS ParticleObject emitters (CParticleObject pool, UpdateClose cadence)
// and of the single AddParticle producer. Rendering/dynamics stay in SA's FxManager.
class FxParticles {
public:
    // LCS particle object types (eParticleObjectType) with an SA recipe.
    static constexpr int32_t object_dark_smoke=4,object_small_fire=10,object_dry_ice=12,
        object_smoke_trail=17,object_rocket_trail=19;
    // LCS particle types (tParticleType) accepted by CREATE_SINGLE_PARTICLE.
    static constexpr int32_t particle_blood=6,particle_blood_small=7;
    static constexpr size_t capacity=70; // MAX_PARTICLEOBJECTS

    FxCallbacks callbacks;
    std::function<void(const std::string&)> diagnostic;

    bool supports_single(int32_t particle_type)const;
    bool supports_object(int32_t object_type)const;
    bool ready()const {return callbacks.emit && callbacks.camera;}

    // 1084: CParticle::AddParticle(type,pos,vel,nil,size). One finite SA particle.
    FxStatus single(int32_t particle_type,const FxVec3& position,const FxVec3& velocity,float size);
    // 930: CParticleObject::AddObject(type,pos,target,size,lifeMs,color,remove=1).
    // `added` is false only when the 70-object pool was full (the original drops silently).
    FxStatus add_moving(int32_t object_type,const FxVec3& position,const FxVec3& target,float size,
        const std::array<int32_t,3>& rgb,uint32_t life_ms,uint32_t now,bool& added);
    // 947: every object whose position is inside the (normalised, inclusive) box ends.
    // Returns the number removed; fire teardown failures are retried by tick().
    size_t remove_in_area(const std::array<float,6>& box);
    // One game frame of CParticleObject::UpdateClose for every live object.
    void tick(uint32_t now);
    // Ends every object and releases native systems (campaign end / shutdown).
    void reset();

    size_t live()const;
    size_t pending_fire_stops()const {return retired_fires_.size();}
    uint64_t emitted()const {return emitted_;}

private:
    struct Emitter {
        bool used=false;
        int32_t type=0;
        FxVec3 position{},target{},head{};
        float size=0,rand_value=0;
        uint8_t skip_frames=1,cycles=1,frame_counter=0;
        int16_t creation_chance=0;
        bool has_color=false,head_started=false;
        std::array<uint8_t,3> rgb{};
        uint8_t alpha=0;
        uint32_t remove_at=0; // 0 = no expiry (CParticleObject::m_nRemoveTimer)
        uint32_t fire_token=0;
    };
    std::array<Emitter,capacity> emitters_{};
    std::vector<uint32_t> retired_fires_;
    uint32_t rng_=0;
    uint64_t emitted_=0;
    uint32_t rand15();
    float rand_range(float low,float high);
    void retire(Emitter&);
    FxStatus emit_one(Emitter&,const FxVec3& position,const FxVec3& velocity,float size,
        FxSystemKind kind,bool force_color,uint8_t r,uint8_t g,uint8_t b,uint8_t a);
    FxStatus emit_default_branch(Emitter&,FxSystemKind kind);
};

}
