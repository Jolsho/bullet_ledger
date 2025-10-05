mod trxs;
mod generators;
mod db;
mod stream;
mod tests;
mod schnorr;

fn main() {
}

/*
*   Have working bullet_proof, account based structure.
*       Can do trxs in the clear and hidden.
*       Uses signatures for state transitions.
*       Required by both parties, because of hiding factor.
*       (will discuss later, but could do no signature for public Trxs)
*
*   Also have "Schnorr" which does hidden and not but without accounts. 
*       Balances essentially act as accounts. 
*       Then you can put these all in a merkle tree. 
*       Potential for super small state without accounts. 
*       Kinda interesting, but has caveats.
*
*
*   Either way Receiver still has to be aware of what they are receiving.
*       IF HIDDEN:: They have to agree to it, to update (r * H + x * G).
*
*       ELSE:: sender reveals the value(or x * G) to the public. Then the 
*       receiver would have to retrieve x * G, So that they could update 
*       their internal balance(claim).
*
*
*   Well if you do it where the receiver doesn't need to approve
*       each transaction that means you need to hold historical state.
*       I personally don't like that. I would rather the state stay
*       temporally lean. So that would mean we would ideally make 
*       receivers publish TRXS. Which I think in practice is fine.
*       Since they don't have to generate proofs, its not that difficult.
*       You could easily process these rapidly.
*
*   Otherwise,
*       you are going to need to hold past state. And maybe you could 
*       implement some way of removing retrieved state. So like when a 
*       person comes back online, they catch up, and tell the other 
*       nodes they can trim that part of the history. You could do 
*       that as well. And just do a schnoor to prove they "own" that 
*       series of transitions.
*       
*
*   How to do Trx Fees?
*       You would have some number left in the clear.
*       With a corresponding delta.
*       The senders final state includes that deduction.
*       You use transaction context hash for binding to TRX.
*           this way people cant just take it
*       The network can realize that delta wasn't intended for this.
*/
