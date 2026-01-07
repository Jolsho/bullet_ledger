# Bullet Ledger

## Sharded Block Based Verkle State Trie
*USE AT OWN RISK*

---

## Trie
*/src/trie*  
[Verkle Trie](https://math.mit.edu/research/highschool/primes/materials/2018/Kuszmaul.pdf)  


A verkle tree is a B+ Tree with a branching factor of 256.  Except the children are "commited" to through a polynomial commitment scheme (KZG).  We will get into the scheme later, but for now imagine each node in the tree as possesing some function f(x).  f(x) has properties such that when x = 0 -> 256, f(x) = child_x.  The evaluation is the child at x polynomial hashed and interpreted as a 256bit number.  This is not exactly true, but for now it will suffice.

Now, since each node holds a single polynomial that evaluates to its children,  if you want to prove a value exists at a specific position within the tree,  you can just transfer over something like compressed polynomials along the path to the value instead of all 256 children at each level down to the value.  

This makes the space complexity of an “existence proof” equal to the depth of the tree rather than proportional to the branching factor and depth.  This is why a branching factor of 256 is chosen: to limit depth and hence the space complexity of an existence proof.



### Node Basics
Inside `/src/trie` navigate to `node.h`.  Here you will see the virtual class that defines a node in the Verkle Trie.  This class is then implemented by the class Branch in `/src/trie/branch.cpp` and class Leaf in `/src/trie/leaf.cpp`.  The header files for both include another virtual class which is the interface for themselves.  When you see leaf referenced outside `leaf.cpp` its type will be `Leaf_i` not `Leaf`, and the same is true of `Branch` and `Branch_i`. Leaves and Branches consist of a similar underlying structure, and differ predominantly in methods.  

Their structures generally consist of a `NodeId`, `Commitment`, `Children`, and `child_block_ids`.  There is of course more than this, but for now this will give you the basic understanding.  A `NodeId` is a contiguous array split into two parts: the `node_id` and the `block_id`.  The prior is a `uint_64` which tells you the position of the node within the tree,  and is calculated by adding its index in its parent to the parent’s `node_id` and multiplying by the branching factor. For example, if you are at the root node where `node_id == 1`,  the 0th child’s node_id is `(0 + 1) * 256 == 256`, and the 1st child is `(1 + 1) * 256 == 512`, etc. When you want to load a node you call `Node_allocator.load_node()`,  and pass it a `NodeId` you have created on the fly.  The purpose of doing it this way as opposed to conventional keying via hash  is because a Verkle Trie makes it computationally difficult to derive the child’s polynomial, which is called finalizing. So by using the node_ids instead we can wait for a more convenient time to derive those child hashes.  If you want to peek ahead you can look at both the leaf and branch implementation of `finalize()`;  either way it will be covered in depth later. 

As for the latter part of the `NodeId`, the `block_id` is used to distinguish between nodes in different blocks.  Multiple blocks will have the same node_ids, so we need a way to distinguish which one we are looking for.  The `children` field is essentially an ordered vector of child hashes,  aka all the y-values from f(x) when `x >= 0 && x < 256`.  Specifically in Branch, children are scalars, which are essentially the hash interpreted as a field element.  Finally, `child_block_ids` is a similar ordered vector,  but it simply tells the block id of the child at that index.  This is used to differentiate nodes in different blocks as mentioned above. We will be skipping the `Commitment` field for now,  but just imagine it as the polynomial mentioned earlier, or some function f(x) that evaluates to the hash of its children.


### Node Methods
As we step up a layer of abstraction, it is useful to modify terminology slightly.  Leaves in the Verkle Trie are often better thought of as accounts. The reason is because you traverse the tree by popping off a byte from the front of the key,  interpreting it as a `uint_8` and using it to guide you to the next node.  In theory, at the end of this, you would land in a leaf which is just a value.  However, in this case the leaf is actually a branch whose children are value hashes. In order to achieve this account-like behavior, the last byte of the key can be overwritten from `1 -> 255`.  Notice the 0th index is reserved, and this is by design. If you have two keys that share a prefix but only one exists in the tree,  without also proving the leaf belongs to one of the whole keys, the proof is ambiguous.  The verifier would not know for certain which account actually exists within the tree. 

The most basic ways to interact with the Verkle Trie are:     `create_account()`, `delete_account()`, `replace()`, `put()`, and `remove()`. Create Account traverses the tree and creates the account if it does not already exist.  Delete Account is self-explanatory. Replace first checks the previous value matches, and if so overwrites it. With Put, you have a simple key value overwrite.  The last byte of the key is modified to the index where the value belongs.  If the account is found, the value hash is modified and the value is saved.  If not, the operation aborts. Remove is effectively the same, except the hash at that index is overwritten with zeros. Beyond these, there are three more abstract methods primarily used for processing blocks: `finalize()`, `justify()`, and `prune()`. Finalize finds all nodes touched in a given block, derives their polynomials,  and hands them to their parents, which hash and insert them at the appropriate child index.  At the end of this process, you are left with the root polynomial. Justify must occur after Finalize for a given block.  It changes every node whose `block_id != 0` to `0` by scanning `child_block_ids`,  loading nodes, and recursing downward. Prune traverses the tree and deletes every node whose `block_id` does not match the specified block id. 

Together, these methods provide enough functionality to explore modified versions of canonical state,  merge new states into the canonical one, and prune useless versions—  all of which comprise a blockchain. You may have noticed the absence of `generate_proof()`.  That is intentional.  Its explanation depends on understanding what a Commitment actually is,  and it will be covered later.

---

## Ledger
*/src/ledger*

The Ledger class is the object exposed through bindings.  It is best thought of as an object that references other objects.  Most of its methods are wrappers around lower-level methods, hiding housekeeping details. The Ledger is composed of a pointer to `Gadgets`, a `shard_prefix`, and more as the project evolves.  `Gadgets` is comprised of two main components. The first is the `Node Allocator`, which manages caching, loading, and deleting of nodes and values.  It has access to the underlying LMDB handler, an LRU cache, and can be passed between threads. The second component of `Gadgets` is `KZG Settings`. This introduces the material avoided so far.  Within the settings is a field called *roots*, short for *roots of unity*.  These are the inputs for the node polynomials. Instead of using 0,1,2,3... as inputs, roots of unity are used:  
`root_i = (g^m)^i = g^(m * i)`    
`g = 5`  
`m = (p - 1) / 256`  
`p = prime order of BLS12-381 elliptic curve subgroup`  
For further exploration, see `/src/kzg/settings.cpp` `build_roots()`.

The final piece of `KZG Settings` is the `setup` field. Nodes do not store full polynomials.  Instead, during finalization, a polynomial is derived and evaluated at a secret x value.  The result is a single point called a `Commitment`. This is efficient: instead of storing 256 coefficients, only a 48-byte commitment is needed. The `setup` field contains vectors representing powers of a secret value.  Each value `v_i` equals `a^i`, where `a` is the secret. We cannot derive `a^i` ourselves because we never know `a` directly.  The only form available is `g * a`, where `g` is the BLS12-381 G1 generator. The value `a` is generated during a trusted setup.  In this process, someone computes `g * (a^i)` for all `i`,  then deletes the secret `a` and shares the resulting vectors. These vectors form the `setup` field.  The system also allows generating your own setup or loading one via `set_srs()`. For full details, see `/src/kzg/settings.cpp` and `/src/kzg/settings.h`.

---

## KZG & Existence Proofs
*/src/kzg*  
[KZG](https://math.mit.edu/research/highschool/primes/materials/2018/Kuszmaul.pdf),
[libBLST](https://github.com/supranational/blst),
[Fourier Transform](https://en.wikipedia.org/wiki/Discrete_Fourier_transform)  

Up to this point you should have some idea what is meant by a Commitment, and understand the concept of a polynomial evaluating to a set of values given a set of inputs. Now the whole reason for doing all of these modifications of a classical Merkle Tree is to be able to create relatively small proofs of existence without needing the whole state trie to verify. Now the question is what are these proofs, how are they constructed and how can we be sure they are trustworthy.
First what is the proof.  
Given `f(x) = 1 - x + x^2 + x^3`  & `a == random setup value` & `f(a) == commitment to f(x)`  
`Proof == (f(a) - f(x_i)) / (a - x_i)`    

What this means is that you can take a commitment to a polynomial, subtract out the evaluation you are proving, and then divide by the binomial `(a - x_i)` which is the secret setup value minus the input to the evaluation. From here it should be obvioius that both `f(a)` & `f(x_i)` are just scalars(field elements) and can be subtracted very easily, but on the other hand creating that binomial might seem mysterious because as I said earlier we dont know `a`.

This leads to me the next thing which should start to illuminate what is going on here, and that is **Homorphism**. Homomorphism in this context just means that you can perform some arithmetic operations on some values and you can do the same thing with wrappers around them without losing the relationship.  
 For example, `f(x) + f(a) == f(x + a)`, or in our case `(g * a) + (g * x) = g * (a + x)`.

In fact, a proof I gave has the term `f(a)` which is a commitment to `f(x)`, but we don't know `a` so in that is actually not the whole picture and a commitment is more accurately something like `g * f(a)` and the reason for that is because if you have a polynomials coeffecients you can take each coeffecient and the setup vector with `g*(a^i)` and then multiply each pointwise to get something like `g * (c_i * a^i)` where `c_i` is the coeffecient at i. Given such `C == sum[ while i < degree of f(x) ]( g * c_i * a^i )` or more succinctly `C = g * f(a)`. The next step to match the structure of the numerator would be to take `f(x_i) == y_i` and multiply it by `g` to get `g * y_i` which can then be subtracted from `C` and because of homomorphism you get `g * (f(a) - f(x_i))`.

To go back to the denominator we can achieve something similar by taking our setup vector at index 1 to get `g * (a^1) == g * a` and then multiply `g * x_i` and subtracting the two to get `g * (a - x_i)`. However, these values are only additively homomorphic meaning we cant achieve something like division. Although it may seem like I just led you into a dead end I did not, this will all be useful when we are verifying, but for now we need to take a few steps.

When I have gave you `Proof == (f(a) - f(x_i)) / (a - x_i)` what I didn't tell you was that this is just a theoretical formula and in fact we don't derve `Proof` this way in practice. Instead what we do is we derive `Q(X) == f(X) - f(x_i) / (X - x_i)` and what this means is that we are doing this all before involving `a` at all. Then what we do is we solve `Q(a)` which I already told you how we can do that withou knowing a and that leaves us with `g * Q(a) == g * (f(a) - f(x_i) / (a - x_i))` and for the last time the previous proof formula I gave you was missing the multiplication by the term `g`. 

However, at the end of all that what we now see is that we have:  
`Proof == Pi == g * Q(a) == g * (f(a) - f(x_i) / (a - x_i))`  
From here the verifier can take `C == g * f(a)` & `y_i = f(x_i)` & `g * (a - x_i)` to check that  
`C - y_i == Pi * (g * (a - x))`  
Although I said we aren't allowed to do division, multiplication is slightly different because at the end of the day multiplication is just repeated addition. FE: `3 * 3 = 3 + 3 + 3`, meaning we can do  `g * (Q(a) * (a - x))`.

In all what is illustrated here is that if there is some agreed commitment to a polynomial `C == f(a)`, which in our case is a root polynomial. Then at any later time we can prove that the ith child of that root evaluates to the hash of another commitment to a polynomial, and we can do this over and over until we reach some value that we are trying to prove.

---

## Bindings
*/src/bindings*

I suggest just referencing `/bindings/extern.h` and then the implementation in `src/bindings/ledger.cpp` when you are trying to figure how to use this library. They should be fairly self explanatory at this point. I suppose the only thing that might be confusing is the `ledger_open()` call because it takes some odd parameters. 

First, `cache_size` is how many nodes are held in the LRU cache and each node is just below 2.3kb. Next,`map_size` is the upper bound of the entire databases size. The underlying database is LMDB so it has to virtually map everything meaning it needs an upper bound. I need to figure out some rough calculations but it depends on how much sharding is going on and how close keys are. But I would say generally assuming even distribution of keys and shards of size around 1,000 accounts putting 20GB would be pretty reasonable. 

The next parameter is tag which is just a domain seperation tag, aka a string, and should just be something unique to your project like its name. Last is the secret bytes which should just be random bytes sampled from a cryptographically secure source. If they are not provided the function will use a linux syscall to do it for you, so these are not mandatory. If you want to import a `setup` from someone else you can call `ledger_set_SRS()` or if you want to export yours so you can share it call `ledger_get_SRS()` which return them in an encoded format.
