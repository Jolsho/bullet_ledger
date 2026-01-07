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
fn hidden() {
    use ed25519_dalek::SigningKey;
    use curve25519_dalek::ristretto::CompressedRistretto;
    use crate::utils::random::random_b32; 
    use crate::tests::TestFile;
    use crate::blockchain::ledger::Ledger;
    use crate::blockchain::schnorr::TrxGenerators;
    use crate::blockchain::trxs::{
        PROOF_LENGTH, RECEIVER, SENDER, TRX_LENGTH,
        hidden_value_commit, visible_value_commit,
        hidden::{HiddenTrx, TOTAL_TRX_PROOF},
    };
    use std::time::Instant;

    println!("RANGE PROOF:   {} bytes", PROOF_LENGTH);
    println!("COMMITMENTS:   {} bytes", TRX_LENGTH - PROOF_LENGTH);
    println!("SIGNATURES:    {} bytes", 64 * 2);
    println!("TOTAL TRX:     {} bytes", TOTAL_TRX_PROOF);

    let test_file = TestFile::new("ledger_hidden");

    let ledger = Ledger::open(
        "assets/fake_db", 32, 
        10 * 1024 * 1024, 
        "fake_tag", None
    ).unwrap();
    let gens = TrxGenerators::new("custom_zkp", 1);

    // ==================================================
    // Initialize some balances to work with
    // ==================================================
    
    // initialize sender account
    let mut sender_priv = SigningKey::from(random_b32());
    let sender_pub = sender_priv.verifying_key();

    let x = 42u64;
    let sender_balance = hidden_value_commit(&gens, x);
    ledger.db_put(sender_pub.as_bytes(), sender_balance.commit.as_bytes())
        .expect("SENDER INIT PUT FAILED");

    // initialize receiver account
    let mut receiver_priv = SigningKey::from(random_b32());
    let receiver_pub = receiver_priv.verifying_key();

    let receiver_balance = hidden_value_commit(&gens, 0u64);
    ledger.db_put(receiver_pub.as_bytes(), receiver_balance.commit.as_bytes())
        .expect("RECEIVER INIT PUT FAILED");


    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // sender and receiver share the same delta commit
    let delta = hidden_value_commit(&gens, 2u64);
    let fee = visible_value_commit(&gens, 2u64);

    // new trx struct with addresses
    let mut trx = HiddenTrx::new(gens.tag);
    trx.set_addr(sender_pub, receiver_pub);

    // both also commit to own final balance and generate proof
    let new_sender = trx.state_transition(
        SENDER, &gens, &sender_balance, &delta, fee.clone()
    ).unwrap();
    assert_eq!(new_sender.val, 38u64);

    // In reality you do this as sender and send to reciever to fullfill
    let new_receiver = trx.state_transition(
        RECEIVER, &gens, &receiver_balance, &delta, fee
    ).unwrap();
    assert_eq!(new_receiver.val, 2u64);

    let mut buffer = Vec::with_capacity(TOTAL_TRX_PROOF);

    trx.sender_sign(&mut sender_priv, &mut buffer);
    trx.receiver_sign(&mut receiver_priv, &mut buffer);

    println!("Generation (time estimate): {:.3?}", start.elapsed());

    // ==================================================
    // trx_bytes WOULD BE gossiped through network
    // for purpose of testing just rebuild and validate
    // ==================================================
    let start = Instant::now();

    // reconstruct trx
    trx.unmarshal(&mut buffer).unwrap();

    // get initial balances
    let sender_init = CompressedRistretto::from_slice(
        &ledger.db_get(sender_pub.as_bytes()).unwrap()
    ).unwrap();

    let receiver_init = CompressedRistretto::from_slice(
        &ledger.db_get(receiver_pub.as_bytes()).unwrap()
    ).unwrap();

    // Validate it
    let res = trx.is_valid(&gens, sender_init, receiver_init);
    assert!(res.is_ok());
    let (sender_final, receiver_final) = res.unwrap();

    // Put new balances
    ledger
        .db_put(sender_pub.as_bytes(), sender_final.as_bytes())
        .expect("SENDER FINAL PUT FAILED");
    ledger
        .db_put(receiver_pub.as_bytes(), receiver_final.as_bytes())
        .expect("RECEIVER FINAL PUT FAILED");

    println!("Validation (time estimate): {:.3?}", start.elapsed());
}
