/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

#ifndef _LOFDC_COMMON_DB_H_
#define _LOFDC_COMMON_DB_H_

#include <sqlite3.h>

/* We have several RFID readers; distinguish entries in the database by
 * their type
 */
enum rfid_token_type {
  RFID_TOKTY_PARALLAX = 1,
  RFID_TOKTY_MYFARE = 2
};

/* XXX? rfid entries not salted due to incredibly small search space
 * (32 bits!); we may as well be honest and spare the CPU cycles.
 */

#define DATABASE_SCHEMA_CREATE                          \
    "CREATE TABLE IF NOT EXISTS users ("                \
      "user_id INTEGER PRIMARY KEY ASC,"                \
      "email TEXT NOT NULL UNIQUE ON CONFLICT ABORT,"   \
      "pwsalt BLOB NOT NULL,"                           \
      "pw BLOB NOT NULL,"                               \
      "admin BOOLEAN NOT NULL DEFAULT 0"                \
    ");"                                                \
    "CREATE TABLE IF NOT EXISTS rfid ("                 \
      "rfid_id INTEGER PRIMARY KEY ASC,"                \
      "user_id INTEGER NOT NULL,"                       \
      "rfid_type INTEGER NOT NULL,"                     \
      "token BLOB NOT NULL,"                            \
      "FOREIGN KEY (user_id) REFERENCES users(user_id)" \
    ");"                                                \


#if 0 // not yet
    "CREATE TABLE IF NOT EXISTS acl ("                  \
      "acl_id INTEGER PRIMARY KEY ASC,"                 \
      "user_id INTEGER NOT NULL,"                       \
      "lock_id STRING NOT NULL,"                        \
      "FOREIGN KEY (user_id) REFERENCES users(user_id)" \
    ");"                                                \
    "CREATE TABLE IF NOT EXISTS log ("                  \
      "log_id INTEGER PRIMARY KEY ASC,"                 \
      "user_id INTEGER,"                                \
      "time TEXT,"                                      \
      "message TEXT,"                                   \
      "FOREIGN KEY (user_id) REFERENCES users(user_id)" \
    ");"
#endif

int db_init_pwhash(sqlite3 *db);

#endif
