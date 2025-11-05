


use verkle_kzg() to commit n prove all nodes along many paths.
    The aggregate proof and commit is used along side,
        all node ids/values to check...PREV_ROOT is included in that

then verify_verkle_kzg() is the verification of that proof...
    takes values and ids sums and checks

so that is like first half of block execution...
Ensure everything claimed to exist does in fact exist...

then using all those witnesses....
calculate delta in leaves and recurse back to root...
check that old_root + delta  == new_claimed_root




==========================================

TRXS need their own inclusion proofs.
    once that is validated shred it.
    its just for block builders.
    so they dont have to query state.
OR MAYBE NOT...MIGHT BE ABLE TO USE THAT...
JUST DO SOME SUBTRACTION OR SOMETHING...

block builder then aggregates all nodes and leaves
    proves that every init_state in trx is valid
    and that all internal nodes exist
and then broadcassts...


internal node commitment must be included in block
this means 257 in l0 + l1
then (2 || 3) * 2k = (4k || 6k) internal commitments
that is like (205kb || 300kb) / block

but once you have those...
you can compute delta of each leaf and then recurse back to root
in the end Old_root - delta and then check against New_root
you wouldnt need state to do this...


ALSO::
    could you be a proposer without state...
    yes... if trxs have inclusion proofs.
    
Wait what if each node only kept track of what it wanted to.
So like what if you only track your path to root.
    so only nodes on that path are what you care about.
Then if you miss a block you are fucked.
    Well you could incentivize some more robust nodes as well.
    but in theory yea. you can just track your state.
        and just validate the rest of it.
        but internally only hold what you care about
This would be pretty cool.
GIVES US A LOT MORE COMPUTE TO MESS AROUND WITH

==============================================================

transactors create proofs for l2 nodes
    odds of collision for l2 are 3% for 2k trxs
block builder creates commits for collided_l2 + l1 + l0(root)
so that would be just more than 300 proofs...
seems manageable...


