#[test]
fn schnorr() {
    use curve25519_dalek::Scalar;
    use crate::crypto::{random_b32, TrxGenerators};
    use crate::crypto::schnorr::SchnorrProof;

    let gens = TrxGenerators::new("zk_schnorr", 1);
    let mut proof = SchnorrProof::default();
    
    let x = Scalar::from(42u64);
    let blinder = Scalar::from_bytes_mod_order(random_b32());
    let commit = gens.pedersen.commit(x, blinder).compress();

    let mut hasher = blake3::Hasher::new();
    hasher.update(b"fake_trx_data");
    let hash = hasher.finalize();
    
    proof.generate(&gens, x, blinder, hash.as_bytes());
    assert!(proof.verify(&gens, &commit, hash.as_bytes()));
}
