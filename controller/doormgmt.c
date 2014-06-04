/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

//                                                                      }}}
// Prelude                                                              {{{
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "controller/common-db.h"

#define ASIZE(n) (sizeof(n)/sizeof(n[0]))

//                                                                      }}}
// Database                                                             {{{

sqlite3 *db;

static void db_deinit() {
  sqlite3_close(db);
}

static int db_init(char *fn) {
  int ret;

  ret = sqlite3_open(fn, &db);
  if (ret) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    goto err;
  }

  db_init_pwhash(db);
  assert(ret == 0);

  return 0;
err:
  db_deinit();
  return -1;
}

static int db_cmd_user_arg(char **argv, int argc, int optind, char *sql) {
  if (argc - optind != 3) {
    fprintf(stderr, "Need two arguments\n");
    return -1;
  }

  sqlite3_stmt *stmt = NULL;
  const char *tail = NULL;

  int ret = sqlite3_prepare_v2(db, sql, -1, &stmt, &tail);
  assert (ret == SQLITE_OK && *tail == '\0');
  
  ret = sqlite3_bind_text(stmt, 1, argv[optind+1], -1, SQLITE_STATIC); 
  assert (ret == SQLITE_OK);
  
  ret = sqlite3_bind_text(stmt, 2, argv[optind+2], -1, SQLITE_STATIC); 
  assert (ret == SQLITE_OK);
  
  ret = sqlite3_step(stmt);
  assert (ret == SQLITE_DONE);
  
  sqlite3_finalize(stmt);

  return 0;
}

//                                                                      }}}
// Main                                                                 {{{
int main(int argc, char **argv){
  int mret = 0;
  char *db_fn = NULL;
  {
    int opt;
    while((opt = getopt(argc, argv, "D:")) != -1) {
      switch (opt) {
        case 'D': db_fn = optarg; break;
        default: printf("Bad argument %c\n", opt); exit(-1);
      }
    }
  }
  if (!db_fn) {
    printf("Please specify -D\n");
    return -1;
  }

  if(db_init(db_fn)) { return -1; }

  if(argc-optind>0) {
           if(!strcasecmp("INIT", argv[optind])) {
      const char *db_init_sql = DATABASE_SCHEMA_CREATE;
      int ret = sqlite3_exec(db, db_init_sql, NULL, NULL, NULL);
      assert(ret == SQLITE_OK);
    } else if (!strcasecmp("DEBUGUSERS", argv[optind])) {
      const char *db_init_sql =
        "INSERT OR REPLACE INTO users (user_id, email, pwsalt, pw, admin) VALUES "
          "(0, \"admin@example.com\", x'1234', pwhash(x'1234',\"admin\"), 1);"
        "INSERT OR REPLACE INTO users (user_id, email, pwsalt, pw, admin) VALUES "
          "(1, \"user@example.com\", x'2345', pwhash(x'2345',\"user\"), 0);"
        "INSERT OR REPLACE INTO rfid (user_id, rfid_type, token) VALUES"
          "(0, 1, \"34008159C3\");"
        "INSERT OR REPLACE INTO rfid (user_id, rfid_type, token) VALUES"
          "(1, 1, \"3400B6FE53\");";
      int ret = sqlite3_exec(db, db_init_sql, NULL, NULL, NULL);
      assert(ret == SQLITE_OK);
    } else if(!strcasecmp("EXEC", argv[optind])) { 
      char *err;
      int ret = sqlite3_exec(db, argv[optind+1], NULL, NULL, &err);
      if (ret != SQLITE_OK) {
        fprintf(stderr, "Can't execute SQL: %s (%s)\n", sqlite3_errmsg(db), err);
        if(err) { sqlite3_free(err); }
        mret = -1;
      } else {
        fprintf(stderr, "Successfully executed SQL\n");
      }
    } else if(!strcasecmp("UPW", argv[optind])) { 
      mret = db_cmd_user_arg(argv, argc, optind,
          "INSERT OR REPLACE INTO users (email,pwsalt,pw)"
          " SELECT ?1, pwsalt, pwhash(pwsalt, ?2)"
          " FROM (SELECT randomblob(10) AS pwsalt);");
    } else if(!strcasecmp("ADMIN", argv[optind])) { 
      mret = db_cmd_user_arg(argv, argc, optind,
          "UPDATE users SET admin = ?2 WHERE email = ?1");
    } else if(!strcasecmp("ENABLE", argv[optind])) { 
      mret = db_cmd_user_arg(argv, argc, optind,
          "UPDATE users SET enable = ?2 WHERE email = ?1");
    } else if(!strcasecmp("ADDRFID", argv[optind])) { 
      mret = db_cmd_user_arg(argv, argc, optind,
          "INSERT OR REPLACE INTO rfid (user_id,token)"
          " SELECT user_id, ?2 FROM users WHERE email = ?1;");
    } else {
      fprintf(stderr, "Unknown verb: %s\n", argv[optind]);
      mret = -1;
    }
  }

  db_deinit();
  return mret;
}



//                                                                      }}}

// vim: set foldmethod=marker:ts=2:expandtab
