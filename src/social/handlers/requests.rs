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

fn temp() {
    /*
    *   decode temp request
    *   TEMP = {
    *       user: string,
    *       id: option<bytes>,
    *       cid: option<bytes>,
    *       page: i32,
    *       perm_hash: bytes,
    *       expiration: u64,
    *       to: string
    *   }
    *
    *   if has cid
    *       if cid not public
    *           if perm hash exists(r.user, r.perm_hash)
    *               verify its a give signed by sender
    *               this means the requester has permission to recieve
    *
    *   send notification to user
    */
}


fn settle_temp() {
    /*
    *   decode temp response
    *   TEMP_RES = {
    *       accepted: bool,
    *       req_hash,
    *       length: i32,
    *       user: string,
    *   }
    *
    *   make sure there was a request matching req_hash sent
    *
    *   assert(req.to == sender)
    *
    *   if res.accepepted
    *       send response to user...
    *       however that is going to work...
    *   else 
    *       also let them know but in a different way
    */
}
