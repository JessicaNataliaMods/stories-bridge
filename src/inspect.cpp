#include "lcs/vm.hpp"
#include "lcs/runtime.hpp"
#include <iomanip>
#include <iostream>
#include <map>

static bool audit_missions(const lcs::ScmImage& image) {
    std::map<uint16_t,size_t> adapters;
    std::array<size_t,5> statuses{};
    std::cout<<"probe_mode=isolated_mission_entry\nstartup_globals_initialized=false\ngame_world_created=false\n";
    for (size_t i=0;i<image.mission_offsets.size();++i) {
        lcs::Runtime runtime(image);
        const auto id=runtime.load_mission(i);
        runtime.tick(0,0);
        const auto& script=runtime.script(id);
        ++statuses.at(size_t(script.result));
        if (script.result==lcs::StepResult::unsupported) ++adapters[script.thread.last_opcode];
        std::cout<<"mission_probe="<<i<<" bytes="<<runtime.vm().mission_size()<<" name="<<script.thread.name
                 <<" status="<<int(script.result)<<" ip="<<script.thread.last_ip<<" opcode=0x"
                 <<std::hex<<std::setw(4)<<std::setfill('0')<<script.thread.last_opcode<<std::dec
                 <<" command="<<lcs::opcode_name(script.thread.last_opcode)<<" diagnostic="<<script.thread.diagnostic<<'\n';
    }
    for (const auto& adapter:adapters)
        std::cout<<"missing_entry_command="<<lcs::opcode_name(adapter.first)<<" mission_blocks="<<adapter.second<<'\n';
    std::cout<<"mission_probes="<<image.mission_offsets.size()<<" yielded="<<statuses[size_t(lcs::StepResult::yielded)]
             <<" terminated="<<statuses[size_t(lcs::StepResult::terminated)]
             <<" unsupported="<<statuses[size_t(lcs::StepResult::unsupported)]
             <<" failed="<<statuses[size_t(lcs::StepResult::failed)]<<'\n';
    std::cout<<"playability_verified=false\n";
    return statuses[size_t(lcs::StepResult::failed)]==0;
}

int main(int argc,char** argv) {
    if (argc<2 || argc>3) { std::cerr<<"Usage: lcs-inspect MAIN.SCM [--verify|--missions|--audit-missions]\n"; return 2; }
    const std::string option=argc==3 ? argv[2] : "";
    if (!option.empty() && option!="--verify" && option!="--missions" && option!="--audit-missions") {
        std::cerr<<"Unknown option: "<<option<<'\n'; return 2;
    }
    try {
        const auto image=lcs::ScmImage::load(argv[1]);
        std::cout<<"format=LCS_PS2\nmain_bytes="<<image.main_size<<"\nlargest_mission_bytes="<<image.largest_mission
          <<"\nglobal_space_bytes="<<image.variable_space<<"\nentry_point="<<image.entry_point
          <<"\nsaved_variables="<<image.saved_variables.size()<<"\nscript_objects="<<image.objects.size()
          <<"\nmission_scripts="<<image.mission_offsets.size()<<"\nexclusive_missions="<<image.exclusive_missions<<'\n';
        if (option=="--missions") {
            for (size_t i=0;i<image.mission_offsets.size();++i) {
                const auto code=image.mission(i);
                std::cout<<"mission="<<i<<" offset="<<image.mission_offsets[i]<<" bytes="<<code.size()<<'\n';
            }
        }
        lcs::Vm vm(image); lcs::Thread thread;
        size_t commands=0;
        lcs::StepResult result=lcs::StepResult::continued;
        for (; commands<10000; ++commands) {
            result=vm.step(thread,0);
            if (result!=lcs::StepResult::continued) { ++commands; break; }
        }
        std::cout<<"startup_commands_attempted="<<commands<<"\nstartup_ip="<<thread.last_ip
          <<"\nstartup_opcode=0x"<<std::hex<<std::setw(4)<<std::setfill('0')<<thread.last_opcode<<std::dec
          <<"\nstartup_command="<<lcs::opcode_name(thread.last_opcode)<<"\nstartup_status="<<int(result)
          <<"\nstartup_diagnostic="<<thread.diagnostic<<'\n';
        if (result==lcs::StepResult::failed) return 1;
        if (commands==10000) throw std::runtime_error("Startup exceeded instruction budget");
        for (size_t i=0;i<image.mission_offsets.size();++i) (void)image.mission(i);
        if (option=="--audit-missions" && !audit_missions(image)) return 1;
        return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
