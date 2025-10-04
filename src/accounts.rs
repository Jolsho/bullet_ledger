use curve25519_dalek::constants::ED25519_BASEPOINT_TABLE;
use curve25519_dalek::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek::scalar::Scalar;
use sha2::{Digest, Sha512};

/// Helper: decode a compressed point (returns Option<EdwardsPoint>)
fn decode_point(bytes: &[u8; 32]) -> Option<EdwardsPoint> {
    return match CompressedEdwardsY::from_slice(bytes) {
        Ok(res) => res.decompress(),
        _ => None
    }
}

fn clamp_scalar_bytes(mut b: [u8; 32]) -> [u8; 32] {
    // RFC8032 clamping for the secret scalar:
    // clear lowest 3 bits, clear highest bit, set second-highest bit
    b[0]  &= 248;
    b[31] &= 127;
    b[31] |= 64;
    b
}

pub fn keypair_from_seed(seed: &[u8; 32]) -> (
    [u8; 32] /*pubkey*/, 
    [u8; 32] /*expanded_secret (prefix)*/ , 
    Scalar /*secret scalar a*/
) {
    // 1) H = SHA512(seed)
    let mut hasher = Sha512::default();
    hasher.update(seed);
    let digest = hasher.finalize(); // 64 bytes

    let mut digest_bytes = [0u8; 64];
    digest_bytes.copy_from_slice(&digest);

    // 2) a_bytes = digest[0..32] clamped -> secret scalar 'a'
    let mut a_bytes = [0u8; 32];
    a_bytes.copy_from_slice(&digest_bytes[0..32]);
    let clamped = clamp_scalar_bytes(a_bytes);

    // scalar a
    let a = Scalar::from_bytes_mod_order(clamped);

    // 3) A = a * B (public key point)
    let a_b: EdwardsPoint = ED25519_BASEPOINT_TABLE * &a;
    let pubkey_bytes = a_b.compress().to_bytes();

    // 4) prefix (expanded secret, used when creating r): digest[32..64]
    let mut prefix = [0u8; 32];
    prefix.copy_from_slice(&digest_bytes[32..64]);

    (pubkey_bytes, prefix, a)
}

/// Sign message `m` with seed (32-byte secret seed). Returns 64-byte signature (R||s)
pub fn sign(a: &Scalar, prefix: &[u8; 32], message: &[u8]) -> [u8; 64] {
    // Build keypair pieces
    let a_b: EdwardsPoint = ED25519_BASEPOINT_TABLE * &a;
    let pubkey_bytes = a_b.compress().to_bytes();

    // Step 1: compute r = SHA512(prefix || message) reduced mod L
    let mut h = Sha512::new();
    h.update(&prefix);
    h.update(message);
    let r_digest = h.finalize();
    let mut r_digest_bytes = [0u8; 64];
    r_digest_bytes.copy_from_slice(&r_digest);
    let r = Scalar::from_bytes_mod_order_wide(&r_digest_bytes);

    // Step 2: R = r * B
    let r_b = ED25519_BASEPOINT_TABLE * &r;
    let r_bytes = r_b.compress().to_bytes();

    // Step 3: compute h = SHA512(encode(R) || encode(A) || M) reduced mod L
    let mut h2 = Sha512::new();
    h2.update(&r_bytes);
    h2.update(&pubkey_bytes);
    h2.update(message);
    let h2_digest = h2.finalize();
    let mut h2_bytes = [0u8; 64];
    h2_bytes.copy_from_slice(&h2_digest);
    let h_scalar = Scalar::from_bytes_mod_order_wide(&h2_bytes);

    // Step 4: s = (r + h * a) mod L
    let s = &r + &(&h_scalar * a);
    let s_bytes = s.to_bytes();

    // Signature = R(32) || s(32)
    let mut sig = [0u8; 64];
    sig[..32].copy_from_slice(&r_bytes);
    sig[32..].copy_from_slice(&s_bytes);
    sig
}

/// Verify a signature. Returns true if valid.
pub fn verify(pubkey_bytes: &[u8; 32], message: &[u8], signature: &[u8]) -> bool {
    // Split signature
    let mut r_bytes = [0u8; 32];
    let mut s_bytes = [0u8; 32];
    r_bytes.copy_from_slice(&signature[..32]);
    s_bytes.copy_from_slice(&signature[32..]);

    // Decode R and A
    let r_point_opt = decode_point(&r_bytes);
    let a_point_opt = decode_point(pubkey_bytes);

    if r_point_opt.is_none() || a_point_opt.is_none() {
        return false;
    }
    let r_point = r_point_opt.unwrap();
    let a_point = a_point_opt.unwrap();

    // s must be reduced mod l; if s >= l then invalid. But Scalar::from_canonical_bytes returns Option.
    let s_scalar_opt = Scalar::from_canonical_bytes(s_bytes);
    if s_scalar_opt.is_none().into() { return false; }
    let s_scalar = s_scalar_opt.unwrap();

    // Compute h = SHA512(encode(R) || encode(A) || M) reduced mod L
    let mut h = Sha512::new();
    h.update(&r_bytes);
    h.update(pubkey_bytes);
    h.update(message);
    let h_digest = h.finalize();
    let mut h_bytes = [0u8; 64];
    h_bytes.copy_from_slice(&h_digest);
    let h_scalar = Scalar::from_bytes_mod_order_wide(&h_bytes);

    // Check: s*B == R + h*A
    let s_b = ED25519_BASEPOINT_TABLE * &s_scalar;       // s * B
    let h_a = &a_point * &h_scalar;                      // h * A
    let rhs = r_point + h_a;

    s_b == rhs
}
