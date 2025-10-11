#[test]
fn schnorr() {
    use curve25519_dalek::Scalar;
    use sha2::{Sha256, Digest};
    use crate::crypto::{random_b32, TrxGenerators};
    use crate::crypto::schnorr::SchnorrProof;

    let gens = TrxGenerators::new("zk_schnorr", 1);
    let mut proof = SchnorrProof::default();
    
    let x = Scalar::from(42u64);
    let blinder = Scalar::from_bytes_mod_order(random_b32());
    let commit = gens.pedersen.commit(x, blinder);

    let mut hash = [0u8; 32];
    hash.copy_from_slice(Sha256::digest(b"fake_trx_data").as_slice());
    
    proof.generate(&gens, x, blinder, &hash);
    assert!(proof.verify(&gens, &commit, &hash));
}
