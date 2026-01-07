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

fn handle_new_card() {
    /* 
    *
    *   read in permission 
    *   perm = {
    *       hash,
    *       giver_sig,
    *       sequence: i32,
    *       sequence_sig,
    *       payload_sig
    *   }
    *
    *   verify giver == sender
    *
    *   giver addr is in network_state(allowed)
    *
    *   permission hash exists locally
    *       (not revoked)
    *
    *   --------------------
    *
    *   Hash the card
    *   validate signature from sender
    *
    *   file_id = hash
    *   if file does not exist
    *       write card to a file
    *
    *   figure out how to notify users of such
    *
    */
}

