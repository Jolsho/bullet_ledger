// SPDX-License-Identifier: GPL-2.0-only

#[test]
fn ledger() {
    use rand::RngCore;
    use std::time::Instant;
    use crate::{core::ledger::Ledger, tests::TestFile};

    let test_file = TestFile::new("ledger");
    let mut ledger = Ledger::new(&test_file.path, 128).unwrap();

    let mut raw_hashes = Vec::with_capacity(100);
    let mut rng: rand::rngs::StdRng = rand::SeedableRng::from_seed([0u8; 32]);
    while raw_hashes.len() < raw_hashes.capacity() {
        let mut hash = [0u8; 32];
        rng.fill_bytes(&mut hash);
        raw_hashes.push(hash);
    }

    // --- Timing accumulators ---
    let (mut total_virtual_put, mut total_put, mut total_get, mut total_remove) =
        (0u128, 0u128, 0u128, 0u128);

    // --- Insert phase ---
    for (i, value) in raw_hashes.iter().enumerate() {
        let vec = value.to_vec();

        let start = Instant::now();
        let virt_root = ledger.virtual_put(value, vec.clone()).unwrap();
        total_virtual_put += start.elapsed().as_nanos();

        let start = Instant::now();
        let root = ledger.put(value, vec).unwrap();
        total_put += start.elapsed().as_nanos();

        assert_eq!(virt_root, root);

        let start = Instant::now();
        let got_value = ledger.get_value(value).unwrap();
        total_get += start.elapsed().as_nanos();

        assert_eq!(got_value, value);

        if i % 1000 == 0 {
            println!("Progress: inserted {i} entries...");
        }
    }

    // --- Remove phase ---
    for (i, value) in raw_hashes.iter().enumerate() {
        let start = Instant::now();
        ledger.remove(value).unwrap();
        total_remove += start.elapsed().as_nanos();

        assert_eq!(ledger.get_value(value), None);

        if i % 1000 == 0 {
            println!("Removed {i} entries...");
        }
    }

    // --- Print averages ---
    let n = raw_hashes.len() as u128;
    println!("\n--- Ledger Operation Averages ({} ops) ---", n);
    println!("virtual_put: {:.3} µs", total_virtual_put as f64 / n as f64 / 1000.0);
    println!("put:         {:.3} µs", total_put as f64 / n as f64 / 1000.0);
    println!("get_value:   {:.3} µs", total_get as f64 / n as f64 / 1000.0);
    println!("remove:      {:.3} µs", total_remove as f64 / n as f64 / 1000.0);
}

