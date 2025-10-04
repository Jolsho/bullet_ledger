use std::fs;
use std::time::Instant;

mod trxs;
mod accounts;
mod utils;
mod db;
mod stream;

fn main() {
    println!("TRANSACTION LENGTH: {} bytes", trxs::TRX_LENGTH);
    println!("PROOF LENGTH: {} bytes", trxs::PROOF_LENGTH);
    let ledger = db::start_db().unwrap();
    let p = utils::Params::new("custom_zkp", 1);



    // ==================================================
    // Initialize some balances to work with
    // ==================================================

    // initialize sender account
    let sender_seed = utils::random_b32();
    let (sender, sender_pre, sender_priv) = accounts::keypair_from_seed(&sender_seed);
    let (sender_init_s, sender_init_c) = trxs::value_commit(&p, 42);
    db::initialize_account(&ledger, &sender, &sender_init_c).unwrap();

    // initialize receiver account
    let rec_seed = utils::random_b32();
    let (receiver, receiver_pre, receiver_priv) = accounts::keypair_from_seed(&rec_seed);
    let (receiver_init_s, receiver_init_c) = trxs::value_commit(&p, 0);
    db::initialize_account(&ledger, &receiver, &receiver_init_c).unwrap();



    // ==================================================
    // generate commits and proofs and build trx
    // ==================================================
    let start = Instant::now();

    // sender and receiver share the same delta commit
    let (delta_s, delta_c) = trxs::value_commit(&p, 2);

    // start with the basics
    let mut trx = trxs::Trx::new();
    trx.set_delta_commit(&delta_c);
    trx.set_sender(&sender);
    trx.set_receiver(&receiver);

    // both also commit to own final balance and generate proof
    let mut sender_receipt = trxs::state_transition_sender(
        &p, sender_init_s, &delta_s
    ).unwrap();
    assert_eq!(sender_receipt.secrets.x, 40);
    let mut receiver_receipt = trxs::state_transition_receiver(
        &p, receiver_init_s, &delta_s
    ).unwrap();
    assert_eq!(receiver_receipt.secrets.x, 2);

    // Copy values from receipt into TRX
    trx.copy_sender_receipt(&sender, &mut sender_receipt);
    trx.copy_receiver_receipt(&receiver, &mut receiver_receipt);

    // Make sure everything is written to internal buffer
    trx.to_bytes();

    // both sign transaction for testing
    // in practice sender would sign and send to receiver to submit
    trx.sign_sender(&sender_priv, &sender_pre);
    trx.sign_receiver(&receiver_priv, &receiver_pre);

    println!("Generation 2x(time estimate): {:.3?}", start.elapsed());



    // ==================================================
    // trx_bytes WOULD BE gossiped through network 
    // for purpose of testing just rebuild and validate
    // ==================================================
    let start = Instant::now();

    // reconstruct trx
    trx.from_bytes().unwrap();

    // validate Trx signatures
    assert!(trx.verify_signatures());

    // get account balance from local db & validate state transition
    let sender_balance = db::get_balance(&ledger, &sender).unwrap();
    let receiver_balance = db::get_balance(&ledger, &receiver).unwrap();
    assert!(trxs::validate_trx(&p, &mut trx, &sender_balance, &receiver_balance));

    // update local balance
    db::update_balance(&ledger, &sender, &trx.sender_commit).unwrap();
    db::update_balance(&ledger, &receiver, &trx.receiver_commit).unwrap();

    println!("Validation (time estimate): {:.3?}", start.elapsed());
    let _ = fs::remove_file("ledger.sqlite3");
}

