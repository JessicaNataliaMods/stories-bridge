#include "lcs/collectives.hpp"
#include "lcs/engine_dispatch.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int32_t bits(float f){int32_t v;std::memcpy(&v,&f,4);return v;}
struct Result {lcs::StepResult status;int32_t value;};
template<class Adapter>
Result command(Adapter& service,uint16_t op,std::initializer_list<int32_t> args,bool output=false){
    std::vector<uint8_t> code(64);code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));
    for(auto v:args){code.push_back(6);for(int j=0;j<4;++j)code.push_back(uint8_t(uint32_t(v)>>(8*j)));}
    if(output)code.push_back(12);
    lcs::Vm vm(code,64,64);lcs::Thread thread;thread.ip=64;thread.locals[0]=-99;
    vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t o){return lcs::try_engine_adapter(service,v,t,o).value_or(lcs::StepResult::unsupported);};
    auto status=vm.step(thread,0);return {status,thread.locals[0]};
}
int main(){try{
    lcs::Collectives service;bool available=true;int queries=0,calls=0;
    std::map<int32_t,lcs::ObjectiveActor> actors;
    std::vector<lcs::CollectiveCandidate> candidates;
    for(int i=1;i<=50;++i)actors[i*256]={i*256,uintptr_t(0x1000+i*16)};
    service.available=[&]{return available;};
    service.live=[&](int32_t handle)->std::optional<lcs::ObjectiveActor>{auto it=actors.find(handle);return it==actors.end()?std::optional<lcs::ObjectiveActor>{}:it->second;};
    service.candidates=[&](float x,float y,float radius)->std::optional<std::vector<lcs::CollectiveCandidate>>{
        check(x==10 && y==20 && radius==30,"XY and radius passed unchanged");++queries;return candidates;
    };
    std::vector<int32_t> applied;
    service.flee=[&](const lcs::ObjectiveActor& actor,const lcs::ObjectiveActor& target){
        check(target.handle==50*256,"Real target identity");++calls;applied.push_back(actor.handle);lcs::NativeResult r;r.executed=true;return r;
    };
    {
        lcs::ScmImage image;lcs::NativeAdapter adapter(image);adapter.world_ready=true;adapter.live_ped_identity=service.live;
        int native_calls=0;adapter.execute=[&](const lcs::NativeCall& call){
            check(call.opcode==0x05DB && call.inputs==std::vector<int32_t>({256,50*256,100,-1}),"Collective consumer shares individual ALWAYS native integer distance packet");
            ++native_calls;lcs::NativeResult result;result.executed=true;return result;
        };
        check(adapter.execute_collective_flee(actors[256],actors[50*256]).executed && native_calls==1,"Real consumer accepted");
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS,{256,50*256}).status==lcs::StepResult::continued && native_calls==2,"Individual source flee also sends integer native distance");
        auto recycled=actors[256];++recycled.native_ped;
        check(!adapter.execute_collective_flee(recycled,actors[50*256]).executed && native_calls==2,"Consumer revalidates identity immediately before native entry");
    }
    auto create=[&]{return command(service,COMMAND_ADD_PEDS_IN_AREA_TO_COLL,{bits(10),bits(20),bits(30)},true);};
    auto flee=[&](int32_t group){return command(service,COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS,{group,50*256});};
    available=false;check(create().status==lcs::StepResult::unsupported && !queries,"Unavailable world leaves output/table untouched");available=true;
    candidates={{actors[256],0,1},{actors[512],6,1},{actors[768],4,2},{actors[1024],4,1},{actors[1024],4,1}};
    const auto first=create();check(first.status==lcs::StepResult::continued && first.value==0,"First source group ID");
    check(flee(first.value).status==lcs::StepResult::continued && applied==std::vector<int32_t>({1024,1024}),"Player/cop/mission filters, duplicates and order preserved");
    candidates.clear();for(int i=5;i<45;++i)candidates.push_back({actors[i*256],4,1});
    const auto second=create();calls=0;applied.clear();flee(second.value);check(calls==30 && applied.front()==5*256 && applied.back()==34*256,"32 shared slots; existing group never overwritten");
    const auto full=create();calls=0;flee(full.value);check(calls==0 && full.value==second.value+1,"Full table still returns an empty group ID");
    command(service,COMMAND_CLEAR_COLL,{first.value});calls=0;flee(first.value);check(!calls,"Clear removes entries without native ped mutation");
    candidates={{actors[9*256],4,1},{actors[10*256],4,1},{actors[11*256],4,1}};const auto reuse=create();
    actors.erase(9*256);applied.clear();flee(reuse.value);check(applied==std::vector<int32_t>({10*256}),"First free slots reused; stale pool generations removed");
    command(service,COMMAND_CLEAR_COLL,{second.value});command(service,COMMAND_CLEAR_COLL,{reuse.value});
    candidates={{actors[12*256],4,1}};candidates[0].actor.native_ped++;
    check(create().status==lcs::StepResult::unsupported,"Changed identity refuses before group allocation/output");
    candidates={{actors[12*256],4,1},{actors[13*256],4,1}};const auto partial=create();
    int entered=0;service.flee=[&](const auto&,const auto&){lcs::NativeResult r;r.executed=++entered==1;return r;};
    check(flee(partial.value).status==lcs::StepResult::failed && entered==2,"Partial operation stops at first refusal");
    check(flee(partial.value).status==lcs::StepResult::failed && entered==2,"Poisoned service never replays accepted members");
    {
        // 408 / 606 / 614: finite SA flee packets (05DA point, 05DB character), never the ALWAYS packet.
        // Occupants leave through a real SA script sequence first. The mock below models the pool/global
        // contracts read from the executable: 0615 overwrites the global under construction (8D2E98) and
        // returns (uint16 generation<<16)|slot or -1 when full; tasks with ped -1 append to that global;
        // 0616 clears it unconditionally; 0618 takes a task reference; 061B marks removal pending while a
        // ped still holds a reference and frees otherwise; the handle is invalid once cleared.
        lcs::ScmImage image;lcs::NativeAdapter adapter(image);adapter.world_ready=true;adapter.live_ped_identity=service.live;
        struct Performed {int32_t ped;std::vector<lcs::NativeCall> tasks;bool repeat=false;};
        std::vector<lcs::NativeCall> sent;std::vector<Performed> performed;std::vector<uint16_t> trace;
        std::set<int32_t> occupied;
        std::set<int32_t> vehicles;std::map<int32_t,std::array<float,3>> car_position,car_speed;std::map<int32_t,int32_t> ped_car,driver_of,seat_ped;
        int32_t max_passengers=4,game_timer=100000;std::vector<int32_t> random_queue;bool in_control=true,fail_dive=false;
        std::vector<std::pair<int32_t,bool>> heed_calls;std::vector<lcs::NativeCall> dive_calls;
        const auto entity_float_equal=[](int32_t encoded,float expected){float value;std::memcpy(&value,&encoded,4);return std::fabs(value-expected)<1e-4f;};
        adapter.set_char_heed_threats=[&](int32_t ped,bool enabled){heed_calls.push_back({ped,enabled});return true;};
        adapter.char_in_control=[&](int32_t)->std::optional<bool>{return in_control;};
        struct Pool {
            int32_t active=-1;std::array<uint8_t,64> open{},pending{},issued{};std::array<uint32_t,64> first_task{},references{};
            std::array<uint16_t,64> generation{};std::array<uint8_t,64> repeat{};std::map<int,std::vector<lcs::NativeCall>> tasks;
        } pool;
        bool origin_finite=true;int violations=0;bool ground_fails=false;float ground_height=5.f;std::vector<std::vector<int32_t>> ground_queries;
        uint16_t fail_op=0,throw_op=0;int fail_nth=0,seen_op=0,fail_at=0,seen=0;std::function<void()> after_close;
        int open_fault_stage=0;bool open_fault_returns_failure=false; // 1: after allocate+global, 2: after handle issue
        const auto allocated=[&](size_t s){return pool.open[s]||pool.first_task[s];};
        const auto valid_handle=[&](int32_t h)->int{ // 4839A0: slot from low 16 bits, generation from high 16 bits, issued flag
            const size_t slot=uint32_t(h)&0xFFFFu;
            return slot<64 && pool.issued[slot] && pool.generation[slot]==uint16_t(uint32_t(h)>>16)?int(slot):-1;
        };
        adapter.task_sequence_state=[&]()->std::optional<lcs::TaskSequenceState>{
            lcs::TaskSequenceState s;s.active=pool.active;s.open=pool.open;s.pending=pool.pending;s.issued=pool.issued;
            s.first_task=pool.first_task;s.references=pool.references;s.generation=pool.generation;
            if(pool.active>=0 && !pool.open[size_t(pool.active)])return std::nullopt; // contradictory, as the production reader reports it
            return s;
        };
        adapter.abort_unissued_task_sequence=[&](size_t slot,uint16_t previous_generation,uint8_t previous_issued){
            if(slot>=32 || !pool.open[slot] || (pool.active!=-1 && pool.active!=int32_t(slot)) ||
               pool.generation[slot]!=previous_generation || pool.issued[slot]!=previous_issued ||
               pool.first_task[slot] || pool.pending[slot] || pool.references[slot] || !pool.tasks[slot].empty())return false;
            pool.open[slot]=0;if(pool.active==int32_t(slot))pool.active=-1;return true;
        };
        adapter.execute=[&](const lcs::NativeCall& call){
            lcs::NativeResult r;r.executed=true;trace.push_back(call.opcode);
            if(fail_op==call.opcode && ++seen_op==fail_nth){r.executed=false;return r;}
            if(throw_op==call.opcode && ++seen_op==fail_nth)throw std::runtime_error("guarded native refusal after entry");
            const bool task=call.opcode==0x05D3||call.opcode==0x0633||call.opcode==0x05DA||call.opcode==0x05DB||call.opcode==0x05BA||call.opcode==0x05CD;
            switch(call.opcode){
            case 0x00DF:r.condition=occupied.count(call.inputs[0])!=0;return r;
            case 0x00A0:r.values[0]=origin_finite?bits(1.5f):bits(std::numeric_limits<float>::quiet_NaN());r.values[1]=bits(-2.f);r.values[2]=bits(3.f);return r;
            case 0x0615: { // non-mission script: slots 0..31, free when not open and no first task
                int slot=-1;for(int s=0;s<32;++s)if(!allocated(size_t(s))){slot=s;break;}
                if(slot<0){r.values[0]=-1;return r;}
                pool.open[size_t(slot)]=1;pool.first_task[size_t(slot)]=0;pool.references[size_t(slot)]=0;pool.pending[size_t(slot)]=0;
                pool.tasks[slot].clear();pool.repeat[size_t(slot)]=0;pool.active=slot; // unconditional overwrite of any foreign construction context
                if(open_fault_stage==1){if(open_fault_returns_failure){r.executed=false;return r;}throw std::runtime_error("fault after allocation and global write");}
                auto& g=pool.generation[size_t(slot)];g=g>=0xFFFE?uint16_t(1):uint16_t(g+1);pool.issued[size_t(slot)]=1;
                if(open_fault_stage==2){if(open_fault_returns_failure){r.executed=false;return r;}throw std::runtime_error("fault after handle issue");}
                r.values[0]=int32_t((uint32_t(g)<<16)|uint32_t(slot));return r;
            }
            case 0x0616: {
                const int slot=call.inputs.size()==1?valid_handle(call.inputs[0]):-1;
                if(slot<0){++violations;r.executed=false;return r;}
                pool.open[size_t(slot)]=0;pool.active=-1;if(after_close)after_close();return r; // 49193A resets the global regardless of owner
            }
            case 0x0618: {
                const int slot=call.inputs.size()==2?valid_handle(call.inputs[1]):-1;
                if(slot<0 || pool.open[size_t(slot)] || !pool.first_task[size_t(slot)] || !service.live(call.inputs[0])){++violations;r.executed=false;return r;}
                ++pool.references[size_t(slot)];performed.push_back({call.inputs[0],pool.tasks[slot],pool.repeat[size_t(slot)]!=0});return r;
            }
            case 0x061B: {
                const int slot=call.inputs.size()==1?valid_handle(call.inputs[0]):-1;
                if(slot<0){++violations;r.executed=false;return r;}
                pool.open[size_t(slot)]=0;pool.issued[size_t(slot)]=0; // handle dies in both branches
                if(pool.references[size_t(slot)])pool.pending[size_t(slot)]=1;
                else {pool.pending[size_t(slot)]=0;pool.first_task[size_t(slot)]=0;pool.tasks[slot].clear();}
                return r;
            }
            case 0x056E:r.condition=vehicles.count(call.inputs[0])!=0;return r;
            case 0x0811:r.values[0]=ped_car.count(call.inputs[0])?ped_car[call.inputs[0]]:-1;return r;
            case 0x00AA:{const auto p=car_position[call.inputs[0]];for(size_t i=0;i<3;++i)r.values[i]=bits(p[i]);return r;}
            case 0x06A2:{const auto p=car_speed[call.inputs[0]];for(size_t i=0;i<3;++i)r.values[i]=bits(p[i]);return r;}
            case 0x046C:r.values[0]=driver_of.count(call.inputs[0])?driver_of[call.inputs[0]]:-1;return r;
            case 0x01EA:r.values[0]=max_passengers;return r;
            case 0x0432:{const auto key=call.inputs[0]*16+call.inputs[1];r.values[0]=seat_ped.count(key)?seat_ped[key]:-1;return r;}
            case 0x01BD:r.values[0]=game_timer;return r;
            case 0x0643:{const int slot=call.inputs.size()==2?valid_handle(call.inputs[0]):-1;
                if(slot<0 || !pool.open[size_t(slot)] || pool.active!=slot || call.inputs[1]!=1){++violations;r.executed=false;return r;}
                pool.repeat[size_t(slot)]=1;return r;}
            case 0x056D:r.condition=service.live(call.inputs[0]).has_value();return r;
            case 0x02CE:ground_queries.push_back(call.inputs);if(ground_fails){r.executed=false;return r;}r.values[0]=bits(ground_height);return r;
            case 0x0209:
                if(random_queue.empty()){r.executed=false;return r;}
                r.values[0]=random_queue.front();random_queue.erase(random_queue.begin());
                if(call.inputs.size()!=2 || r.values[0]<call.inputs[0] || r.values[0]>=call.inputs[1]){++violations;r.executed=false;}
                return r;
            case 0x0673:if(fail_dive){r.executed=false;return r;}dive_calls.push_back(call);return r;
            default:break;
            }
            if(task && call.inputs[0]==-1) { // sequence form: appended to the global under construction
                if(pool.active<0 || pool.tasks[pool.active].size()>=8){++violations;r.executed=false;return r;}
                pool.tasks[pool.active].push_back(call);pool.first_task[size_t(pool.active)]=0x1000u+uint32_t(pool.active);return r;
            }
            if(task && pool.active>=0){++violations;r.executed=false;return r;} // a real-ped task would be swallowed by the open sequence
            sent.push_back(call);if(fail_at && ++seen==fail_at)r.executed=false;return r;
        };
        std::set<size_t> foreign; // slots owned by someone else in a given scenario
        const auto clean=[&]{ // nothing open, no leaked slot: only performed sequences may stay (pending behind a reference)
            if(pool.active!=-1 || violations)return false;
            for(size_t s=0;s<64;++s){if(pool.open[s])return false;if(allocated(s) && !foreign.count(s) && !(pool.pending[s] && pool.references[s]))return false;}
            return true;
        };
        const auto sa_finishes_performed_tasks=[&]{ // SA releases each performing ped's reference and frees pending sequences
            for(size_t s=0;s<64;++s)if(pool.references[s]){--pool.references[s];if(!pool.references[s] && pool.pending[s]){pool.pending[s]=0;pool.first_task[s]=0;}}
            for(auto& entry:pool.tasks)if(!pool.first_task[size_t(entry.first)])entry.second.clear();
        };
        const std::vector<int32_t> point_256={256,bits(1.5f),bits(-2.f),bits(3.f),bits(30.f),10000};
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::continued && sent.size()==1 &&
              sent[0].opcode==0x05DA && sent[0].inputs==point_256 && performed.empty() && trace==std::vector<uint16_t>({0x00DF,0x00A0,0x05DA}),
              "408 on foot still sends the SA flee-point directly with no sequence, 30 m radius and the LCS 10 s duration");
        sent.clear();trace.clear();occupied.insert(256);
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::continued && sent.empty() && clean() && performed.size()==1,"408 from a vehicle performs one sequence and releases it");
        check(performed[0].ped==256 && performed[0].tasks.size()==2 && performed[0].tasks[0].opcode==0x0633 && performed[0].tasks[0].inputs==std::vector<int32_t>({-1}) &&
              performed[0].tasks[1].opcode==0x05DA && performed[0].tasks[1].inputs==std::vector<int32_t>({-1,bits(1.5f),bits(-2.f),bits(3.f),bits(30.f),10000}),
              "Sequence is leave-any-car first, then the same finite flee-point with the sequence ped operand -1");
        check(trace==std::vector<uint16_t>({0x00DF,0x00A0,0x0615,0x0633,0x05DA,0x0616,0x0618,0x061B}),"Exact SA order: open, leave, flee, close, perform, clear");
        check(pool.pending[0] && pool.references[0]==1 && allocated(0),"After PERFORM the sequence stays allocated with removal pending behind the ped's task reference, as SA does");
        sa_finishes_performed_tasks();check(!allocated(0) && clean(),"SA later frees the pending sequence; nothing leaks");
        performed.clear();trace.clear();origin_finite=false;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::unsupported && performed.empty() && trace==std::vector<uint16_t>({0x00DF,0x00A0}),"408 refuses a nonfinite origin before opening a sequence");
        origin_finite=true;
        check(!adapter.execute_flee_point_till_safe(lcs::ObjectiveActor{256,actors[256].native_ped+1}).executed && performed.empty() && clean(),"Stale actor identity never reaches a flee task or sequence");

        // F1: generation 0x8000 yields a valid NEGATIVE int32 handle; -1 stays the pool-full sentinel.
        pool.generation[0]=0x7FFF;trace.clear();performed.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::continued && clean() && performed.size()==1 &&
              trace.size()==8 && trace[2]==0x0615,"A valid negative generation-bearing handle is accepted and used for close, perform and clear");
        check(pool.generation[0]==0x8000 && int32_t(uint32_t(pool.generation[0])<<16)<0,"Fixture really produced a negative handle");
        sa_finishes_performed_tasks();
        for(size_t s=0;s<32;++s){pool.first_task[s]=0x77;foreign.insert(s);} // pool completely full of foreign sequences
        trace.clear();performed.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::unsupported && performed.empty() && pool.active==-1 && violations==0 &&
              trace==std::vector<uint16_t>({0x00DF,0x00A0,0x0615}),"The -1 sentinel (pool full) is a clean refusal that touches nothing");
        for(size_t s=0;s<32;++s)pool.first_task[s]=0;foreign.clear();

        // F2: a sequence under construction by another script is never overwritten.
        pool.active=7;pool.open[7]=1;pool.issued[7]=1;pool.generation[7]=5;foreign.insert(7);trace.clear();performed.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::unsupported && performed.empty() &&
              trace==std::vector<uint16_t>({0x00DF,0x00A0}) && pool.active==7 && pool.open[7] && pool.tasks[7].empty() && violations==0,"A foreign open sequence is detected before OPEN and left untouched");
        const auto saved_reader=adapter.task_sequence_state;adapter.task_sequence_state=nullptr;trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::unsupported && trace==std::vector<uint16_t>({0x00DF,0x00A0}),"Without the native state reader an occupant is refused before OPEN");
        adapter.task_sequence_state=[]()->std::optional<lcs::TaskSequenceState>{return std::nullopt;};trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::unsupported && trace==std::vector<uint16_t>({0x00DF,0x00A0}),"Unprovable native sequence state is refused before OPEN");
        adapter.task_sequence_state=saved_reader;pool.active=-1;pool.open[7]=0;pool.issued[7]=0;foreign.clear();
        // Foreign allocated-but-closed sequences are fine and are never touched.
        pool.first_task[3]=0x55;pool.issued[3]=1;pool.generation[3]=9;foreign.insert(3);performed.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::continued && performed.size()==1 && pool.first_task[3]==0x55 && pool.issued[3]==1 && clean(),"Foreign closed sequences are neither used nor released");
        sa_finishes_performed_tasks();pool.first_task[3]=0;pool.issued[3]=0;foreign.clear();

        // Failure injection: before entry, after each sequence step, and by guarded exception.
        for(const uint16_t op:{uint16_t(0x0615),uint16_t(0x0633),uint16_t(0x05DA),uint16_t(0x0616),uint16_t(0x0618),uint16_t(0x061B)}) {
            for(const bool thrown:{false,true}) {
                performed.clear();trace.clear();fail_op=thrown?0:op;throw_op=thrown?op:0;fail_nth=1;seen_op=0;
                const auto status=command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status;
                const bool before_entry=op==0x0615 && !thrown;
                check(status==(before_entry?lcs::StepResult::unsupported:lcs::StepResult::failed),"Failure before entry is a refusal; after entry it requires repair");
                check(clean(),"A failed sequence is always closed and freed or pending behind a reference, never leaked or left open");
                check(performed.size()==((op==0x061B)?1u:0u),"Only a fully accepted PERFORM may have reached the actor");
                sa_finishes_performed_tasks();
            }
        }
        fail_op=throw_op=0;performed.clear();
        // F3: OPEN mutates (allocates, writes the global, issues the handle) and THEN fails.
        for(const bool returns_failure:{false,true}) {
            open_fault_returns_failure=returns_failure;trace.clear();
            open_fault_stage=2; // handle already issued: ownership is provable, the slot is closed and cleared
            const auto status=command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status;
            check(status==lcs::StepResult::failed && clean() && performed.empty() && !allocated(0) &&
                  std::find(trace.begin(),trace.end(),uint16_t(0x0616))!=trace.end() && std::find(trace.begin(),trace.end(),uint16_t(0x061B))!=trace.end(),
                  "OPEN failing after it allocated and issued a handle releases exactly that owned slot and is not replayed");
            open_fault_stage=1; // no handle was issued: the typed backend aborts only this empty OPEN
            trace.clear();foreign.clear();
            const auto status_a=command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status;
            check(status_a==lcs::StepResult::failed && trace==std::vector<uint16_t>({0x00DF,0x00A0,0x0615}) &&
                  performed.empty() && pool.active==-1 && !pool.open[0] && !allocated(0) && violations==0,
                  "OPEN failing before handle issuance aborts only its empty predicted slot and performs nothing");
        }
        open_fault_stage=0;open_fault_returns_failure=false;
        // A newly allocated foreign slot after CLOSE is outside this OPEN's
        // predicted ownership and must survive repair untouched.
        fail_op=0x0618;throw_op=0;fail_nth=1;seen_op=0;trace.clear();performed.clear();foreign.clear();
        after_close=[&]{pool.first_task[1]=0x55;pool.issued[1]=1;pool.generation[1]=9;foreign.insert(1);};
        const auto foreign_added=command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status;
        check(foreign_added==lcs::StepResult::failed && !allocated(0) && pool.first_task[1]==0x55 &&
              pool.issued[1]==1 && pool.generation[1]==9 && violations==0 && performed.empty(),
              "Recovery releases only its predicted slot and preserves a different newly allocated slot");
        after_close=nullptr;pool.first_task[1]=0;pool.issued[1]=0;foreign.clear();fail_op=throw_op=0;
        after_close=[&]{actors[256].native_ped+=1;};
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,{256}).status==lcs::StepResult::failed && performed.empty() && clean(),"Actor identity change before PERFORM aborts the sequence and frees it");
        after_close=nullptr;actors[256].native_ped-=1;

        lcs::Collectives fresh;fresh.available=[&]{return true;};fresh.live=service.live;fresh.candidates=service.candidates;
        int always_calls=0;
        fresh.flee=[&](const auto&,const auto&){++always_calls;lcs::NativeResult r;r.executed=true;return r;};
        fresh.flee_till_safe=[&](const lcs::ObjectiveActor& a,const lcs::ObjectiveActor& t){return adapter.execute_collective_flee_till_safe(a,t);};
        fresh.flee_point_till_safe=[&](const lcs::ObjectiveActor& a){return adapter.execute_flee_point_till_safe(a);};
        candidates={{actors[256],4,1},{actors[512],4,1},{actors[768],4,1}};
        occupied.clear();sent.clear();performed.clear();
        const auto made=command(fresh,COMMAND_ADD_PEDS_IN_AREA_TO_COLL,{bits(10),bits(20),bits(30)},true);
        check(command(fresh,COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE,{made.value,50*256}).status==lcs::StepResult::continued && sent.size()==3 && !always_calls && performed.empty(),"614 on foot applies the finite character flee to every member, not the ALWAYS consumer");
        for(size_t i=0;i<3;++i)check(sent[i].opcode==0x05DB && sent[i].inputs==std::vector<int32_t>({int32_t((i+1)*256),50*256,30,6000}),"614 packet uses FILD integer 30 m and the LCS 6 s flee duration");
        sent.clear();
        check(command(fresh,COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE,{made.value,49*256+1}).status==lcs::StepResult::unsupported && sent.empty() && performed.empty(),"614 with a missing threat refuses before any member changes");
        occupied={512,768}; // first member on foot, the other two in vehicles
        check(command(fresh,COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE,{made.value,50*256}).status==lcs::StepResult::continued && sent.size()==1 && sent[0].inputs[0]==256 &&
              performed.size()==2 && clean(),"614 handles each member separately: on foot direct, occupants by sequence");
        for(size_t i=0;i<2;++i)check(performed[i].ped==int32_t((i+2)*256) && performed[i].tasks.size()==2 && performed[i].tasks[0].opcode==0x0633 &&
              performed[i].tasks[1].opcode==0x05DB && performed[i].tasks[1].inputs==std::vector<int32_t>({-1,50*256,30,6000}),"614 occupant sequence is leave then FILD 30 m / 6 s character flee");
        sa_finishes_performed_tasks();sent.clear();performed.clear();occupied.clear();
        check(command(fresh,COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE,{made.value}).status==lcs::StepResult::continued && sent.size()==3 &&
              sent[2].opcode==0x05DA && sent[2].inputs[0]==768 && sent[2].inputs[5]==10000 && !always_calls && performed.empty(),"606 on foot consumes only the group and flees each member from its own position");
        sent.clear();occupied={256,768};
        check(command(fresh,COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE,{made.value}).status==lcs::StepResult::continued && sent.size()==1 && sent[0].inputs[0]==512 && performed.size()==2 && clean() &&
              performed[0].tasks[1].opcode==0x05DA && performed[1].tasks[1].inputs[5]==10000,"606 mixes on-foot and sequence members without sharing sequence state");
        sa_finishes_performed_tasks();

        // Partial collective: first member accepted by sequence, second fails after entry => quarantine, no replay.
        occupied={256,512,768};performed.clear();sent.clear();
        lcs::Collectives partial_service;partial_service.available=[&]{return true;};partial_service.live=service.live;partial_service.candidates=service.candidates;
        partial_service.flee_point_till_safe=[&](const lcs::ObjectiveActor& a){return adapter.execute_flee_point_till_safe(a);};
        const auto second_group=command(partial_service,COMMAND_ADD_PEDS_IN_AREA_TO_COLL,{bits(10),bits(20),bits(30)},true);
        fail_op=0x0618;fail_nth=2;seen_op=0;
        check(command(partial_service,COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE,{second_group.value}).status==lcs::StepResult::failed && performed.size()==1 && clean(),"606 failure after the first accepted occupant quarantines the collective and releases the failed sequence");
        fail_op=0;const auto trace_size=trace.size();
        check(command(partial_service,COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE,{second_group.value}).status==lcs::StepResult::failed && performed.size()==1 && trace.size()==trace_size && clean(),"Quarantined collective never replays accepted members or touches the sequence pool");
        // ---- 1136 / 1124 / 1526: vehicle family on the same sequence backend ----
        sa_finishes_performed_tasks();performed.clear();sent.clear();trace.clear();occupied.clear();foreign.clear();fail_op=throw_op=0;pool=Pool{};violations=0;
        const int32_t car_a=0x1000,car_b=0x2000;
        vehicles={car_a,car_b};car_position[car_a]={10.f,20.f,3.f};car_position[car_b]={1.f,2.f,3.f};
        const auto packet_bits=[&](int32_t ped,std::array<float,3> at,int32_t ms){return std::vector<int32_t>({ped,bits(at[0]),bits(at[1]),bits(at[2]),bits(30.f),ms});};
        // 1136: occupied actor leaves its OWN vehicle and flees from it (operand differs), 6 s, 30 m.
        occupied={256};ped_car[256]=car_a;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CAR,{256,car_b}).status==lcs::StepResult::continued && sent.empty() && clean() && performed.size()==1 &&
              performed[0].ped==256 && performed[0].tasks.size()==2 && performed[0].tasks[0].opcode==0x0633 && performed[0].tasks[1].opcode==0x05DA &&
              performed[0].tasks[1].inputs==([&]{auto v=packet_bits(-1,{10.f,20.f,3.f},6000);return v;})(),"1136 from a vehicle: leave first, then finite point flee from the actor's own vehicle (6000 ms, 30 m)");
        sa_finishes_performed_tasks();performed.clear();trace.clear();
        occupied.clear();ped_car.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CAR,{256,car_b}).status==lcs::StepResult::continued && performed.empty() && sent.size()==1 && sent[0].opcode==0x05DA &&
              sent[0].inputs==packet_bits(256,{1.f,2.f,3.f},6000),"1136 on foot flees directly from the operand vehicle without a sequence");
        sent.clear();trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CAR,{256,0x7777}).status==lcs::StepResult::unsupported && sent.empty() && performed.empty() && trace.back()==0x056E,"1136 with a missing vehicle refuses before any task");
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CAR,{49*256+1,car_b}).status==lcs::StepResult::unsupported && sent.empty() && performed.empty(),"1136 with a stale actor refuses");
        occupied={256};ped_car[256]=car_a;fail_op=0x0618;fail_nth=1;seen_op=0;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_FLEE_CAR,{256,car_a}).status==lcs::StepResult::failed && clean() && performed.empty(),"1136 sequence failure after entry needs repair and releases its sequence");
        fail_op=0;occupied.clear();ped_car.clear();performed.clear();sent.clear();trace.clear();
        // 1124: driver first, then passengers by seat with the LCS absolute timer (100, +random 200..400).
        driver_of[car_a]=256;max_passengers=4;seat_ped[car_a*16+0]=512;seat_ped[car_a*16+2]=768;occupied={256,512,768};
        game_timer=100000;random_queue={300,250};
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::continued && performed.empty() && clean() &&
              sent.size()==3 && sent[0].opcode==0x05CD && sent[0].inputs==std::vector<int32_t>({256,car_a}) && sent[1].inputs==std::vector<int32_t>({512,car_a}) && sent[2].inputs==std::vector<int32_t>({768,car_a}),
              "1124 with the clock past every LCS timer exits driver then passengers in seat order, each with the ordinary leave-car task");
        check(random_queue.empty(),"1124 draws one SA random delay per present passenger");
        sent.clear();trace.clear();game_timer=int32_t(0x80000000u);random_queue={300,250};
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::continued &&
              sent.size()==3 && performed.empty() && clean(),"1124 treats the native clock as unsigned after its sign bit becomes set; it does not schedule a bogus long wait");
        sent.clear();trace.clear();game_timer=0;random_queue={300,250};
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::continued && sent.size()==1 && sent[0].inputs[0]==256 && performed.size()==2 && clean() &&
              performed[0].ped==512 && performed[0].tasks[0].opcode==0x05BA && performed[0].tasks[0].inputs==std::vector<int32_t>({-1,100}) && performed[0].tasks[1].opcode==0x05CD && performed[0].tasks[1].inputs==std::vector<int32_t>({-1,car_a}) &&
              performed[1].ped==768 && performed[1].tasks[0].inputs==std::vector<int32_t>({-1,400}),"1124 early in the clock: driver now, passengers wait 100 ms and 100+300 ms before leaving");
        sa_finishes_performed_tasks();performed.clear();sent.clear();game_timer=250;random_queue={300,250};
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::continued && sent.size()==2 && sent[1].inputs[0]==512 && performed.size()==1 && performed[0].ped==768 &&
              performed[0].tasks[0].inputs==std::vector<int32_t>({-1,150}) && clean(),"1124 mixes immediate and delayed passengers using the remaining timer only");
        sa_finishes_performed_tasks();performed.clear();sent.clear();trace.clear();
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{0x7777}).status==lcs::StepResult::unsupported && sent.empty() && trace.back()==0x056E,"1124 with a missing vehicle refuses");
        max_passengers=9;check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::unsupported && sent.empty(),"1124 refuses an impossible passenger capacity");max_passengers=4;
        driver_of.erase(car_a);seat_ped.clear();trace.clear();
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::continued && sent.empty() && performed.empty() && std::find(trace.begin(),trace.end(),uint16_t(0x05CD))==trace.end(),"1124 on an empty vehicle changes nothing");
        driver_of[car_a]=256;seat_ped[car_a*16+0]=512;seat_ped[car_a*16+2]=768;game_timer=100000;random_queue={300,250};fail_at=2;seen=0;sent.clear();
        check(command(adapter,COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR,{car_a}).status==lcs::StepResult::failed && sent.size()==2,"1124 failure after the first accepted exit stops, needs repair and does not replay or continue");
        fail_at=0;sent.clear();performed.clear();random_queue.clear();trace.clear();
        // 1526: heed-threats flag first, then (only when in control and on foot) a perpendicular native dive.
        driver_of.clear();seat_ped.clear();occupied.clear();heed_calls.clear();dive_calls.clear();in_control=true;
        car_speed[car_a]={3.f,0.f,0.f};random_queue={1};
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::continued && heed_calls.size()==1 && heed_calls[0]==std::make_pair(256,true) && dive_calls.size()==1 &&
              dive_calls[0].opcode==0x0673 && dive_calls[0].inputs[0]==256 && entity_float_equal(dive_calls[0].inputs[1],0.f) && entity_float_equal(dive_calls[0].inputs[2],1.f) && dive_calls[0].inputs[3]==1,"1526 dives 90 degrees from the car movement (random side one)");
        random_queue={0};dive_calls.clear();
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::continued && dive_calls.size()==1 && dive_calls[0].inputs[2]==bits(-1.f),"1526 other random side dives the other way");
        car_speed[car_a]={0.f,0.f,0.f};dive_calls.clear();
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::continued && dive_calls.size()==1 &&
              entity_float_equal(dive_calls[0].inputs[1],(1.5f-10.f)/std::hypot(1.5f-10.f,-2.f-20.f)) && entity_float_equal(dive_calls[0].inputs[2],(-2.f-20.f)/std::hypot(1.5f-10.f,-2.f-20.f)),"1526 stationary car: dive directly away from the car");
        in_control=false;dive_calls.clear();heed_calls.clear();
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::continued && heed_calls.size()==1 && dive_calls.empty(),"1526 sets the flag but does nothing else when the ped is not in control");
        in_control=true;occupied={256};heed_calls.clear();
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::continued && heed_calls.size()==1 && dive_calls.empty(),"1526 an occupant of a vehicle sets the flag but cannot dive");
        occupied.clear();heed_calls.clear();
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,0x7777}).status==lcs::StepResult::unsupported && heed_calls.empty(),"1526 with a missing vehicle refuses before touching the ped");
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{49*256+1,car_a}).status==lcs::StepResult::unsupported && heed_calls.empty(),"1526 with a stale actor refuses");
        const auto saved_control=adapter.char_in_control;adapter.char_in_control=nullptr;
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::unsupported && heed_calls.empty(),"1526 without the native in-control query refuses before mutation");
        adapter.char_in_control=saved_control;
        fail_dive=true;car_speed[car_a]={3.f,0.f,0.f};random_queue={1};
        check(command(adapter,COMMAND_SET_CHAR_DIVE_FROM_CAR,{256,car_a}).status==lcs::StepResult::failed && heed_calls.size()==1,"1526 dive failure after the flag was set requires repair");
        fail_dive=false;
        // ---- 409 SET_CHAR_OBJ_GUARD_SPOT: repeating native sequence [walk to the spot, stand still] ----
        sa_finishes_performed_tasks();performed.clear();sent.clear();trace.clear();occupied.clear();foreign.clear();fail_op=throw_op=0;pool=Pool{};violations=0;ground_queries.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(10.f),bits(20.f),bits(30.f)}).status==lcs::StepResult::continued && sent.empty() && clean() && performed.size()==1 &&
              performed[0].ped==256 && performed[0].repeat && performed[0].tasks.size()==2 && performed[0].tasks[0].opcode==0x05D3 &&
              performed[0].tasks[0].inputs==std::vector<int32_t>({-1,bits(10.f),bits(20.f),bits(30.f),4,-2}) &&
              performed[0].tasks[1].opcode==0x05BA && performed[0].tasks[1].inputs==std::vector<int32_t>({-1,1000}),
              "409 holds the exact spot with a repeating walk-then-stand sequence owned by the SA task manager");
        sa_finishes_performed_tasks();performed.clear();trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(10.f),bits(20.f),bits(-100.f)}).status==lcs::StepResult::continued && ground_queries.size()==1 &&
              ground_queries[0]==std::vector<int32_t>({bits(10.f),bits(20.f),bits(1000.f)}) && performed.size()==1 &&
              performed[0].tasks[0].inputs[3]==bits(5.f),"409 resolves the -100 sentinel with the native terrain query");
        sa_finishes_performed_tasks();performed.clear();trace.clear();
        ground_fails=true;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(-100.f)}).status==lcs::StepResult::unsupported && performed.empty() && clean(),"409 with an unavailable terrain query refuses before opening a sequence");
        ground_fails=false;trace.clear();
        for(const auto invalid_ground:{-1000.f,std::numeric_limits<float>::quiet_NaN()}){
            ground_height=invalid_ground;
            check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(-100.f)}).status==lcs::StepResult::unsupported && performed.empty() && clean(),"409 refuses absent or non-finite native terrain before opening a sequence");
        }
        ground_height=5.f;trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(std::numeric_limits<float>::infinity()),bits(2.f),bits(3.f)}).status==lcs::StepResult::unsupported && performed.empty() && trace.empty(),"409 refuses a non-finite spot before any native call");
        occupied={256};trace.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(3.f)}).status==lcs::StepResult::continued && performed.size()==1 && performed[0].repeat && performed[0].tasks[0].opcode==0x05D3 && performed[0].tasks[0].inputs.back()==-2 && clean(),"409 delegates vehicle exit to the real GoToPointAndStandStill task before guarding");
        sa_finishes_performed_tasks();performed.clear();
        occupied.clear();
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{49*256+1,bits(1.f),bits(2.f),bits(3.f)}).status==lcs::StepResult::unsupported && performed.empty(),"409 with a stale actor refuses");
        sent.clear();check(command(adapter,COMMAND_SET_CHAR_RUNNING,{256,1}).status==lcs::StepResult::continued,"fixture: script marks the ped as running");
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(3.f)}).status==lcs::StepResult::continued && performed.size()==1 && performed[0].tasks[0].inputs[4]==6,"409 keeps a script-selected run speed on the way to the spot");
        sa_finishes_performed_tasks();performed.clear();
        fail_op=0x0643;fail_nth=1;seen_op=0;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(3.f)}).status==lcs::StepResult::failed && performed.empty() && clean(),"409 failing to set the repeat flag needs repair and releases the half-built sequence");
        fail_op=0;fail_nth=0;
        fail_op=0x0618;fail_nth=1;seen_op=0;
        check(command(adapter,COMMAND_SET_CHAR_OBJ_GUARD_SPOT,{256,bits(1.f),bits(2.f),bits(3.f)}).status==lcs::StepResult::failed && performed.empty() && clean(),"409 failing to give the sequence to the ped needs repair and does not replay");
        fail_op=0;
    }
    std::cout<<"PASS: collective selection, capacity, filters, ordered duplicate consumers, stale generations, clear/reuse and partial failure quarantine\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
