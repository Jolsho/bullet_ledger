
// TODO -- both of these tests are trash...
// they dont create the right packets.

#[test]
fn net_timeout() {
    use std::io::{Read, Write};
    use std::net::TcpStream;
    use std::time::Duration;
    use ringbuf::traits::Split;

    use crate::config::load_config;
    use crate::core::msg::CoreMsg;
    use crate::networker::header::PacketCode;
    use crate::msging::{MsgQ, Ring};
    use crate::networker::{start_networker, netman::NetMsg};
    use crate::{trxs::Trx, NETWORKER, CORE};
    use crate::shutdown;


    // -- setup ----------------------------------------------------------------
    let config = load_config("config.toml");

    let trx_ring = Ring::<Trx>::new(512);
    let (_trx_prod, trx_con) = trx_ring.split();

    let net_to_core = MsgQ::<CoreMsg>::new(128, NETWORKER).unwrap();
    let core_to_net = MsgQ::<NetMsg>::new(128, CORE).unwrap();

    let (net_core_prod, _) = net_to_core.split().unwrap();
    let (_, core_net_cons) = core_to_net.split().unwrap();

    let addr = "127.0.0.1:4141".to_string();
    let net_handle = start_networker( config.network.clone(), trx_con, net_core_prod, 
        vec![(core_net_cons, CORE)]
    );

    // -- connect ----------------------------------------------------------------
    let mut clients: Vec<TcpStream> = Vec::new();
    for _ in 0..10 {
        let s = TcpStream::connect(&addr).unwrap();
        s.set_nonblocking(true).unwrap();
        s.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
        clients.push(s);
    }

    // -- message loop -----------------------------------------------------------
    let expected = b"Pong";
    let mut len_buf = [0u8; 8];
    let mut buf = [0u8; 1024];

    for round in 0..1 {
        for (i, c) in clients.iter_mut().enumerate() {
            let msg = format!("Ping {i}-{round}");
            let msg_bytes = msg.as_bytes();
            buf[0] = PacketCode::PingPong as u8;
            buf[1..9].copy_from_slice(&msg_bytes.len().to_le_bytes());
            buf[9..9 + msg_bytes.len()].copy_from_slice(msg_bytes);

            if round == 0 && i == 0 {
                c.write_all(&buf[..9 + msg_bytes.len() - 2]).unwrap();
                println!("Sleeping(3s)... to test timeouts, and partial reads");
                std::thread::sleep(Duration::from_secs(3));
                std::thread::yield_now();
                c.write_all(&buf[9 + msg_bytes.len() - 2..]).unwrap();

            } else if i > 0 {
                c.write(&buf[..9 + msg_bytes.len()]).unwrap();
                c.read_exact(&mut len_buf).unwrap_err();
                let len = usize::from_le_bytes(len_buf);
                c.read_exact(&mut buf[..len]).unwrap();
                assert_ne!(&buf[..len], expected);
            }

        }
    }

    shutdown::request_shutdown();
    net_handle.join().unwrap();
}

#[test]
fn net_stress() {
    use std::io::{Read, Write};
    use std::net::TcpStream;
    use std::time::Duration;
    use std::thread::JoinHandle;
    use ringbuf::traits::Split;

    use crate::config::load_config;
    use crate::core::msg::CoreMsg;
    use crate::networker::header::{PacketCode, HEADER_LEN};
    use crate::msging::{MsgQ, Ring};
    use crate::networker::{start_networker, netman::NetMsg};
    use crate::{trxs::Trx, NETWORKER, CORE};
    use crate::shutdown;

    // -- setup ----------------------------------------------------------------
    let mut config = load_config("config.toml");

    let trx_ring = Ring::<Trx>::new(512);
    let (_trx_prod, trx_con) = trx_ring.split();

    let net_to_core = MsgQ::<CoreMsg>::new(128, NETWORKER).unwrap();
    let (net_core_prod, _net_core_cons) = net_to_core.split().unwrap();

    let core_to_net = MsgQ::<NetMsg>::new(128, CORE).unwrap();
    let (_core_net_prod, core_net_cons) = core_to_net.split().unwrap();

    let addr = "127.0.0.1:4142".to_string();
    config.network.bind_addr = addr.clone();
    let net_handle = start_networker(
        config.network.clone(), trx_con, net_core_prod, 
        vec![(core_net_cons, CORE)]
    );


    // -- connect ----------------------------------------------------------------
    let size:usize = 6;
    let msgs:usize = 50;
    let mut threads = Vec::<JoinHandle<()>>::with_capacity(size);
    for i in 0..size { 
        let addr_clone = addr.clone();
        threads.push(std::thread::spawn(move || {
            let mut s = TcpStream::connect(&addr_clone).unwrap();
            s.set_nonblocking(false).unwrap();
            s.set_nodelay(true).unwrap();
            s.set_read_timeout(Some(Duration::from_secs(1))).unwrap();
            s.set_write_timeout(Some(Duration::from_secs(1))).unwrap();

            let expected = b"Pong";
            let mut head_buf = [0u8; HEADER_LEN];
            let mut buf = [0u8; 1024];
            for round in 0..msgs {
                let msg = format!("Ping {i}-{round}").as_bytes().to_owned();
                buf[0] = PacketCode::PingPong as u8;
                buf[1..9].copy_from_slice(&msg.len().to_ne_bytes());
                buf[9..9 + msg.len()].copy_from_slice(&msg);

                s.write_all(&buf[..9 + msg.len()]).unwrap();
                head_buf.fill(0);

                println!("{i}:{round}");
                s.read_exact(&mut head_buf).unwrap();
                let len = usize::from_le_bytes(head_buf[1..9].try_into().unwrap());
                s.read_exact(&mut buf[..len]).unwrap();
                assert_eq!(&buf[..len], expected);
            }
        }));
    }

    for t in threads { t.join().unwrap(); }
    std::thread::sleep(Duration::from_secs(1));
    shutdown::request_shutdown();

    net_handle.join().unwrap();
}
