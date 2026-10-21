    struct StatisticSpec {uint16_t original;SourceStatistic stat;bool operand;};
    static constexpr StatisticSpec statistic_specs[]={
        {COMMAND_REGISTER_MONEY_MADE_TAXI,SourceStatistic::taxi_money,true},
        {COMMAND_REGISTER_PASSENGER_DROPPED_OFF_TAXI,SourceStatistic::taxi_passengers,false},
        {COMMAND_REGISTER_LIFE_SAVED,SourceStatistic::ambulance_lives,false},
        {COMMAND_REGISTER_CRIMINAL_CAUGHT,SourceStatistic::vigilante_criminals,false},
        {COMMAND_REGISTER_FIRE_EXTINGUISHED,SourceStatistic::extinguished_fires,false},
        {COMMAND_REGISTER_VIGILANTE_LEVEL,SourceStatistic::vigilante_level,true},
        {COMMAND_REGISTER_AMBULANCE_LEVEL,SourceStatistic::ambulance_level,true},
        {COMMAND_REGISTER_FIRE_LEVEL,SourceStatistic::fire_level,true}
    };
    const auto statistic_spec=std::find_if(std::begin(statistic_specs),std::end(statistic_specs),
        [opcode](const StatisticSpec& item){return item.original==opcode;});
    if(statistic_spec!=std::end(statistic_specs)) {
        NativeCall call;call.source_statistic=statistic_spec->stat;
        call.inputs={statistic_spec->operand?vm.parameter(thread):1};
        if(!world_ready || !execute){thread.diagnostic="Native original statistic backend is unavailable";return StepResult::unsupported;}
        const auto result=execute(call);
        if(!result.executed){thread.diagnostic=result.diagnostic.empty()?"Original statistic update rejected before entry":result.diagnostic;return StepResult::unsupported;}
        return StepResult::continued;
    }
