// SPDX-License-Identifier: GPL-2.0-only


#[test]
fn ephemeral() {
    use crate::tests::TestFile;
    use crate::core::ledger::Ledger;
    use crate::crypto;
    use crate::trxs::{
        PROOF_LENGTH, RECEIVER, SENDER, TRX_LENGTH,
        ephemeral::{EphemeralTrx, TOTAL_TRX_PROOF},
        hidden_value_commit, visible_value_commit,
    };
    use std::time::Instant;

    println!("RANGE PROOF:   {} bytes", PROOF_LENGTH);
    println!("COMMITMENTS:   {} bytes", TRX_LENGTH - PROOF_LENGTH);
    println!("SCHNORRS:      {} bytes", 96 * 2);
    println!("TOTAL TRX:     {} bytes", TOTAL_TRX_PROOF);

    let test_file = TestFile::new("ledger_ephemeral");

    let mut ledger = Ledger::new(&test_file.path, 20).unwrap();
    let gens = crypto::TrxGenerators::new("custom_zkp", 1);

    // ==================================================
    // Initialize some balances to work with
    // ==================================================

    // initialize sender account
    let x = 42u64;
    let sender = hidden_value_commit(&gens, x);
    ledger
        .put(sender.commit.as_bytes(), sender.commit.as_bytes().to_vec())
        .expect("SENDER INIT PUT FAILED");

    // initialize receiver account
    let receiver = hidden_value_commit(&gens, 0u64);
    ledger
        .put(receiver.commit.as_bytes(), receiver.commit.as_bytes().to_vec())
        .expect("RECEIVER INIT PUT FAILED");

    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // sender and receiver share the same delta commit
    let delta = hidden_value_commit(&gens, 2u64);
    let fee = visible_value_commit(&gens, 2u64);

    let mut trx = EphemeralTrx::new(gens.tag);

    // both also commit to own final balance and generate proof
    let new_sender = trx
        .state_transition(SENDER, &gens, &sender, &delta, fee.clone())
        .unwrap();
    assert_eq!(new_sender.val, 38u64);

    // In reality you do this as sender and send to reciever to fullfill
    let new_receiver = trx
        .state_transition(RECEIVER, &gens, &receiver, &delta, fee)
        .unwrap();
    assert_eq!(new_receiver.val, 2u64);

    let mut buffer = Vec::with_capacity(TOTAL_TRX_PROOF);

    trx.schnorr_sender(&gens, &sender, &mut buffer);
    trx.schnorr_receiver(&gens, &receiver, &mut buffer);

    println!("Generation (time estimate): {:.3?}", start.elapsed());

    // ==================================================
    // trx_bytes WOULD BE gossiped through network
    // for purpose of testing just rebuild and validate
    // ==================================================
    let start = Instant::now();

    // reconstruct trx
    trx.unmarshal(&mut buffer).unwrap();

    // Validate it
    let res = trx.is_valid(&gens);
    assert!(res.is_ok());
    let (sender_final, receiver_final) = res.unwrap();

    // remove old balances
    ledger.remove(trx.sender_init.as_bytes())
        .expect("SENDER INIT REMOVE FAILED");

    ledger.remove(trx.receiver_init.as_bytes())
        .expect("RECEIVER INIT REMOVE FAILED");

    // Put new balances
    ledger
        .put(sender_final.as_bytes(), sender_final.as_bytes().to_vec())
        .expect("SENDER FINAL PUT FAILED");
    ledger
        .put(
            receiver_final.as_bytes(),
            receiver_final.as_bytes().to_vec(),
        )
        .expect("RECEIVER FINAL PUT FAILED");

    println!("Validation (time estimate): {:.3?}", start.elapsed());
}
