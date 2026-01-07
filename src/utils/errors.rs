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

use core::fmt;

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetError {
    ConnectionAborted,
    MalformedPrefix,
    Unauthorized,
    NegotiationFailed,
    Encryption(String),
    Decryption(String),
    SocketFailed,
    Other(String),
}

impl NetError {
    pub fn to_score(&self) -> usize {
        match self {
            NetError::Unauthorized => 30,
            NetError::MalformedPrefix => 20,
            NetError::Decryption(_) => 10,
            _ => 0
        }
    }
}

pub type NetResult<T> = Result<T, NetError>;

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum InternalError {
    Ledger(i32),
}

// Implement Display for user-friendly messages
impl fmt::Display for InternalError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            InternalError::Ledger(code) => write!(f, "Ledger error with code {}", code),
        }
    }
}

// Implement std::error::Error
impl std::error::Error for InternalError {}
