#[test]
fn trxx() {
    use std::fs;
    use std::time::Instant;
    use crate::{db, trxs, crypto};
    use crate::trxs::{RECEIVER, SENDER};
    use crate::trxs::Trx;

    println!("RANGE PROOF:   {} bytes", trxs::PROOF_LENGTH);
    println!("COMMITMENTS:   {} bytes", trxs::TRX_LENGTH - trxs::PROOF_LENGTH);
    println!("SCHNORRS:      {} bytes", 96 * 2);
    println!("TOTAL TRX:     {} bytes", trxs::TOTAL_TRX_PROOF);
    let ledger = db::start_db().unwrap();
    let gens = crypto::TrxGenerators::new("custom_zkp", 1);



    // ==================================================
    // Initialize some balances to work with
    // ==================================================

    // initialize sender account
    let x = 42u64;
    let sender = trxs::hidden_value_commit(&gens, x);
    db::initialize_account(&ledger, &sender.commit).unwrap();

    // initialize receiver account
    let receiver = trxs::hidden_value_commit(&gens, 0u64);
    db::initialize_account(&ledger, &receiver.commit).unwrap();



    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // sender and receiver share the same delta commit
    let delta = trxs::hidden_value_commit(&gens, 2u64);
    let fee = trxs::hidden_value_commit(&gens, 2u64);

    let mut trx = Trx::new(gens.tag);

    // both also commit to own final balance and generate proof
    let new_sender = trx.state_transition(
        SENDER, &gens, &sender, &delta, fee.clone()
    ).unwrap();
    assert_eq!(new_sender.val, 38u64);

    // In reality you do this as sender and send to reciever to fullfill
    let new_receiver = trx.state_transition(
        RECEIVER, &gens, &receiver, &delta, fee
    ).unwrap();
    assert_eq!(new_receiver.val, 2u64);

    trx.schnorr_sender(&gens, &sender);
    trx.schnorr_receiver(&gens, &receiver);

    println!("Generation (time estimate): {:.3?}", start.elapsed());



    // ==================================================
    // trx_bytes WOULD BE gossiped through network 
    // for purpose of testing just rebuild and validate
    // ==================================================
    let start = Instant::now();

    // reconstruct trx
    trx.unmarshal_internal().unwrap();

    // Validate it
    assert!(trx.is_valid(&gens));

    // update local balance
    db::update_balance(&ledger, &trx.sender_init, &trx.sender_final).unwrap();
    db::update_balance(&ledger, &trx.receiver_init, &trx.receiver_final).unwrap();

    println!("Validation (time estimate): {:.3?}", start.elapsed());
    let _ = fs::remove_file("ledger.sqlite3");
}

