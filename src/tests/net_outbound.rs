use std::time::Duration;

#[test]
fn net_outbound() {
    /*
    *   TODO
    *   
    *
    * Start two servers...
    *   Ask one of them to contact the other...
    *   Just start with basic ping/pong
    *
    *   Maybe test some errors... like when one times out...
    *   Or some other kind of error...
    *       make sure the other one can notice, and clean up the dead one
    *
    */
    use ringbuf::traits::Split;

    use crate::config::load_config;
    use crate::core::msg::CoreMsg;
    use crate::msging::{MsgQ, Ring};
    use crate::networker::{start_networker, netman::NetMsg};
    use crate::{trxs::Trx, NETWORKER, CORE};
    use crate::shutdown;

    fn get_stuff() -> (Ring<Trx>, MsgQ<CoreMsg>,MsgQ<NetMsg>) {
        let trx = Ring::<Trx>::new(64);
        let ntc = MsgQ::<CoreMsg>::new(32, NETWORKER).unwrap();
        let ctn = MsgQ::<NetMsg>::new(32, CORE).unwrap();
        (trx, ntc, ctn)
    }

    let mut config = load_config("config.toml");

    // -- setup1 (receiver) ----------------------------------------------------------------
    let (trx,n_to_c, c_to_n) = get_stuff();
    let (_trx_prod, trx_con) = trx.split();
    let (to_c, _from_n) = n_to_c.split().unwrap();
    let (_to_n, from_c) = c_to_n.split().unwrap();

    let cfg = config.network.clone();
    let net_handle1 = start_networker(cfg, trx_con, to_c, from_c);

    // -- setup2 (sender)----------------------------------------------------------------
    let remote = config.network.bind_addr;
    config.network.bind_addr = "127.0.0.1:4242".to_string();

    let (trx,n_to_c, c_to_n) = get_stuff();
    let (trx_prod, trx_con) = trx.split();
    let (to_c, from_n) = n_to_c.split().unwrap();
    let (mut to_n, from_c) = c_to_n.split().unwrap();

    let cfg = config.network.clone();
    let net_handle2 = start_networker(cfg, trx_con, to_c, from_c);

    // -- send da ting ----------------------------------------------------------------
    let mut msg = NetMsg::default();
    msg.addr = Some(remote.parse().unwrap());
    msg.code = crate::networker::header::PacketCode::PingPong;
    msg.from_code = CORE;
    msg.body.extend_from_slice(b"Ping");
    assert_eq!(to_n.push(Box::new(msg)).is_ok(), true);
    /*
    *
    *   when connection gets create it has write flags...
    *       but it has negotiation connstate
    *
    *   this means it will write the packet right away...
    *   because of how the write flags are written
    *   
    */


    std::thread::sleep(Duration::from_secs(4));
    // -----------------------------------------------------------------
    shutdown::request_shutdown();
    net_handle1.join().unwrap();
    net_handle2.join().unwrap();
}

