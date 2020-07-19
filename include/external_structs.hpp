
    struct actionhooks {
      uint64_t hook_id;
      eosio::name hooked_action;// must be the action name on which to apply the hook
      eosio::name hooked_contract;// must be the contract where the hooked_action is
      eosio::name hook_action_name;
      std::string description;
      uint64_t exec_count;
      eosio::time_point_sec last_exec;
      bool enabled;
      auto primary_key() const { return hook_id; }
      uint128_t by_hook() const { return (uint128_t{hooked_action.value} << 64) | hooked_contract.value; }

    };
    typedef eosio::multi_index<eosio::name("actionhooks"), actionhooks,
      eosio::indexed_by<"byhook"_n, eosio::const_mem_fun<actionhooks, uint128_t, &actionhooks::by_hook>>
    > actionhooks_table;







