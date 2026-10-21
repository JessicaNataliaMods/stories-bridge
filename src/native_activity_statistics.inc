    struct ActivitySpec {uint16_t original;ActivityStatistic statistic;uint8_t inputs;};
    static constexpr ActivitySpec activity_specs[]={
        {COMMAND_INDUSTRIAL_PASSED,ActivityStatistic::industrial_passed,1},
        {COMMAND_COMMERCIAL_PASSED,ActivityStatistic::commercial_passed,1},
        {COMMAND_SUBURBAN_PASSED,ActivityStatistic::suburban_passed,1},
        {COMMAND_REGISTER_CAR_SOLD,ActivityStatistic::cars_sold,0},
        {COMMAND_ADD_MONEY_MADE_WITH_CAR_SALES,ActivityStatistic::car_sales_money,1},
        {COMMAND_REGISTER_BIKE_SOLD,ActivityStatistic::bikes_sold,0},
        {COMMAND_ADD_MONEY_MADE_WITH_BIKE_SALES,ActivityStatistic::bike_sales_money,1},
        {COMMAND_ADD_MONEY_MADE_COLLECTING_TRASH,ActivityStatistic::trash_money,1},
        {COMMAND_REGISTER_HITMAN_KILLED,ActivityStatistic::hitmen_killed,0},
        {COMMAND_REGISTER_GUARDIAN_ANGEL_MISSION_PASSED,ActivityStatistic::guardian_missions,0},
        {COMMAND_REGISTER_HIGHEST_GUARDIAN_ANGEL_JUSTICE_DISHED,ActivityStatistic::guardian_justice,1},
        {COMMAND_REGISTER_BEST_BANDIT_LAP_TIME,ActivityStatistic::bandit_lap,2},
        {COMMAND_REGISTER_BEST_BANDIT_POSITION,ActivityStatistic::bandit_position,2},
        {COMMAND_REGISTER_MOST_TIME_LEFT_TRAIN_RACE,ActivityStatistic::train_time_left,1},
        {COMMAND_REGISTER_STREET_RACE_FASTEST_TIME,ActivityStatistic::street_time,2},
        {COMMAND_REGISTER_STREET_RACE_FASTEST_LAP,ActivityStatistic::street_lap,2},
        {COMMAND_REGISTER_STREET_RACE_BEST_POSITION,ActivityStatistic::street_position,2},
        {COMMAND_REGISTER_MAX_SECONDS_ON_CARNAGE_LEFT,ActivityStatistic::carnage_seconds,1},
        {COMMAND_REGISTER_MAX_KILLS_ON_RC_TRIAD,ActivityStatistic::rc_triad_kills,1},
        {COMMAND_REGISTER_HIGHEST_LEVEL_SLASH_TV,ActivityStatistic::slash_level,1},
        {COMMAND_ADD_MONEY_MADE_WITH_SLASH_TV,ActivityStatistic::slash_money,1},
        {COMMAND_ADD_TOTAL_KILLS_ON_SLASH_TV,ActivityStatistic::slash_kills,1},
        {COMMAND_REGISTER_DIRT_BIKE_FASTEST_LAP,ActivityStatistic::dirt_lap,2},
        {COMMAND_REGISTER_DIRT_BIKE_FASTEST_TIME,ActivityStatistic::dirt_time,2},
        {COMMAND_REGISTER_DIRT_BIKE_AIR_TIME,ActivityStatistic::dirt_air_time,1},
        {COMMAND_REGISTER_BEST_TIME_GOGO_FAGGIO,ActivityStatistic::gogo_time,1},
        {COMMAND_REGISTER_GUARDIAN_ANGEL_LEVEL,ActivityStatistic::guardian_levels,3},
        {COMMAND_ADD_UNLOCKED_COSTUME,ActivityStatistic::unlocked_costume,1},
        {COMMAND_REGISTER_FACE_PLANT_DISTANCE,ActivityStatistic::face_plant_distance,1},
        {COMMAND_REGISTER_SCRAPYARD_CHALLENGE_SCORE,ActivityStatistic::scrapyard_score,1},
        {COMMAND_REGISTER_9MM_MAYHEM_SCORE,ActivityStatistic::mayhem_score,1},
        {COMMAND_REGISTER_SCOOTER_SHOOTER_SCORE,ActivityStatistic::scooter_score,1},
        {COMMAND_REGISTER_WICHITA_WIPEOUT_SCORE,ActivityStatistic::wichita_score,1},
        {COMMAND_ADD_PIZZAS_DELIVERED,ActivityStatistic::pizzas_delivered,1},
        {COMMAND_ADD_NOODLES_DELIVERED,ActivityStatistic::noodles_delivered,1},
        {COMMAND_ADD_MONEY_MADE_FROM_TOURIST,ActivityStatistic::tourist_money,1},
        {COMMAND_ADD_TOURISTS_TAKEN_TO_SPOTS,ActivityStatistic::tourist_spots,1}
    };
    const auto activity_spec=std::find_if(std::begin(activity_specs),std::end(activity_specs),
        [opcode](const ActivitySpec& item){return item.original==opcode;});
    if(activity_spec!=std::end(activity_specs)) {
        std::array<int32_t,3> parameters{};
        for(size_t i=0;i<activity_spec->inputs;++i)parameters[i]=vm.parameter(thread);
        if(!world_ready){thread.diagnostic="Original activity statistics require the initialized campaign";return StepResult::unsupported;}
        if(!update_activity_statistic(activity_statistics_,activity_spec->statistic,parameters)) {
            thread.diagnostic="Original activity statistic index is outside its PS2 array";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
