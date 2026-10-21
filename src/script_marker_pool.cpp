#include "lcs/script_marker_pool.hpp"
#include <cmath>
#include <algorithm>

namespace lcs {
namespace {
constexpr float tau=6.283185482025146484375f;
float proximity(float size,float distance){
    if(distance>=25.f)return size;
    if(distance<=5.f)return size-size*.3f;
    return size-((25.f-distance)*size*.3f)*.05f;
}
void identity(std::array<float,12>& m){m={1,0,0,0,1,0,0,0,1,0,0,0};}
void rotate_z(std::array<float,12>& m,float angle){
    const auto c=std::cos(angle),s=std::sin(angle);
    for(size_t i=0;i<9;i+=3){const auto x=m[i],y=m[i+1];m[i]=c*x-s*y;m[i+1]=c*y+s*x;}
}
void directed(std::array<float,12>& m,const std::array<float,3>& position,const std::array<float,3>& target){
    float x=target[0]-position[0],y=target[1]-position[1];
    if(x*x+y*y<.0001f)x=1.f;
    const auto length=std::sqrt(x*x+y*y);x/=length;y/=length;
    // Retail builds [cross(Z,target), target, Z] and postmultiplies the
    // SetRotateX(-20 degrees) matrix. Target Z is deliberately not consulted.
    constexpr float angle=-.3490658700466156005859375f;
    const float c=std::cos(angle),s=std::sin(angle);
    m={-y,x,0, c*x,c*y,s, -s*x,-s*y,c, position[0],position[1],position[2]};
}
}
bool ScriptMarkerPool::submit(const ScriptMarkerCommand& command,const ScriptMarkerEnvironment& environment){
    if(!std::isfinite(environment.time_step))return false;
    for(auto value:environment.player)if(!std::isfinite(value))return false;
    // Commit source pool state only after the required environment queries
    // succeed. This does not promise rollback of native query side effects:
    // an engine adapter must quarantine an entered native failure.
    auto draft=*this;
    if(command.copy_before){
        const auto copy=*command.copy_before;
        for(const auto& entry:entries_)if(entry.source.identity==copy.source_identity){
            if(!entry.live)break;
            auto placement=entry.source;
            placement.identity=copy.new_identity;
            placement.position={entry.matrix[9],entry.matrix[10],entry.matrix[11]};
            placement.size=entry.current_size;placement.vertical_size=100;
            placement.target.reset();placement.force_position=copy.force_position;
            placement.persistent_fade=copy.persistent_fade;
            if(!draft.place(placement,environment))return false;
            break;
        }
    }
    if(!draft.place(command.placement,environment))return false;
    entries_=draft.entries_;
    return true;
}
bool ScriptMarkerPool::place(const ScriptMarkerPlacement& p,const ScriptMarkerEnvironment& environment){
    if(!p.identity || (p.type!=1 && p.type!=2 && p.type!=5) || !p.pulse_period ||
       !std::isfinite(p.size) || !std::isfinite(p.pulse_fraction) || !std::isfinite(p.vertical_size))return false;
    for(auto value:p.position)if(!std::isfinite(value))return false;
    if(p.target)for(auto value:*p.target)if(!std::isfinite(value))return false;
    const auto dx=p.position[0]-environment.player[0],dy=p.position[1]-environment.player[1];
    const float distance_squared=dx*dx+dy*dy;
    const float distance=std::sqrt(distance_squared);
    if(!std::isfinite(distance))return false;
    ScriptMarkerInstance* chosen=nullptr;
    for(auto& entry:entries_)if(!entry.used && entry.source.identity==p.identity){chosen=&entry;break;}
    if(!chosen)for(auto& entry:entries_)if(!entry.live){chosen=&entry;break;}
    if(!chosen && (p.type==1 || p.type==2)){
        for(auto& entry:entries_)if(entry.source.type==1 && entry.camera_range>distance &&
            (!chosen || chosen->camera_range<entry.camera_range))chosen=&entry;
        if(chosen)chosen->live=false;
    }
    if(!chosen)return true; // Retail pool exhaustion has no script failure/output.
    auto& entry=*chosen;
    const bool fresh=!entry.live || entry.source.identity!=p.identity || entry.source.type!=p.type;
    const bool preserve_grounded_point=!fresh && p.type==5 && p.force_position &&
        entry.source.position==p.position;
    if(!fresh && p.type==5 && p.force_position && !preserve_grounded_point)
        entry.find_z=true;
    if(fresh){
        const auto previous_matrix=entry.matrix;
        entry={};entry.matrix=previous_matrix;identity(entry.matrix);
        entry.source=p;entry.start_time=environment.now;
        entry.current_size=p.size;entry.standard_size=p.size;entry.live=true;
    }
    entry.camera_range=distance;
    entry.source.colour[0]=p.colour[0];entry.source.colour[1]=p.colour[1];entry.source.colour[2]=p.colour[2];
    const bool arrow=p.type==1 || p.type==2;
    if(arrow)entry.standard_size=proximity(p.size,distance);
    else{
        float alpha=float(p.colour[3]);
        if(distance<p.size+12.f){
            if(distance>p.size+1.f)alpha*=1.f-((p.size+12.f)-distance)*.063636362552642822265625f;
            else alpha*=.3f;
        }
        entry.source.colour[3]=uint8_t(alpha);
    }
    const float phase=float((environment.now-entry.start_time)&(entry.source.pulse_period-1))*tau/float(entry.source.pulse_period);
    const float wave=std::sin(phase);
    if(!fresh)entry.current_size=entry.standard_size-(p.pulse_fraction*entry.standard_size)*wave;
    // Creation stores the vertical scale BEFORE the arrow proximity/pulse
    // update at24B030. Refresh stores it AFTER24A420. Keep that first-frame
    // asymmetry instead of assuming arrows are always uniformly scaled.
    entry.vertical_size=p.vertical_size>0?p.vertical_size:entry.current_size;
    if(fresh && arrow)entry.current_size=entry.standard_size-(p.pulse_fraction*entry.standard_size)*wave;
    auto position=p.position;
    if(arrow)position[2]+=entry.standard_size*(wave*.25f);
    if(!fresh && entry.source.rotate_rate)rotate_z(entry.matrix,float(entry.source.rotate_rate)*environment.time_step*.0174532942473888397216796875f);
    if(arrow && p.target)directed(entry.matrix,position,*p.target);
    else if(!preserve_grounded_point){entry.matrix[9]=position[0];entry.matrix[10]=position[1];entry.matrix[11]=position[2];}
    if(p.force_position && !preserve_grounded_point){entry.matrix[9]=p.position[0];entry.matrix[10]=p.position[1];entry.matrix[11]=p.position[2];}
    if(fresh && p.type==5)entry.find_z=true;
    if(entry.find_z && distance_squared<10000.f){
        if(!environment.floor)return false;
        const auto floor=environment.floor(p.position);
        if(!floor)return false;
        if(floor->queryable){
            if(floor->height){
                if(!std::isfinite(*floor->height))return false;
                if(*floor->height!=0)entry.matrix[11]=*floor->height-p.size*.05f;
            }
            entry.find_z=false;
        }
    }
    // Remember the raw script placement. The matrix may hold a ground-snapped
    // Z, which must survive the next refresh with the same source coordinates.
    entry.source.position=p.position;
    entry.source.persistent_fade=p.persistent_fade;
    entry.used=true;
    return true;
}
bool ScriptMarkerPool::frame(const Draw& draw){
    if(!draw)return false;
    for(size_t i=0;i<entries_.size();++i){
        auto& entry=entries_[i];
        if(!entry.used){entry.live=false;entry.source.identity=0;continue;}
        if(entry.current_size!=entry.vertical_size || entry.camera_range<150.f){
            if(entry.source.persistent_fade){
                if(entry.source.colour[3]<=12){entry.live=false;entry.used=false;entry.source.identity=0;continue;}
                entry.source.colour[3]-=12;
            }
            bool duplicate=false;
            for(size_t j=0;j<i;++j)if(entries_[j].matrix==entry.matrix){duplicate=true;break;}
            if(!duplicate && !draw(i,entry))return false;
        }
        if(!entry.source.persistent_fade)entry.used=false;
    }
    return true;
}
}
