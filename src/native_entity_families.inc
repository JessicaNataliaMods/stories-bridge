    struct EntitySpec {uint16_t original;EntityCommand action;uint8_t inputs,outputs;bool predicate=false;};
    static constexpr EntitySpec entity_specs[]={
        {COMMAND_CLEAR_AREA_OF_OBJECTS,EntityCommand::clear_area_objects,6,0},
        {COMMAND_BLOW_UP_RC_BUGGY,EntityCommand::blow_up_rc_buggy,0,0},
        {COMMAND_CANCEL_REMOTE_MODE,EntityCommand::cancel_remote_mode,0,0},
        {COMMAND_GIVE_REMOTE_CONTROLLED_CAR_TO_PLAYER,EntityCommand::create_remote_car,5,1},
        {COMMAND_OVERRIDE_CAR_REMOTE_CONTROL,EntityCommand::override_remote_car,2,0},
        {COMMAND_SET_CAR_MOVE_SPEED,EntityCommand::car_move_speed,4,0},
        {COMMAND_SET_CAR_TURN_SPEED,EntityCommand::car_turn_speed,4,0},
        {COMMAND_SET_OBJECT_TURN_SPEED,EntityCommand::object_turn_speed,4,0},
        {COMMAND_GET_CAR_ORIENTATION,EntityCommand::get_car_orientation,1,3},
        {COMMAND_SET_CAR_ORIENTATION,EntityCommand::set_car_orientation,4,0},
        {COMMAND_GET_OBJECT_ORIENTATION,EntityCommand::get_object_orientation,1,3},
        {COMMAND_SET_OBJECT_ORIENTATION,EntityCommand::set_object_orientation,4,0},
        {COMMAND_GIVE_CAR_ALARM,EntityCommand::car_alarm,1,0},
        {COMMAND_SET_OBJECT_RENDERED_DAMAGED,EntityCommand::object_render_damaged,1,0},
        {COMMAND_SET_GET_OUT_OF_JAIL_FREE,EntityCommand::get_out_of_jail_free,2,0},
        {COMMAND_GET_RANDOM_CAR_IN_AREA_NO_CHECKS_NO_SAVE,EntityCommand::random_car_unchecked,4,1},
        {COMMAND_GET_RANDOM_CAR_IN_AREA_NO_SAVE,EntityCommand::random_car_no_save,5,1},
        {COMMAND_MAKE_PLAYER_FIRE_PROOF,EntityCommand::player_fireproof,2,0},
        {COMMAND_INCREASE_PLAYER_MAX_HEALTH,EntityCommand::player_max_health,2,0},
        {COMMAND_INCREASE_PLAYER_MAX_ARMOUR,EntityCommand::player_max_armour,2,0},
        {COMMAND_DIM_BLIP,EntityCommand::radar_brightness,2,0},
        {COMMAND_IS_BOAT,EntityCommand::car_is_boat,1,0,true},
        {COMMAND_IS_CAR_BIKE,EntityCommand::car_is_bike,1,0,true},
        {COMMAND_IS_CAR_DRIVER_BEING_JACKED,EntityCommand::car_being_jacked,1,0,true},
        {COMMAND_IS_BOAT_IN_WATER,EntityCommand::boat_in_water,1,0,true},
        {COMMAND_SET_HELI_ROTOR_BLADES_FULLSPEED,EntityCommand::heli_rotor_full_speed,1,0},
        {COMMAND_GET_PICKUP_VALUE,EntityCommand::get_pickup_value,1,1},
        {COMMAND_SET_PICKUP_VALUE,EntityCommand::set_pickup_value,2,0},
        {COMMAND_IS_CHAR_DROWNING_IN_WATER,EntityCommand::char_drowning,1,0,true},
        {COMMAND_IS_CHAR_LEAVING_VEHICLE_TO_DIE,EntityCommand::char_leaving_vehicle_to_die,1,0,true},
        {COMMAND_IS_PLAYER_USING_FIRST_PERSON_WEAPON_CAMERA,EntityCommand::first_person_weapon_camera,1,0,true},
        {COMMAND_SET_PLAYER_CURRENT_WEAPON_AMMO_IN_CLIP,EntityCommand::player_clip_ammo,1,0},
        {COMMAND_REMOVE_CAR_BOOT,EntityCommand::remove_car_boot,1,0},
        {COMMAND_HAS_PHOTOGRAPH_BEEN_TAKEN,EntityCommand::photograph_taken,0,0,true},
        {COMMAND_ARE_CREDITS_FINISHED,EntityCommand::credits_finished,0,0,true},
        {COMMAND_ACTIVATE_SAVE_MENU,EntityCommand::activate_save_menu,0,0},
        {COMMAND_HAS_SAVE_GAME_FINISHED,EntityCommand::save_game_finished,0,0,true},
        {COMMAND_ENABLE_PLAYER_CONTROL_CAMERA,EntityCommand::enable_control_camera,0,0},
        {COMMAND_SET_ANIM_GROUP_FOR_CHAR,EntityCommand::char_locomotion_group,2,0},
        {COMMAND_RESET_HAVOC_CAUSED_BY_PLAYER,EntityCommand::reset_player_havoc,1,0},
        {COMMAND_GET_HAVOC_CAUSED_BY_PLAYER,EntityCommand::get_player_havoc,1,1},
        {COMMAND_ATTACH_OBJECT_TO_CAR,EntityCommand::attach_object_car,5,0},
        {COMMAND_DETACH_OBJECT_FROM_CAR,EntityCommand::detach_object_car,1,0},
        {COMMAND_HELI_GOTO_COORDS,EntityCommand::heli_goto,5,0},
        {COMMAND_SET_CAR_RAM_CAR,EntityCommand::car_ram_car,2,0},
        {COMMAND_ATTACH_CHAR_TO_OBJECT,EntityCommand::char_attach_object,8,0},
        {COMMAND_STORE_CAR_COLLIDED_WITH_NO_SAVE,EntityCommand::car_collision,1,1},
        {COMMAND_WRECK_CAR,EntityCommand::car_wreck,1,0},
        {COMMAND_IS_CHAR_WANDER_PATH_CLEAR,EntityCommand::char_wander_path,5,0,true},
        {COMMAND_IS_CHAR_IN_CHARS_GROUP,EntityCommand::char_in_chars_group,2,0,true},
        {COMMAND_RESTART_CRITICAL_MISSION,EntityCommand::restart_critical_mission,4,0}
    };
    const auto entity_spec=std::find_if(std::begin(entity_specs),std::end(entity_specs),
        [opcode](const EntitySpec& item){return item.original==opcode;});
    if(entity_spec!=std::end(entity_specs)) {
        NativeCall call;call.source_entity_command=entity_spec->action;call.outputs=entity_spec->outputs;call.predicate=entity_spec->predicate;
        for(size_t i=0;i<entity_spec->inputs;++i)call.inputs.push_back(vm.parameter(thread));
        auto destinations=thread;for(size_t i=0;i<call.outputs;++i)vm.variable(destinations);
        if(!world_ready || !execute){thread.diagnostic="Native source entity family backend is unavailable";return StepResult::unsupported;}
        if(entity_spec->action==EntityCommand::random_car_no_save && call.inputs[4]>=0) {
            const auto target=binding(call.inputs[4]);
            if(!target || target->kind!=ModelKind::vehicle){thread.diagnostic="Original random vehicle filter has no native vehicle binding";return StepResult::unsupported;}
            call.inputs[4]=target->id;
        }
        if(entity_spec->action==EntityCommand::restart_critical_mission &&
           !std::all_of(call.inputs.begin(),call.inputs.end(),[](int32_t value){return std::isfinite(entity_float(value));})) {
            thread.diagnostic="Non-finite critical mission restart position/heading";return StepResult::unsupported;
        }
        if(entity_spec->inputs==4 && entity_spec->action!=EntityCommand::random_car_unchecked && entity_spec->action!=EntityCommand::restart_critical_mission &&
           !source_vector(call.inputs.data()+1)){thread.diagnostic="Non-finite source entity vector";return StepResult::unsupported;}
        const auto action=entity_spec->action;
        if(action==EntityCommand::override_remote_car)call.mission_context=mission_script && mission_script(thread);
        if(action==EntityCommand::create_remote_car) {
            const auto rc=binding(169); // retail 110E18: fixed MI_RCBANDIT
            if(!rc || rc->kind!=ModelKind::vehicle || !source_vector(call.inputs.data()+1) ||
               !std::isfinite(entity_float(call.inputs[4]))) {
                thread.diagnostic="Original RC model binding or coordinates unavailable";return StepResult::unsupported;
            }
            // Retail ignores operand zero, including when a different player
            // index is supplied. The actual focused player's RC slot is used.
            call.inputs.push_back(rc->id);
        }
        if(action==EntityCommand::clear_area_objects && !source_clear_object_box(call.inputs.data())) {
            thread.diagnostic="Non-finite original object-clear bounds";return StepResult::unsupported;
        }
        if(action==EntityCommand::car_wreck) {
            const auto rc=binding(169); // original MI_RCBANDIT exception
            if(!rc || rc->kind!=ModelKind::vehicle){thread.diagnostic="Original RC Bandit identity has no native vehicle binding";return StepResult::unsupported;}
            call.inputs.push_back(rc->id); // internal identity, no extra SCM operand
        }
        if(((action==EntityCommand::attach_object_car || action==EntityCommand::char_attach_object) && !source_vector(call.inputs.data()+2)) ||
           (action==EntityCommand::char_attach_object && !std::isfinite(entity_float(call.inputs[6]))) ||
           (action==EntityCommand::heli_goto && !source_vector(call.inputs.data()+1))) {
            thread.diagnostic="Non-finite original attachment/flight coordinates";return StepResult::unsupported;
        }
        int32_t attached_weapon=-1;
        if(action==EntityCommand::char_attach_object) {
            attached_weapon=call.inputs[7];
            if(attached_weapon<0 || size_t(attached_weapon)>=std::size(weapon_counterparts) || weapon_counterparts[attached_weapon]<0) {
                thread.diagnostic="Original attached weapon has no native counterpart";return StepResult::unsupported;
            }
            call.inputs[7]=weapon_counterparts[attached_weapon];
        }
        const bool player=action==EntityCommand::get_out_of_jail_free || action==EntityCommand::player_fireproof ||
            action==EntityCommand::player_max_health || action==EntityCommand::player_max_armour ||
            action==EntityCommand::reset_player_havoc || action==EntityCommand::get_player_havoc;
        if(player && (call.inputs[0]<0 || call.inputs[0]>1)) {
            thread.diagnostic="Source player operation requires player zero or one";return StepResult::unsupported;
        }
        const auto result=execute(call);
        if(!result.executed){
            if(action==EntityCommand::restart_critical_mission && result.native_entered)
                throw std::runtime_error("Critical mission restart failed after native entry; replay refused: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native source entity family rejected before entry":result.diagnostic;return StepResult::unsupported;
        }
        if(action==EntityCommand::char_locomotion_group && clear_char_move_anim_override) {
            const auto cleared=clear_char_move_anim_override(call.inputs[0]);
            if(!cleared.executed)throw std::runtime_error("Animation group already changed but override clear failed; replay rejected: "+cleared.diagnostic);
        }
        if(equivalent_entities && attached_weapon>=0 && result.condition)equivalent_weapons_[call.inputs[0]][call.inputs[7]]=attached_weapon;
        for(size_t i=0;i<call.outputs;++i)vm.store(thread,result.values[i]);
        if(call.predicate)vm.compare(thread,result.condition);
        return StepResult::continued;
    }
    if(opcode==COMMAND_GET_DEBUG_CAMERA_POINT_AT) {
        auto destinations=thread;for(size_t i=0;i<3;++i)vm.variable(destinations);
        if(!world_ready || !execute){thread.diagnostic="Native debug camera point query is unavailable";return StepResult::unsupported;}
        const auto result=execute({0x0463,{}, {},0,3});
        if(!result.executed){thread.diagnostic=result.diagnostic;return StepResult::unsupported;}
        if(!source_vector(result.values.data())){thread.diagnostic="Non-finite native debug camera point";return StepResult::unsupported;}
        for(size_t i=0;i<3;++i)vm.store(thread,result.values[i]);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_PLAYER_IN_REMOTE_MODE) {
        const auto player=vm.parameter(thread);
        if(player<0 || player>1 || !world_ready || !execute){thread.diagnostic="Remote-mode query requires a mounted native player zero or one";return StepResult::unsupported;}
        const auto result=execute({0x0241,{player},{},0,0,true});
        if(!result.executed){thread.diagnostic=result.diagnostic;return StepResult::unsupported;}
        vm.compare(thread,result.condition);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_ANGLE_BETWEEN_POINTS) {
        std::array<float,4> points{};for(auto& p:points)p=entity_float(vm.parameter(thread));
        auto destination=thread;vm.variable(destination);
        const auto angle=source_angle_between_points(points);
        if(!angle){thread.diagnostic="Non-finite original angle query";return StepResult::unsupported;}
        vm.store(thread,entity_bits(*angle));return StepResult::continued;
    }
