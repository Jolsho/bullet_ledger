# SUMMARY


# ARCHITECTURE::
## GENERIC STRUCTURES

### src/server.rs
this is the generic server structure.
It is implemented inside src/peer_net and src/rpc.
    

### src/spsc.rs
singe producer single consumer messaging
used to communicate between actors in the system
rpc -> peer_net, peer_net -> consensus_client



## P2P Networker
`src/peer_net`
This is the main interaction with the outside world.
All connections and messages come through here.

### src/peer_net/mod.rs`
`server.start()` takes a few arguments.
>1. `froms` is a vector of consumers of a spsc struct and tokens. This is the vector of other internal actors who wish to send this server a message. Which will be handled by the following parameter.
>2. `handle_internal()` which is a function that expects a `NetMsg` and a reference to the server. This function is meant to handle internal messages from other actors in the system.
>3. `allow_connection()` is a function that given a `SocketAddr` returns a bool whether the connection should be accepted or not.
>4. `handle_errored()` is a function that take a `NetError`, `SocketAddr` and `&mut NetServer` and is expected to do whatever needs to be done when an error occurs, whether you want to log it or record the behaviour for peer management or whatever.


### src/peer_net/peers.rs
The implementation of peer management for this peer to peer network.
STRUCTURE
`add_peer()` + `remove_peer()`
`is_banned()`
`record_behaviour()`

            
### src/peer_net/header.rs 
The general idea here is that this is the general structure of every packet header outgoing and inbound from the external world. The layout is as follows:
>1. `PacketCode` - Used to find the right handler.
>2. `response_handler` - Option field used on outgoing packets to force handle the next packet on that connection.
>3. `msg_id` - Acts as the key for the response handler, so a response can send the msg_id which allows us to see if there is a handler waiting for it.
>4. `nonce` - In order to prevent shared message structure attacks on encrypted packets.
>4. `tag` - Domain specific tag also for encryption and ensuring message uniqueness.
>5. `is_marshalled` - Just a technique to prevent marhsalling the packet more than once.


### src/peer_net/connection.rs
STRUCTURE + new(), init(), reset()
READABLE / WRITABLE
>`incoming.rs`
>`outgoing.rs`


### src/peer_net/handlers
PACKET CODES
HANDLER / HANDLER_RES
`code_switcher()` BESIDES NEGOTIATION JUST FORWARD TO ACTOR
    
`forward_to_block_chain()`, `forward_to_?()`



## BLOCKCHAIN


## SOCIAL
