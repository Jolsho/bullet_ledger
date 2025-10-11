use crate::{core::{consensus::Consensus, pool::{Hash, TrxPool}}, trxs::Trx};

pub enum CoreMsg {
    NONE,
    NewTrx(Box<Trx>),
    NewBlock(Vec<Hash>),
}

impl Default for CoreMsg {
    fn default() -> Self { 
        Self::NONE
    }
}

pub fn handle_from_net(
    pool: &mut TrxPool, 
    _consensus: &mut Consensus,
    m: CoreMsg
) {
    match m {
        CoreMsg::NewTrx(trx) => pool.insert(trx),
        CoreMsg::NewBlock(keys) => {
            /*  
            *   TODO
            *   for each key -> get TRX
            *       validate & update balances
            */
            pool.remove_batch(&keys)
        },
        _ => {}
    }
}
