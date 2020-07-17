    struct actionhooks {
      uint64_t hook_id;
      eosio::name hooked_action;// must be the action name on which to apply the hook
      eosio::name hooked_contract;// must be the contract where the hooked_action is
      eosio::name hook_action_name;
      uint64_t exec_count;
      eosio::time_point_sec last_exec;
      bool enabled;
      auto primary_key() const { return hook_id; }
      uint128_t by_hook() const { return (uint128_t{hooked_action.value} << 64) | hooked_contract.value; }

    };
    typedef eosio::multi_index<eosio::name("actionhooks"), actionhooks,
      eosio::indexed_by<"byhook"_n, eosio::const_mem_fun<actionhooks, uint128_t, &actionhooks::by_hook>>
    > actionhooks_table;



    struct  hookmanager{

      hookmanager(eosio::name hooked_action, eosio::name self_)  { 

        modules_table _modules(self_, self_.value);
        auto mod_itr = _modules.find(eosio::name("hooks").value );
        if(mod_itr != _modules.end() ){
        
            eosio::name hooks_contract = *mod_itr->slave_permission.actor;
            actionhooks_table _actionhooks(hooks_contract, hooks_contract.value);
            auto by_hook = _actionhooks.get_index<"byhook"_n>();
            uint128_t composite_id = (uint128_t{hooked_action.value} << 64) | self_.value;
            auto hook_itr = by_hook.find(composite_id);
            if(hook_itr != by_hook.end() ){
                eosio::action(
                    eosio::permission_level{ self_, "owner"_n },
                    hooks_contract,
                    hook_itr->hook_action_name,
                    std::make_tuple( hooked_action )
                ).send();
            
            }
        }
      }

    };