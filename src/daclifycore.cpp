#include <daclifycore.hpp>
#include <functions.cpp>

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


///////////////////////////////////
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
//////////////
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


ACTION daclifycore::offchain(const string&  description) {
  ;
}

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

ACTION daclifycore::manthreshold(name threshold_name, int8_t threshold, bool remove){
  require_auth(get_self() );
  insert_or_update_or_delete_threshold(threshold_name, threshold, remove, false);//!!!!!!!!!!!!! false
}

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

ACTION daclifycore::unlinkmodule(name module_name){
  require_auth(get_self() );
  modules_table _modules(get_self(), get_self().value);
  auto itr = _modules.find(module_name.value);
  check(itr != _modules.end(), "Module doesn't exists.");
  _modules.erase(itr);
  hookmanager(name("unlinkmodule"), get_self() );
}

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

//interface for payroll
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

//this action just adds content to the action trace
ACTION daclifycore::fileupload(name uploader, name file_scope, string content){
  require_auth(uploader);
  check(file_scope.value != 0, "must supply a non empty file_scope");
  check(content.size() != 0, "Content can't be empty");
}

//this action populates the dacfiles table with upload references
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

ACTION daclifycore::filedelete(name file_scope, uint64_t id){
  require_auth(get_self() );
  dacfiles_table _dacfiles(get_self(), file_scope.value);
  auto itr = _dacfiles.find(id);
  check(itr != _dacfiles.end(), "can't find id "+to_string(id)+" in file scope "+file_scope.to_string() );
  _dacfiles.erase(itr);
  hookmanager(name("filedelete"), get_self() );
}


//notify transfer handler
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

ACTION daclifycore::clearbals(name scope){
  require_auth(get_self());
  balances_table _balances( get_self(), scope.value);
  auto itr = _balances.begin();
  while(itr != _balances.end() ) {
      itr = _balances.erase(itr);
  }
}




