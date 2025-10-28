#[test]
fn regular() {
    use ed25519_dalek::SigningKey;
    use crate::crypto::random_b32;
    use crate::tests::TestFile;
    use crate::core::ledger::Ledger;
    use crate::crypto;
    use crate::trxs::regular::{RegularTrx, TRX_LENGTH, TOTAL_TRX_LENGTH};
    use std::time::Instant;

    println!("TRX LENGTH:    {} bytes", TRX_LENGTH);
    println!("SIGNATURES:    {} bytes", 64 * 2);
    println!("TOTAL TRX:     {} bytes", TOTAL_TRX_LENGTH);

    let test_file = TestFile::new("ledger_regular");

    let mut ledger = Ledger::new(&test_file.path, 20).unwrap();
    let gens = crypto::TrxGenerators::new("custom_zkp", 1);

    // ==================================================
    // Initialize some balances to work with
    // ==================================================
    
    // initialize sender account
    let mut sender_priv = SigningKey::from(random_b32());
    let sender_pub = sender_priv.verifying_key();

    let sender_balance = 42u64;
    ledger.put(sender_pub.as_bytes(), sender_balance.to_le_bytes().to_vec())
        .expect("SENDER INIT PUT FAILED");

    // initialize receiver account
    let mut receiver_priv = SigningKey::from(random_b32());
    let receiver_pub = receiver_priv.verifying_key();

    let receiver_balance = 0u64;
    ledger.put(receiver_pub.as_bytes(), receiver_balance.to_le_bytes().to_vec())
        .expect("RECEIVER INIT PUT FAILED");


    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // new trx struct with addresses
    let mut trx = RegularTrx::new(gens.tag);
    trx.set_addr(sender_pub, receiver_pub);
    trx.set_fee_and_delta(2, 2);

    let mut buffer = Vec::with_capacity(TOTAL_TRX_LENGTH);

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
    let sender_init_raw = ledger.get_value(sender_pub.as_bytes()).unwrap();
    let sender_init = u64::from_le_bytes(sender_init_raw.try_into().unwrap());

    let receiver_init_raw = ledger.get_value(receiver_pub.as_bytes()).unwrap();
    let receiver_init = u64::from_le_bytes(receiver_init_raw.try_into().unwrap());

    // Validate it
    let res = trx.is_valid(sender_init, receiver_init);
    assert!(res.is_ok());
    let (sender_final, receiver_final) = res.unwrap();

    // Put new balances
    ledger.put(sender_pub.as_bytes(), sender_final.to_le_bytes().to_vec())
        .expect("SENDER FINAL PUT FAILED");
    ledger.put(receiver_pub.as_bytes(), receiver_final.to_le_bytes().to_vec())
        .expect("RECEIVER FINAL PUT FAILED");

    println!("Validation (time estimate): {:.3?}", start.elapsed());
}
