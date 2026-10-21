#pragma once
#include "lcs/campaign_checkpoint.hpp"
#include "lcs/hud.hpp"
#include "lcs/collectives.hpp"
#include "lcs/route_scripts.hpp"
#include "lcs/script_coronas.hpp"
#include "lcs/continuous_sound_adapter.hpp"
#include "lcs/fx_particles.hpp"
#include "sa_world_sectors.hpp"
#include <cmath>
#include <map>
namespace lcs {
struct CampaignServicesSave {
    struct PendingWorld {std::map<int32_t,int32_t> states;bool requested=false;};
    static void finite(float x){if(!std::isfinite(x))throw std::runtime_error("Nonfinite campaign service state");}
    static uint32_t count(CampaignSaveReader& r,uint32_t max){const auto n=r.u32();if(n>max)throw std::runtime_error("Campaign service pool exceeds limit");return n;}
    static bool flag(CampaignSaveReader& r){const auto n=r.u32();if(n>1)throw std::runtime_error("Invalid campaign flag");return n!=0;}
    static void request(CampaignSaveWriter& w,const HudRequest& q){w.u32(uint32_t(q.action));w.u32(q.native_offset);w.u32(q.type);w.u32(uint32_t(q.line));w.text(q.label);w.u32(q.total);w.text(q.heading);w.u32(q.total_type);w.pod(q.foreground);w.pod(q.background);w.u32(q.custom);}
    static HudRequest request(CampaignSaveReader& r){HudRequest q{};const auto action=r.u32();if(action>uint32_t(HudRequest::Action::counter_prefix))throw std::runtime_error("Invalid HUD request");q.action=HudRequest::Action(action);q.native_offset=r.u32();q.type=int32_t(r.u32());q.line=r.u32();q.label=r.text();q.total=int32_t(r.u32());q.heading=r.text();q.total_type=uint16_t(r.u32());q.foreground=r.pod<std::array<uint8_t,4>>();q.background=r.pod<std::array<uint8_t,4>>();q.custom=flag(r);if(q.line>3 || q.label.size()>8 || q.heading.size()>8)throw std::runtime_error("Invalid HUD label/slot");return q;}
    static std::vector<uint8_t> save(const HudBridge& h,const Collectives& c,const RouteScripts& routes,const ScriptCoronas& coronas,const ContinuousSoundAdapter& sound,const FxParticles& fx,const SaWorldSectors& sectors,uint32_t now){
        if(sound.uncertain_)throw std::runtime_error("Cannot save campaign services: continuous sound is quarantined by an uncertain native voice operation");
        if(sectors.faulted_)throw std::runtime_error("Cannot save campaign world sectors: service is faulted: "+(sectors.fault_diagnostic_.empty()?std::string("no diagnostic was recorded"):sectors.fault_diagnostic_));
        // Retired fire tokens are cleanup receipts only. They are intentionally
        // absent from the logical snapshot and remain owned by the live service
        // so its normal tick/shutdown path can retry release.
        CampaignSaveWriter w;w.u32(2);
        for(size_t i=0;i<4;++i){const auto& e=h.entries_[i];w.u32(e.original);w.u32(e.active);if(e.active){if(!h.saved_requests_[i])throw std::runtime_error("HUD renderer has no restorable request");request(w,*h.saved_requests_[i]);}}
        for(const auto& prefix:h.saved_prefixes_)w.u32(prefix?(*prefix?2:1):0);
        w.u32(h.frozen_);w.pod(h.pending_foreground_);w.pod(h.pending_background_);
        w.u32(c.next_);w.u32(c.poisoned_);for(const auto& e:c.entries_){w.u32(e.group);w.u32(e.ped);}
        for(const auto& n:routes.table_.nodes()){w.u32(uint16_t(n.route));w.pod(n.position);}
        w.u32(ScriptCoronas::next_id_);uint32_t active=0;for(const auto& o:coronas.objects_)if(o.active)++active;w.u32(active);
        for(const auto& o:coronas.objects_)if(o.active){w.u32(o.id);w.pod(o.position);w.pod(o.size);w.u32(o.type);w.u32(o.flare);w.u32(o.r);w.u32(o.g);w.u32(o.b);}
        w.u32(sound.objects_.cursor_);
        for(const auto& slot:sound.objects_.slots_){w.u32(slot.flag);w.u64(slot.epoch);if(!(slot.flag&0x80)){w.u32(slot.object.sound);w.pod(slot.object.position);}}
        w.u32(fx.rng_);w.u64(fx.emitted_);
        for(const auto& e:fx.emitters_){w.u32(e.used);if(!e.used)continue;w.u32(e.type);w.pod(e.position);w.pod(e.target);w.pod(e.head);w.pod(e.size);w.pod(e.rand_value);w.u32(e.skip_frames);w.u32(e.cycles);w.u32(e.frame_counter);w.u32(uint16_t(e.creation_chance));w.u32(e.has_color);w.u32(e.head_started);w.pod(e.rgb);w.u32(e.alpha);w.u32(e.remove_at!=0);if(e.remove_at)w.u32(int32_t(e.remove_at-now)>0?e.remove_at-now:0);}
        w.u32(uint32_t(sectors.committed_states_.size()));for(const auto& pair:sectors.committed_states_){w.u32(pair.first);w.u32(pair.second);}
        w.u32(uint32_t(sectors.staged_states_.size()));for(const auto& pair:sectors.staged_states_){w.u32(pair.first);w.u32(pair.second);}
        w.u32(sectors.plan_.has_value());
        return w.data;
    }
    // Parse into copies first. Native consumers only run after the entire section validates.
    static PendingWorld restore(const std::vector<uint8_t>& bytes,HudBridge& h,Collectives& c,RouteScripts& routes,ScriptCoronas& coronas,ContinuousSoundAdapter& sound,FxParticles& fx,SaWorldSectors& sectors,Vm& vm,uint32_t now){
        if(sound.uncertain_)throw std::runtime_error("Cannot restore campaign services: continuous sound is quarantined by an uncertain native voice operation");
        CampaignSaveReader r{bytes};const auto version=r.u32();if(version!=1&&version!=2)throw std::runtime_error("Unsupported campaign service version");
        auto nh=h;auto nc=c;auto nr=routes;auto ncor=coronas;auto ns=sound;auto nf=fx;
        for(size_t i=0;i<4;++i){auto& e=nh.entries_[i];e.original=r.u32();e.active=flag(r);nh.saved_requests_[i].reset();if(e.active){auto q=request(r);if(e.original<8 || e.original%4 || uint64_t(e.original)+4>vm.variable_space || q.native_offset!=HudBridge::native_cell(i) || q.action!=(i?HudRequest::Action::add_counter:HudRequest::Action::add_clock))throw std::runtime_error("Invalid HUD global reference");nh.saved_requests_[i]=q;}}
        for(auto& prefix:nh.saved_prefixes_){const auto v=count(r,2);prefix=v?std::optional<bool>(v==2):std::nullopt;}
        nh.frozen_=flag(r);nh.pending_foreground_=r.pod<std::array<uint8_t,4>>();nh.pending_background_=r.pod<std::array<uint8_t,4>>();
        nc.next_=int32_t(r.u32());nc.poisoned_=flag(r);for(auto& e:nc.entries_){e.group=int32_t(r.u32());e.ped=int32_t(r.u32());}
        OriginalRouteTable table;for(size_t i=0;i<OriginalRouteTable::capacity;++i){const auto route=int16_t(r.u32());const auto pos=r.pod<std::array<float,3>>();for(float x:pos)finite(x);table.nodes_[i]={route,pos};}nr.table_=table;
        const auto next_id=r.u32();ncor.objects_.clear();const auto n=count(r,65536);for(uint32_t i=0;i<n;++i){ScriptCorona o;o.id=r.u32();o.position=r.pod<std::array<float,3>>();o.size=r.pod<float>();for(float x:o.position)finite(x);finite(o.size);o.type=uint8_t(r.u32());o.flare=uint8_t(r.u32());o.r=uint8_t(r.u32());o.g=uint8_t(r.u32());o.b=uint8_t(r.u32());if(o.type>8 || o.flare>2)throw std::runtime_error("Invalid corona type");ncor.objects_.push_back(o);}
        ns.objects_.cursor_=int32_t(r.u32());if(ns.objects_.cursor_<-1 || ns.objects_.cursor_>=int32_t(ContinuousSoundObjects::capacity))throw std::runtime_error("Invalid continuous sound cursor");
        for(auto& slot:ns.objects_.slots_){const auto flags=r.u32();if(flags>255)throw std::runtime_error("Invalid sound generation");slot.flag=uint8_t(flags);slot.epoch=r.u64();slot.object={};if(!(slot.flag&0x80)){slot.object.sound=uint16_t(r.u32());slot.object.position=r.pod<std::array<float,3>>();for(float x:slot.object.position)finite(x);if(!slot.epoch)throw std::runtime_error("Invalid sound identity");slot.object.audio_entity=-5;}}
        nf.rng_=r.u32();nf.emitted_=r.u64();
        // Replacing the logical emitter table does not release the previous
        // SA fire systems. Preserve their cleanup receipts until tick/reset can
        // confirm that each native fire is stopped.
        for(const auto& old:nf.emitters_)if(old.used&&old.fire_token)nf.retired_fires_.push_back(old.fire_token);
        for(auto& e:nf.emitters_){e={};e.used=flag(r);if(!e.used)continue;e.type=int32_t(r.u32());e.position=r.pod<FxVec3>();e.target=r.pod<FxVec3>();e.head=r.pod<FxVec3>();e.size=r.pod<float>();e.rand_value=r.pod<float>();for(float x:e.position)finite(x);for(float x:e.target)finite(x);for(float x:e.head)finite(x);finite(e.size);finite(e.rand_value);e.skip_frames=uint8_t(r.u32());e.cycles=uint8_t(r.u32());e.frame_counter=uint8_t(r.u32());e.creation_chance=int16_t(r.u32());e.has_color=flag(r);e.head_started=flag(r);e.rgb=r.pod<std::array<uint8_t,3>>();e.alpha=uint8_t(r.u32());const bool expires=flag(r);if(expires)e.remove_at=now+r.u32();if(expires && !e.remove_at)e.remove_at=1;e.fire_token=0;}
        std::map<int32_t,int32_t> committed_groups,staged_groups;
        const auto read_groups=[&](std::map<int32_t,int32_t>& groups){const auto groups_n=count(r,128);for(uint32_t i=0;i<groups_n;++i){const auto group=int32_t(r.u32()),state=int32_t(r.u32());if(group<0 || group>127 || !groups.emplace(group,state).second)throw std::runtime_error("Invalid world sector group");}};
        read_groups(committed_groups);bool had_plan=false;if(version>=2){read_groups(staged_groups);had_plan=flag(r);}r.end();
        if(sectors.faulted_)throw std::runtime_error("Cannot restore campaign world sectors: service is faulted: "+(sectors.fault_diagnostic_.empty()?std::string("no diagnostic was recorded"):sectors.fault_diagnostic_));
        // The adapter restore may have staged a different world group first.
        // Replace only logical ledgers and the derived plan; keep retained SA
        // pool receipts and model requests owned until the real transaction
        // transitions the loaded world.
        sectors.committed_states_.clear();sectors.staged_states_.clear();sectors.plan_.reset();++sectors.state_revision_;
        for(const auto& pair:committed_groups){const auto staged=sectors.set(pair.first,pair.second);if(!staged.executed)throw std::runtime_error("Cannot stage committed world-sector state for restore: "+staged.diagnostic);}
        // Re-create owned audio voices with the exact script slot/generation, never native channel IDs.
        for(size_t i=0;i<ns.objects_.slots_.size();++i){auto& slot=ns.objects_.slots_[i];if(slot.flag&0x80)continue;const ContinuousSoundObjects::Snapshot source{{int32_t((i<<8)|slot.flag),slot.epoch},slot.object};const auto receipt=ns.create?ns.create(source):ContinuousEntityReceipt{};if(!receipt.accepted)throw std::runtime_error("Cannot restore owned continuous sound");slot.object.audio_entity=receipt.entity;}
        h=std::move(nh);c=std::move(nc);routes=std::move(nr);coronas=std::move(ncor);sound=std::move(ns);fx=std::move(nf);ScriptCoronas::next_id_=std::max(ScriptCoronas::next_id_,next_id);
        for(auto& emitter:fx.emitters_)if(emitter.used && emitter.type==FxParticles::object_small_fire){
            FxStatus status;if(!fx.callbacks.fire_start)throw std::runtime_error("Saved fire has no native producer");
            emitter.fire_token=fx.callbacks.fire_start(emitter.position,emitter.size,status);
            if(!emitter.fire_token || !status.ok)throw std::runtime_error("Cannot restore owned fire: "+status.diagnostic);
        }
        if(!h.push(vm))throw std::runtime_error("Cannot restore HUD cells");for(size_t i=0;i<4;++i)if(h.entries_[i].active && (!h.update || !h.update(*h.saved_requests_[i])))throw std::runtime_error("Cannot restore HUD widget");if(h.frozen_ && (!h.update || !h.update({HudRequest::Action::freeze,0,1})))throw std::runtime_error("Cannot restore HUD freeze");
        for(size_t i=0;i<3;++i)if(h.entries_[i+1].active && h.saved_prefixes_[i] && (!h.update || !h.update({HudRequest::Action::counter_prefix,HudBridge::native_cell(i+1),int32_t(*h.saved_prefixes_[i]),i})))throw std::runtime_error("Cannot restore HUD prefix");
        return PendingWorld{std::move(staged_groups),had_plan};
    }
    // Apply script-staged world intent after the archived committed world has
    // gone through its real request/complete/commit boundary. A request may
    // remain pending on streaming; the VM resumes the original source protocol.
    static void apply_pending(const PendingWorld& pending,SaWorldSectors& sectors){
        for(const auto& pair:pending.states){const auto staged=sectors.set(pair.first,pair.second);if(!staged.executed)throw std::runtime_error("Cannot restore pending world-sector declaration: "+staged.diagnostic);}
        if(pending.requested){const auto request=sectors.request();if(!request.executed)throw std::runtime_error("Cannot resume saved world-sector request: "+request.diagnostic);}
    }
};
}
