#include "lcs/objective_receipts.hpp"
#include "lcs/objective_walk.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    ObjectiveReceipts book;ObjectiveActor first{27,0x1000},second{0x11C,0x2000};
    check(!book.latch(first).has_value(),"An unobserved actor cannot receive a fabricated objective result");
    check(!book.accepted(first,1,false),"Non-reset operation cannot create an unknown latch");
    for(ObjectiveActor invalid: {ObjectiveActor{-1,0x1000},ObjectiveActor{0x80,0x1000},ObjectiveActor{27,0}})
        check(!book.accepted(invalid,1,true) && !book.latch(invalid).has_value(),"Invalid identity refused");
    check(!book.accepted(first,0,true),"No objective receipt for an absent source opcode");
    auto one=book.accepted(first,140,true);check(one && book.latch(first)==false,"Accepted original reset initializes false");
    check(book.observed_completion(*one) && book.latch(first)==true && book.latch(first)==true,"Reviewed completion remains latched across repeated queries");
    check(book.stop_monitoring(*one) && book.latch(first)==true,"Task removal/cancellation observation cannot erase an already recorded source latch");
    check(!book.observed_completion(*one),"Stopped callback cannot publish completion");
    auto two=book.accepted(first,140,true);check(two && book.latch(first)==false,"Repeated identical objective command still performs its reviewed source reset");
    check(two->revision!=one->revision && !book.observed_completion(*one) && book.latch(first)==false,"Completion of replaced task cannot complete new objective");
    auto forged=*two;forged.original_opcode=141;check(!book.observed_completion(forged),"Receipt cannot cross source objective opcode");
    forged=*two;forged.actor.native_ped=0x3000;check(!book.observed_completion(forged),"Foreign native actor cannot claim receipt");
    check(book.stop_monitoring(*two) && !book.observed_completion(*two) && book.latch(first)==false,"Uncompleted stopped objective remains false rather than task-absent success");
    auto other=book.accepted(second,141,true);check(other && book.observed_completion(*other) && book.latch(second)==true && book.latch(first)==false,"Actors retain independent latches");
    auto preserved=book.accepted(second,142,false);check(preserved && book.latch(second)==true,"Reviewed non-reset operation retains existing latch");
    ObjectiveActor recycled{28,0x1000};check(!book.latch(recycled).has_value(),"Recycled slot generation cannot inherit previous completion");
    const auto live_old=book.accepted(first,140,true);
    auto generation=book.accepted(recycled,140,true);check(generation && book.latch(recycled)==false && !book.latch(first).has_value() &&
        !book.observed_completion(*live_old),"Live new generation invalidates the old receipt before a delayed cleanup callback");
    book.retired(second);check(!book.latch(second).has_value() && !book.observed_completion(*preserved),"Retired actor cannot answer or complete an old receipt");
    ObjectiveActor reused_pointer{27,0x4000};auto new_identity=book.accepted(reused_pointer,140,true);
    book.retired(first);check(new_identity && book.latch(reused_pointer)==false,"Old cleanup cannot remove new native identity with same script handle");
    book.clear();auto after_reset=book.accepted(reused_pointer,140,true);
    check(after_reset && after_reset->revision!=new_identity->revision && !book.observed_completion(*new_identity),"World reset cannot alias queued receipt to newly accepted objective");
    check(source_walk_goal_complete({0,0},{.5f,0},true,true,true)==true,"Retail half-meter XY boundary is inclusive");
    check(source_walk_goal_complete({0,0},{.5f,.125f},true,true,true)==false,"Destination outside original radius cannot complete walking objective");
    check(source_walk_goal_complete({0,0},{0,0},true,false,true)==false,"Uncontrollable ped at destination cannot publish original walking completion");
    check(source_walk_goal_complete({0,0},{0,0},true,true,false)==false,"A ped inside a car does not complete on-foot arrival by proximity");
    check(!source_walk_goal_complete({0,0},{0,0},false,true,true).has_value(),"Foreign/replaced task has no completion evidence for owned source objective");
    check(!source_walk_goal_complete({0,0},{std::numeric_limits<float>::quiet_NaN(),0},true,true,true).has_value(),"Non-finite world query cannot generate mission result");
    const auto arrival=source_walk_goal_complete({0,0},{.25f,.25f},true,true,true);
    check(arrival && *arrival && book.observed_completion(*after_reset) && book.latch(reused_pointer)==true,"Reviewed arrival condition can latch only its current accepted receipt");
    std::cout<<"PASS: objective receipt ownership, accepted-reset scope, non-consuming latch, replacement, retirement and stopped callbacks; native consumers not yet integrated\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
