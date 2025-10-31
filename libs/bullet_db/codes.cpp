// codes.cpp
#include <lmdb.h>

extern "C" {

// Correctly export integer constants
extern const int SUCCESS         = MDB_SUCCESS;
extern const int NOTFOUND        = MDB_NOTFOUND;
extern const int ALREADY_EXISTS  = MDB_KEYEXIST;

extern const int TXN_FULL        = MDB_TXN_FULL;
extern const int MAP_FULL        = MDB_MAP_FULL;
extern const int DBS_FULL        = MDB_DBS_FULL;
extern const int READERS_FULL    = MDB_READERS_FULL;

extern const int PAGE_NOTFOUND   = MDB_PAGE_NOTFOUND;
extern const int CORRUPTED       = MDB_CORRUPTED;
extern const int PANIC           = MDB_PANIC;
extern const int VERSION_MISMATCH= MDB_VERSION_MISMATCH;

extern const int INVALID         = MDB_INVALID;
extern const int MAP_RESIZED     = MDB_MAP_RESIZED;

}
