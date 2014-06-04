#include <assert.h>
#include <sqlite3.h>
#include <openssl/evp.h>
#include "controller/common-db.h"

#define DB_PWHASH_ITERS  1000
#define DB_PWHASH_OUTLEN 20

static void db_pwhash(sqlite3_context *context, int argc, sqlite3_value **argv){
  int res;

  assert( argc==2 );
  assert( sqlite3_value_type(argv[0]) == SQLITE_BLOB );
  assert( sqlite3_value_type(argv[1]) == SQLITE3_TEXT );

  const int saltsize = sqlite3_value_bytes(argv[0]);
  const unsigned char *salt = sqlite3_value_blob(argv[0]);

  const int pwsize = sqlite3_value_bytes(argv[1]);
  const unsigned char *pw = sqlite3_value_blob(argv[1]);

  unsigned char *out = calloc(sizeof (unsigned char), DB_PWHASH_OUTLEN);

  res = PKCS5_PBKDF2_HMAC_SHA1((const char *)pw, pwsize, salt, saltsize,
                               DB_PWHASH_ITERS, DB_PWHASH_OUTLEN, out);
  assert(res != 0);

  sqlite3_result_blob(context, out, DB_PWHASH_OUTLEN, free);
}

int db_init_pwhash(sqlite3 *db) {
  return sqlite3_create_function_v2(db, "pwhash", 2, SQLITE_UTF8, NULL,
                                    db_pwhash, NULL, NULL, NULL);
}
