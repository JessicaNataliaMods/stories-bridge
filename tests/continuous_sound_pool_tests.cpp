#include "lcs/continuous_sound.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
int main(){try {
    lcs::ContinuousSoundObjects pool;
    using Token=lcs::ContinuousSoundObjects::Token;
    std::vector<Token> tokens;
    for(int i=0;i<192;++i) {
        const auto value=pool.allocate(0x10002,{float(i),2,3});
        check(value && value->token.reference==(i<<8|1),"Retail initial slot order and generation1");
        check(value->object.sound==2 && value->object.audio_entity==-5 &&
            value->object.position[0]==float(i),"Original uint16 ID, position and uncreated audio entity retained");
        tokens.push_back(value->token);
    }
    check(!pool.allocate(0,{0,0,0}),"Pool exhaustion cannot overwrite a live object");
    check(!pool.find(-1) && !pool.find(192<<8|1) && !pool.find(0x7FFFFFFF),"Hostile handles checked before indexing");
    check(!pool.find(0) && !pool.find(0x81),"Mismatched/free generation cannot resolve");
    check(pool.attach_entity(tokens[5],73) && pool.find(tokens[5].reference)->object.audio_entity==73,
        "Actual entity receipt stored independently of script handle");
    check(pool.erase_released(tokens[5]) && !pool.find(tokens[5].reference),"Released reference retires immediately");
    const auto next=pool.allocate(51,{9,8,7});
    check(next && next->token.reference==(5<<8|2),"Full scan finds released slot and increments generation");
    check(!pool.attach_entity(tokens[5],99) && !pool.erase_released(tokens[5]),"Old epoch cannot mutate replacement");
    auto current=next->token;
    for(int cycle=0;cycle<128;++cycle) {
        check(pool.erase_released(current),"Current owner can retire");
        const auto replacement=pool.allocate(2,{1,2,3});check(bool(replacement),"Released slot can be reused");
        current=replacement->token;
    }
    check(current.reference==next->token.reference && current.epoch!=next->token.epoch,
        "Retail seven-bit generation wraps; internal owner epoch remains distinct");
    check(!pool.erase_released(next->token) && !pool.attach_entity(next->token,88),
        "Ancient backend owner cannot act after script-reference wrap");
    check(pool.find(current.reference).has_value(),"Script reference retains original wrapped representation");
    lcs::ContinuousSoundObjects order;
    const auto a=order.allocate(0,{0,0,0});const auto b=order.allocate(0,{0,0,0});
    check(a && b && order.erase_released(a->token),"Early release fixture");
    check(order.allocate(0,{0,0,0})->token.reference==(2<<8|1),
        "Original delete rewinds cursor to freed slot, next allocation probes after it");
    lcs::ContinuousSoundObjects exhausted;
    std::vector<Token> full;
    for(int i=0;i<192;++i)full.push_back(exhausted.allocate(0,{0,0,0})->token);
    check(!exhausted.allocate(0,{0,0,0}),"Exhausted scan refuses allocation");
    check(exhausted.erase_released(full[100]) && exhausted.erase_released(full[50]),"Release two slots after exhausted scan");
    check(exhausted.allocate(0,{0,0,0})->token.reference==(50<<8|2),
        "Exhausted retail scan leaves cursor0, preserving subsequent free-slot order");
    std::cout<<"PASS: retail audio object pool capacity, cursor, generation wrap and internal ownership; no runtime/device\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
