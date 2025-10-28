


#[test]
fn ledger() {
    use rand::RngCore;
    use crate::{core::ledger::Ledger, tests::TestFile};

    let test_file = TestFile::new("ledger");

    let mut ledger = Ledger::new(&test_file.path, 20).unwrap();
    let mut raw_hashes = Vec::with_capacity(200);
    let mut rng: rand::rngs::StdRng = rand::SeedableRng::from_seed([0u8;32]);
    while raw_hashes.len() < raw_hashes.capacity() {
        let mut hash = [0u8;32];
        rng.fill_bytes(&mut hash);
        raw_hashes.push(hash);
    }

    for (i, value) in raw_hashes.iter().enumerate() {
        println!(" i{i},");
        // println!(" {}", hex::encode(hash));
        ledger.put(value, value.to_vec()).unwrap();

        let got_value = ledger.get_value(value).unwrap();

        assert_eq!(got_value, value);
    }

    for (i, hash) in raw_hashes.iter().enumerate() {
        println!(" r{i},");
        ledger.remove(hash).unwrap();
        assert_eq!(ledger.get_value(hash), None);
    }
}
