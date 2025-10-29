use curve25519_dalek::ristretto::CompressedRistretto;
use std::{cell::RefCell, collections::HashMap, rc::Rc, time::{Duration, SystemTime, UNIX_EPOCH}};

use crate::core::Hash;


#[derive(Clone)]
pub struct VoteInterval {
    pub epoch: u64,
    pub hash: Hash,
}

pub struct Vote {
    pub source: VoteInterval,
    pub target: VoteInterval,
}

pub type CheckpointPointer = Rc<RefCell<Checkpoint>>;
pub struct Checkpoint {
    hash: Hash,
    weight: u64,
    parent: CheckpointPointer,
    heaviest_child: CheckpointPointer,
    children: Vec<CheckpointPointer>,
}

pub struct Consensus {
    next_epoch: SystemTime,
    epoch_interval: u64,
    super_majority: u64,

    last_justified: u64,
    validators: HashMap<Hash, Vote>,
    checkpoint_buckets: HashMap<u64, HashMap<Hash, CheckpointPointer>>,
}

impl Consensus {

    pub fn new(epoch_interval: u64) -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Time went backwards");

        let next_epoch_secs = ((now.as_secs() / epoch_interval) + 1) * epoch_interval;
        let next_epoch = UNIX_EPOCH + Duration::from_secs(next_epoch_secs);

        Self { 
            epoch_interval, 
            next_epoch,
            super_majority: 1_000_000, // TODO -- how to derive super_majority
            last_justified: 0,
            validators: HashMap::new(),
            checkpoint_buckets: HashMap::new(),
        }
    }

    pub fn poll(&mut self) {
        if SystemTime::now() >= self.next_epoch {

            // derive the next validator state
            
            self.next_epoch += Duration::from_secs(self.epoch_interval);
        }
    }

    pub fn is_validator(&self, _validator: &CompressedRistretto) -> bool { 
        // is there something about only one proposal per slot??

        // Check that this person should be listened to...
        // if they are actually the validator
        // if they are apart of the current epoch list of proposers
        true 
    }

    pub fn on_vote(&mut self, voter: Hash, vote: Vote) -> Option<Hash> {
        if let Some(prev_vote) = self.validators.get_mut(&voter) {
            if prev_vote.target.epoch > vote.target.epoch { 
                // TODO left off here
            }
        }

        let mut is_justified = false;
        if let Some(bucket) = self.checkpoint_buckets.get_mut(&vote.target.epoch) {
            if let Some(t) = bucket.get_mut(&vote.target.hash) {
                let mut target = t.borrow_mut();

                // Cast Vote
                target.weight += 32;  // TODO -- what about different weights?
                
                // if not super_majority &&
                if target.weight < self.super_majority {
                    let mut parent = target.parent.borrow_mut();

                    // if target is not currently heaviest child
                    if target.hash != parent.heaviest_child.borrow().hash {

                        // if target is now heaviest child
                        if target.weight > parent.heaviest_child.borrow().weight {
                            // update parent to point to target
                            parent.heaviest_child = t.clone();


                        // else if target is most recent to reach heaviest weight
                        // and target hash < heaviest hash
                        } else if target.weight == parent.heaviest_child.borrow().weight &&
                            target.hash < parent.heaviest_child.borrow().hash 
                        {
                            // update parent to point to target
                            parent.heaviest_child = t.clone();
                            
                        }
                    }
                    return None;
                } else {
                    is_justified = true;
                }
            }
        }
        if is_justified {

            // if current is justified collapse all epochs from last justified to current
            let mut current = vote.target.clone();
            let mut previous = self.last_justified;

            while previous < current.epoch {

                if let Some((_, mut epoch_map)) = self.checkpoint_buckets.remove_entry(&current.epoch) {

            // TODO -- send this to execution layer to merge with canonized chain.
            // engine needs to execute the justified blocks

                    if let Some((_, checkpoint)) = epoch_map.remove_entry(&current.hash) {
                        current.hash.copy_from_slice(&checkpoint.borrow().parent.borrow().hash);
                        current.epoch -= 1;
                        previous += 1; // NOT SURE ABOUT THIS...GOT DISTRACTED
                    }
                }
            }
            return Some(vote.target.hash);
        } else {
            return None;
        }
   }
}

/*
*   TODO -- how to do fees and what not
*   also who is currently proposing
*       - how to derive and store that
*
*   MAKE_VOTE::
*       walk from last justified checkpoint (root of DAG)
*       choose heaviest at each step
*       go until no children...
*       THAT IS YOU VOTE
*/
