use std::os::fd::RawFd;
use std::{thread::JoinHandle};
use nix::{poll::PollTimeout, sys::epoll::EpollEvent};
use ringbuf::traits::Producer;

use crate::config::CoreConfig;
use crate::core::consensus::Consensus;

use super::msging::{MsgCons, MsgProd, Poller, RingProd};
use super::{networker::netman::NetMsg, NETWORKER};
use super::trxs::Trx;
use pool::TrxPool;
use msg::{CoreMsg, handle_from_net};

pub mod pool;
pub mod consensus;
pub mod execution;
pub mod msg;


pub fn start_core(
    config: CoreConfig,
    mut trx_prod: RingProd<Trx>, 
    mut from_net: MsgCons<CoreMsg>,
    mut _to_net: MsgProd<NetMsg>,
) -> JoinHandle<()> {
    let mut epoll = Poller::new().unwrap();
    epoll.listen_to(&from_net).unwrap();
    std::thread::spawn(move || {

        // Initialize some trxs
        for _ in 0..config.init_trx_len {
            let _ = trx_prod.try_push(Box::new(Trx::default()));
        }
        // Initialize the trx pool
        let mut t_pool = TrxPool::new(config.pool_cap, trx_prod);
        let mut consensus = Consensus::new();

        // Start polling
        let mut events = vec![EpollEvent::empty(); config.event_len];
        loop {
            let n = epoll.wait(&mut events, PollTimeout::from(config.idle_polltimeout));
            if n.is_err() { continue }

            for ev in &events[..n.unwrap()] {
                match ev.data() as RawFd {
                    NETWORKER => { // Msg from Networker
                        if let Some(m) = from_net.pop() {
                            handle_from_net(
                                &mut t_pool, 
                                &mut consensus, 
                                *m,
                            );
                        }
                    }
                    _ => {}
                }
            }
        }
    })
}


/*
*   Engine? NOT pool? => 
*       Engine entails validation if need be.
*       Execution client...
*           Validates Trxs as they come in...
*           Processes Commands from Consensus
*               like remove batch...
*           Must update state in DB
*
*   Consensus => Own Message Types
*       Needs to communicate with DB for block history...
*           Maybe... probably not
*
*/



