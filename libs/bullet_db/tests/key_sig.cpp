#include <cassert>
#include "../src/utils/utils.h"

void test_single_key_sig() {
    bytes32 seed = gen_rand_32();
    key_pair keys = gen_key_pair("bullet_ledger", seed);

    auto [msg, msg_len] = str_to_bytes("the_message");
    auto [dst, dst_len] = str_to_bytes("bullet_ledger");

    blst_p2 hash;
    blst_hash_to_g2(&hash, msg, msg_len, dst, dst_len);

    blst_p2 raw_sig;
    blst_sign_pk_in_g1(&raw_sig, &hash, &keys.sk);

    assert(verify_sig(
        keys.pk, raw_sig, 
        msg, msg_len,
        dst, dst_len
    ));
    printf("SIGNATURE VALIDATED. \n");
    printf("\n");
}

void test_many_key_sig() {
    auto [tag, tag_len] = str_to_bytes("bullet_ledger");
    auto [msg, msg_len] = str_to_bytes("msg");

    blst_p2 hash;
    blst_hash_to_g2(&hash, msg, msg_len, tag, tag_len);

    blst_p2 agg_sig = new_p2();

    std::vector<blst_p1> pks(5);
    for (size_t i = 0; i < 5; i++) {

        bytes32 seed = gen_rand_32();
        key_pair keys = gen_key_pair("bullet_ledger", seed);

        blst_p2 tmp_sig = new_p2();
        blst_sign_pk_in_g1(&tmp_sig, &hash, &keys.sk);

        blst_p2_add_or_double(&agg_sig, &agg_sig, &tmp_sig);
        pks.push_back(keys.pk);
    }

    assert(verify_aggregate_signature(pks, agg_sig, msg, msg_len, tag, tag_len));
    printf("AGGREGATE_SIGNATURE VALIDATED. \n");
    printf("\n");
}

void main_key_sig() {
    printf("TESTING ONE KEY & SIGNATURE \n");
    test_single_key_sig();

    printf("\n");

    printf("TESTING MANY KEYS & SIGNATURES \n");
    test_many_key_sig();

    printf("=====================================\n");
}
