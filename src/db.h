#pragma once
#include <string>
#include <vector>
#include <libpq-fe.h>
#include "indexer.h"

class Database {
public:
    Database() : conn(nullptr) {}
    ~Database() { if (conn) PQfinish(conn); }

    bool createUser(const std::string &username, const std::string &hash, const std::string &role) {
        const char *sql =
            "INSERT INTO users (username, password_hash, role) VALUES ($1, $2, $3) "
            "ON CONFLICT (username) DO NOTHING RETURNING id";
        const char *params[] = {username.c_str(), hash.c_str(), role.c_str()};
        PGresult *res = PQexecParams(conn, sql, 3, nullptr, params, nullptr, nullptr, 0);
        bool ok = PQntuples(res) > 0;
        PQclear(res);
        return ok;
    }

    bool verifyUser(const std::string &username, const std::string &hash, std::string &outRole) {
        const char *sql = "SELECT role FROM users WHERE username=$1 AND password_hash=$2";
        const char *params[] = {username.c_str(), hash.c_str()};
        PGresult *res = PQexecParams(conn, sql, 2, nullptr, params, nullptr, nullptr, 0);
        bool ok = PQntuples(res) > 0;
        if (ok) outRole = PQgetvalue(res, 0, 0);
        PQclear(res);
        return ok;
    }
    
    bool connect(const std::string &connStr) {
        conn = PQconnectdb(connStr.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            fprintf(stderr, "DB connect failed: %s\n", PQerrorMessage(conn));
            PQfinish(conn);
            conn = nullptr;
            return false;
        }
        return createSchema();
    }

    bool isConnected() const {
        return conn && PQstatus(conn) == CONNECTION_OK;
    }

    bool saveDocument(const std::string &path, const std::string &content,
                      long size_bytes, long mtime) {
        const char *sql =
            "INSERT INTO documents (path, content, size_bytes, mtime) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (path) DO UPDATE SET "
            "content=EXCLUDED.content, size_bytes=EXCLUDED.size_bytes, "
            "mtime=EXCLUDED.mtime, indexed_at=NOW()";
        std::string sizeStr = std::to_string(size_bytes);
        std::string mtimeStr = std::to_string(mtime);
        const char *params[] = {
            path.c_str(), content.c_str(), sizeStr.c_str(), mtimeStr.c_str()
        };
        PGresult *res = PQexecParams(conn, sql, 4, nullptr, params, nullptr, nullptr, 0);
        bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
        PQclear(res);
        return ok;
    }

    std::vector<Document> loadAllDocuments() {
        std::vector<Document> docs;
        PGresult *res = PQexec(conn,
            "SELECT path, content, size_bytes, mtime FROM documents ORDER BY id");
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            return docs;
        }
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            Document d;
            d.path       = PQgetvalue(res, i, 0);
            d.content    = PQgetvalue(res, i, 1);
            d.size_bytes = atol(PQgetvalue(res, i, 2));
            d.mtime      = atol(PQgetvalue(res, i, 3));
            docs.push_back(d);
        }
        PQclear(res);
        return docs;
    }

    int documentCount() {
        PGresult *res = PQexec(conn, "SELECT COUNT(*) FROM documents");
        int count = 0;
        if (PQresultStatus(res) == PGRES_TUPLES_OK)
            count = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return count;
    }

private:
    PGconn *conn;

    bool createSchema() {
        const char *sql =
            "CREATE TABLE IF NOT EXISTS documents ("
            "  id SERIAL PRIMARY KEY,"
            "  path TEXT UNIQUE NOT NULL,"
            "  content TEXT,"
            "  size_bytes BIGINT DEFAULT 0,"
            "  mtime BIGINT DEFAULT 0,"
            "  indexed_at TIMESTAMP DEFAULT NOW()"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_documents_path ON documents(path);";

            "CREATE TABLE IF NOT EXISTS users ("
            "  id SERIAL PRIMARY KEY,"
            "  username TEXT UNIQUE NOT NULL,"
            "  password_hash TEXT NOT NULL,"
            "  role TEXT DEFAULT 'user',"
            "  created_at TIMESTAMP DEFAULT NOW()"
            ");"
        PGresult *res = PQexec(conn, sql);
        bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
        PQclear(res);
        return ok;
    }
};