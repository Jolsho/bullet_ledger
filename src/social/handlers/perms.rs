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

fn give() {
    /*
    *   decode give request
    *   GIVE = {
    *       hash,
    *       to: string,
    *       to_cid: bytes,
    *       giver: String,
    *       giver_cid: bytes
    *       nonce: bytes
    *       signature: bytes
    *   }
    *
    *   give.to exists locally
    *
    *   give.giver == sender
    *   && is allowed to communicate
    *
    *   save the permission??
    *       SOMEHOW??
    *
    *   send notification to user if alive??
    */
}

fn settle_give() {
    /*
    *   decode settlegive
    *   settle_give = {
    *       hash,
    *       accepted: bool
    *   }
    *
    *   if not settle_give.accepted
    *       delete the pending give??
    *       return 
    *
    *   set permission as active
    *
    *   send the notification
    */
}

fn ask() {
    /*
    *   decode ask
    *   ask = {
    *       asker: string,
    *       asked: string,
    *       asker_cid: bytes,
    *       local_cid: bytes
    *   }
    *
    *   validate ask.asker == sender
    *
    *   validate ask.asked exists locally
    *
    *   Send the notification...
    *       find a way to store these? 
    *       or do that in the go server
    *
    */
}

fn revoke() {
    /*
    *   decode revoke
    *   revoke = {
    *       hash,
    *       signature,
    *   }
    *
    *   verify revoke_signature = H("revoke" + revoke.hash)
    *   came from sender
    *
    *   retrieve the permission being revoked local signature
    *   if it matches with sender we delete it
    *
    *   somehow get the person who had the permissions address...
    *   then we can notify them...
    */
}
