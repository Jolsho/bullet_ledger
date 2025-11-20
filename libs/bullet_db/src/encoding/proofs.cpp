#include <cstdint>
#include <optional>
#include <tuple>
#include "points.h"
#include "scalars.h"

size_t calculate_proof_size(
    Commitment &C, Proof &Pi,
    std::vector<Commitment> &Ws,
    std::vector<scalar_vec> &Ys,
    scalar_vec &Zs
) {
    return 48 + 48 + (48 * Ws.size()) 
        + (32 * Ys.size() * Zs.size()) 
        + (32 * Zs.size());
}

void marshal_existence_proof(
    byte* beginning,
    Commitment C, Proof Pi,
    std::vector<Commitment> Ws,
    std::vector<scalar_vec> Ys,
    scalar_vec Zs
) {
    byte* cursor = beginning;

    blst_p1_compress(cursor, &C);
    cursor += 48;

    Commitment Pi_p1 = p1_from_affine(Pi);
    blst_p1_compress(cursor, &Pi_p1);
    cursor += 48;


    *cursor = static_cast<byte>(Ws.size());
    cursor++;
    for (auto &w: Ws) {
        blst_p1_compress(cursor, &w);
        cursor += 48;
    }

    *cursor = static_cast<byte>(Zs.size());
    for (auto &z: Zs) 
        for (auto &b: z.b) 
            *cursor = b;
            cursor++;

    for (auto &r: Ys) 
        for (auto &Y: r) 
            for (auto &b: Y.b) 
                *cursor = b;
                cursor++;
}

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<scalar_vec>, 
    scalar_vec
>> unmarshal_existence_proof(const byte* beginning, size_t size) {

    const byte* cursor = beginning;

    if (size < 96) return std::nullopt;
    size -= 96;

    Commitment C;
    p1_from_bytes(cursor, &C);
    cursor += 48;

    Commitment Pi_p1;
    p1_from_bytes(cursor, &Pi_p1);
    Proof Pi = p1_to_affine(Pi_p1);
    cursor += 48;

    uint8_t ws_len = *cursor;
    if (size < ws_len * 48) return std::nullopt;
    size -= (ws_len * 48);
    cursor++;

    std::vector<Commitment> Ws(ws_len, new_p1()); 
    for (auto &w: Ws) {
        p1_from_bytes(cursor, &w);
        cursor += 48;
    }

    uint8_t zs_len = *cursor;
    if (size < ((zs_len * 32) + (32 * ws_len * zs_len))) 
        return std::nullopt;
    cursor++;

    scalar_vec Zs(zs_len, new_scalar());
    for (auto &z: Zs) {
        blst_scalar_from_le_bytes(&z, cursor, 32);
        cursor += 32;
    }

    std::vector<scalar_vec> Ys(
        Ws.size(), scalar_vec(Zs.size(), new_scalar())
    );
    for (auto &Y_row: Ys) {
        for (auto &y: Y_row) {
            blst_scalar_from_le_bytes(&y, cursor, 32);
            cursor += 32;
        }
    }

    return std::make_tuple(C, Pi, Ws, Ys, Zs);
}
