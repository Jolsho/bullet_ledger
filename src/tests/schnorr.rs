/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#[test]
fn schnorr() {
    use curve25519_dalek::Scalar;
    use crate::crypto::{random_b32, TrxGenerators};
    use crate::crypto::schnorr::SchnorrProof;

    let gens = TrxGenerators::new("zk_schnorr", 1);
    let mut proof = SchnorrProof::default();
    
    let x = Scalar::from(42u64);
    let blinder = Scalar::from_bytes_mod_order(random_b32());
    let commit = gens.pedersen.commit(x, blinder).compress();

    let mut hasher = blake3::Hasher::new();
    hasher.update(b"fake_trx_data");
    let hash = hasher.finalize();
    
    proof.generate(&gens, x, blinder, hash.as_bytes());
    assert!(proof.verify(&gens, &commit, hash.as_bytes()));
}
