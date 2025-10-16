


#[test]
fn net_outbound() {
    use std::{time::Duration, fs};
    use crate::config::load_config;
    use crate::msging::MsgQ;
    use crate::peer_net::handlers::ping_pong::send_ping;
    use crate::peer_net::{start_peer_networker};
    use crate::crypto::montgomery::load_keys;
    use crate::{shutdown, CORE};

    let mut config = load_config("config.toml");
    config.peer.bind_addr = "127.0.0.1:4143".to_string();
    config.peer.key_path = "assets/keys1.bullet".to_string();
    config.peer.db_path = "assets/net1.sqlit3".to_string();

    // -- setup1 (receiver) ----------------------------------------------------------------
    let (to_c, _from_n) = MsgQ::new(32, Some(config.peer.max_buffer_size)).unwrap();
    let (_to_n, from_c) = MsgQ::new(32, Some(config.peer.max_buffer_size)).unwrap();

    let cfg = config.peer.clone();
    let net_handle1 = start_peer_networker( cfg,
        vec![(to_c, CORE), ],
        vec![(from_c, CORE), ]
    ).unwrap();

    // -- setup2 (sender)----------------------------------------------------------------
    let remote = config.peer.bind_addr.clone();
    config.peer.bind_addr = "127.0.0.1:4144".to_string();
    config.peer.key_path = "assets/keys2.bullet".to_string();
    config.peer.db_path = "assets/net2.sqlit3".to_string();

    let (to_c, _from_n) = MsgQ::new(32, Some(config.peer.max_buffer_size)).unwrap();
    let (mut to_n, from_c) = MsgQ::new(32, Some(config.peer.max_buffer_size)).unwrap();

    let cfg = config.peer.clone();
    let net_handle2 = start_peer_networker(cfg,
        vec![(to_c, CORE), ],
        vec![(from_c, CORE), ]
    ).unwrap();

    // -- send da tings ----------------------------------------------------------------
    
    let (public, _private) = load_keys("assets/keys1.bullet").unwrap();
    for i in 0..100 { 
        send_ping(
            &mut to_n, 
            remote.clone().parse().unwrap(), 
            public, CORE, Some(i)
        );
    }

    // -----------------------------------------------------------------
    
    std::thread::sleep(Duration::from_secs(3));
    shutdown::request_shutdown();
    let _ = net_handle1.join().unwrap();
    let _ = net_handle2.join().unwrap();

    let _ = fs::remove_file("assets/keys1.bullet".to_string());
    let _ = fs::remove_file("assets/net1.sqlit3".to_string());

    let _ = fs::remove_file("assets/keys2.bullet".to_string());
    let _ = fs::remove_file("assets/net2.sqlit3".to_string());
}

