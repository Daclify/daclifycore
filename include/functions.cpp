daclifycore::groupconf daclifycore::get_group_conf(){
  //groupconf conf;
  //return conf;
  coreconf_table _coreconf(get_self(), get_self().value);
  auto setting = _coreconf.get_or_create(get_self(), coreconf());
  return setting.conf;
}

bool daclifycore::is_member(const name& accountname){
  if(accountname.value == 0 ){
    return false;
  }
  else if(accountname == get_self() ){
    return true; 
  }
  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(accountname.value);
  
  if(mem_itr == _members.end() ){
    return false;
  }
  else{
    //check if signed userterms
    return true;
  }

}

bool daclifycore::has_module(const name& module_name){
  modules_table _modules(get_self(), get_self().value);
  auto itr = _modules.find(module_name.value);
  return itr != _modules.end();
}

bool daclifycore::member_has_balance(const name& accountname){
  balances_table _balances( get_self(), accountname.value);
  if(_balances.begin() != _balances.end() ){
    return true;
  }
  else{
    return false;
  }
}


void daclifycore::update_member_count(int delta){

  corestate_table _corestate(get_self(), get_self().value);
  auto state = _corestate.get_or_create(get_self(), corestate());
  state.state.member_count = state.state.member_count + delta;
  _corestate.set(state, get_self());
}

void daclifycore::update_custodian_count(int delta){

  corestate_table _corestate(get_self(), get_self().value);
  auto state = _corestate.get_or_create(get_self(), corestate());
  state.state.cust_count = state.state.cust_count + delta;
  _corestate.set(state, get_self());
}

bool daclifycore::is_account_voice_wrapper(const name& account){
  //if config voice only do stuff
  return is_account(account);
}

bool daclifycore::is_custodian(const name& account, const bool& update_last_active, const bool& check_if_alive) {

  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);
  if(cust_itr == _custodians.end() ){
    return false;
  }
  else{
    if(check_if_alive){
      check(is_account_alive(cust_itr->last_active), "You've been inactive, please verify you're alive first.");
    }
    if(update_last_active){
      _custodians.modify( cust_itr, same_payer, [&]( auto& a) {
          a.last_active = time_point_sec(current_time_point().sec_since_epoch());
      });
    }
    return true;
  }
}

void daclifycore::update_custodian_last_active(const name& account){
  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);
  check(cust_itr != _custodians.end(), "Internal group error: trying to update non existing custodian.");
  _custodians.modify( cust_itr, same_payer, [&]( auto& a) {
      a.last_active = time_point_sec(current_time_point().sec_since_epoch());
  });
}

bool daclifycore::is_account_alive(time_point_sec last_active){
  
  bool is_alive = (last_active == time_point_sec(0) );
  if(get_group_conf().inactivate_cust_after_sec != 0){
    time_point_sec now = time_point_sec(current_time_point());
    uint32_t inactive_period = now.sec_since_epoch() - last_active.sec_since_epoch();
    is_alive = (inactive_period >= get_group_conf().inactivate_cust_after_sec);
  }
  return !is_alive;
}


//TODO
void daclifycore::update_custodian_weight(const name& account, const uint8_t& weight) {}
//TODO

void daclifycore::update_owner_maintainance(const permission_level& maintainer){
  vector<eosiosystem::permission_level_weight> accounts;

  eosiosystem::permission_level_weight code_account{
        .permission = permission_level(get_self(), name("eosio.code") ),
        .weight = (uint16_t) 1,
  };

  eosiosystem::permission_level_weight self_active_account{
        .permission = permission_level(get_self(), name("active") ),
        .weight = (uint16_t) 1,
  };

  if(maintainer.actor != name(0) && is_account(maintainer.actor) ){
    eosiosystem::permission_level_weight maintainer_account{
          .permission = maintainer,
          .weight = (uint16_t) 1,
    };
    accounts.push_back(maintainer_account);
  }
  accounts.push_back(self_active_account);
  accounts.push_back(code_account);
  

  if(accounts.size() > 1){
    std::sort(accounts.begin(), accounts.end(), sort_authorization_by_name());
  }

  eosiosystem::authority new_owner_authority{
        .threshold = (uint16_t) 1,
        .accounts = accounts,
        .waits = {},
        .keys = {}
  };

  action(
    permission_level{get_self(), "owner"_n},
    "eosio"_n, "updateauth"_n,
    std::make_tuple(get_self(), "owner"_n, ""_n, new_owner_authority)
  ).send();

}

void daclifycore::update_active() {

  custodians_table _custodians(get_self(), get_self().value);

  vector<eosiosystem::permission_level_weight> accounts;//active custodians
  vector<eosiosystem::permission_level_weight> inactive_accounts;//inactive custodians

  for (auto itr = _custodians.begin(); itr != _custodians.end(); itr++) {

    eosiosystem::permission_level_weight account{
          .permission = permission_level(itr->account, itr->authority),
          .weight = (uint16_t) 1,
    };

    if(is_account_alive(itr->last_active) ){
      accounts.push_back(account);
    }
    else{
      inactive_accounts.push_back(account);
    }
  }

  accounts = accounts.size() == 0 ? inactive_accounts : accounts;

  eosiosystem::authority new_authority{
        .threshold = accounts.size(),
        .accounts = accounts,
        .waits = {},
        .keys = {}
  };

  action(
    permission_level{get_self(), "owner"_n},
    "eosio"_n, "updateauth"_n,
    std::make_tuple(get_self(), "active"_n, "owner"_n, new_authority)
  ).send();


  uint8_t default_threshold;
  if(accounts.size() == 1){
    default_threshold = 1;
  }
  else if(accounts.size() <= 3){
    default_threshold = 2;
  }
  else{
    default_threshold = floor(accounts.size()*0.8);//hardcode 80% default threshold
    default_threshold = default_threshold <= 0 ? 1 : default_threshold;
  }

  insert_or_update_or_delete_threshold(name("default"), default_threshold, false, true);
}

void daclifycore::sub_balance( const name& account, const extended_asset& value) {

   balances_table _balances( get_self(), account.value);
   auto by_contr_sym = _balances.get_index<"bycontrsym"_n>(); 
   uint128_t composite_id = (uint128_t{value.contract.value} << 64) | value.quantity.symbol.raw();
   const auto& itr = by_contr_sym.get( composite_id, "No balance with this symbol and contract.");
   check( itr.balance >= value, "Overdrawn balance");

  if(account != get_self() && itr.balance == value){
    _balances.erase(itr);
    return;
  }

  _balances.modify( itr, same_payer, [&]( auto& a) {
     a.balance -= value;
  });


}


void daclifycore::add_balance( const name& account, const extended_asset& value){
  //uint128_t composite_id = (uint128_t{value.contract.value} << 64) | value.quantity.symbol.raw();
   balances_table _balances( get_self(), account.value);
   auto by_contr_sym = _balances.get_index<"bycontrsym"_n>(); 
   uint128_t composite_id = (uint128_t{value.contract.value} << 64) | value.quantity.symbol.raw();
   auto itr = by_contr_sym.find(composite_id);

   if( itr == by_contr_sym.end() ) {
      _balances.emplace( get_self(), [&]( auto& a){
        a.id = _balances.available_primary_key();
        a.balance = value;
      });
   } 
   else {
      by_contr_sym.modify( itr, same_payer, [&]( auto& a) {
        a.balance += value;
      });
   }
}

bool daclifycore::is_existing_threshold_name(const name& threshold_name){
  thresholds_table _thresholds(get_self(), get_self().value);
  auto thresh_itr = _thresholds.find(threshold_name.value);
  if(thresh_itr == _thresholds.end() ){
    return false;
  }
  else{
    return true;
  }
}

uint8_t daclifycore::get_threshold_by_name(const name& threshold_name){
  thresholds_table _thresholds(get_self(), get_self().value);
  auto thresh_itr = _thresholds.find(threshold_name.value);
  if(thresh_itr == _thresholds.end() ){
    return get_threshold_by_name(name("default"));
  }
  else{
    return thresh_itr->threshold;
  }
}

void daclifycore::insert_or_update_or_delete_threshold(const name& threshold_name, const int8_t& threshold, const bool& remove, const bool& privileged){
   thresholds_table _thresholds(get_self(), get_self().value);
   auto thresh_itr = _thresholds.find(threshold_name.value);

   bool is_linked = is_threshold_linked(threshold_name);

   if(remove){
      check(!is_linked, "Can't remove a threshold that is linked. Remove all links first.");
      check(thresh_itr != _thresholds.end(), "Can't remove non existing thresholdname.");
      check(threshold_name != name("default"), "Can't delete the default threshold."); //!!!!!!!!!!!!!!
      _thresholds.erase(thresh_itr);
      //assert when last threshold?
      return;
   }

   if(is_linked){
    check(thresh_itr->threshold != -1, "Can't change the threshold value when it is negative.");
    check(threshold != -1, "Can't set the value of a linked threshold to negative.");
   }

   if(!privileged){
      //prevent manipulating certain thresholds here
      check(threshold_name != name("default"), "Can't manipulate the default threshold.");
   }
   //to do validate if the threshold is within bounds need number of custodians for this.
   check(threshold >= -1, "Threshold can't be less then -1");
   check(threshold_name != name(0), "Invalid threshold name");
   
   if(thresh_itr == _thresholds.end() ) {
      _thresholds.emplace( get_self(), [&]( auto& a){
        a.threshold_name = threshold_name;
        a.threshold = threshold;
      });
   } 
   else {
      if(threshold_name == name("default") ){
        check(threshold > 0, "Default threshold must be greater then zero.");
      }
      _thresholds.modify( thresh_itr, same_payer, [&]( auto& a) {
        a.threshold = threshold;
      });
   }
}

bool daclifycore::is_threshold_linked(const name& threshold_name){
  threshlinks_table _threshlinks(get_self(), get_self().value);
  auto by_threshold = _threshlinks.get_index<"bythreshold"_n>(); 
  auto link_itr = by_threshold.find(threshold_name.value);
  if(link_itr == by_threshold.end() ){
    return false;
  }
  else{
    return true;
  }
}

//
daclifycore::threshold_name_and_value daclifycore::get_required_threshold_name_and_value_for_contract_action(const name& contract, const name& action_name){
  
  threshlinks_table _threshlinks(get_self(), get_self().value);
  auto by_cont_act = _threshlinks.get_index<"bycontact"_n>();
  uint128_t composite_id = (uint128_t{contract.value} << 64) | action_name.value;
  auto link_itr = by_cont_act.find(composite_id);

  if(link_itr != by_cont_act.end() ){
    //there is a FULL MATCH
    return threshold_name_and_value{link_itr->threshold_name, get_threshold_by_name(link_itr->threshold_name ) };
  }
  else{
    //check if the action name is linked
    link_itr = by_cont_act.find((uint128_t{name(0).value} << 64) | action_name.value);
    if(link_itr != by_cont_act.end() ){
      //action name is linked
      return threshold_name_and_value{link_itr->threshold_name, get_threshold_by_name(link_itr->threshold_name ) };
    }
    else{
      //check if contract is linked
      link_itr = by_cont_act.find((uint128_t{contract.value} << 64) | name(0).value);
      if(link_itr != by_cont_act.end() ){
        //contract is linked
        return threshold_name_and_value{link_itr->threshold_name, get_threshold_by_name(link_itr->threshold_name ) };
      }
      else{
        //NO MATCH -> return default
        return threshold_name_and_value{name("default"), get_threshold_by_name(name("default") ) };
      }
    }
  }
}


uint8_t daclifycore::get_total_approved_proposal_weight(proposals_table::const_iterator& prop_itr){
  uint8_t total_weight = 0;
  if(prop_itr->approvals.size()==0){
    return total_weight;
  }
  else{
    custodians_table _custodians(get_self(), get_self().value);
    
    for(name approver : prop_itr->approvals){
      auto cust_itr = _custodians.find(approver.value);
      if(cust_itr != _custodians.end() ){
        total_weight += cust_itr->weight;
      }
    }
    return total_weight;
  }
}

void daclifycore::archive_proposal(const name& archive_type, proposals_table& idx, proposals_table::const_iterator& prop_itr){

  uint8_t keep_history = get_group_conf().proposal_archive_size;

  if(keep_history > 0){

    proposals_table h_proposals(get_self(), archive_type.value);
    auto end_itr = h_proposals.end();

    if(h_proposals.begin() != end_itr ){//there are entries in the table
      end_itr--;
      uint64_t count = ( end_itr->id - h_proposals.begin()->id ) +1;
      
      if(count >= keep_history){
        //h_proposals.erase(h_proposals.begin() );

        uint32_t batch_size = 1;
        action(
            permission_level{ get_self(), "owner"_n },
            get_self(),
            "trunchistory"_n,
            std::make_tuple(archive_type, batch_size)
        ).send();
      }   
    }

    h_proposals.emplace(get_self(), [&](auto& n) {
      n.id = h_proposals.available_primary_key();
      n.proposer = prop_itr->proposer;
      n.actions = prop_itr->actions;
      n.approvals = prop_itr->approvals;
      n.expiration = prop_itr->expiration;
      n.submitted = prop_itr->submitted;
      n.description = prop_itr->description;
      n.title = prop_itr->title;
      n.last_actor = prop_itr->last_actor;
      n.required_threshold = prop_itr->required_threshold;
      n.trx_id = prop_itr->trx_id;
    });

  }
  idx.erase(prop_itr);

}











