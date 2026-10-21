    struct TrafficSpec {uint16_t original;TrafficCommand action;uint8_t inputs;};
    static constexpr TrafficSpec traffic_specs[]={
        {COMMAND_ENABLE_EMERGENCY_VEHICLES,TrafficCommand::emergency_vehicles,1},
        {COMMAND_SET_GENERATE_CARS_AROUND_CAMERA,TrafficCommand::cars_around_camera,1},
        {COMMAND_DONT_REMOVE_CAR,TrafficCommand::release_vehicle_cleanup,1},
        {COMMAND_POP_CAR_BOOT_USING_PHYSICS,TrafficCommand::pop_boot_physics,1},
        {COMMAND_SET_TANK_DETONATE_CARS,TrafficCommand::tank_detonates_cars,2},
        {COMMAND_SET_DRUNK_INPUT_DELAY,TrafficCommand::drunk_input_delay,2}
    };
    const auto traffic_spec=std::find_if(std::begin(traffic_specs),std::end(traffic_specs),
        [opcode](const TrafficSpec& spec){return spec.original==opcode;});
    if(traffic_spec!=std::end(traffic_specs)) {
        NativeCall call;call.source_traffic=traffic_spec->action;
        for(size_t n=0;n<traffic_spec->inputs;++n)call.inputs.push_back(vm.parameter(thread));
        if(!world_ready || !execute){thread.diagnostic="Original traffic/control service requires mounted native world";return StepResult::unsupported;}
        if(traffic_spec->action==TrafficCommand::drunk_input_delay &&
           (call.inputs[0]<0 || call.inputs[0]>1 || call.inputs[1]<0 || call.inputs[1]>=10)) {
            thread.diagnostic="Original drunk steering selector/delay exceeds the native buffer";return StepResult::unsupported;
        }
        const auto result=execute(call);
        if(!result.executed){thread.diagnostic=result.diagnostic.empty()?"Original traffic/control service refused before entry":result.diagnostic;return StepResult::unsupported;}
        return StepResult::continued;
    }
