
#[test]
fn net_outbound() {
    use std::{time::Duration, fs};
    use crate::config::load_config;
    use crate::msging::MsgQ;
    use crate::networker::handlers::ping_pong::send_ping;
    use crate::networker::{start_networker, utils::NetMsg};
    use crate::crypto::montgomery::load_keys;
    use crate::{NETWORKER, CORE};
    use crate::shutdown;

    fn get_stuff() -> (MsgQ<NetMsg>,MsgQ<NetMsg>) {
        let ntc = MsgQ::<NetMsg>::new(32, NETWORKER).unwrap();
        let ctn = MsgQ::<NetMsg>::new(32, CORE).unwrap();
        (ntc, ctn)
    }

    let mut config = load_config("config.toml");
    config.network.bind_addr = "127.0.0.1:4143".to_string();
    config.network.key_path = "assets/keys1.bullet".to_string();
    config.network.db_path = "assets/net1.sqlit3".to_string();

    // -- setup1 (receiver) ----------------------------------------------------------------
    let (n_to_c, c_to_n) = get_stuff();
    let (to_c, _from_n) = n_to_c.split().unwrap();
    let (_to_n, from_c) = c_to_n.split().unwrap();

    let cfg = config.network.clone();
    let net_handle1 = start_networker(
        cfg, to_c, vec![(from_c, CORE), ]
    ).unwrap();

    // -- setup2 (sender)----------------------------------------------------------------
    let remote = config.network.bind_addr;
    config.network.bind_addr = "127.0.0.1:4144".to_string();
    config.network.key_path = "assets/keys2.bullet".to_string();
    config.network.db_path = "assets/net2.sqlit3".to_string();

    let (n_to_c, c_to_n) = get_stuff();
    let (to_c, _from_n) = n_to_c.split().unwrap();
    let (mut to_n, from_c) = c_to_n.split().unwrap();

    let cfg = config.network.clone();
    let net_handle2 = start_networker(
        cfg, to_c, vec![(from_c, CORE), ]
    ).unwrap();

    // -- send da tings ----------------------------------------------------------------
    
    let (public, _private) = load_keys("assets/keys1.bullet").unwrap();
    for i in 0..100 { 
        send_ping(
            &mut to_n, remote.clone(), public, CORE, Some(i)
        ).unwrap();
    }

    // -----------------------------------------------------------------
    
    std::thread::sleep(Duration::from_secs(3));
    shutdown::request_shutdown();
    net_handle1.join().unwrap();
    net_handle2.join().unwrap();

    let _ = fs::remove_file("assets/keys1.bullet".to_string());
    let _ = fs::remove_file("assets/net1.sqlit3".to_string());

    let _ = fs::remove_file("assets/keys2.bullet".to_string());
    let _ = fs::remove_file("assets/net2.sqlit3".to_string());
}

