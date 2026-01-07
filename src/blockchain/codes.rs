

#[derive(Debug, Copy, Clone)]
pub enum BlockchainCodes {
    None = 0,

    NewBlock = 1,
    NewTrx = 2,
}

pub fn blockchain_code_from_u8(dig: u8) -> BlockchainCodes {
    match dig {
        1 => BlockchainCodes::NewBlock,
        2 => BlockchainCodes::NewTrx,

        _ => BlockchainCodes::None,
    }
}

