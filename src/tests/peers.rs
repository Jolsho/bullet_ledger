#[test]
fn peers() {
    use crate::utils::NetError;
    use std::net::{SocketAddr, Ipv4Addr, IpAddr};
    use std::fs;
    use crate::config::load_config;
    use crate::peer_net::peers::PeerMan;

    let path = "assets/test_peers.sqlit3".to_string();
    let threshold = 100;
    let config = load_config("config.toml").peer;

    let mut p_man = PeerMan::new(path.clone(), 
        threshold, config.bootstraps.clone(),
    ).unwrap();

    let ip = Ipv4Addr::new(127,0,0,1);
    let addr = SocketAddr::new(IpAddr::V4(ip), 0);

    // p_man.add_peer(&ip).unwrap(); this is done for 127.0.0.1 in PeerMan::new()
    p_man.is_banned(&addr).unwrap();
    let e = NetError::Unauthorized;
    let times_to_ban = threshold / e.to_score();
    for _ in 0..times_to_ban + 1 {
        p_man.record_behaviour(&addr, e.clone()).unwrap();
    }
    p_man.is_banned(&addr).unwrap_err();


    p_man.remove_peer(&ip);
    assert_eq!(
        p_man.is_banned(&addr).unwrap_err(), 
        NetError::Unauthorized
    );
    assert_eq!(
        p_man.record_behaviour(
            &addr, NetError::MalformedPrefix,
        ).unwrap_err(), 
        NetError::Unauthorized
    );

    let _ = fs::remove_file(path);
}
