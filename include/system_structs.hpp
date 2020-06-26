//Authority Structs
namespace eosiosystem {

    struct key_weight {
        eosio::public_key key;
        uint16_t weight;
        EOSLIB_SERIALIZE( key_weight, (key)(weight) )
    };

    struct permission_level_weight {
        eosio::permission_level permission;
        uint16_t weight;
    };

    struct wait_weight {
        uint32_t wait_sec;
        uint16_t weight;
    };

    struct authority {

        uint32_t threshold;
        std::vector<key_weight> keys;
        std::vector<permission_level_weight> accounts;
        std::vector<wait_weight> waits;
    };
}