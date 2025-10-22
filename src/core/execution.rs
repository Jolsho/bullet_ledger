/*
*
*
*
*   STRUCTS:: 
*       CANONICAL
*           + INITS
*
*       BLOCKS
*           + [ HASH ] -> { INITS, FINALS, PARENT }
*
*
*   PROPOSED BLOCKS::
*       SIZES::: 2MB/Block  && 1000B/TRX = 2000 TRXs / BLOCK
*
*       Compressed TRX = 128 Bytes
*       Processed Block = 2000 * 128 = 256KB
*
*       SEARCH COMPLEXITY:: 32 Processed Blocks * log2( 2000 Compressed TRXs / Processed Block )
*
*       IF holding up to 3 different checkpoints: 
*           TOTAL MEMORY FOR BLOCKS ~= 25MB = 3 * 32 * 256KB 
*
*       Structs::
*           + ROOT
*           + PARENT
*           + Proposer_Info
*           + Slot#
*           + Trxs
*           - HASH = H(Slot# + Trxs)
*           
*       Procedure::
*           + check if already received ( BLOCKS[HASH] )
*           + validate proposer info
*
*           + validate trxs
*               - block = BLOCKS[HASH]
*
*               - for each trx to be valid
*                   = prev_blocks.INITS[trx_init_states] not exists 
*                   = (and prev_blocks.FINALS[trx_init_states] or CANONICAL[trx_init_states} exists)
*                   = Then validation process
*
*           + if all trxs are valid begin staging
*               - if there are multiple transitions for a single address only keep latest
*               - insert trx.init_states to block.INITS(ENSURE ORDERING)
*               - insert trx.final_states to block.FINALS(ENSURE ORDERING)
*               - accumulate fees
*
*           + compute proposer + fees state change and stage it
*
*           + add block hash to BLOCKMAP
*               - BRANCHES[ROOT].BLOCKS[Hash] = {}
*
*
*/
