#include <daclifycore.hpp>
#include <functions.cpp>

/**
 * updateconf updates the configuration of the contract
 * 
 * @pre requires the authority of the core contract
 * 
 * @param new_conf The new configuration to be set.
 * @param remove If true, the configuration will be removed.
 * 
 * @return The action is returning nothing.
 */
ACTION daclifycore::updateconf(groupconf new_conf, bool remove){
    require_auth(get_self());

    coreconf_table _coreconf(get_self(), get_self().value);
    if(remove){
      _coreconf.remove();
      return;
    }
    auto conf = _coreconf.get_or_default(coreconf());
    if(conf.conf.maintainer_account.actor != new_conf.maintainer_account.actor || conf.conf.maintainer_account.permission != new_conf.maintainer_account.permission){
      update_owner_maintainance(new_conf.maintainer_account);
    }
    conf.conf = new_conf;
    _coreconf.set(conf, get_self());
}



/**
 * propose action allows a custodian to propose a group action
 * 
 * @pre requires the authority of the proposer
 * 
 * @param proposer the account proposing the action
 * @param title The title of the proposal
 * @param description The description of the proposal
 * @param actions a vector of actions that will be executed if the proposal is approved.
 * @param expiration The time at which the proposal will expire.
 */
ACTION daclifycore::propose(name proposer, string title, string description, vector<action> actions, time_point_sec expiration) {
  require_auth(proposer);

  if(proposer != get_self() ){ //allow get_self to propose
    check(is_custodian(proposer, true, true), "You can't propose group actions because you are not a custodian.");
  }
  
  time_point_sec now = time_point_sec(current_time_point());

  //validate actions
  check(actions.size() > 0 && actions.size() < 8, "Number of actions not allowed.");

  //find  max required threshold
  threshold_name_and_value max_required_threshold;
  for (std::vector<int>::size_type i = 0; i != actions.size(); i++){
    if(actions[i].account == get_self() ){
      check(actions[i].name != name("propose"), "can't propose a proposal on self.");
    }
    threshold_name_and_value tnav = get_required_threshold_name_and_value_for_contract_action(actions[i].account, actions[i].name);
    check(tnav.threshold >= 0, "Action "+actions[i].name.to_string()+" is blocked via negative threshold");
    if(i==0){
      max_required_threshold = tnav;
    }
    else if(tnav.threshold > max_required_threshold.threshold){
      max_required_threshold = tnav;
    }
  }

  groupconf conf = get_group_conf();
  if(max_required_threshold.threshold == 0 && conf.exec_on_threshold_zero ){
    //immediate execution, no signatures needed
    for(action act : actions) { 
        act.send();
    }
    return;
  }

  //validate expiration
  check( now < expiration, "Expiration must be in the future.");
  uint32_t seconds_left = expiration.sec_since_epoch() - now.sec_since_epoch();
  check(seconds_left >= 60*60, "Minimum expiration not met.");

  

  vector<name>approvals;
  if(proposer != get_self() ){
    approvals.push_back(proposer);
  }
  name ram_payer = get_self();

  proposals_table _proposals(get_self(), get_self().value);
  _proposals.emplace(ram_payer, [&](auto& n) {
    n.id = _proposals.available_primary_key();
    n.proposer = proposer;
    n.actions = actions;
    n.approvals = approvals;
    n.expiration = expiration;
    n.submitted = now;
    n.description = description;
    n.title = title;
    n.last_actor = proposer;
    n.trx_id = get_trx_id();
    n.required_threshold = max_required_threshold.threshold_name;
  });

  if(true){
  //messagebus(name sender_group, name event, string message)

    string msg  = "New proposal by "+proposer.to_string();
    
    action(
        permission_level{ get_self(), "owner"_n },
        conf.hub_account,
        "messagebus"_n,
        std::make_tuple(get_self(), name("propose"), msg, vector<name>{} ) //name sender_group, name event, string message, vector<name> receivers
    ).send();
    
  
/*
    std::vector<char> data;
    action(
        permission_level{ get_self(), "owner"_n },
        conf.hub_account,
        "messagebus2"_n,
        std::make_tuple(get_self(), name("propose"), msg, vector<name>{name("croneosdac22")}, data)
    ).send();

*/
    
  }

  hookmanager(name("propose"), get_self() );

}


/**
 * approve action adds the approver to the proposal's approvals list, and then calls the hookmanager
 * 
 * @pre requires the authority of the approver
 * 
 * @param approver The account that is approving the proposal.
 * @param id The proposal id.
 */
ACTION daclifycore::approve(name approver, uint64_t id) {
  require_auth(approver);
  check(is_custodian(approver, true, true), "You can't approve because you are not a custodian.");
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");

  std::set<name> new_approvals{};
  new_approvals.insert(approver);
  for (name old_approver: prop_itr->approvals) {
    //check for dups and clean out non/old custodians.
    //todo calculate current weight here
    if(is_custodian(old_approver, false, false)){
      check(new_approvals.insert(old_approver).second, "You already approved this proposal.");
    }
  }
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = approver;
      n.approvals = vector<name>(new_approvals.begin(), new_approvals.end() );
  });
  hookmanager(name("approve"), get_self() );
}

/**
 * offchain action does nothing except collect the offchain task description string
 * It allows the creation of a proposal containing an offchain activity
 * 
 * @param description A string that describes the off-chain activity.
 */
ACTION daclifycore::offchain(const string&  description) {
  hookmanager(name("offchain"), get_self() );
}

/**
 * unapprove action removes the unapprover from the list of approvals for the proposal
 * 
 * @pre requires the authority of the unapprover
 * 
 * @param unapprover The account that is unapproving the proposal.
 * @param id The id of the proposal.
 */
ACTION daclifycore::unapprove(name unapprover, uint64_t id) {
  require_auth(unapprover);
  check(is_custodian(unapprover, true, true), "You can't unapprove because you are not a custodian.");
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");

  std::set<name> new_approvals{};
  bool has_approved = false;
  for (name old_approver: prop_itr->approvals) {
    //check for dups and clean out non/old custodians.
    if(old_approver == unapprover ){
      has_approved = true;
    }
    else if(is_custodian(old_approver, false, false) ){
      new_approvals.insert(old_approver);
    }
  }
  check(has_approved, "You are not in the list of approvals.");
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.approvals = vector<name>(new_approvals.begin(), new_approvals.end() );
      n.last_actor = unapprover;
  });
  hookmanager(name("unapprove"), get_self() );
}



/**
 * cancel action allows the proposer to cancel a proposal, or the contract to cancel a proposal if it's been
 * inactive for more than `max_inactive_proposal_time` seconds
 * 
 * @pre requires the authority of the core contract or the canceler
 * 
 * @param canceler the account that is cancelling the proposal.
 * @param id the proposal id
 */
ACTION daclifycore::cancel(name canceler, uint64_t id) {

  
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");

  if(!has_auth(get_self() ) ){
    require_auth(prop_itr->proposer);//only proposer can cancel
  }
  else{
    canceler = get_self();
  }
  
  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = canceler;
  });
  archive_proposal(name("cancelled"), _proposals, prop_itr);
  is_custodian(canceler, true, true);//this will update the timestamp if canceler is (still) custodian
  hookmanager(name("cancel"), get_self() );
}

/**
 * exec action checks if the proposal has expired, if it has enough votes, and if so, it executes the actions in
 * the proposal
 * 
 * @pre requires the authority of the executer
 * 
 * @param executer the account that executes the proposal.
 * @param id the proposal id
 */
ACTION daclifycore::exec(name executer, uint64_t id) {
  require_auth(executer);
  proposals_table _proposals(get_self(), get_self().value);
  auto prop_itr = _proposals.find(id);
  check(prop_itr != _proposals.end(), "Proposal not found.");
  time_point_sec now = time_point_sec(current_time_point());

  check( now < prop_itr->expiration, "Proposal Expired.");

  //verify if can be executed -> highest threshold met?
  uint8_t total_approved_weight = get_total_approved_proposal_weight(prop_itr);

  uint8_t highest_action_threshold = get_threshold_by_name(prop_itr->required_threshold);
  
  check(total_approved_weight >= highest_action_threshold, "Not enough vote weight for execution.");

  //exec
  for(action act : prop_itr->actions) { 
      act.send();
  }

  _proposals.modify( prop_itr, same_payer, [&]( auto& n) {
      n.last_actor = executer;
  });

  archive_proposal(name("executed"), _proposals, prop_itr);
  //_proposals.erase(prop_itr);

  is_custodian(executer, true, true);//this will update the timestamp if canceler is (still) custodian
  hookmanager(name("exec"), get_self() );
}

/**
 * invitecust action allows the group to invite a new custodian
 * 
 * @pre requires the authority of the core contract
 * 
 * @param account The account to invite as a custodian.
 */
ACTION daclifycore::invitecust(name account){
  require_auth(get_self() );
  check(account != get_self(), "Self can't be a custodian.");

  //don't allow invitation of custodians when election module is installed
  check(!has_module(name("elections")), "Can't invite a custodian when election module is linked.");
  
  auto conf = get_group_conf();

  check(is_account_voice_wrapper(account), "Account does not exist or doesn't meet requirements.");

  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);

  check(cust_itr == _custodians.end(), "Account already a custodian.");

  corestate_table _corestate(get_self(), get_self().value);
  auto state = _corestate.get_or_create(get_self(), corestate());

  //time_point_sec last_active = state.state.cust_count==0 ? time_point_sec(current_time_point() ) : time_point_sec(0);

  if(state.state.cust_count==0){
    _custodians.emplace( get_self(), [&]( auto& n){
        n.account = account;
        n.last_active = time_point_sec(current_time_point() );
    });
    state.state.cust_count = state.state.cust_count + 1;
    _corestate.set(state, get_self());
    update_active();
  
  }
  else{
    _custodians.emplace( get_self(), [&]( auto& n){
        n.account = account;
    });
    state.state.cust_count = state.state.cust_count + 1;
    _corestate.set(state, get_self());
  }
  hookmanager(name("invitecust"), get_self() );

}

/**
 * removecust action removes a custodian from the custodian table
 * 
 * @pre requires the authority of the core contract
 * 
 * @param account The account to remove as a custodian.
 */
ACTION daclifycore::removecust(name account){
  require_auth(get_self());

  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);

  check(cust_itr != _custodians.end(), "Account is not a custodian.");
    
  _custodians.erase(cust_itr);
  update_custodian_count(-1);
  if(_custodians.begin() != _custodians.end() ){
    //the erased entry was not the last one.
    update_active();
  }
  else{
    check(false, "Can't remove the last custodian.");
  }
  hookmanager(name("removecust"), get_self() );
}

/**
 * imalive action allows a custodian to prove they are still active (alive)
 * 
 * @pre requires the authority of the custodian
 * 
 * @param account The account that is calling the action.
 */
ACTION daclifycore::imalive(name account){
  require_auth(account);
  custodians_table _custodians(get_self(), get_self().value);
  auto cust_itr = _custodians.find(account.value);
  check(cust_itr != _custodians.end(), "You are not a custodian, no proof of live needed.");
  if(!is_account_alive(cust_itr->last_active) ){
    update_custodian_last_active(account);
    update_active();
  }
  else{
    update_custodian_last_active(account);
  }
  hookmanager(name("imalive"), get_self() );
}

/**
 * This function is called by the elections contract to set the custodians of the group
 * 
 * @param accounts a vector of account names that will be the new custodians
 */
ACTION daclifycore::isetcusts(vector<name> accounts){
  
  //require_auth(get_self() );
  modules_table _modules(get_self(), get_self().value);
  auto itr = _modules.find(name("elections").value);
  check(itr != _modules.end(), "Group doesn't have module elections");

  require_auth(itr->slave_permission.actor); //elections_contract

  int count_new = accounts.size();
  auto conf = get_group_conf();
  check(count_new <= conf.max_custodians, "Too many new custodians");
  check(count_new != 0, "Empty custodian list not allowed");

  vector<custodians> new_custs;
  custodians_table _custodians(get_self(), get_self().value);
  
  time_point_sec now = time_point_sec(current_time_point().sec_since_epoch());
  //this can be done more efficient->intersection
  for(name cand : accounts){
    //check if cand is already a custodian
    auto itr_existing = _custodians.find(cand.value);
    if(itr_existing != _custodians.end()){
      new_custs.push_back(*itr_existing);
    }
    else{
      custodians newelected{};
      newelected.account = cand;
      //newelected.weight = 1;
      //newelected.authority = name("active");
      //newelected.joined = time_point_sec(current_time_point().sec_since_epoch());
      newelected.last_active = now;
      new_custs.push_back(newelected);
    }
  }

  //empty current custodian table
  auto clean_itr = _custodians.begin();
  while(clean_itr != _custodians.end() ) {
    clean_itr = _custodians.erase(clean_itr);
  }

  //repopulate with new elected
  for(custodians nc : new_custs){
    _custodians.emplace( get_self(), [&]( auto& n){
        n = nc;
    });
  }

  update_active();

  corestate_table _corestate(get_self(), get_self().value);
  auto state = _corestate.get_or_create(get_self(), corestate());
  state.state.cust_count = count_new;
  _corestate.set(state, get_self());

  hookmanager(name("isetcusts"), get_self() );

}

/**
 * withdraw action withdraws an amount of an asset from the user account.
 * It takes an account name and an extended asset, checks that the account is not the contract itself,
 * that the amount is greater than zero, and that withdrawals are enabled. Then it sends a transfer
 * action to the contract that issued the asset
 * 
 * @pre requires the authority of the user
 * 
 * @param account The account that is withdrawing
 * @param amount The amount of tokens to withdraw.
 */
ACTION daclifycore::widthdraw(name account, extended_asset amount) {
  require_auth(account);
  check(get_group_conf().withdrawals, "Withdrawals are disabled");
  check(account != get_self(), "Can't withdraw to self.");
  check(amount.quantity.amount > 0, "Amount must be greater then zero.");

  //sub_balance(account, amount); this is handled by the on_notify !!!
  
  action(
    permission_level{get_self(), "owner"_n},
    amount.contract, "transfer"_n,
    make_tuple(get_self(), account, amount.quantity, string("withdraw from user account"))
  ).send();
  hookmanager(name("widthdraw"), get_self() );
}

/**
 * internalxfr action allows members to transfer tokens to other members
 * 
 * @pre requires the authority of the 'from' account
 * 
 * @param from The account that is sending the funds.
 * @param to The account that will receive the funds.
 * @param amount The amount of tokens to transfer.
 * @param msg The message that will be displayed in the transaction memo.
 */
ACTION daclifycore::internalxfr(name from, name to, extended_asset amount, string msg){
  require_auth(from);
  groupconf conf = get_group_conf();
  check(conf.internal_transfers, "Internal transfers are disabled.");
  check(is_member(from), "Sender must be a member." );
  check(is_member(to), "Receiver must be a member." );
  check(amount.quantity.amount > 0, "Transfer value must be greater then zero.");
  sub_balance(from, amount);
  add_balance(to, amount);
  hookmanager(name("internalxfr"), get_self() );
}

/**
 * manthreshold action allows the contract owner to set or remove a threshold
 * 
 * @pre requires the authority of the core contract
 * 
 * @param threshold_name The name of the threshold.
 * @param threshold the threshold value to be set
 * @param remove true if you want to remove the threshold, false if you want to add it.
 */
ACTION daclifycore::manthreshold(name threshold_name, int8_t threshold, bool remove){
  require_auth(get_self() );
  insert_or_update_or_delete_threshold(threshold_name, threshold, remove, false);//!!!!!!!!!!!!! false
}

/**
 * manthreshlin action adds or removes a threshold link between a contract and an action
 * 
 * @pre requires the authority of the core contract
 * 
 * @param contract The account that owns the action.
 * @param action_name The name of the action to be linked to the threshold.
 * @param threshold_name The name of the threshold to assign to the action.
 * @param remove true to remove the link, false to add it.
 */
ACTION daclifycore::manthreshlin(name contract, name action_name, name threshold_name, bool remove){
  require_auth(get_self() );

  //the code can handle wildcards but disabled for now with these checks
  //check(action_name.value != 0, "Can't wildcard action name.");//disable  contract::*
  check(contract.value != 0, "Can't wildcard contract name.");//disable *::action 

  check(threshold_name.value != 0, "Threshold name can't be empty.");
  check(threshold_name != name("default"), "Default threshold can't be assigned.");

  if(contract.value != 0 ){
    check(is_account(contract), "Contract isn't an existing account.");
  }
  check(is_existing_threshold_name(threshold_name), "Threshold name doesn't exist. Create it first.");

  threshlinks_table _threshlinks(get_self(), get_self().value);

  auto by_cont_act = _threshlinks.get_index<"bycontact"_n>();
  uint128_t composite_id = (uint128_t{contract.value} << 64) | action_name.value;
  auto link_itr = by_cont_act.find(composite_id);

  if(link_itr != by_cont_act.end() ){
    //link already exists so remove
    if(remove){
      by_cont_act.erase(link_itr);
    }
  }
  else{
    //new link so add it to the table
    if(remove){
      check(false, "Can't remove a non existing threshold link.");
    }
    _threshlinks.emplace( get_self(), [&]( auto& n){
      n.id = _threshlinks.available_primary_key();
      n.contract = contract;
      n.action_name = action_name;
      n.threshold_name = threshold_name;
    });
  }
}

/**
 * trunchistory action deletes the oldest proposals from the history table
 * 
 * @pre requires the authority of the core contract
 * 
 * @param archive_type The name of the scope to clear.
 * @param batch_size The number of proposals to delete in a single transaction.
 */
ACTION daclifycore::trunchistory( name archive_type, uint32_t batch_size){
  require_auth(get_self() );
  check(archive_type != get_self(), "Not allowed to clear this scope.");
  proposals_table h_proposals(get_self(), archive_type.value);
  check(h_proposals.begin() != h_proposals.end(), "History scope empty.");

  uint32_t counter = 0;
  auto itr = h_proposals.begin();
  while(itr != h_proposals.end() && counter++ < batch_size) {
    itr = h_proposals.erase(itr);
  }
}

/**
 * regmember action registers an account as a member of the DAC
 * 
 * @pre requires the authority of the member account
 * 
 * @param actor The accountname of the member to be registered.
 */
ACTION daclifycore::regmember(name actor){
  require_auth(actor);
  groupconf conf = get_group_conf();
  check(conf.member_registration, "Member registration is disabled.");
  check(is_account_voice_wrapper(actor), "Accountname not eligible for registering as member.");
  check(actor != get_self(), "Contract can't be a member of itself.");
  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(actor.value);
  check(mem_itr == _members.end(), "Accountname already a member.");

  _members.emplace( actor, [&]( auto& n){
    n.account = actor;
    n.member_since = time_point_sec(current_time_point() );
  });
  update_member_count(1);
  hookmanager(name("regmember"), get_self() );
}

/**
 * signuserterm action allows a member to agree or disagree with the latest userterms version
 * 
 * @pre requires the authority of the member account
 * 
 * @param member The account name of the member who is signing the user terms.
 * @param agree_terms true if the user agrees to the terms, false if they do not.
 */
ACTION daclifycore::signuserterm(name member, bool agree_terms){
  require_auth(member);
  groupconf conf = get_group_conf();
  check(conf.userterms, "Userterms disabled.");

  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(member.value);
  check(mem_itr != _members.end(), "Must register before signing the userterms.");

  dacfiles_table _dacfiles(get_self(), name("userterms").value);
  auto latest_terms = _dacfiles.end();
  check(latest_terms != _dacfiles.begin(), "Userterms enabled but no file published yet in dacfiles usertems");

  --latest_terms;

  uint64_t updated_agreed_version;

  if(agree_terms){
    check(mem_itr->agreed_userterms_version < latest_terms->id, "Member already agreed the latest userterms version "+to_string(mem_itr->agreed_userterms_version) );
    updated_agreed_version = latest_terms->id;
  }
  else{
    updated_agreed_version = 0;
  }

  _members.modify( mem_itr, same_payer, [&]( auto& n) {
      n.agreed_userterms_version = updated_agreed_version;
  });  
  hookmanager(name("signuserterm"), get_self() );

}


/**
 * unregmember action removes a member from the members table
 * 
 * @pre requires the authority of the member (actor) account
 * 
 * @param actor The accountname of the member to unregister.
 */
ACTION daclifycore::unregmember(name actor){
  require_auth(actor);
  check(!member_has_balance(actor),"Member has positive balance, withdraw first.");
  members_table _members(get_self(), get_self().value);
  auto mem_itr = _members.find(actor.value);
  check(mem_itr != _members.end(), "Accountname is not a member.");
  _members.erase(mem_itr);
  update_member_count(-1);
  hookmanager(name("unregmember"), get_self() );
}

/**
 * updateavatar action updates the user's avatar to the link provided
 * 
 * @pre requires the authority of the user (actor) account
 * 
 * @param actor The account that is calling the action.
 * @param img_url The URL of the image to be stored.
 */
ACTION daclifycore::updateavatar(name actor, string img_url){
  require_auth(actor);
  check(img_url.rfind("https://", 0) == 0, "Url must be https://");
  avatars_table _avatars(get_self(), get_self().value);
  auto itr = _avatars.find(actor.value);
  if(itr == _avatars.end() ){
    //add new
    _avatars.emplace( actor, [&]( auto& n){
      n.account = actor;
      n.img_url = img_url;
    });
  }
  else{
    //update
    _avatars.modify( itr, same_payer, [&]( auto& n) {
      n.img_url = img_url;
    });   
  }
}

/**
 * updatprofile action updates a field in the user's profile with the string provided
 * 
 * @pre requires the authority of the user (actor) account
 * 
 * @param actor The account that is updating the profile.
 * @param key the key of the data you want to update.
 * @param data The data to be stored.
 */
ACTION daclifycore::updatprofile(name actor, name key, string data){
  require_auth(actor);
  //check(is_member(actor), "Must be a member before updating profile." );
  profiledata_table _profiledata(get_self(), get_self().value);
  auto itr = _profiledata.find(actor.value);
  if(itr == _profiledata.end() ){
    //add new
    _profiledata.emplace( actor, [&]( auto& n){
      n.account = actor;
      n.data[key] = data;
      n.last_update = time_point_sec(current_time_point());
    });
  }
  else{
    //update
    _profiledata.modify( itr, same_payer, [&]( auto& n) {
      n.data[key] = data;
      n.last_update = time_point_sec(current_time_point());
    });   
  }
}

/**
 * delprofile action deletes the profile data and avatar data of the actor.
 * 
 * @pre requires the authority of the user (actor) account
 * 
 * @param actor The account name of the user who is deleting their profile.
 */
ACTION daclifycore::delprofile(name actor){
  require_auth(actor);
  profiledata_table _profiledata(get_self(), get_self().value);
  auto prof_itr = _profiledata.find(actor.value);
  if(prof_itr != _profiledata.end() ){
    _profiledata.erase(prof_itr);
  }

  avatars_table _avatars(get_self(), get_self().value);
  auto ava_itr = _avatars.find(actor.value);
  if(ava_itr != _avatars.end() ){
    _avatars.erase(ava_itr);
  }
}

/**
 * linkmodule action creates a new row in the modules table, and stores the module name, the parent contract, the
 * slave permission, and whether or not the module has a contract
 * 
 * @pre requires the authority of the core contract
 * 
 * @param module_name The name of the module.
 * @param slave_permission The permission level of the module contract.
 * @param has_contract If the module is a contract, set this to true. If not, set this to false.
 */
ACTION daclifycore::linkmodule(name module_name, permission_level slave_permission, bool has_contract){
  require_auth(get_self() );
  modules_table _modules(get_self(), get_self().value);
  auto itr = _modules.find(module_name.value);
  check(module_name.value != 0, "Module name must be specified");
  check(itr == _modules.end(), "Duplicate module name.");
  check(is_account(slave_permission.actor), "Actor is not an existing account");
  check(slave_permission.permission.value != 0, "Permission can't be empty");
  permission_level master = permission_level(get_self(), name("owner") );
  check(is_master_authorized_to_use_slave(master, slave_permission), "Core contract is not allowed to use module, fix permissions on module contract");
  _modules.emplace( get_self(), [&]( auto& n){
      n.module_name = module_name;
      n.parent = get_self();
      n.slave_permission = slave_permission;
      n.has_contract = has_contract;
  });
  hookmanager(name("linkmodule"), get_self() );
}

/**
 * unlinkmodule action removes a module from the modules table
 * 
 * @pre requires the authority of the core contract
 * 
 * @param module_name The name of the module to be unlinked.
 */
ACTION daclifycore::unlinkmodule(name module_name){
  require_auth(get_self() );
  modules_table _modules(get_self(), get_self().value);
  auto itr = _modules.find(module_name.value);
  check(itr != _modules.end(), "Module doesn't exists.");
  _modules.erase(itr);
  hookmanager(name("unlinkmodule"), get_self() );
}

/**
 * This function allows the contract owner to set the components of a frame, and the data associated
 * with the frame
 * 
 * @pre requires the authority of the core contract
 * 
 * @param frame_id the id of the frame
 * @param comp_ids a vector of component ids that are part of this frame
 * @param data the data to be stored in the table
 */
ACTION daclifycore::setuiframe(uint64_t frame_id, vector<uint64_t>comp_ids, string data){
  require_auth(get_self() );
  uiframes_table _uiframes(get_self(), get_self().value);
  auto itr = _uiframes.find(frame_id);
  if(itr == _uiframes.end() ){
    check(comp_ids.size() != 0, "must specify at least one component id");
    _uiframes.emplace( get_self(), [&]( auto& n){
        n.frame_id = frame_id;
        n.comp_ids = comp_ids;
        n.data = data;
    });
  }
  else{
    if(comp_ids.size() == 0){
      _uiframes.erase(itr);
      return;
    }
    _uiframes.modify( itr, same_payer, [&]( auto& n) {
        n.comp_ids = comp_ids;
        n.data = data;
    });  
  }

}


/**
 * ipayroll action is a function that allows any module to call the payroll interface
 * 
 * @param sender_module_name the name of the module that is calling the payroll interface.
 * @param payroll_tag a unique name for the payroll
 * @param payments a vector of payment structs, which are defined in the payroll contract.
 * @param memo a string that will be added to the memo of the transaction
 * @param due_date The date when the payroll should be paid.
 * @param repeat 0 = no repeat, 1 = repeat once, 2 = repeat twice, etc.
 * @param recurrence_sec The number of seconds between each payment.
 * @param auto_pay If true, the payroll will be paid automatically. If false, the payroll will be paid
 * manually.
 */
ACTION daclifycore::ipayroll(
                      name sender_module_name, 
                      name payroll_tag, 
                      vector<payment> payments, 
                      string memo, 
                      time_point_sec due_date, 
                      uint8_t repeat, 
                      uint64_t recurrence_sec, 
                      bool auto_pay
                    ){

  /* all modules are allowed to call the payroll interface */
  modules_table _modules(get_self(), get_self().value);
  auto payroll_module = _modules.get(name("payroll").value, "payroll module not available");
  auto module_sender = _modules.get(sender_module_name.value, "Module that tries to use the payroll interface doesn't exist.");
  require_auth(module_sender.slave_permission.actor);

  //only allow inline calls
  //check(get_sender() == module_sender.slave_permission.actor, "Only inline calls allowed");

  //name payroll_tag, vector<payment> payments, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay
  action(
    payroll_module.slave_permission,
    payroll_module.slave_permission.actor,
    "addmany"_n,
    std::make_tuple(
      payroll_tag,
      payments,
      memo,
      due_date,
      repeat,
      recurrence_sec,
      auto_pay
    )
  ).send();
  
}

/**
 * fileupload action adds content to the action trace
 * 
 * @pre requires the authority of the uploader account
 * 
 * @param uploader The account that is uploading the file.
 * @param file_scope The scope of the file. This is the name of the account that owns the file.
 * @param content The content of the file.
 */
ACTION daclifycore::fileupload(name uploader, name file_scope, string content){
  require_auth(uploader);
  check(file_scope.value != 0, "must supply a non empty file_scope");
  check(content.size() != 0, "Content can't be empty");
}

/**
 * filepublish action populates the dacfiles table with upload references
 * 
 * @pre requires the authority of the core contract
 * 
 * @param file_scope The scope of the file. This is the name of the account that owns the file.
 * @param title The title of the file
 * @param trx_id The transaction id of the uploaded file.
 * @param block_num The block number of the transaction that uploaded the file.
 */
ACTION daclifycore::filepublish(name file_scope, string title, checksum256 trx_id, uint32_t block_num){
  require_auth(get_self() );
  check(file_scope.value != 0, "must supply a non empty file_scope");
  checksum256 test;
  check(test != trx_id, "Must supply the transaction id pointing to the uploaded content");

  dacfiles_table _dacfiles(get_self(), file_scope.value);
  uint64_t id = _dacfiles.available_primary_key()==0?1:_dacfiles.available_primary_key();
  _dacfiles.emplace( get_self(), [&]( auto& n){
      n.id = id;
      n.trx_id = trx_id;
      n.title = title;
      n.block_num = block_num;
      n.published = time_point_sec(current_time_point());
  });
  hookmanager(name("filepublish"), get_self() );
}

/**
 * filedelete action deletes a file from the dacfiles table
 * 
 * @pre requires the authority of the core contract
 * 
 * @param file_scope The scope of the file. This is the name of the table.
 * @param id the id of the file to delete
 */
ACTION daclifycore::filedelete(name file_scope, uint64_t id){
  require_auth(get_self() );
  dacfiles_table _dacfiles(get_self(), file_scope.value);
  auto itr = _dacfiles.find(id);
  check(itr != _dacfiles.end(), "can't find id "+to_string(id)+" in file scope "+file_scope.to_string() );
  _dacfiles.erase(itr);
  hookmanager(name("filedelete"), get_self() );
}


/**
 * asset-transfer notification handler
 * 
 * If the transfer is incoming, it will either add to the group wallet or to a user wallet if the memo
 * is correct. If the transfer is outgoing, it will either withdraw from the group wallet or from a
 * user wallet if the memo is correct
 * 
 * @param from The account that sent the transfer.
 * @param to The account that is receiving the funds.
 * @param quantity The amount of tokens being transferred.
 * @param memo The memo is a string that is attached to the transfer. It can be used to store
 * additional information about the transfer.
 * 
 */
void daclifycore::on_transfer(name from, name to, asset quantity, string memo){

  check(quantity.amount > 0, "Transfer amount must be greater then zero");
  check(to != from, "Invalid transfer.");

  extended_asset extended_quantity = extended_asset(quantity, get_first_receiver());
  groupconf conf = get_group_conf();
  //////////////////////
  //incomming transfers
  //////////////////////
  if (to == get_self() ) {
    //check memo if it's a transfer to top up a user wallet
    if(memo.substr(0, 21) == "add to user account: " ){
      check(conf.deposits, "Deposits to user accounts is disabled.");
      string potentialaccountname = memo.length() >= 22 ? memo.substr(21, 12 ) : "";
      check(is_member(name(potentialaccountname) ), "Receiver in memo is not a registered member.");
      add_balance( name(potentialaccountname), extended_quantity);
      return;
    }
    else{
      //fund group wallet
      add_balance( to, extended_quantity); //to == self
      return;   
    }
  }
  //////////////////////
  //outgoing transfers
  //////////////////////
  if (from == get_self() ) {
    //check memo if it is a user withrawal
    if(memo.substr(0, 26) == "withdraw from user account" ){
      print("user withdraw");
      check(is_member(to), "To is not an regstered member.");
      sub_balance( to, extended_quantity);
      return;
    }
    else{
      sub_balance( from, extended_quantity);
      return;    
    }
  }
}

//dev

/**
 * clearbals action clears the balances table for a given scope.
 * 
 * @pre requires the authority of the core contract
 * 
 * @param scope The scope of the table.
 */
ACTION daclifycore::clearbals(name scope){
  require_auth(get_self());
  balances_table _balances( get_self(), scope.value);
  auto itr = _balances.begin();
  while(itr != _balances.end() ) {
      itr = _balances.erase(itr);
  }
}




