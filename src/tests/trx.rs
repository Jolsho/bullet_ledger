#[test]
fn trx() {
    use crate::trxs::TOTAL_TRX_PROOF;
    use std::fs;
    use std::time::Instant;
    use crate::{trxs, crypto};
    use crate::trxs::{RECEIVER, SENDER, Trx};
    use crate::core::db::Ledger;

    println!("RANGE PROOF:   {} bytes", trxs::PROOF_LENGTH);
    println!("COMMITMENTS:   {} bytes", trxs::TRX_LENGTH - trxs::PROOF_LENGTH);
    println!("SCHNORRS:      {} bytes", 96 * 2);
    println!("TOTAL TRX:     {} bytes", trxs::TOTAL_TRX_PROOF);

    let ledger = Ledger::new("ledger.sqlit3".to_string()).unwrap();
    let gens = crypto::TrxGenerators::new("custom_zkp", 1);


    // ==================================================
    // Initialize some balances to work with
    // ==================================================

    // initialize sender account
    let x = 42u64;
    let sender = trxs::hidden_value_commit(&gens, x);
    assert_eq!(ledger.initialize_balance(&sender.commit).unwrap(), 1);

    // initialize receiver account
    let receiver = trxs::hidden_value_commit(&gens, 0u64);
    assert_eq!(ledger.initialize_balance(&receiver.commit).unwrap(), 1);



    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // sender and receiver share the same delta commit
    let delta = trxs::hidden_value_commit(&gens, 2u64);
    let fee = trxs::visible_value_commit(&gens, 2u64);

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
    assert!(trx.is_valid(&gens));

    // update local balance
    assert_eq!(ledger.update_balance(&trx.sender_init, &trx.sender_final).unwrap(), 1);
    assert_eq!(ledger.update_balance(&trx.receiver_init, &trx.receiver_final).unwrap(), 1);

    println!("Validation (time estimate): {:.3?}", start.elapsed());
    let _ = fs::remove_file("ledger.sqlit3");
}

