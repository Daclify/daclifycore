
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
//#include <eosio/system.hpp>
#include <eosio/permission.hpp>
#include <eosio/singleton.hpp>
#include <system_structs.hpp>
#include <external_structs.hpp>

#include <math.h>

using namespace std;
using namespace eosio;

CONTRACT daclifycore : public contract {
  public:
    using contract::contract;

    struct action_threshold{
      name action_name;
      name threshold_name;
    };

    struct user_agreement{
      string md5_hash;
      string markdown_url;
      time_point_sec last_update;
    };

    struct groupstate{
      uint8_t cust_count;
      uint64_t member_count;
    };

    struct payment{
        eosio::name receiver;
        eosio::asset amount;
    };

    //json
    /*
        {
          "max_custodians":0,
          "inactivate_cust_after_sec":2592000,
          "exec_on_threshold_zero":false,
          "proposal_archive_size":3,
          "member_registration":true,
          "userterms": false,
          "profile_provider": "",
          "withdrawals": true,
          "internal_transfers": false,
          "deposits": false,
          "maintainer_account": {"actor": "piecesnbits1", "permission":"active"},
          "hub_account": "daclifyhub11",
          "r1": false,
          "r2": false,
          "r3": 0
        }
    */
    //

    struct groupconf{
      uint8_t max_custodians = 0;
      uint32_t inactivate_cust_after_sec = 60*60*24*30;
      bool exec_on_threshold_zero = false;
      uint8_t proposal_archive_size = 3;
      bool member_registration = false;
      bool userterms = false;
      name profile_provider;
      bool withdrawals = false;
      bool internal_transfers = false;
      bool deposits = false;
      permission_level maintainer_account = permission_level(name("daclifyhub11"), name("active") );//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      name hub_account = name("daclifyhub11");
      
      bool r1;
      bool r2;
      uint64_t r3;
      //user_agreement user_agreement;
    };

    ACTION invitecust(name account);
    ACTION removecust(name account);

    ACTION isetcusts(vector<name> accounts);//"elections" module interface action

    //(name payroll_tag, vector<payment> payments, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay)
    ACTION ipayroll(name sender_module_name, name payroll_tag, vector<payment> payments, string memo, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay);

    ACTION propose(name proposer, string title, string description, vector<action> actions, time_point_sec expiration);
    ACTION approve(name approver, uint64_t id);
    ACTION unapprove(name unapprover, uint64_t id);
    ACTION cancel(name canceler, uint64_t id);
    ACTION exec(name executer, uint64_t id);
    ACTION trunchistory(name archive_type, uint32_t batch_size);

    ACTION widthdraw(name account, extended_asset amount);
    ACTION internalxfr(name from, name to, extended_asset amount, string msg);
    ACTION imalive(name account);
    //ACTION spawnchildac(name new_account, asset ram_amount, asset net_amount, asset cpu_amount, name parent, name module_name);
    //ACTION addchildac(name account, name parent, name module_name);
   // ACTION remchildac(name account);

    ACTION linkmodule(name module_name, permission_level slave_permission, bool has_contract);
    ACTION unlinkmodule(name module_name);

    ACTION setuiframe(uint64_t frame_id, vector<uint64_t>comp_ids, string data);

    ACTION manthreshold(name threshold_name, int8_t threshold, bool remove);
    //ACTION manactlinks(name contract, vector<action_threshold> new_action_thresholds);//will be deprecated
    ACTION manthreshlin(name contract, name action_name, name threshold_name, bool remove);

    ACTION regmember(name actor);
    ACTION unregmember(name actor);
    ACTION signuserterm(name member, bool agree_terms);
    ACTION updateavatar(name actor, string img_url);
    ACTION updatprofile(name actor, name key, string data);
    ACTION delprofile(name actor);

    ACTION updateconf(groupconf new_conf, bool remove);

    ACTION fileupload(name uploader, name file_scope, string content);
    ACTION filepublish(name file_scope, string title, checksum256 trx_id, uint32_t block_num);
    ACTION filedelete(name file_scope, uint64_t id);

    //dev
    ACTION clearbals(name scope);

    //notification handlers
    [[eosio::on_notify("*::transfer")]]
    void on_transfer(name from, name to, asset quantity, string memo);


  private:
  
    struct threshold_name_and_value{
      name threshold_name;
      uint8_t threshold;
    };


    TABLE coreconf{
      groupconf conf;
    };
    typedef eosio::singleton<"coreconf"_n, coreconf> coreconf_table;

    TABLE corestate{
      groupstate state;
    };
    typedef eosio::singleton<"corestate"_n, corestate> corestate_table;

    TABLE threshlinks {
      uint64_t id;
      name contract;
      name action_name;
      name threshold_name;

      auto primary_key() const { return id; }
      //uint64_t by_action() const { return action_name.value; }
      uint64_t by_threshold() const { return threshold_name.value; }
      uint128_t by_cont_act() const { return (uint128_t{contract.value} << 64) | action_name.value; }
    };
    typedef multi_index<name("threshlinks"), threshlinks,
      //eosio::indexed_by<"byaction"_n, eosio::const_mem_fun<threshlinks, uint64_t, &threshlinks::by_action>>,
      eosio::indexed_by<"bythreshold"_n, eosio::const_mem_fun<threshlinks, uint64_t, &threshlinks::by_threshold>>,
      eosio::indexed_by<"bycontact"_n, eosio::const_mem_fun<threshlinks, uint128_t, &threshlinks::by_cont_act>>
    > threshlinks_table;

    TABLE thresholds {
      name threshold_name;
      int8_t threshold;
      auto primary_key() const { return threshold_name.value; }
    };
    typedef multi_index<name("thresholds"), thresholds> thresholds_table;

    TABLE proposals {
      uint64_t id;
      string title;
      string description;
      name proposer;
      vector<action> actions;
      time_point_sec submitted;
      time_point_sec expiration;
      vector<name> approvals;
      name required_threshold;
      name last_actor;
      checksum256 trx_id;

      auto primary_key() const { return id; }
      uint64_t by_threshold() const { return required_threshold.value; }
      uint64_t by_proposer() const { return proposer.value; }
      uint64_t by_expiration() const { return expiration.sec_since_epoch(); }
    };
    typedef multi_index<name("proposals"), proposals,
      eosio::indexed_by<"bythreshold"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_threshold>>,
      eosio::indexed_by<"byproposer"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_proposer>>,
      eosio::indexed_by<"byexpiration"_n, eosio::const_mem_fun<proposals, uint64_t, &proposals::by_expiration>>
    > proposals_table;

    TABLE custodians {
      name account;
      name authority = name("active");
      uint8_t weight = 1;
      time_point_sec joined = time_point_sec(current_time_point().sec_since_epoch());
      time_point_sec last_active;

      auto primary_key() const { return account.value; }
      uint64_t by_last_active() const { return last_active.sec_since_epoch(); }
    };
    typedef multi_index<name("custodians"), custodians,
      eosio::indexed_by<"bylastactive"_n, eosio::const_mem_fun<custodians, uint64_t, &custodians::by_last_active>>
    > custodians_table;

    TABLE members {
      name account;
      time_point_sec member_since;
      uint64_t agreed_userterms_version;
      uint64_t r2;
      auto primary_key() const { return account.value; }

    };
    typedef multi_index<name("members"), members> members_table;

    TABLE avatars {
      name account;
      string img_url;
      auto primary_key() const { return account.value; }
    };
    typedef multi_index<name("avatars"), avatars> avatars_table;

    TABLE profiledata {
      name account;
      map<name,string> data;
      time_point_sec last_update;
      uint64_t r1;
      auto primary_key() const { return account.value; }
    };
    typedef multi_index<name("profiledata"), profiledata> profiledata_table;

    //scoped table (example: userterms)
    TABLE dacfiles {
      uint64_t id;
      string title;
      checksum256 trx_id;
      uint32_t block_num;
      time_point_sec published;
      uint64_t primary_key()const { return id; }
      uint64_t by_published() const { return published.sec_since_epoch(); }
    };
    typedef multi_index<"dacfiles"_n, dacfiles,
      eosio::indexed_by<"bypublished"_n, eosio::const_mem_fun<dacfiles, uint64_t, &dacfiles::by_published>>
    > dacfiles_table;


    //scoped table
    TABLE balances {
      uint64_t id;
      extended_asset balance;
      uint64_t primary_key()const { return id; }
      uint128_t by_contr_sym() const { return (uint128_t{balance.contract.value} << 64) | balance.quantity.symbol.raw(); }
    };
    typedef multi_index<"balances"_n, balances,
      eosio::indexed_by<"bycontrsym"_n, eosio::const_mem_fun<balances, uint128_t, &balances::by_contr_sym>>
    > balances_table;

    //scoped by user
    TABLE uiframes {
      uint64_t frame_id;
      vector<uint64_t> comp_ids;
      string data;
      uint64_t primary_key() const { return frame_id; }
    };
    typedef multi_index<name("uiframes"), uiframes> uiframes_table;
    
    TABLE modules {

      name module_name;
      permission_level slave_permission;
      name parent;
      bool has_contract;
      bool enabled = true;
      auto primary_key() const { return module_name.value; }
      uint64_t by_module_acc() const { return slave_permission.actor.value; }
    };
    typedef multi_index<name("modules"), modules,
      eosio::indexed_by<"bymoduleacc"_n, eosio::const_mem_fun<modules, uint64_t, &modules::by_module_acc>>
    > modules_table;

    //functions//
    groupconf get_group_conf();
    bool is_account_voice_wrapper(const name& account);
    void update_owner_maintainance(const permission_level& maintainer);
    //action whitelist stuff
    
    //void update_whitelist_action(const name& contract, const name& action_name, const name& threshold_name);
    //void remove_whitelist_action(const name& contract, const name& action_name);
    //bool is_action_whitelisted(const name& contract, const name& action_name);

    //internal threshold system
    bool is_existing_threshold_name(const name& threshold_name);
    uint8_t get_threshold_by_name(const name& threshold_name);
    void insert_or_update_or_delete_threshold(const name& threshold_name, const int8_t& threshold, const bool& remove, const bool& privileged);
    void update_thresholds_based_on_number_custodians();
    threshold_name_and_value get_required_threshold_name_and_value_for_contract_action(const name& contract, const name& action_name);
    bool is_threshold_linked(const name& threshold_name);

    //vector<threshold_name_and_value> get_counts_for
    //https://eosio.stackexchange.com/questions/4999/how-do-i-pass-an-iterator/5012#5012
    uint8_t get_total_approved_proposal_weight(proposals_table::const_iterator& prop_itr);
    
    //custodians
    bool is_custodian(const name& account, const bool& update_last_active, const bool& check_if_alive);
    void update_custodian_weight(const name& account, const uint8_t& weight);
    void update_active();
    void update_custodian_last_active(const name& account);
    bool is_account_alive(time_point_sec last_active);
    void update_custodian_count(int delta);

    //internal accounting
    void sub_balance(const name& account, const extended_asset& value);
    void add_balance(const name& account, const extended_asset& value);

    //proposals
    void delete_proposal(const uint64_t& id);
    void approve_proposal(const uint64_t& id, const name& approver);
    //void assert_invalid_authorization( vector<permission_level> auths);
    void archive_proposal(const name& archive_type, proposals_table& idx, proposals_table::const_iterator& prop_itr);

    bool has_module(const name& module_name);

    //members
    bool is_member(const name& accountname);
    bool member_has_balance(const name& accountname);
    void update_member_count(int delta);

    bool is_master_authorized_to_use_slave(const permission_level& master, const permission_level& slave){
      vector<permission_level> masterperm = { master };
      auto packed_master = pack(masterperm);
      auto test = eosio::check_permission_authorization(
                  slave.actor,
                  slave.permission,
                  (const char *) 0, 0,
                  packed_master.data(),
                  packed_master.size(), 
                  microseconds(0)
      );
      return test > 0 ? true : false;
    };

    struct sort_authorization_by_name{
      inline bool operator() (const eosiosystem::permission_level_weight& plw1, const eosiosystem::permission_level_weight& plw2){
        return (plw1.permission.actor < plw2.permission.actor);
      }
    }; 

    template <typename T>
    void cleanTable(name code, uint64_t account, const uint32_t batchSize){
      T db(code, account);
      uint32_t counter = 0;
      auto itr = db.begin();
      while(itr != db.end() && counter++ < batchSize) {
          itr = db.erase(itr);
      }
    }
    checksum256 get_trx_id(){
      auto size = transaction_size();
      char* buffer = (char*)( 512 < size ? malloc(size) : alloca(size) );
      uint32_t read = read_transaction( buffer, size );
      check( size == read, "ERR::READ_TRANSACTION_FAILED::read_transaction failed");
      checksum256 trx_id = sha256(buffer, read);
      return trx_id;
    }

    struct  hookmanager{

      hookmanager(eosio::name hooked_action, eosio::name self_)  { 

        modules_table _modules(self_, self_.value);
        auto mod_itr = _modules.find(eosio::name("hooks").value );
        if(mod_itr != _modules.end() ){
        
          eosio::name hooks_contract = mod_itr->slave_permission.actor;
          actionhooks_table _actionhooks(hooks_contract, hooks_contract.value);
          auto by_hook = _actionhooks.get_index<"byhook"_n>();
          uint128_t composite_id = (uint128_t{hooked_action.value} << 64) | self_.value;
          auto hook_itr = by_hook.find(composite_id);
          if(hook_itr != by_hook.end() ){
            if(hook_itr->enabled){
              eosio::action(
                  eosio::permission_level{ self_, "owner"_n },
                  hooks_contract,
                  hook_itr->hook_action_name,
                  std::make_tuple( hooked_action )
              ).send();
            }
          }
        }
      }

    };


};


