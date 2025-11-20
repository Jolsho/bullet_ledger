#include <cstdint>
#include <cstring>
#include "proofs.h"
#include "verkle.h"


// TODO --
//      i think we have to commit to remainder path in leaf...
//          use a leaf slot and commit to first 31 bytes of key + 255(uint8_t) constant
//      because otherwise attacker can just create an account with the same prefix
//          you couldnt tell the difference and you dont want that...
//      issue is this does introduce one more point per query...
//          well no... because you can derive the Y, and the zs...
//          and the witness is the same as the leaf...

size_t calculate_proof_size(
    Commitment &C, Proof &Pi,
    std::vector<Commitment> &Ws,
    std::vector<scalar_vec> &Ys,
    Bitmap<32> &Zs
) {
    return 48 + 48 + (48 * Ws.size()) 
        + (32 * Ys.size()) 
        + Zs.BYTE_SIZE
        + (32 * Zs.count());
}

void marshal_existence_proof(
    byte* beginning,
    Commitment C, Proof Pi,
    std::vector<Commitment> Ws,
    std::vector<scalar_vec> Ys,
    Bitmap<32> Zs
) {
    byte* cursor = beginning;

    blst_p1_compress(cursor, &C); cursor += 48;

    Commitment Pi_p1 = p1_from_affine(Pi);
    blst_p1_compress(cursor, &Pi_p1); cursor += 48;


    *cursor = static_cast<uint8_t>(Ws.size()); cursor++;
    for (auto &w: Ws) {
        blst_p1_compress(cursor, &w); cursor += 48;
    }

    *cursor = static_cast<uint8_t>(Zs.count());
    std::memcpy(cursor, Zs.data_ptr(), Zs.BYTE_SIZE);

    for (auto &r: Ys) 
        for (auto &Y: r) 
            for (auto &b: Y.b) 
                *cursor = b; cursor++;
}

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<scalar_vec>, 
    Bitmap<32>
>> unmarshal_existence_proof(
    const byte* beginning, 
    size_t size
) {

    const byte* cursor = beginning;

    if (size < 96) return std::nullopt;
    size -= 96;

    // AGGREGATE COMMITMENT
    Commitment C;
    p1_from_bytes(cursor, &C); cursor += 48;

    // AGGREGATE PROOF
    Commitment Pi_p1;
    p1_from_bytes(cursor, &Pi_p1); cursor += 48;
    Proof Pi = p1_to_affine(Pi_p1);


    // Witnesses LENGTH
    uint8_t ws_len = *cursor; cursor++;
    if (size < ws_len * 48) return std::nullopt;
    size -= (ws_len * 48);

    // Witnesses
    std::vector<Commitment> Ws(ws_len, new_p1()); 
    for (auto &w: Ws) {
        p1_from_bytes(cursor, &w); cursor += 48;
    }

    // Evaluation indices & Evaluation outputs LENGTH
    uint8_t zs_len = *cursor; cursor++;
    if (size != (4 + (32 * ws_len * zs_len)))
        return std::nullopt;

    // Evaluation indices
    Bitmap<32> Zs(cursor);
    cursor += Zs.BYTE_SIZE;

    // Evaluation outputs
    std::vector<scalar_vec> Ys(Ws.size(), scalar_vec(zs_len, new_scalar()));
    for (auto &Y_row: Ys) {
        for (auto &y: Y_row) {
            blst_scalar_from_le_bytes(&y, cursor, 32); 
            cursor += 32;
        }
    }

    return std::make_tuple(C, Pi, Ws, Ys, Zs);
}
