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
fn key_ex() {
    use std::fs;
    use zeroize::Zeroize;
    use chacha20poly1305::aead::{OsRng, AeadMutInPlace};
    use chacha20poly1305::{AeadCore, ChaCha20Poly1305, KeyInit};
    use crate::utils::random::random_b32; 
    use crate::utils::keys::ecdh_shared_secret; 
    use crate::utils::keys::hkdf_derive_key;
    use crate::utils::keys::load_keys;

    // Alice
    let (alice_pk, mut alice_sk) = load_keys("assets/test1.keys").unwrap();

    // Bob
    let (bob_pk, mut bob_sk) = load_keys("assets/test2.keys").unwrap();

    // Compute shared secrets
    let mut alice_shared = ecdh_shared_secret(alice_sk, bob_pk);
    let mut bob_shared   = ecdh_shared_secret(bob_sk, alice_pk);

    alice_sk.zeroize();
    alice_shared.zeroize();

    bob_sk.zeroize();
    bob_shared.zeroize();

    assert_eq!(alice_shared, bob_shared, "ECDH results must match");

    // Derive symmetric key from shared secret
    let info = b"handshake v1";
    let salt = random_b32();
    let mut symmetric_key = hkdf_derive_key(&alice_shared, info, salt);

    println!("Alice public : {:02x?}", alice_pk);
    println!("Bob public   : {:02x?}", bob_pk);
    println!("Shared key   : {:02x?}", symmetric_key);

    let key = chacha20poly1305::Key::from_slice(&symmetric_key);
    
    let raw_msg = b"this is the message";
    let mut msg_buf = raw_msg.to_vec();

    let mut cipher = ChaCha20Poly1305::new(&key);
    let mut nonce = ChaCha20Poly1305::generate_nonce(&mut OsRng);
    let _ = cipher.encrypt_in_place(
        &nonce, &[0u8;0], &mut msg_buf
    ).unwrap();
    let _ = cipher.decrypt_in_place(
        &nonce, &[0u8;0], &mut msg_buf
    ).unwrap();

    assert_eq!(msg_buf, raw_msg);

    symmetric_key.zeroize();
    nonce.zeroize();
    let _ = fs::remove_file("assets/test1.keys".to_string());
    let _ = fs::remove_file("assets/test2.keys".to_string());
}

