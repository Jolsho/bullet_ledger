#[test]
fn pool() {
    use crate::core::utils::{TrxPool, Hash};
    let length = 5;

    let mut t_pool = TrxPool::new(length);
    let mut inserted: Vec<Box<Hash>> = Vec::with_capacity(3);

    for i in 0..length {
        let mut t = t_pool.get_value();
        t.hash[0] = 69 + i as u8;
        t.fee_value = i as u64;

        if i != 0 {
            let mut k = t_pool.get_key();
            k.copy_from_slice(&t.hash[..]);
            inserted.push(k);
        }
        
        t_pool.insert(t);
    }
    assert_eq!(t_pool.len(), 5);

    let trx = t_pool.pop().unwrap();
    assert_eq!(trx.fee_value, 4);

    for key in inserted { 
        t_pool.remove_one(&key); 
        t_pool.recycle_key(key);
    }
    assert_eq!(t_pool.len(), 1);

    let trx = t_pool.pop().unwrap();
    assert_eq!(trx.fee_value, 0);
}
