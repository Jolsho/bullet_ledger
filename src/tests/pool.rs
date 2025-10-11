#[test]
fn pool() {
    use ringbuf::traits::{Split, Consumer, Producer};
    use crate::msging::Ring;
    use crate::core::{pool::{Hash, TrxPool}};
    use crate::trxs::Trx;
    let length = 5;


    let trxs = Ring::new(length);
    let (mut prod, mut cons) = trxs.split();
    for _ in 0..length {
        let _ = prod.try_push(Box::new(Trx::default()));
    }

    let mut keys = Vec::<Hash>::with_capacity(length);
    for _ in 0..length {
        let _ = keys.push(Hash::ZERO);
    }

    let mut t_pool = TrxPool::new(length, prod);
    let mut inserted: Vec<Hash> = Vec::with_capacity(3);

    for i in 0..length {
        let mut t = cons.try_pop().unwrap();
        t.hash[0] = 69 + i as u8;
        t.fee_value = i as u64;

        if i != 0 {
            let mut k = keys.pop().unwrap();
            k.copy_from(&t.hash);
            inserted.push(k);
        }
        
        t_pool.insert(t);
    }
    assert_eq!(t_pool.length(), 5);

    let h = t_pool.get_head();
    assert_eq!(h.unwrap().fee_value, 4);

    t_pool.remove_batch(&inserted);
    assert_eq!(t_pool.length(), 1);

    let h = t_pool.get_head();
    assert_eq!(h.unwrap().fee_value, 0);
}
