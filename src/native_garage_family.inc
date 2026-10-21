    struct GarageSpec {uint16_t original;GarageCommand action;uint8_t inputs;bool predicate,named;};
    static constexpr GarageSpec garage_specs[]={
        {COMMAND_IS_CAR_IN_MISSION_GARAGE,GarageCommand::car_delivered,1,true,true},
        {COMMAND_HAS_RESPRAY_HAPPENED,GarageCommand::respray_happened,1,true,true},
        {COMMAND_DOES_GARAGE_CONTAIN_CAR,GarageCommand::contains_car,2,true,true},
        {COMMAND_SET_FREE_BOMBS,GarageCommand::free_bombs,1,false,false},
        {COMMAND_SET_FREE_RESPRAYS,GarageCommand::free_resprays,1,false,false},
        {COMMAND_IS_CAR_ARMED_WITH_BOMB,GarageCommand::car_bomb_type,2,true,false},
        {COMMAND_ARM_CAR_WITH_BOMB,GarageCommand::arm_car_bomb,2,false,false},
        {COMMAND_DISARM_CAR_BOMB,GarageCommand::disarm_car_bomb,1,false,false}
    };
    const auto garage_spec=std::find_if(std::begin(garage_specs),std::end(garage_specs),
        [opcode](const GarageSpec& spec){return spec.original==opcode;});
    if(garage_spec!=std::end(garage_specs)) {
        NativeCall call;call.source_garage=garage_spec->action;call.predicate=garage_spec->predicate;
        for(size_t n=0;n<garage_spec->inputs;++n)call.inputs.push_back(vm.parameter(thread));
        if(!world_ready || !execute){thread.diagnostic="Original garage service requires mounted native world";return StepResult::unsupported;}
        if(garage_spec->named) {
            const auto named=garages_.find(call.inputs[0]);
            if(named==garages_.end()){thread.diagnostic="Original garage service has no registered native name";return StepResult::unsupported;}
            call.text=named->second;call.inputs.erase(call.inputs.begin());
        }
        if((garage_spec->action==GarageCommand::car_bomb_type || garage_spec->action==GarageCommand::arm_car_bomb) &&
           (call.inputs[1]<0 || call.inputs[1]>5)) {
            thread.diagnostic="Unknown original vehicle bomb type";return StepResult::unsupported;
        }
        const auto result=execute(call);
        if(!result.executed){thread.diagnostic=result.diagnostic.empty()?"Original garage service refused before entry":result.diagnostic;return StepResult::unsupported;}
        if(call.predicate)vm.compare(thread,result.condition);
        return StepResult::continued;
    }
