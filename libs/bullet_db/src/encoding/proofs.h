#pragma once
#include <optional>
#include <tuple>
#include "points.h"
#include "scalars.h"
#include "verkle.h"

size_t calculate_proof_size(
    Commitment &C, Proof &Pi,
    std::vector<Commitment> &Ws,
    std::vector<scalar_vec> &Ys,
    scalar_vec &Zs
);

void marshal_existence_proof(
    byte* beginning,
    Commitment C, Proof Pi,
    std::vector<Commitment> Ws,
    std::vector<scalar_vec> Ys,
    scalar_vec Zs
);

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<scalar_vec>, 
    Bitmap<32>
>> unmarshal_existence_proof(
    const byte* beginning, 
    size_t size
);
