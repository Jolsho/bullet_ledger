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
fn net_outbound() {
    use std::{time::Duration, fs};
    use crate::config::load_config;
    use crate::spsc::SpscQueue;
    use crate::peer_net::ping_pong::send_ping;
    use crate::peer_net::{start_peer_networker};
    use crate::utils::keys::load_keys;
    use crate::{utils, BLOCKCHAIN};

    let mut config = load_config("config.toml");
    config.peer.bind_addr = "127.0.0.1:4143".to_string();
    config.peer.key_path = "assets/keys1.bullet".to_string();
    config.peer.db_path = "assets/net1.sqlit3".to_string();

    // -- setup1 (receiver) ----------------------------------------------------------------
    let (to_c, _from_n) = SpscQueue::new(32, Some(config.peer.max_buffer_size)).unwrap();
    let (_to_n, from_c) = SpscQueue::new(32, Some(config.peer.max_buffer_size)).unwrap();

    let cfg = config.peer.clone();
    let net_handle1 = start_peer_networker( cfg,
        vec![(to_c, BLOCKCHAIN), ],
        vec![(from_c, BLOCKCHAIN), ]
    ).unwrap();

    // -- setup2 (sender)----------------------------------------------------------------
    let remote = config.peer.bind_addr.clone();
    config.peer.bind_addr = "127.0.0.1:4144".to_string();
    config.peer.key_path = "assets/keys2.bullet".to_string();
    config.peer.db_path = "assets/net2.sqlit3".to_string();

    let (to_c, _from_n) = SpscQueue::new(32, Some(config.peer.max_buffer_size)).unwrap();
    let (mut to_n, from_c) = SpscQueue::new(32, Some(config.peer.max_buffer_size)).unwrap();

    let cfg = config.peer.clone();
    let net_handle2 = start_peer_networker(cfg,
        vec![(to_c, BLOCKCHAIN), ],
        vec![(from_c, BLOCKCHAIN), ]
    ).unwrap();

    // -- send da tings ----------------------------------------------------------------
    
    let (public, _private) = load_keys("assets/keys1.bullet").unwrap();
    for i in 0..100 { 
        send_ping(
            &mut to_n, 
            remote.clone().parse().unwrap(), 
            public, BLOCKCHAIN, Some(i)
        );
    }

    // -----------------------------------------------------------------
    
    std::thread::sleep(Duration::from_secs(3));
    utils::shutdown::request_shutdown();
    let _ = net_handle1.join().unwrap();
    let _ = net_handle2.join().unwrap();

    let _ = fs::remove_file("assets/keys1.bullet".to_string());
    let _ = fs::remove_file("assets/net1.sqlit3".to_string());

    let _ = fs::remove_file("assets/keys2.bullet".to_string());
    let _ = fs::remove_file("assets/net2.sqlit3".to_string());
}

