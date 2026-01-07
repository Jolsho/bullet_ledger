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
fn pool() {
    use crate::blockchain::trxs::{Trx, EPHEMERAL};
    use crate::blockchain::{priority::TrxPool, Hash};
    let length = 5;

    let mut t_pool = TrxPool::new(length);
    let mut inserted: Vec<Box<Hash>> = Vec::with_capacity(3);

    for i in 0..length {
        if let Trx::Ephemeral(mut t) = t_pool.get_value(EPHEMERAL).unwrap() {
            t.hash[0] = 69 + i as u8;
            t.fee_value = i as u64;

            if i != 0 {
                let mut k = t_pool.get_key();
                k.copy_from_slice(&t.hash[..]);
                inserted.push(k);
            }
            
            t_pool.insert(Trx::Ephemeral(t));
        } else {
            panic!("not hidden");
        }
    }
    assert_eq!(t_pool.len(), 5);

    if let Trx::Ephemeral(trx) = t_pool.pop().unwrap() {
        assert_eq!(trx.fee_value, 4);
    } else {
        panic!("not hidden");
    }

    for key in inserted { 
        t_pool.remove_one(&key); 
        t_pool.recycle_key(key);
    }
    assert_eq!(t_pool.len(), 1);

    if let Trx::Ephemeral(trx) = t_pool.pop().unwrap() {
        assert_eq!(trx.fee_value, 0);
    } else {
        panic!("not hidden");
    }
}
