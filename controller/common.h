/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

#ifndef _LOFDC_MISC_COMMON_H_
#define _LOFDC_MISC_COMMON_H_

/* Anybody else who wants to use the store here to authenticate has to use
 * the same PBKDF parameters that we use for hashing.
 */
#define DB_PWHASH_ITERS  1000
#define DB_PWHASH_OUTLEN 20

#define DATABASE_SCHEMA_CREATE                          \
    "CREATE TABLE IF NOT EXISTS users ("                \
      "user_id INTEGER PRIMARY KEY ASC,"                \
      "email TEXT NOT NULL UNIQUE ON CONFLICT ABORT,"   \
      "pwsalt BLOB NOT NULL,"                           \
      "pw BLOB NOT NULL,"                               \
      "admin BOOLEAN NOT NULL DEFAULT 0,"               \
      "enabled BOOLEAN NOT NULL DEFAULT 1"              \
    ");"                                                \
    "CREATE TABLE IF NOT EXISTS rfid ("                 \
      "rfid_id INTEGER PRIMARY KEY ASC,"                \
      "user_id INTEGER NOT NULL,"                       \
      "token TEXT NOT NULL UNIQUE ON CONFLICT ABORT,"   \
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
