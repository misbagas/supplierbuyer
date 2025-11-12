#include "CivetServer.h"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <unordered_set>
#include <iostream>
#include <string>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <thread>
#include <mutex> // Added by HEAD
#include <chrono>
#include <csignal>
extern "C" {
    #include "civetweb.h"
}
#include "sqlite3.h"
#include <unordered_map> // Added by HEAD
#include <random> // Added by HEAD
#include <map> // Added by HEAD
#include <sstream> // ✅ ADDED: Required for JSON escaping
#include <iomanip> // ✅ ADDED: Required for JSON escaping
#include "civetweb.h"
// ✅ ADDED: Helper function to safely escape a string for JSON

std::string current_date_string() {
    time_t t = time(nullptr);
    struct tm buf;
    char str[20];
    localtime_s(&buf, &t);
    strftime(str, sizeof(str), "%Y-%m-%d", &buf);
    return str;
}

std::string escapeJsonString(const std::string& input) {
    if (input.empty()) {
        return "";
    }
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b";  break;
            case '\f': oss << "\\f";  break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    // Control characters must be escaped
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}


// Safe DB open helper: never crashes, just logs and returns nullptr
sqlite3* safe_open(const char* path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        if (db) {
            fprintf(stderr, "⚠️ DB open failed (%s): %s\n", path, sqlite3_errmsg(db));
            sqlite3_close(db);
        } else {
            fprintf(stderr, "⚠️ DB open failed (%s): no handle\n", path);
        }
        return nullptr;
    }
    return db;
}

#ifndef FORM_FIELD_STORAGE_GET
#define FORM_FIELD_STORAGE_GET 1
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

std::unordered_map<std::string, std::string> sessions;
std::mutex sessions_mutex;

// Generate a random session ID
std::string generateSessionId() {
    static const char alphanum[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string sid;
    for (int i = 0; i < 32; ++i) {
        sid += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return sid;
}

// Get username from cookie
std::string getUsernameFromCookie(mg_connection *conn) {
    const char *cookie = mg_get_header(conn, "Cookie");
    if (!cookie) return "";
    std::string cookies(cookie);
    size_t pos = cookies.find("session_id=");
    if (pos == std::string::npos) return "";
    std::string sid = cookies.substr(pos + 11);

    // ✅ cut off at ; if multiple cookies are set
    size_t semicolon = sid.find(';');
    if (semicolon != std::string::npos) {
        sid = sid.substr(0, semicolon);
    }

    if (sessions.count(sid)) return sessions[sid];
    return "";
}


sqlite3 *usersDb = nullptr; 
const char *USERS_DB_PATH = "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer.db";
const char *PRODUCTS_DB_PATH = "C:/Users/priva/OneDrive/Desktop/supplierbuyer/products.db";
volatile sig_atomic_t running = 1;
// ---------------- Signal Handler ----------------
void signal_handler(int) {
    running = false;
}
// ---------------- Callbacks ----------------
// Global variables to manage the file stream and filename during the upload
static std::ofstream outputFileStream;
static std::string uploadedFilename;
// ---------------- Register & Login ----------------

static int handle_register(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/auth/joinpage.html");
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        // --- 1. Read Post Data ---
        int len = (int)ri->content_length;
        if (len <= 0 || len > 4096) len = 4096;
        std::vector<char> post_data(len + 1);
        int n = mg_read(conn, post_data.data(), len);
        post_data[n] = '\0';

        char username[128], password[128], question[256], answer[128];
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));
        mg_get_var(post_data.data(), len, "security_question", question, sizeof(question));
        mg_get_var(post_data.data(), len, "security_answer", answer, sizeof(answer));

        if (strlen(username) == 0 || strlen(password) == 0) {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /auth/joinpage.html?error=empty\r\nContent-Length: 0\r\n\r\n");
            return 302;
        }

        // --- 2. Open DB (One time) ---
        sqlite3 *db = safe_open(USERS_DB_PATH);
        if (!db) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
            return 500;
        }

        // --- 3. Check if user ALREADY exists in main 'users' table ---
        sqlite3_stmt *check_stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM users WHERE username=?;", -1, &check_stmt, nullptr) != SQLITE_OK) {
            fprintf(stderr, "Prepare error (check users): %s\n", sqlite3_errmsg(db));
            sqlite3_close(db); // Close on error
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nCheck failed");
            return 500;
        }
        sqlite3_bind_text(check_stmt, 1, username, -1, SQLITE_TRANSIENT);
        
        int check_rc = sqlite3_step(check_stmt);
        sqlite3_finalize(check_stmt); // Finalize immediately after use

        if (check_rc == SQLITE_ROW) {
            sqlite3_close(db); // Close before redirect
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: /auth/joinpage.html?error=alreadyregistered\r\n"
                "Content-Length: 0\r\n\r\n");
            return 302;
        }
        // (If check_rc is SQLITE_DONE, user does not exist, so we continue)

        // --- 4. Ensure 'pending_users' table exists ---
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS pending_users ("
            "username TEXT PRIMARY KEY, "
            "password TEXT, "
            "security_question TEXT, "
            "security_answer TEXT);",
            nullptr, nullptr, nullptr);

        // --- 5. Insert into 'pending_users' ---
        sqlite3_stmt *stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO pending_users (username, password, security_question, security_answer) VALUES (?, ?, ?, ?);";
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            fprintf(stderr, "Prepare error (insert pending): %s\n", sqlite3_errmsg(db));
            sqlite3_close(db); // Close on error
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nPrepare failed");
            return 500;
        }

        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, question, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, answer, -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        
        // --- 6. Handle result and close DB ---
        if (rc == SQLITE_DONE) {
            // Success! Redirect to payment.
            sqlite3_finalize(stmt);
            sqlite3_close(db); // Close DB before redirecting
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: https://nowpayments.io/payment/?iid=4491821266" // This is the "pay now"
                "Content-Length: 0\r\n\r\n");
        } else {
            // Failure. Log the error (db is still open, so errmsg works)
            fprintf(stderr, "⚠️ handle_register: sqlite3_step failed: %s (code %d)\n", sqlite3_errmsg(db), rc);
            sqlite3_finalize(stmt);
            sqlite3_close(db); // Close DB before redirecting
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: /auth/joinpage.html?error=dberror\r\n"
                "Content-Length: 0\r\n\r\n");
        }
        return 302;
    }

    return 405; // Method Not Allowed
}

static int handle_confirm_payment(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    char username[128] = "";

    if (ri->query_string)
        mg_get_var(ri->query_string, strlen(ri->query_string), "username", username, sizeof(username));

    if (strlen(username) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing username");
        return 400;
    }

    sqlite3 *db = safe_open(USERS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB unavailable");
        return 500;
    }

    // Move user from pending_users -> users
    const char *insertSQL =
        "INSERT OR IGNORE INTO users (username, password, security_question, security_answer) "
        "SELECT username, password, security_question, security_answer FROM pending_users WHERE username=?;";
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Delete from pending_users
    sqlite3_prepare_v2(db, "DELETE FROM pending_users WHERE username=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    
    // Calculate expiry (next month)

    // Calculate expiry per second (1 second duration)
time_t now = time(nullptr);
sqlite3_int64 current_time = static_cast<sqlite3_int64>(now);
sqlite3_int64 expiry_time = current_time + 1; // Add 1 second

// Update subscriptions with per-second billing
const char *subSQL =
    "INSERT OR REPLACE INTO subscriptions (username, last_payment, expiry_date) VALUES (?, ?, ?);";
if (sqlite3_prepare_v2(db, subSQL, -1, &stmt, nullptr) != SQLITE_OK) {
    fprintf(stderr, "Confirm payment (SUB) prepare error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB error 3");
    return 500;
}
sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
sqlite3_bind_int64(stmt, 2, current_time);
sqlite3_bind_int64(stmt, 3, expiry_time);
sqlite3_step(stmt);
sqlite3_finalize(stmt);
sqlite3_close(db);

mg_printf(conn,
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nActivated %s for 1 second billing period",
    username);
return 200;
}


static int handle_login(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/auth/loginpage.html");
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        int len = (int)ri->content_length;
        if (len <= 0 || len > 4096) len = 4096;
        std::vector<char> post_data(len + 1);
        int n = mg_read(conn, post_data.data(), len);
        post_data[n] = '\0';

        char username[128], password[128];
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));

        sqlite3 *db = safe_open(USERS_DB_PATH);
        if (!db) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB unavailable");
            return 500;
        }

        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT password FROM users WHERE username=?;", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *storedPass = (const char*)sqlite3_column_text(stmt, 0);
            if (storedPass && strcmp(storedPass, password) == 0) {
                // Check subscription
                sqlite3_stmt *substmt = nullptr;
                sqlite3_prepare_v2(db, "SELECT expiry_date FROM subscriptions WHERE username=?;", -1, &substmt, nullptr);
                sqlite3_bind_text(substmt, 1, username, -1, SQLITE_TRANSIENT);

                int hasSub = (sqlite3_step(substmt) == SQLITE_ROW);
                sqlite3_int64 expiry_time = hasSub ? sqlite3_column_int64(substmt, 0) : 0;
time_t now = time(nullptr);

if (!hasSub || expiry_time <= static_cast<sqlite3_int64>(now)) {
    // Expired or missing subscription — redirect to billing
    mg_printf(conn,
        "HTTP/1.1 302 Found\r\n"
        "Location: https://nowpayments.io/payment/?iid=4491821266"
        "Content-Length: 0\r\n\r\n");
    sqlite3_finalize(substmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 302;
}

                sqlite3_finalize(substmt);
                std::string sid = generateSessionId();
                sessions[sid] = username;
                mg_printf(conn,
                    "HTTP/1.1 302 Found\r\nSet-Cookie: session_id=%s; HttpOnly; Path=/\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\nContent-Length: 0\r\n\r\n",
                    sid.c_str());
            } else {
                mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /auth/loginpage.html?error=invalid\r\nContent-Length: 0\r\n\r\n");
            }
        } else {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /auth/loginpage.html?error=usernotfound\r\nContent-Length: 0\r\n\r\n");
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 302;
    }

    return 405;
}

int handle_admin(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *req_info = mg_get_request_info(conn);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Connection: close\r\n\r\n");

    mg_printf(conn,
              "<html><body><h1>Admin Page</h1>"
              "<p>Requested URI: %s</p></body></html>",
              req_info->local_uri);

    return 200; // must return an int
}


// Global variables for SQLite connections (as used in the original code)
// Global variables for SQLite connections

// Parse multipart/form-data and save uploaded file
bool parseMultipart(struct mg_connection *conn,
                    std::map<std::string, std::string> &fields,
                    std::string &savedFilePath) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    long long len = ri->content_length;
    if (len <= 0) return false;

    std::vector<char> body(len);
    mg_read(conn, body.data(), (size_t)len);
    std::string data(body.begin(), body.end());

    // Extract boundary from Content-Type
    const char *ctype = mg_get_header(conn, "Content-Type");
    if (!ctype) return false;
    std::string ct(ctype);
    size_t bpos = ct.find("boundary=");
    if (bpos == std::string::npos) return false;
    std::string boundary = "--" + ct.substr(bpos + 9);

    size_t pos = 0;
    while ((pos = data.find(boundary, pos)) != std::string::npos) {
        size_t header_end = data.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) break;
        std::string header = data.substr(pos, header_end - pos);
        size_t next = data.find(boundary, header_end);
        if (next == std::string::npos) break;
        std::string content = data.substr(header_end + 4,
                                          next - header_end - 6); // cut \r\n before boundary

        // Parse "name" and "filename"
        std::string name, filename;
        size_t npos = header.find("name=\"");
        if (npos != std::string::npos) {
            size_t nend = header.find("\"", npos + 6);
            name = header.substr(npos + 6, nend - (npos + 6));
        }
        size_t fpos = header.find("filename=\"");
        if (fpos != std::string::npos) {
            size_t fend = header.find("\"", fpos + 10);
            filename = header.substr(fpos + 10, fend - (fpos + 10));
        }

        if (!filename.empty()) {
            // save uploaded file
            std::string uploadDir = "C:/Users/priva/OneDrive/Desktop/supplierbuyer/uploads/";
#ifdef _WIN32
            _mkdir(uploadDir.c_str());
#else
            mkdir(uploadDir.c_str(), 0777);
#endif
            std::string outPath = uploadDir + filename;
            std::ofstream ofs(outPath, std::ios::binary);
            ofs.write(content.c_str(), content.size());
            ofs.close();
            savedFilePath = "/uploads/" + filename;
        } else if (!name.empty()) {
            fields[name] = content;
        }

        pos = next;
    }
    return true;
}

int main() {
    
    // Register signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize databases   
    if (sqlite3_open(USERS_DB_PATH, &usersDb) != SQLITE_OK) {
    fprintf(stderr, "⚠️ Warning: Cannot open users database: %s\n", sqlite3_errmsg(usersDb));
    usersDb = nullptr; // mark as unavailable
} else {

sqlite3 *subdb = nullptr;
if (sqlite3_open(USERS_DB_PATH, &subdb) == SQLITE_OK) {
    sqlite3_exec(subdb,
        "CREATE TABLE IF NOT EXISTS subscriptions ("
        "username TEXT PRIMARY KEY,"
        "last_payment INTEGER,"
        "expiry_date INTEGER);",
        nullptr, nullptr, nullptr);
    sqlite3_close(subdb);
}


sqlite3_exec(usersDb,
    "CREATE TABLE IF NOT EXISTS users ("
    "username TEXT PRIMARY KEY, "
    "password TEXT, "
    "security_question TEXT, "
    "security_answer TEXT);",
    nullptr, nullptr, nullptr);

    sqlite3_close(usersDb);
    usersDb = nullptr;

}





sqlite3 *db;
if (sqlite3_open(USERS_DB_PATH, &db) == SQLITE_OK) {
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN security_question TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN security_answer TEXT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
}


// Open products DB (non-fatal if fails)
sqlite3 *pdb = nullptr;
if (sqlite3_open(PRODUCTS_DB_PATH, &pdb) != SQLITE_OK) {
    fprintf(stderr, "⚠️ Warning: Cannot open products database: %s\n", sqlite3_errmsg(pdb));
    pdb = nullptr; // mark as unavailable
} else {
    sqlite3_exec(pdb,
        "CREATE TABLE IF NOT EXISTS products ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "minPrice REAL, "
        "maxPrice REAL, "
        "image TEXT, "
        "priceUnits TEXT, "
        "owner TEXT, "
        "tags TEXT"
        ");",
        nullptr, nullptr, nullptr);

    // Try safe ALTERs (ignore errors if columns already exist)
    sqlite3_exec(pdb, "ALTER TABLE products ADD COLUMN owner TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(pdb, "ALTER TABLE products ADD COLUMN tags TEXT;", nullptr, nullptr, nullptr);

    sqlite3_close(pdb);
}


 // Close the products DB connection, it will be opened/closed per-request
sqlite3 *msgdb = nullptr;
if (sqlite3_open(PRODUCTS_DB_PATH, &msgdb) == SQLITE_OK) {
    sqlite3_exec(msgdb,
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "product_id INTEGER, "
        "sender TEXT, "
        "receiver TEXT, "
        "message TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);",
        nullptr, nullptr, nullptr);
    sqlite3_close(msgdb);
}

sqlite3 *reqdb = nullptr;
if (sqlite3_open(PRODUCTS_DB_PATH, &reqdb) == SQLITE_OK) {
sqlite3_exec(reqdb,
    "CREATE TABLE IF NOT EXISTS requests ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "destination TEXT,"
    "target_price REAL,"
    "suppliers_from TEXT,"
    "req_date TEXT,"
    "payment_terms TEXT,"
    "description TEXT,"
    "image TEXT,"
    "requester TEXT,"
    "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
    ");",
    nullptr, nullptr, nullptr);

    sqlite3_close(reqdb);

}
sqlite3_exec(usersDb,
    "CREATE TABLE IF NOT EXISTS pending_users ("
    "username TEXT PRIMARY KEY, "
    "password TEXT, "
    "security_question TEXT, "
    "security_answer TEXT);",
    nullptr, nullptr, nullptr);


sqlite3_exec(pdb,
    "CREATE TABLE IF NOT EXISTS messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "sender TEXT, "
    "receiver TEXT, "
    "product_id INTEGER, "
    "request_id INTEGER, "
    "message TEXT, "
    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
    ");",
    nullptr, nullptr, nullptr);



sqlite3 *adb = nullptr;
if (sqlite3_open(PRODUCTS_DB_PATH, &adb) == SQLITE_OK) {
    sqlite3_exec(adb,
        "CREATE TABLE IF NOT EXISTS admin ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT, "
        "email TEXT);",
        nullptr, nullptr, nullptr);

    // Insert default row if empty
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(adb, "SELECT COUNT(*) FROM admin;", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
        sqlite3_exec(adb,
            "INSERT INTO admin (id, name, email) VALUES (1, 'Default Admin', 'admin@example.com');",
            nullptr, nullptr, nullptr);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(adb);
}


    const char *options[] = {
        "document_root", "C:/Users/priva/OneDrive/Desktop/supplierbuyer",
        "listening_ports", "8080",
        "enable_directory_listing", "no",
        "extra_mime_types", ".js=application/javascript,.css=text/css,.jpg=image/jpeg,.png=image/png",
        "index_files", "supplierbuyer.html",
        "max_request_size", "524288000", 
        0
    };

    mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = [](const struct mg_connection *, const char *msg) {
        printf("CivetWeb log: %s\n", msg);
        return 0;
    };

mg_context *ctx = mg_start(&callbacks, nullptr, options);
if (!ctx) {
    fprintf(stderr, "⚠️ CivetWeb failed to start!\n");
    ctx = nullptr; // keep running, but without HTTP service
}


    if (ctx) {

// Register login and register handlers
        mg_set_request_handler(ctx, "/login", handle_login, nullptr);
        mg_set_request_handler(ctx, "/register", handle_register, nullptr);

        // Optionally, serve the HTML pages directly if requested
        mg_set_request_handler(ctx, "/auth/loginpage.html", [](mg_connection *conn, void *) -> int {
            mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/auth/loginpage.html");
            return 200;
        }, nullptr);

        mg_set_request_handler(ctx, "/auth/joinpage.html", [](mg_connection *conn, void *) -> int {
            mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/auth/joinpage.html");
            return 200;
        }, nullptr);



mg_set_request_handler(ctx, "/submit_request", [](mg_connection *conn, void *) -> int {
    // Use parseMultipart to handle the form data and file upload
    std::map<std::string, std::string> fields;
    std::string imagePath;
    parseMultipart(conn, fields, imagePath);

    // Get form fields from the map
    std::string destination = fields["destination"];
    std::string target_price = fields["target_price"];
    std::string suppliers_from = fields["suppliers_from"];
    std::string req_date = fields["req_date"];
    std::string payment_terms = fields["payment_terms"];
    std::string description = fields["description"];

    std::string requester = getUsernameFromCookie(conn);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    // Updated SQL query to include the 'image' column
    const char *sql = "INSERT INTO requests (destination, target_price, suppliers_from, req_date, payment_terms, description, image, requester) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Submit request prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }

    // Bind all parameters, including the image path
    sqlite3_bind_text(stmt, 1, destination.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, atof(target_price.c_str()));
    sqlite3_bind_text(stmt, 3, suppliers_from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, req_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, payment_terms.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, imagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, requester.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /request_product.html\r\n"
        "Content-Length: 0\r\n\r\n");
    return 303;
}, nullptr);

} // <-- This brace was misplaced, it should not close main() yet.
  // It was closing the `if (ctx)` block, which I will continue adding handlers to.


mg_set_request_handler(ctx, "/api/messages", [](mg_connection *conn, void *) -> int {
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    // ✅ Make sure we get all messages in chronological order
    const char *sql = 
        "SELECT id, product_id, sender, receiver, message, "
        "strftime('%Y-%m-%d %H:%M:%S', created_at) AS created_at "
        "FROM messages "
        "WHERE message IS NOT NULL "
        "ORDER BY id ASC;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "API messages prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    std::string json = "[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) json += ",";
        first = false;

        const char* sender = (const char*)sqlite3_column_text(stmt, 2);
        const char* receiver = (const char*)sqlite3_column_text(stmt, 3);
        const char* message = (const char*)sqlite3_column_text(stmt, 4);
        const char* created_at = (const char*)sqlite3_column_text(stmt, 5);
        int product_id = sqlite3_column_int(stmt, 1);
        int id = sqlite3_column_int(stmt, 0);

        // ✅ FIXED: Use ostringstream and escapeJsonString to build valid JSON
        std::ostringstream oss;
        oss << R"({"id":)" << id
            << R"(,"product_id":)" << product_id
            << R"(,"sender":")" << escapeJsonString(sender ? sender : "") << R"(")"
            << R"(,"receiver":")" << escapeJsonString(receiver ? receiver : "") << R"(")"
            << R"(,"message":")" << escapeJsonString(message ? message : "") << R"(")"
            << R"(,"created_at":")" << (created_at ? created_at : "") << R"("})";
        json += oss.str();
    }
    json += "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s",
        json.c_str());
    return 200;
}, nullptr);



    mg_set_request_handler(ctx, "/admin", handle_admin, nullptr);


    mg_set_request_handler(ctx, "/api/product", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    char id_str[32] = {0};
    if (ri->query_string) {
        mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));
    }
    int id = atoi(id_str);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, name, description, minPrice, image, priceUnits FROM products WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "API product prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_int(stmt, 1, id);

    std::string json = "{}";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // ✅ FIXED: Also escape JSON here for safety
        std::ostringstream oss;
        oss << R"({"id":)" << sqlite3_column_int(stmt, 0)
            << R"(,"name":")" << escapeJsonString((const char*)sqlite3_column_text(stmt, 1)) << R"(")"
            << R"(,"description":")" << escapeJsonString((const char*)sqlite3_column_text(stmt, 2)) << R"(")"
            << R"(,"price":)" << sqlite3_column_double(stmt, 3)
            << R"(,"unit":")" << escapeJsonString((const char*)sqlite3_column_text(stmt, 5)) << R"(")"
            << R"(,"image":")" << escapeJsonString((const char*)sqlite3_column_text(stmt, 4)) << R"("})";
        json = oss.str();
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        json.size(), json.c_str());

    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/api/products", [](mg_connection *conn, void *) -> int {
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, name, description, minPrice, image, priceUnits FROM products;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "API products prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }

    std::string json = "[";
    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) json += ",";
        first = false;

        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc = (const char*)sqlite3_column_text(stmt, 2);
        double price = sqlite3_column_double(stmt, 3);
        const char *image = (const char*)sqlite3_column_text(stmt, 4);
        const char *unit = (const char*)sqlite3_column_text(stmt, 5);

        // ✅ FIXED: Also escape JSON here for safety
        std::ostringstream oss;
        oss << R"({"id":)" << id
            << R"(,"name":")" << escapeJsonString(name ? name : "") << R"(")"
            << R"(,"description":")" << escapeJsonString(desc ? desc : "") << R"(")"
            << R"(,"price":)" << price
            << R"(,"unit":")" << escapeJsonString(unit ? unit : "unit") << R"(")"
            << R"(,"image":")" << escapeJsonString(image ? image : "") << R"("})";
        json += oss.str();
    }
    json += "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        json.size(), json.c_str());

    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerhome.html", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    std::string currentUser = getUsernameFromCookie(conn);
if (currentUser.empty()) {
    mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /login\r\n\r\n");
    return 302;
}

// --- BILLING CHECK ---
sqlite3 *billing_db = safe_open(USERS_DB_PATH);
sqlite3_stmt *billing_stmt = nullptr;
sqlite3_prepare_v2(billing_db, "SELECT expiry_date FROM subscriptions WHERE username=?;", -1, &billing_stmt, nullptr);
sqlite3_bind_text(billing_stmt, 1, currentUser.c_str(), -1, SQLITE_TRANSIENT);
if (sqlite3_step(billing_stmt) == SQLITE_ROW) {
    const char *expiry = (const char *)sqlite3_column_text(billing_stmt, 0);
        // Get current date string in YYYY-MM-DD format
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
    #ifdef _WIN32
        localtime_s(&local_tm, &tt);
    #else
        localtime_r(&tt, &local_tm);
    #endif
        char date_string[11];
        strftime(date_string, sizeof(date_string), "%Y-%m-%d", &local_tm);
        
        if (!expiry || std::string(expiry) < std::string(date_string)) {
        sqlite3_finalize(billing_stmt);
        sqlite3_close(billing_db);
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: https://nowpayments.io/payment/?iid=4491821266\r\n\r\n");
        return 302;
    }
} else {
    // no record = must pay first
    sqlite3_finalize(billing_stmt);
    sqlite3_close(billing_db);
    mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: https://nowpayments.io/payment/?iid=4491821266\r\n\r\n");
    return 302;
}
sqlite3_finalize(billing_stmt);
sqlite3_close(billing_db);

    char search[256] = {0};
    char tag[128] = {0};
    if (ri->query_string) {
        mg_get_var(ri->query_string, strlen(ri->query_string), "search", search, sizeof(search));
        mg_get_var(ri->query_string, strlen(ri->query_string), "tag", tag, sizeof(tag));
    }

    std::string searchTerm(search);
    std::string selectedTag(tag);
    bool hasSearch = !searchTerm.empty();
    bool hasTag = !selectedTag.empty();

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    // 🔹 Get all unique tags first
    std::unordered_set<std::string> allTags;
    sqlite3_stmt *tagStmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT tags FROM products WHERE tags IS NOT NULL;", -1, &tagStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(tagStmt) == SQLITE_ROW) {
            const char *tagsStr = (const char *)sqlite3_column_text(tagStmt, 0);
            if (tagsStr) {
                std::istringstream ss(tagsStr);
                std::string tag;
                while (std::getline(ss, tag, ',')) {
                    std::string trimmed;
                    for (char c : tag) if (!isspace((unsigned char)c)) trimmed += c;
                    if (!trimmed.empty()) allTags.insert(trimmed);
                }
            }
        }
    }
    sqlite3_finalize(tagStmt);

    // 🔹 Build main query
    sqlite3_stmt *stmt = nullptr;
    std::string sql;

    if (hasSearch || hasTag) {
        sql = R"(
            SELECT id, name, description, minPrice, image, priceUnits, tags, 'product' AS type
            FROM products
            WHERE (name LIKE ? OR description LIKE ? OR tags LIKE ?)
        )";
        if (hasTag) sql += " AND tags LIKE ?";
        sql += R"(
            UNION ALL
            SELECT id, destination AS name, description, target_price AS minPrice, image, 'request' AS priceUnits, NULL AS tags, 'request' AS type
            FROM requests
            WHERE (destination LIKE ? OR description LIKE ?)
            ORDER BY id DESC;
        )";
    } else {
        sql = R"(
            SELECT id, name, description, minPrice, image, priceUnits, tags, 'product' AS type FROM products
            UNION ALL
            SELECT id, destination AS name, description, target_price AS minPrice, image, 'request' AS priceUnits, NULL AS tags, 'request' AS type FROM requests
            ORDER BY id DESC;
        )";
    }

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Home query error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    int bindIndex = 1;
    if (hasSearch || hasTag) {
        std::string like = "%" + searchTerm + "%";
        sqlite3_bind_text(stmt, bindIndex++, like.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, like.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, like.c_str(), -1, SQLITE_TRANSIENT);
        if (hasTag) {
            std::string tagLike = "%" + selectedTag + "%";
            sqlite3_bind_text(stmt, bindIndex++, tagLike.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, bindIndex++, like.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, like.c_str(), -1, SQLITE_TRANSIENT);
    }

    // 🔹 Render HTML
mg_printf(conn,
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
    "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Supplier Buyer Home</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#f4f6f9;margin:0;padding:0;}"
    "h1{text-align:center;margin:20px;color:#222;}"
    ".navbar{display:flex;align-items:center;justify-content:space-between;"
    "background-color:#0077cc;color:white;padding:12px 30px;box-shadow:0 2px 6px rgba(0,0,0,0.15);position:sticky;top:0;z-index:1000;}"
    ".navbar .brand{font-size:20px;font-weight:bold;letter-spacing:1px;}"
    ".navbar ul{list-style:none;margin:0;padding:0;display:flex;gap:25px;}"
    ".navbar ul li{display:inline;}"
    ".navbar ul li a{color:white;text-decoration:none;font-size:15px;transition:color 0.3s;}"
    ".navbar ul li a:hover{color:#cce6ff;}"
    ".container{display:flex;align-items:flex-start;max-width:1300px;margin:0 auto;padding:20px;gap:20px;}"
    ".sidebar{width:22%%;background:#fff;border-radius:10px;padding:15px;box-shadow:0 2px 6px rgba(0,0,0,0.1);height:max-content;}"
    ".tags-box b{display:block;margin-bottom:10px;color:#0077cc;font-size:16px;}"
    ".tag{display:inline-block;background:#e8f4fc;color:#0077cc;padding:6px 10px;margin:4px;border-radius:5px;text-decoration:none;font-size:14px;}"
    ".tag:hover{background:#0077cc;color:white;}"
    ".selected-tag{background:#0077cc;color:white;}"
    ".content{flex:1;}"
    ".search-box{text-align:center;margin-bottom:20px;}"
    ".search-box input{padding:8px;width:250px;border:1px solid #ccc;border-radius:5px;}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:20px;}"
    ".card{background:#fff;border-radius:10px;padding:15px;box-shadow:0 2px 8px rgba(0,0,0,0.1);transition:transform 0.2s;text-align:center;}"
    ".card:hover{transform:translateY(-5px);}img{max-width:100%%;height:200px;object-fit:cover;border-radius:8px;margin-bottom:10px;}"
    ".price{font-size:18px;font-weight:bold;color:#00a6e1;margin-top:10px;}"
    ".tags{margin-top:8px;font-size:13px;color:#666;}"
    "</style></head><body>"
     /* 🔹 Navbar HTML */
    "<div class='navbar'>"
    "  <div class='brand'>SupplierBuyer</div>"
    "  <ul>"
    "    <li><a href='/supplierbuyer/supplierbuyerhome.html'>Home</a></li>"
    "    <li><a href='/supplierbuyer/supplierbuyerdash.html'>My Products</a></li>"
    "    <li><a href='/supplierbuyer/messageadmin.html'>Messages</a></li>"
    "    <li><a href='/supplierbuyer/logout.html'>Logout</a></li>"
    "  </ul>"
    "</div>"
    "<h1>All Products & Requests</h1>"
    "<div class='search-box'>"
    "<form method='GET' action='/supplierbuyer/supplierbuyerhome.html'>"
    "<input type='text' name='search' placeholder='Search...' value='%s'>"
    "<button type='submit'>Search</button>"
    "</form></div>"
    "<div class='container'>"
    "<div class='sidebar'><div class='tags-box'><b>Tags</b><br>",
    search
);

// ✅ Tag display
if (allTags.empty()) {
    mg_printf(conn, "<p style='color:#999;'>No tags found.</p>");
} else {
    for (const auto &t : allTags) {
        mg_printf(conn,
            "<a class='tag %s' href='/supplierbuyer/supplierbuyerhome.html?tag=%s'>#%s</a>",
            (selectedTag == t ? "selected-tag" : ""),
            t.c_str(), t.c_str());
    }
}

mg_printf(conn,
    "</div></div>"  // close sidebar
    "<div class='content'>" // start content area
    "<div class='grid'>"
);

    mg_printf(conn, "</div><div class='grid'>");

    bool hasResults = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        hasResults = true;
        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc = (const char*)sqlite3_column_text(stmt, 2);
        double price = sqlite3_column_double(stmt, 3);
        const char *image = (const char*)sqlite3_column_text(stmt, 4);
        const char *unit = (const char*)sqlite3_column_text(stmt, 5);
        const char *tags = (const char*)sqlite3_column_text(stmt, 6);
        const char *type = (const char*)sqlite3_column_text(stmt, 7);

        std::string imageUrl = (image && strlen(image) > 0)
            ? image : "/uploads/noimage.png";

        mg_printf(conn,
            "<div class='card'>"
            "<a href='%s?id=%d'>"
            "<img src='%s' alt='%s'/>"
            "<h3>%s</h3></a>"
            "<p>%s</p>"
            "<div class='price'>$%.2f %s</div>",
            (strcmp(type, "request") == 0 ? "/request_detail" : "/product_detail"),
            id, imageUrl.c_str(),
            name ? name : "Item",
            name ? name : "Unnamed",
            desc ? desc : "",
            price,
            unit ? unit : ""
        );

        if (tags && strlen(tags) > 0) {
            mg_printf(conn, "<div class='tags'>Tags: %s</div>", tags);
        }

        mg_printf(conn, "</div>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (!hasResults) {
        mg_printf(conn, "<p style='text-align:center;color:#666;'>No results found.</p>");
    }

    mg_printf(conn, "</div></body></html>");
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/slider", [](mg_connection *conn, void *) -> int {
    const mg_request_info *req_info = mg_get_request_info(conn); // ✅ correct way
    std::string uri = req_info->local_uri; // e.g. /slider/1.png

    // Map URI to actual Windows path
    std::string filePath = "C:/Users/priva/OneDrive/Desktop/supplierbuyer" + uri;

    FILE *fp = fopen(filePath.c_str(), "rb");
    if (!fp) {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\nFile not found");
        return 404;
    }

    // Detect MIME type
    const char *mime = "application/octet-stream";
    size_t pos = filePath.find_last_of('.');
    if (pos != std::string::npos) {
        std::string ext = filePath.substr(pos + 1);
        if (ext == "png") mime = "image/png";
        else if (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
        else if (ext == "gif") mime = "image/gif";
    }

    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime);

    char buf[4096];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0) {
        mg_write(conn, buf, bytes);
    }

    fclose(fp);
    return 200;
}, nullptr);

// ✅ REPLACED: This handler was broken (used sqlite3_exec with placeholders)
// It's now fixed using prepared statements and includes the DELETE step.
mg_set_request_handler(ctx, "/api/confirm_payment", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);
    char username[128] = "";

    if (ri->query_string)
        mg_get_var(ri->query_string, strlen(ri->query_string), "username", username, sizeof(username));

    if (strlen(username) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing username");
        return 400;
    }

    sqlite3 *db = safe_open(USERS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB unavailable");
        return 500;
    }

    // 1. Move user from pending_users -> users
    const char *insertSQL =
        "INSERT OR IGNORE INTO users (username, password, security_question, security_answer) "
        "SELECT username, password, security_question, security_answer FROM pending_users WHERE username=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
         fprintf(stderr, "Confirm payment (INSERT) prepare error: %s\n", sqlite3_errmsg(db));
         sqlite3_close(db);
         mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB error 1");
         return 500;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 2. Delete from pending_users
    if (sqlite3_prepare_v2(db, "DELETE FROM pending_users WHERE username=?;", -1, &stmt, nullptr) != SQLITE_OK) {
         fprintf(stderr, "Confirm payment (DELETE) prepare error: %s\n", sqlite3_errmsg(db));
         sqlite3_close(db);
         mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB error 2");
         return 500;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 3. Calculate expiry (next month)
    time_t now = time(nullptr);
    struct tm nextMonth;
#ifdef _WIN32
    localtime_s(&nextMonth, &now);
#else
    localtime_r(&now, &nextMonth);
#endif
    nextMonth.tm_mon += 1;
    mktime(&nextMonth);
    char expiry[20];
    strftime(expiry, sizeof(expiry), "%Y-%m-%d", &nextMonth);

    // 4. Update subscriptions
    const char *subSQL =
        "INSERT OR REPLACE INTO subscriptions (username, last_payment, expiry_date) VALUES (?, date('now'), ?);";
    if (sqlite3_prepare_v2(db, subSQL, -1, &stmt, nullptr) != SQLITE_OK) {
         fprintf(stderr, "Confirm payment (SUB) prepare error: %s\n", sqlite3_errmsg(db));
         sqlite3_close(db);
         mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDB error 3");
         return 500;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, expiry, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nActivated %s until %s", username, expiry);
    return 200;
}, nullptr);



mg_set_request_handler(ctx, "/request_detail", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    // --- get request id ---
    char id_str[32] = {0};
    if (ri->query_string)
        mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, destination, target_price, suppliers_from, req_date, payment_terms, description, image, requester, created_at "
                      "FROM requests WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Request detail prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, atoi(id_str));

    std::string html;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dest = (const char*)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt, 2);
        const char *sup = (const char*)sqlite3_column_text(stmt, 3);
        const char *date = (const char*)sqlite3_column_text(stmt, 4);
        const char *pay = (const char*)sqlite3_column_text(stmt, 5);
        const char *desc = (const char*)sqlite3_column_text(stmt, 6);
        const char *img = (const char*)sqlite3_column_text(stmt, 7);
        const char *reqBy = (const char*)sqlite3_column_text(stmt, 8);
        const char *created = (const char*)sqlite3_column_text(stmt, 9);

        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Request Detail</title>"
               "<style>"
               "body{font-family:Arial;margin:30px;background:#f9f9f9;}"
               ".box{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1);max-width:700px;margin:auto;}"
               "img{max-width:100%;border-radius:8px;margin-top:10px;}"
               "</style></head><body>"
               "<div class='box'>"
               "<h2>Request Detail</h2>"
               "<p><b>Destination:</b> " + std::string(dest ? dest : "-") + "</p>";
                char priceStr[32];
                snprintf(priceStr, sizeof(priceStr), "%.2f", price);
                std::string priceDisplay(priceStr);
                if (priceDisplay.find('.') != std::string::npos) {
                    priceDisplay.erase(priceDisplay.find_last_not_of('0') + 1);
                    if (priceDisplay.back() == '.') priceDisplay.pop_back();
                }
                html += "<p><b>Target Price:</b> $" + priceDisplay + "</p>";

               html += "<p><b>Suppliers From:</b> " + std::string(sup ? sup : "-") + "</p>";
               html += ("<p><b>Date:</b> " + std::string(date ? date : "-") + "</p>");
               html += "<p><b>Payment Terms:</b> " + std::string(pay ? pay : "-") + "</p>";
               html += "<p><b>Description:</b><br>" + std::string(desc ? desc : "-") + "</p>";


        if (img && strlen(img) > 0) {
            html += "<img src='" + std::string(img) + "' alt='Request Image'>";
        }

        html += "<p><b>Requester:</b> " + std::string(reqBy ? reqBy : "-") + "</p>"
                "<p><b>Created At:</b> " + std::string(created ? created : "-") + "</p>"
                "<p><a href='/supplierbuyer/supplierbuyerhome.html'>⬅ Back</a></p>"
                "</div></body></html>";
    } else {
        html = "<h3>Request not found</h3>";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n%s",
              html.c_str());
    return 200;
}, nullptr);

// MODIFIED: /product_detail handler to support styled chat view
mg_set_request_handler(ctx, "/product_detail", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    char id_str[32] = {0};
    if (ri->query_string)
        mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));
    
    std::string currentUser = getUsernameFromCookie(conn);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, name, description, minPrice, image, priceUnits, tags, owner "
                      "FROM products WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Product detail prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, atoi(id_str));

    std::string html;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc = (const char*)sqlite3_column_text(stmt, 2);
        double price = sqlite3_column_double(stmt, 3);
        const char *img = (const char*)sqlite3_column_text(stmt, 4);
        const char *unit = (const char*)sqlite3_column_text(stmt, 5);
        const char *tags = (const char*)sqlite3_column_text(stmt, 6);
        const char *owner = (const char*)sqlite3_column_text(stmt, 7);


        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Product Detail</title>"
               "<style>"
               "body{font-family:Arial;margin:30px;background:#f9f9f9;}"
               ".box{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1);max-width:700px;margin:auto;}"
               "img{max-width:100%;border-radius:8px;margin-top:10px;}"
               ".message-history{background:#f9f9f9;border:1px solid #eee;border-radius:5px;padding:15px;max-height:300px;overflow-y:auto;display:flex;flex-direction:column;gap:10px;}"
               ".message{padding:8px 12px;border-radius:8px;max-width:70%;line-height:1.4;}"
               ".sent{background:#DCF8C6;align-self:flex-end;}"
               ".received{background:#FFFFFF;align-self:flex-start;border:1px solid #f0f0f0;}"
               ".message-meta{font-size:11px;color:#888;margin-top:4px;}"
               "</style></head><body>"
               "<div class='box'>"
               "<h2>" + std::string(name ? name : "Unnamed Product") + "</h2>";
               char priceStr[32];
                snprintf(priceStr, sizeof(priceStr), "%.2f", price);
                std::string priceDisplay(priceStr);
                if (priceDisplay.find('.') != std::string::npos) {
                    priceDisplay.erase(priceDisplay.find_last_not_of('0') + 1);
                    if (priceDisplay.back() == '.') priceDisplay.pop_back();
                }
                html += "<p><b>Price:</b> $" + priceDisplay + " " + std::string(unit ? unit : "") + "</p>";
               html += "<p><b>Tags:</b> " + std::string(tags ? tags : "-") + "</p>";
               html += "<p><b>Description:</b><br>" + std::string(desc ? desc : "-") + "</p>";

        if (img && strlen(img) > 0) {
            html += "<img src='" + std::string(img) + "' alt='Product Image'>";
        }

        html += "<p><b>Owner:</b> " + std::string(owner ? owner : "-") + "</p>";

        

        html += "<p><a href='/supplierbuyer/supplierbuyerhome.html'>⬅ Back</a></p>"
                "</div>";

        html += "<p><a href='/message_feature.html?product_id=" + std::string(id_str) +
"'>💬 Message Admin</a></p>";


                "</body></html>";
    } else {
        html = "<h3>Product not found</h3>";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n%s",
              html.c_str());
    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/message_feature.html", [](mg_connection *conn, void *) -> int {
    mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/message_feature.html");
    return 200;
}, nullptr);


// In supplierbuyer.cpp


// ... inside main() ...

// 🟢 MODIFIED: /api/conversation handler (to include message ID)
mg_set_request_handler(ctx, "/api/conversation", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);
    std::string currentUser = getUsernameFromCookie(conn);

    char productIdStr[32] = {0};
    if (ri->query_string)
        mg_get_var(ri->query_string, strlen(ri->query_string), "product_id", productIdStr, sizeof(productIdStr));
    int product_id = atoi(productIdStr);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    // Fetch ALL messages for that product, regardless of sender/receiver
    const char *sql = R"(
        SELECT id, sender, receiver, message, strftime('%Y-%m-%d %H:%M:%S', created_at)
        FROM messages
        WHERE product_id = ?
        ORDER BY id ASC;
    )"; // ✅ CHANGED: Added 'id' to the SELECT statement

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Conversation query error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, product_id);

    std::string json = "[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) json += ",";
        first = false;

        // ✅ CHANGED: Read the 'id' from column 0
        int id = sqlite3_column_int(stmt, 0); 
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *receiver = (const char *)sqlite3_column_text(stmt, 2);
        const char *message = (const char *)sqlite3_column_text(stmt, 3);
        const char *created = (const char *)sqlite3_column_text(stmt, 4);

        // ✅ FIXED: Use ostringstream and escapeJsonString to build valid JSON
        std::ostringstream oss;
        // ✅ CHANGED: Added 'id' to the JSON output
        oss << R"({"id":)" << id 
            << R"(,"sender":")" << escapeJsonString(sender ? sender : "") << R"(")"
            << R"(,"receiver":")" << escapeJsonString(receiver ? receiver : "") << R"(")"
            << R"(,"message":")" << escapeJsonString(message ? message : "") << R"(")"
            << R"(,"created_at":")" << (created ? created : "") << R"("})";
        json += oss.str();
    }
    json += "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json.c_str());
    return 200;
}, nullptr);

// ... inside main() ...

// 🟢 ADDED: Handler to delete a single message by its ID
mg_set_request_handler(ctx, "/api/delete_message", [](mg_connection *conn, void *) -> int {
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        return 405; // Method Not Allowed
    }

    const struct mg_request_info *ri = mg_get_request_info(conn);
    int len = (int)ri->content_length;
    if (len <= 0 || len > 128) len = 128;
    std::vector<char> post_data(len + 1);
    int n = mg_read(conn, post_data.data(), len);
    post_data[n] = '\0';

    char id_str[32] = {0};
    mg_get_var(post_data.data(), n, "id", id_str, sizeof(id_str));

    if (strlen(id_str) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing message ID.");
        return 400;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "DELETE FROM messages WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Delete message prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, atoi(id_str));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc == SQLITE_DONE) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        return 200;
    } else {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDelete failed.");
        return 500;
    }
}, nullptr);

// 🟢 ADDED: Handler to delete an entire conversation thread
mg_set_request_handler(ctx, "/api/clear_conversation", [](mg_connection *conn, void *) -> int {
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        return 405; // Method Not Allowed
    }

    const struct mg_request_info *ri = mg_get_request_info(conn);
    int len = (int)ri->content_length;
    if (len <= 0 || len > 256) len = 256;
    std::vector<char> post_data(len + 1);
    int n = mg_read(conn, post_data.data(), len);
    post_data[n] = '\0';

    char product_id_str[32] = {0}, participant[128] = {0};
    mg_get_var(post_data.data(), n, "product_id", product_id_str, sizeof(product_id_str));
    mg_get_var(post_data.data(), n, "participant", participant, sizeof(participant));

    if (strlen(product_id_str) == 0 || strlen(participant) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing product_id or participant.");
        return 400;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    // This query deletes messages between the specific user ('participant') and 'Admin'
    // for a specific 'product_id'.
    const char *sql = "DELETE FROM messages WHERE product_id = ? AND "
                      "((sender = ? AND receiver = 'Admin') OR (sender = 'Admin' AND receiver = ?));";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Clear conversation prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, atoi(product_id_str));
    sqlite3_bind_text(stmt, 2, participant, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, participant, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc == SQLITE_DONE) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        return 200;
    } else {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nClear failed.");
        return 500;
    }
}, nullptr);

mg_set_request_handler(ctx, "/api/get_current_user", [](mg_connection *conn, void *) -> int {
    std::string username = getUsernameFromCookie(conn);
    // ✅ FIXED: Also escape JSON here for safety
    std::ostringstream oss;
    oss << R"({"username":")" << escapeJsonString(username) << R"("})";
    
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s",
        oss.str().c_str());
    return 200;
}, nullptr);

// This is the new/corrected handler for sending a message.
mg_set_request_handler(ctx, "/send_message", [](mg_connection *conn, void *) -> int {
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        return 405; // Method Not Allowed
    }

    const struct mg_request_info *ri = mg_get_request_info(conn);
    int len = (int)ri->content_length;
    // Safety check for content length
    if (len <= 0 || len > 4096) len = 4096;
    std::vector<char> post_data(len + 1);
    int n = mg_read(conn, post_data.data(), len);
    post_data[n] = '\0';

    char product_id_str[32] = {0}, sender[128] = {0}, receiver[128] = {0}, message[1024] = {0};

    // 1. Extract form data fields
    mg_get_var(post_data.data(), n, "product_id", product_id_str, sizeof(product_id_str));
    mg_get_var(post_data.data(), n, "sender", sender, sizeof(sender));
    
    // Check for both 'receiver' (user side) and 'reply_receiver' (admin side)
    // The messageadmin.html uses 'reply_receiver' for the recipient's name/ID
    if (mg_get_var(post_data.data(), n, "receiver", receiver, sizeof(receiver)) <= 0) {
        // This line was wrong in the original, it should be 'receiver' not 'reply_receiver'
        mg_get_var(post_data.data(), n, "receiver", receiver, sizeof(receiver));
        // Let's check for the admin's field name
        if (strlen(receiver) == 0) {
             mg_get_var(post_data.data(), n, "reply_receiver", receiver, sizeof(receiver));
        }
    }
    mg_get_var(post_data.data(), n, "message", message, sizeof(message));

    // Optional: Log the data (This is likely where your current log message is coming from)
    fprintf(stderr, "Attempting to send message: product_id=%s, sender=%s, receiver=%s, message=%s\n", product_id_str, sender, receiver, message);

    if (strlen(sender) == 0 || strlen(receiver) == 0 || strlen(message) == 0 || strlen(product_id_str) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing required fields.");
        return 400;
    }

    int product_id = atoi(product_id_str);

    // 2. Database connection and insertion
    const char *PRODUCTS_DB_PATH = "C:/Users/priva/OneDrive/Desktop/supplierbuyer/products.db";
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH); // Assuming 'safe_open' is defined in your code
    if (!db) {
        fprintf(stderr, "Error: Database unavailable at %s\n", PRODUCTS_DB_PATH);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO messages (product_id, sender, receiver, message) VALUES (?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Send message prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }

    // Bind parameters
    sqlite3_bind_int(stmt, 1, product_id);
    sqlite3_bind_text(stmt, 2, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, receiver, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message, -1, SQLITE_TRANSIENT);

    // 3. Execute the statement
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc == SQLITE_DONE) {
        // 4. Return success response
        mg_printf(conn,
            "HTTP/1.1 201 Created\r\n"
            "Content-Length: 0\r\n\r\n");
        return 201;
    } else {
        fprintf(stderr, "Message insert failed: SQLite error %d\n", rc);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nMessage insert failed.");
        return 500;
    }
}, nullptr);

    // --- Web request handlers ---
    mg_set_request_handler(ctx, "/supplierbuyer.css", [](mg_connection *conn, void *) -> int {
        mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.css");
        return 200;
    }, nullptr);

    mg_set_request_handler(ctx, "/upload_product", [](mg_connection *conn, void *) -> int {
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset='UTF-8'>"
        "<title>Upload Product</title>"
        "<link rel='stylesheet' href='/supplierbuyer.css'>" // ✅ External CSS
        "<style>"
        "body{font-family:Arial;background:#f4f6f9;margin:0;padding:0;}"
        ".container{max-width:600px;margin:50px auto;background:#fff;padding:30px;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.1);}"
        "h1{text-align:center;color:#0077cc;margin-bottom:20px;}"
        "input,textarea,select{width:100%%;padding:10px;margin:8px 0;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;}"
        "button{background:#0077cc;color:white;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;}"
        "button:hover{background:#005fa3;}"
        "label{font-weight:bold;color:#333;}"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<h1>Upload New Product</h1>"
        "<form method='POST' action='/save_product' enctype='multipart/form-data'>"
        "<label>Name:</label><input type='text' name='name' required/><br/>"
        "<label>Description:</label><textarea name='description'></textarea><br/>"
        "<label>Price:</label><input type='number' step='0.01' name='price' required/><br/>"
        "<label>Unit:</label><select name='priceUnits'>"
        "<option value='unit'>unit</option>"
        "<option value='packs'>packs</option>"
        "<option value='ton'>ton</option>"
        "<option value='gram'>gram</option>"
        "<option value='kilo'>kilo</option>"
        "</select><br/>"
        "<label>Tags (comma-separated):</label><input type='text' name='tags'/><br/>"
        "<label>Upload Image:</label><input type='file' name='image' accept='image/*' required/><br/>"
        "<button type='submit'>Save</button>"
        "</form>"
        "<p style='text-align:center;margin-top:15px;'><a href='/supplierbuyer/supplierbuyerdash.html'>⬅ Back</a></p>"
        "</div></body></html>");
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/save_product", [](mg_connection *conn, void *) -> int {
    std::map<std::string, std::string> fields;
    std::string imagePath;
    parseMultipart(conn, fields, imagePath);

    std::string name = fields["name"];
    std::string description = fields["description"];
    std::string price = fields["price"];
    std::string unit = fields["priceUnits"];
    std::string tags = fields["tags"];

    std::string currentUser = getUsernameFromCookie(conn);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO products (name, description, minPrice, image, priceUnits, owner, tags) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Save product prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, atof(price.c_str()));
    sqlite3_bind_text(stmt, 4, imagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, unit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, currentUser.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, tags.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
        "Content-Length: 0\r\n\r\n");
    return 303;
}, nullptr);

mg_set_request_handler(ctx, "/request_product.html", [](mg_connection *conn, void *) -> int {
    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /login\r\n\r\n");
        return 302;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, destination, target_price, suppliers_from, req_date, payment_terms, description, image, created_at "
                  "FROM requests WHERE requester=? ORDER BY created_at DESC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Request product page prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_text(stmt, 1, currentUser.c_str(), -1, SQLITE_TRANSIENT);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Request Product</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:20px;background:#f4f4f4;}"
        ".container{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:900px;margin:auto;}"
        "h2{color:#00a6e1;}"
        "form input,form textarea{width:100%%;padding:8px;margin:5px 0;border:1px solid #ccc;border-radius:4px;}"
        "table{width:100%%;border-collapse:collapse;margin-top:20px;}"
        "th,td{border:1px solid #ddd;padding:8px;text-align:left;}"
        "th{background:#f9f9f9;}"
        ".btn{background:#00a6e1;color:#fff;border:none;padding:10px 15px;border-radius:4px;cursor:pointer;}"
        ".btn:hover{background:#0088c6;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h2>Request a Product</h2>"
        "<form method='POST' action='/submit_request' enctype='multipart/form-data'>"
        "<label>Destination</label><input type='text' name='destination' required>"
        "<label>Target Price</label><input type='number' step='0.01' name='target_price'>"
        "<label>Looking for suppliers from</label><input type='text' name='suppliers_from'>"
        "<label>Date</label><input type='date' name='req_date'>"
        "<label>Payment Terms</label><input type='text' name='payment_terms'>"
        "<label>Description</label><textarea name='description' rows='4'></textarea>"
        "<label>Upload Image</label><input type='file' name='image' accept='image/*'><br>"
        "<button class='btn' type='submit'>Submit Request</button>"
        "</form>"
        "<h3>Your Requests</h3>"
        "<table><tr>"
        "<th>Destination</th><th>Target Price</th><th>Suppliers From</th>"
        "<th>Date</th><th>Payment</th><th>Description</th><th>Image</th><th>Created</th><th>Action</th>"
        "</tr>"
    );

    while (sqlite3_step(stmt) == SQLITE_ROW) {
    int id = sqlite3_column_int(stmt, 0);
    const char *dest = (const char*)sqlite3_column_text(stmt, 1);
    double price = sqlite3_column_double(stmt, 2);
    const char *sup = (const char*)sqlite3_column_text(stmt, 3);
    const char *date = (const char*)sqlite3_column_text(stmt, 4);
    const char *pay = (const char*)sqlite3_column_text(stmt, 5);
    const char *desc = (const char*)sqlite3_column_text(stmt, 6);
    const char *img = (const char*)sqlite3_column_text(stmt, 7);
    const char *created = (const char*)sqlite3_column_text(stmt, 8);

    std::string imgTag = (img && strlen(img) > 0)
        ? "<img src='" + std::string(img) + "' style='height:60px;border-radius:6px;'>"
        : "-";


        mg_printf(conn,
    "<tr>"
    "<td>%s</td><td>$%.2f</td><td>%s</td><td>%s</td><td>%s</td>"
    "<td>%s</td><td>%s</td><td>%s</td>"
    "<td>"
    "<form method='GET' action='/edit_request' style='display:inline;'>"
    "<input type='hidden' name='id' value='%d'>"
    "<button type='submit' style='background:#ffc107;color:#000;border:none;padding:5px 10px;border-radius:4px;'>Edit</button>"
    "</form>"
    "<form method='POST' action='/delete_request' style='display:inline;margin-left:5px;'>"
    "<input type='hidden' name='id' value='%d'>"
    "<button type='submit' style='background:#dc3545;color:#fff;border:none;padding:5px 10px;border-radius:4px;'>Delete</button>"
    "</form>"
    "</td>"
    "</tr>",
    dest ? dest : "-", price, sup ? sup : "-", date ? date : "-", pay ? pay : "-",
    desc ? desc : "-", imgTag.c_str(), created ? created : "-", id, id);
        }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "</table><p><a href='/supplierbuyer/supplierbuyerdash.html'>⬅ Back to Dashboard</a></p></div></body></html>");
    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/delete_request", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    long long len = ri->content_length;
    std::vector<char> post(len + 1);
    mg_read(conn, post.data(), len);
    post[len] = '\0';

    char id_str[32];
    mg_get_var(post.data(), len, "id", id_str, sizeof(id_str));

    std::string requester = getUsernameFromCookie(conn);

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "DELETE FROM requests WHERE id=? AND requester=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Delete request prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_int(stmt, 1, atoi(id_str));
    sqlite3_bind_text(stmt, 2, requester.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /request_product.html\r\n"
        "Content-Length: 0\r\n\r\n");
    return 303;
}, nullptr);


mg_set_request_handler(ctx, "/update_request", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    long long len = ri->content_length;
    std::vector<char> post(len + 1);
    mg_read(conn, post.data(), len);
    post[len] = '\0';

    char id_str[32], destination[256], target_price[64], suppliers_from[256], req_date[64], payment_terms[256], description[512];
    mg_get_var(post.data(), len, "id", id_str, sizeof(id_str));
    mg_get_var(post.data(), len, "destination", destination, sizeof(destination));
    mg_get_var(post.data(), len, "target_price", target_price, sizeof(target_price));
    mg_get_var(post.data(), len, "suppliers_from", suppliers_from, sizeof(suppliers_from));
    mg_get_var(post.data(), len, "req_date", req_date, sizeof(req_date));
    mg_get_var(post.data(), len, "payment_terms", payment_terms, sizeof(payment_terms));
    mg_get_var(post.data(), len, "description", description, sizeof(description));

    std::string requester = getUsernameFromCookie(conn);
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE requests SET destination=?, target_price=?, suppliers_from=?, req_date=?, payment_terms=?, description=? "
                      "WHERE id=? AND requester=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Update request prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_text(stmt, 1, destination, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, atof(target_price));
    sqlite3_bind_text(stmt, 3, suppliers_from, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, req_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, payment_terms, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, atoi(id_str));
    sqlite3_bind_text(stmt, 8, requester.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /request_product.html\r\n"
        "Content-Length: 0\r\n\r\n");
    return 303;
}, nullptr);



mg_set_request_handler(ctx, "/reset_password", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/auth/resetpassword.html");
        return 200;
    }

if (strcmp(ri->request_method, "POST") == 0) {
    int len = (int)ri->content_length;
    if (len <= 0) len = 0;
    std::vector<char> post_data(len + 1);
    if (len > 0) mg_read(conn, post_data.data(), len);
    post_data[len] = '\0';

    char username[128] = {0};
    mg_get_var(post_data.data(), len, "username", username, sizeof(username));

    if (strlen(username) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing username");
        return 400;
    }

    sqlite3 *db = safe_open(USERS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    printf("Reset request for username: %s\n", username);

    sqlite3_stmt *stmt = nullptr;
    int pr = sqlite3_prepare_v2(db, "SELECT security_question FROM users WHERE username=?;", -1, &stmt, nullptr);
    if (pr != SQLITE_OK) {
        fprintf(stderr, "Reset password prepare error: %s (code %d)\n", sqlite3_errmsg(db), pr);
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query prepare failed");
        return 500;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *question = (const char*)sqlite3_column_text(stmt, 0);
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<h2>Answer Security Question</h2>"
            "<form method='POST' action='/verify_answer'>"
            "<input type='hidden' name='username' value='%s'>"
            "<p>%s</p>"
            "<input type='text' name='answer' placeholder='Your answer' required><br><br>"
            "<input type='password' name='new_password' placeholder='New password' required><br><br>"
            "<button type='submit'>Reset</button>"
            "</form>",
            username, question ? question : "No question found.");
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\nUser not found");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 200;
}


    return 405;
}, nullptr);


mg_set_request_handler(ctx, "/verify_answer", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);
    long long len = ri->content_length;
    std::vector<char> body(len + 1);
    mg_read(conn, body.data(), len);
    body[len] = '\0';

    char username[128], answer[128], newPass[128];
    mg_get_var(body.data(), len, "username", username, sizeof(username));
    mg_get_var(body.data(), len, "answer", answer, sizeof(answer));
    mg_get_var(body.data(), len, "new_password", newPass, sizeof(newPass));

    sqlite3 *db = safe_open(USERS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT security_answer FROM users WHERE username=?;", -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Verify answer (SELECT) prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed");
        return 500;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dbAnswer = (const char*)sqlite3_column_text(stmt, 0);
        if (dbAnswer && strcmp(dbAnswer, answer) == 0) ok = true;
    }
    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_stmt *update;
        if (sqlite3_prepare_v2(db, "UPDATE users SET password=? WHERE username=?;", -1, &update, nullptr) != SQLITE_OK) {
            fprintf(stderr, "Verify answer (UPDATE) prepare error: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed during update");
            return 500;
        }
        sqlite3_bind_text(update, 1, newPass, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update, 2, username, -1, SQLITE_TRANSIENT);
        sqlite3_step(update);
        sqlite3_finalize(update);

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<h3>Password reset successful!</h3><a href='/login'>Go to Login</a>");
    } else {
        mg_printf(conn,
            "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
            "<h3>Incorrect answer. <a href='/reset_password'>Try again</a></h3>");
    }

    sqlite3_close(db);
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/profile", [](mg_connection *conn, void *) -> int {
    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /login.html\r\n\r\n");
        return 302;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    std::string email = "";
    std::string adminName = "";

    if (db) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT name, email FROM admin LIMIT 1;", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *dbName = (const char*)sqlite3_column_text(stmt, 0);
                const char *dbEmail = (const char*)sqlite3_column_text(stmt, 1);
                if (dbName) adminName = dbName;
                if (dbEmail) email = dbEmail;
            }
            sqlite3_finalize(stmt);
        } else {
            fprintf(stderr, "Profile page prepare error: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_close(db);
    }

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>"
        "<html lang='en'><head><meta charset='UTF-8'>"
        "<title>My Profile</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#f4f4f4;margin:0;padding:20px;}"
        ".container{max-width:600px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;"
        "box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
        "h2{color:#00a6e1;margin-bottom:20px;text-align:center;}"
        "label{display:block;margin:10px 0 5px;}"
        "input{width:100%%;padding:8px;border:1px solid #ccc;border-radius:4px;background:#eee;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h2>My Profile</h2>"
        "<label>Name</label><input type='text' value='%s' readonly>"
        "<label>Username</label><input type='text' value='%s' readonly>"
        "<label>Email</label><input type='text' value='%s' readonly>"
        "</div>"
        "</body></html>",
        adminName.empty() ? currentUser.c_str() : adminName.c_str(),
        currentUser.c_str(),
        email.c_str()
    );

    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/update_product", [](mg_connection *conn, void *) -> int {
    std::map<std::string, std::string> fields;
    std::string newImage;
    parseMultipart(conn, fields, newImage);

    std::string id = fields["id"];
    std::string name = fields["name"];
    std::string description = fields["description"];
    std::string price = fields["price"];
    std::string unit = fields["priceUnits"];
    std::string tags = fields["tags"];

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    std::string sql = newImage.empty()
        ? "UPDATE products SET name=?, description=?, minPrice=?, priceUnits=?, tags=? WHERE id=?;"
        : "UPDATE products SET name=?, description=?, minPrice=?, priceUnits=?, tags=?, image=? WHERE id=?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Update product prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, atof(price.c_str()));
    sqlite3_bind_text(stmt, 4, unit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, tags.c_str(), -1, SQLITE_TRANSIENT);

    if (newImage.empty()) {
        sqlite3_bind_int(stmt, 6, atoi(id.c_str()));
    } else {
        sqlite3_bind_text(stmt, 6, newImage.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, atoi(id.c_str()));
    }

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
        "Content-Length: 0\r\n\r\n");
    return 303;
}, nullptr);



    mg_set_request_handler(ctx, "/", [](mg_connection *conn, void *) -> int {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        if (strcmp(ri->local_uri, "/") == 0) {
            mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.html");
            return 200;
        }
        return 0;
    }, nullptr);

    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyer/supplierbuyerproduct.html",
                           [](mg_connection *conn, void *) -> int {
                               mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerproduct.html");
                               return 200;
                           }, nullptr);

    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.css", [](mg_connection *conn, void *) -> int {
        mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerdash.css");
        return 200;
    }, nullptr);

mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.html", [](mg_connection *conn, void *) -> int {
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nDatabase unavailable");
        return 500;
    }

    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn,
            "HTTP/1.1 302 Found\r\n"
            "Location: /login\r\n"
            "Content-Length: 0\r\n\r\n");
        sqlite3_close(db);
        return 302;
    }

    // --- Monthly Billing Check ---
sqlite3 *billing_db = safe_open(USERS_DB_PATH);
sqlite3_stmt *billing_stmt = nullptr;
sqlite3_prepare_v2(billing_db, "SELECT expiry_date FROM subscriptions WHERE username=?;", -1, &billing_stmt, nullptr);
sqlite3_bind_text(billing_stmt, 1, currentUser.c_str(), -1, SQLITE_TRANSIENT);
if (sqlite3_step(billing_stmt) == SQLITE_ROW) {
    const char *expiry = (const char *)sqlite3_column_text(billing_stmt, 0);
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    char date_string[11];
    strftime(date_string, sizeof(date_string), "%Y-%m-%d", &local_tm);
    if (!expiry || std::string(expiry) < std::string(date_string)) {
        sqlite3_finalize(billing_stmt);
        sqlite3_close(billing_db);
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: https://nowpayments.io/payment/?iid=4491821266\r\n\r\n");
        return 302;
    }
} else {
    sqlite3_finalize(billing_stmt);
    sqlite3_close(billing_db);
    mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: https://nowpayments.io/payment/?iid=4491821266\r\n\r\n");
    return 302;
}
sqlite3_finalize(billing_stmt);
sqlite3_close(billing_db);


    // --- Start response ---
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<title>SupplierBuyer Dashboard</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin:20px; }"
        "h2 { margin-bottom: 10px; }"
        "table { width:100%%; border-collapse: collapse; margin-top:15px; }"
        "th, td { padding: 10px; border: 1px solid #ccc; text-align:left; }"
        "th { background:#f4f4f4; }"
        "a.button { padding:6px 12px; background:#007BFF; color:#fff; text-decoration:none; border-radius:4px; }"
        "a.button:hover { background:#0056b3; }"
        "ul { list-style-type: none; padding:0; }"
        "li { margin-bottom:8px; }"
        "</style>"
        "</head><body>"
        "<h3>Welcome, %s</h3>"
        "<h2>SupplierBuyer - Dashboard</h2>"
        "<p>"
        "<a class='button' href='/upload_product'>+ Upload Product</a>"
        "<a class='button' href='/supplierbuyer/supplierbuyerhome.html'>🏠 Home</a>"
        "<a class='button' href='/supplierbuyer/profileadmin.html'>Profile Admin</a>"
        "<a class='button' href='/request_product.html'>📦 Request Product</a>"
        "<a class='button' href='/supplierbuyer/messageadmin.html'>Message Admin</a>"
        "</p>",
        currentUser.c_str()
    );
    // --- Display product list section ---
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, name, description, minPrice, image, priceUnits, tags FROM products WHERE owner=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Dashboard prepare error: %s\n", sqlite3_errmsg(db));
        mg_printf(conn, "<p style='color:red;'>Error loading products.</p>");
        sqlite3_close(db);
        mg_printf(conn, "</body></html>");
        return 500;
    }

    sqlite3_bind_text(stmt, 1, currentUser.c_str(), -1, SQLITE_TRANSIENT);
    mg_printf(conn,
        "<h2>Your Products</h2>"
        "<table>"
        "<tr><th>ID</th><th>Image</th><th>Name</th><th>Description</th>"
        "<th>Price</th><th>Unit</th><th>Tags</th><th>Actions</th></tr>"
    );

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc = (const char *)sqlite3_column_text(stmt, 2);
        double price = sqlite3_column_double(stmt, 3);
        const char *dbImage = (const char *)sqlite3_column_text(stmt, 4);
        const char *unit = (const char *)sqlite3_column_text(stmt, 5);
        const char *tags = (const char *)sqlite3_column_text(stmt, 6);

        std::string imageUrl = (dbImage && strlen(dbImage) > 0)
                                   ? dbImage
                                   : "/uploads/noimage.png";

        mg_printf(conn,
            "<tr>"
            "<td>%d</td>"
            "<td><img src='%s' alt='img' style='height:50px'></td>"
            "<td>%s</td>"
            "<td>%s</td>"
            "<td>$%.2f</td>"
            "<td>%s</td>"
            "<td>%s</td>"
            "<td>"
            "<a class='button' href='/edit_product?id=%d'>Edit</a> "
            "<form method='POST' action='/delete_product' style='display:inline;'>"
            "<input type='hidden' name='id' value='%d'>"
            "<button type='submit' style='padding:6px 12px; background:#dc3545; "
            "color:#fff; border:none; border-radius:4px;' "
            "onclick='return confirm(\"Delete this product?\")'>Delete</button>"
            "</form>"
            "</td>"
            "</tr>",
            id,
            imageUrl.c_str(),
            name ? name : "Untitled",
            desc ? desc : "No description",
            price,
            unit ? unit : "unit",
            tags ? tags : "-",
            id, id
        );
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "</table></body></html>");
    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/edit_product", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    char id_str[32] = {0};
    if (ri->query_string) {
        mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));
    }
    if (strlen(id_str) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nProduct ID is missing.");
        return 400;
    }

    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /login\r\n\r\n");
        return 302;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT name, description, minPrice, priceUnits, tags FROM products WHERE id=? AND owner=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Edit product prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_int(stmt, 1, atoi(id_str));
    sqlite3_bind_text(stmt, 2, currentUser.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc = (const char*)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt, 2);
        const char *unit = (const char*)sqlite3_column_text(stmt, 3);
        const char *tags = (const char*)sqlite3_column_text(stmt, 4);

        std::string unit_str = unit ? unit : "";
        
        // Helper to generate <option> tags with the correct one selected
        auto generate_options = [&](const std::string& current_unit) {
            std::string options_html;
            const char* units[] = {"unit", "packs", "ton", "gram", "kilo"};
            for (const auto& u : units) {
                options_html += "<option value='" + std::string(u) + "'";
                if (current_unit == u) {
                    options_html += " selected";
                }
                options_html += ">" + std::string(u) + "</option>";
            }
            return options_html;
        };

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<!DOCTYPE html><html><head><title>Edit Product</title>"
            "<style>"
            "body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 20px; }"
            ".container { max-width: 600px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
            "h1 { color: #333; }"
            "input[type='text'], input[type='number'], textarea, select { width: 100%%; padding: 8px; margin-bottom: 10px; border-radius: 4px; border: 1px solid #ddd; box-sizing: border-box; }"
            "button { background-color: #28a745; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
            "button:hover { background-color: #218838; }"
            "a { color: #007BFF; text-decoration: none; display: inline-block; margin-top: 15px; }"
            "</style>"
            "</head><body>"
            "<div class='container'>"
            "<h1>Edit Product</h1>"
            "<form method='POST' action='/update_product' enctype='multipart/form-data'>"
            "<input type='hidden' name='id' value='%s'>"
            "Name: <input type='text' name='name' value='%s' required/><br/>"
            "Description: <textarea name='description' rows='4'>%s</textarea><br/>"
            "Price: <input type='number' step='0.01' name='price' value='%.2f' required/><br/>"
            "Unit: <select name='priceUnits'>%s</select><br/>"
            "Tags (comma-separated): <input type='text' name='tags' value='%s'/><br/>"
            "Change Image (optional): <input type='file' name='image' accept='image/*'/><br/><br/>"
            "<button type='submit'>Update Product</button>"
            "</form>"
            "<a href='/supplierbuyer/supplierbuyerdash.html'>&larr; Cancel and go back to Dashboard</a>"
            "</div></body></html>",
            id_str,
            name ? name : "",
            desc ? desc : "",
            price,
            generate_options(unit_str).c_str(),
            tags ? tags : ""
        );
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\nProduct not found or you don't have permission to edit it.");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/logout", [](mg_connection *conn, void *) -> int {
    const char *cookie = mg_get_header(conn, "Cookie");
    if (cookie) {
        std::string cookies(cookie);
        size_t pos = cookies.find("session_id=");
        if (pos != std::string::npos) {
            std::string sid = cookies.substr(pos + 11);
            
            size_t semicolon = sid.find(';');
            if (semicolon != std::string::npos) {
                sid = sid.substr(0, semicolon);
            }
            
            sessions.erase(sid);
        }
    }
    mg_printf(conn,
        "HTTP/1.1 302 Found\r\n"
        "Set-Cookie: session_id=deleted; Path=/; Max-Age=0\r\n"
        "Location: /login\r\n"
        "Content-Length: 0\r\n\r\n");
    return 302;
}, nullptr);



mg_set_request_handler(ctx, "/supplierbuyer/profileadmin.html",
    [](mg_connection *conn, void *) -> int {
        mg_send_file(conn,
            "C:\\Users\\priva\\OneDrive\\Desktop\\supplierbuyer\\supplierbuyer\\profileadmin.html");
        return 200;
    }, nullptr);

    mg_set_request_handler(ctx, "/delete_product", [](mg_connection *conn, void *) -> int {
        const mg_request_info *ri = mg_get_request_info(conn);
        char id[32] = {0};

        if (strcmp(ri->request_method, "POST") == 0) {
            char post_data[1024];
            int read_len = mg_read(conn, post_data, sizeof(post_data) - 1);
            post_data[read_len] = '\0';
            mg_get_var(post_data, read_len, "id", id, sizeof(id));
        } else if (strcmp(ri->request_method, "GET") == 0) {
            if (ri->query_string) {
                mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));
            }
        }

        if (strlen(id) == 0) {
            mg_printf(conn,
                      "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: text/plain\r\n\r\n"
                      "Missing product ID");
            return 400;
        }

        sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
        if (!db) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
            return 500;
        }

        sqlite3_stmt *select_stmt;
        const char *image_path = nullptr;
        std::string image_path_str;
        if (sqlite3_prepare_v2(db, "SELECT image FROM products WHERE id = ?;", -1, &select_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(select_stmt, 1, atoi(id));
            if (sqlite3_step(select_stmt) == SQLITE_ROW) {
                image_path = (const char*)sqlite3_column_text(select_stmt, 0);
                if (image_path) {
                    image_path_str = image_path;
                }
            }
            sqlite3_finalize(select_stmt);
        }

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM products WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
            mg_printf(conn,
                      "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Type: text/plain\r\n\r\n"
                      "Database error: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 500;
        }

        sqlite3_bind_int(stmt, 1, atoi(id));
        int result = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (result == SQLITE_DONE && !image_path_str.empty()) {
            std::string full_path = "C:/Users/priva/OneDrive/Desktop/supplierbuyer" + image_path_str;
            
            if (std::remove(full_path.c_str()) != 0) {
                 printf("Error deleting file: %s\n", full_path.c_str());
            } else {
                 printf("File deleted successfully: %s\n", full_path.c_str());
            }
        }

        sqlite3_close(db);

        if (result != SQLITE_DONE) {
            mg_printf(conn,
                      "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Type: text/plain\r\n\r\n"
                      "Failed to delete product");
            return 500;
        }

        mg_printf(conn,
                  "HTTP/1.1 303 See Other\r\n"
                  "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                  "Content-Length: 0\r\n\r\n");
        return 303;
    }, nullptr);

mg_set_request_handler(ctx, "/api/profile", [](mg_connection *conn, void *) -> int {
    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n\r\nNot logged in");
        return 401;
    }

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, name, email FROM admin LIMIT 1;", -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "API profile prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }

    std::string name = currentUser;
    std::string email = "";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dbEmail = (const char*)sqlite3_column_text(stmt, 2);
        if (dbEmail) email = dbEmail;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // ✅ FIXED: Also escape JSON here for safety
    std::ostringstream oss;
    oss << R"({ "name": ")" << escapeJsonString(name) << R"(", "email": ")" << escapeJsonString(email) << R"(" })";

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s",
        oss.str().c_str());

    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/api/update_login", [](mg_connection *conn, void *) -> int {
    char post_data[1024];
    mg_read(conn, post_data, sizeof(post_data));

    char newUsername[128], newPassword[128];
    mg_get_var(post_data, strlen(post_data), "newUsername", newUsername, sizeof(newUsername));
    mg_get_var(post_data, strlen(post_data), "newPassword", newPassword, sizeof(newPassword));

    std::string currentUser = getUsernameFromCookie(conn);
    if (currentUser.empty()) {
        mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n\r\nYou are not logged in.");
        return 401;
    }

    sqlite3 *db = safe_open(USERS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "UPDATE users SET username=?, password=? WHERE username=?;", -1, &stmt, nullptr) != SQLITE_OK) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to prepare query");
        sqlite3_close(db);
        return 500;
    }

    sqlite3_bind_text(stmt, 1, newUsername, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newPassword, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, currentUser.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc == SQLITE_DONE) {
        for (auto &pair : sessions) {
            if (pair.second == currentUser) {
                pair.second = newUsername;
                break;
            }
        }

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nLogin info updated successfully!");
    } else {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to update login info");
    }

    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/api/update_profile", [](mg_connection *conn, void *) -> int {
    char post_data[1024];
    mg_read(conn, post_data, sizeof(post_data));

    char name[100], email[100];
    mg_get_var(post_data, strlen(post_data), "name", name, sizeof(name));
    mg_get_var(post_data, strlen(post_data), "email", email, sizeof(email));

    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE admin SET name=?, email=? WHERE id=1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "API update profile prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nProfile updated successfully!");
    } else {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to update profile");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/product", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    char id_str[32] = {0};
    if (ri->query_string) {
        mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));
    }

    int id = atoi(id_str);
    if (id == 0) {
        mg_printf(conn,
            "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n"
            "<h1>Product ID is missing or invalid.</h1>");
        return 400;
    }
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase unavailable");
        return 500;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT name, description, minPrice, image, priceUnits, owner, tags FROM products WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Product detail page prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nDatabase query failed");
        return 500;
    }
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0); // Corrected index
        const char *desc = (const char*)sqlite3_column_text(stmt, 1); // Corrected index
        double price = sqlite3_column_double(stmt, 2); // Corrected index
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3); // Corrected index
        const char *priceUnits = (const char*)sqlite3_column_text(stmt, 4); // Corrected index
        const char *owner = (const char*)sqlite3_column_text(stmt, 5); // Corrected index
        const char *tags = (const char*)sqlite3_column_text(stmt, 6); // Corrected index


        std::string safeName = name ? name : "Untitled Product";
        std::string safeDesc = desc ? desc : "No description provided.";
        std::string safeImagePath = (imagePath && strlen(imagePath) > 0) ? imagePath : "/uploads/noimage.png";
        std::string safePriceUnits = priceUnits ? priceUnits : "unit";
        std::string safeOwner = owner ? owner : "unknown";

        mg_printf(conn,
"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
"<!DOCTYPE html><html lang='en'><head>"
"<meta charset='UTF-8'/>"
"<meta name='viewport' content='width=device-width, initial-scale=1'/>"
"<title>Product Detail</title>"
"<style>"
"body { font-family: Arial, sans-serif; background:#f4f4f4; margin:0; padding:20px; }"
".container { max-width:800px; margin:0 auto; background:#fff; padding:20px; border-radius:8px; box-shadow:0 0 10px rgba(0,0,0,0.1); }"
".product-image img { max-width:100%%; height:auto; border-radius:4px; }"
".product-details h1 { color:#00a6e1; }"
".price-tag { font-size:24px; font-weight:bold; color:#333; }"
".chat-btn { display:inline-block; margin-top:15px; padding:10px 15px; background:#28a745; color:#fff; text-decoration:none; border-radius:4px; cursor:pointer; }"
".chat-btn:hover { background:#218838; }"
".chat-overlay { position:fixed; top:0; right:0; width:70%%; max-width:900px; height:100%%; background:#fff; border-left:1px solid #ddd; display:none; flex-direction:column; box-shadow:-3px 0 6px rgba(0,0,0,0.2); }"
".chat-header { background:#fff; padding:15px; border-bottom:1px solid #ddd; font-size:16px; font-weight:bold; display:flex; justify-content:space-between; align-items:center; }"
".chat-header button { background:none; border:none; font-size:18px; cursor:pointer; }"
".chat-body { flex:1; display:flex; }"
".sidebar { width:280px; background:#f9f9f9; border-right:1px solid #ddd; overflow-y:auto; }"
".sidebar h3 { margin:0; padding:15px; border-bottom:1px solid #ddd; font-size:16px; }"
".conversation { padding:12px; cursor:pointer; border-bottom:1px solid #eee; }"
".conversation:hover { background:#f1f1f1; }"
".chat-area { flex:1; display:flex; flex-direction:column; }"
".chat-box { flex:1; background:#efeae2; padding:20px; overflow-y:auto; display:flex; flex-direction:column; gap:10px; }"
".message { padding:10px 15px; border-radius:10px; max-width:300px; background:#fff; }"
".sent { background:#dcf8c6; align-self:flex-end; }"
".received { background:#fff; align-self:flex-start; }"
".meta { font-size:11px; color:#666; margin-top:5px; }"
".chat-input { display:flex; padding:10px; border-top:1px solid #ddd; background:#fff; gap:10px; }"
".chat-input input, .chat-input textarea { border:1px solid #ccc; border-radius:4px; padding:8px; }"
".chat-input input { width:150px; }"
".chat-input textarea { flex:1; resize:none; }"
".chat-input button { background:#007bff; color:#fff; border:none; border-radius:4px; padding:0 20px; cursor:pointer; }"
".chat-input button:hover { background:#0056b3; }"
"</style>"
"</head><body>"
"<div class='container'>"
"  <div class='product-image'>"
"    <img src='%s' alt='%s' />"
"  </div>"
"  <div class='product-details'>"
"    <h1>%s</h1>"
"    <p class='price-tag'>Price: $%.2f / %s</p>"
"    <p>%s</p>"
"    <p><b>Tags:</b> %s</p>"   
"    <p><b>Owner:</b> %s</p>"   
"    <a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a><br>"
"  </div>"
"</div>"
"<div class='chat-overlay' id='chatOverlay'>"
"  <div class='chat-header'>"
"    <span>Chat about %s</span>"
"    <button onclick='closeChat()'>✖</button>"
"  </div>"
"  <div class='chat-body'>"
"    <div class='sidebar'>"
"      <h3>Messages</h3>"
"      <div id='conversationList'></div>"
"    </div>"
"    <div class='chat-area'>"
"      <div class='chat-box' id='chatBox'></div>"
"      <form class='chat-input' id='messageForm'>"
"        <input type='hidden' id='product_id' name='product_id' value='%d'>"
"        <input type='text' id='sender' name='sender' placeholder='Your name' required>"
"        <textarea id='message' name='message' placeholder='Type a message...' required></textarea>"
"        <button type='submit'>Send</button>"
"      </form>"
"    </div>"
"  </div>"
"</div>"
"<script>"
"function openChat(){ document.getElementById('chatOverlay').style.display='flex'; loadConversations(); }"
"function closeChat(){ document.getElementById('chatOverlay').style.display='none'; }"
"async function loadConversations(){"
"  const res=await fetch('/api/messages');"
"  const msgs=await res.json();"
"  const grouped={}; msgs.forEach(m=>{if(!grouped[m.product_id]) grouped[m.product_id]=[]; grouped[m.product_id].push(m);});"
"  const list=document.getElementById('conversationList'); list.innerHTML='';"
"  Object.keys(grouped).forEach(pid=>{"
"    const last=grouped[pid][0];"
"    const div=document.createElement('div');"
"    div.className='conversation';"
"    div.innerHTML=`<b>Product #${pid}</b><br>${last.sender}: ${last.message}`;"
"    div.onclick=()=>openConversation(pid,grouped[pid]);"
"    list.appendChild(div);"
"  });"
"}"
"function openConversation(pid,messages){"
"  const chatBox=document.getElementById('chatBox'); chatBox.innerHTML='';"
"  messages.slice().reverse().forEach(m=>{"
"    const div=document.createElement('div');"
"    div.className='message';"
"    div.classList.add(m.sender==='Admin'?'sent':'received');"
"    div.innerHTML=`<b>${m.sender}</b><br>${m.message}<div class='meta'>${m.created_at}</div>`;"
"    chatBox.appendChild(div);"
"  });"
"  chatBox.scrollTop=chatBox.scrollHeight;"
"  document.getElementById('product_id').value=pid;"
"}"
"</script>"
"</body></html>",
            safeImagePath.c_str(),
            safeName.c_str(),
            safeName.c_str(),
            price,
            safePriceUnits.c_str(),
            safeDesc.c_str(),
            tags ? tags : "-", 
            safeOwner.c_str(),
            safeName.c_str(),
            id
);
    } else {
        mg_printf(conn,
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
            "<h1>Product not found.</h1>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/supplierbuyer/messageadmin.html", [](mg_connection *conn, void *) -> int {
    // This line serves the correct file you've been editing
    mg_send_file(conn, "C:/Users/priva/OneDrive/Desktop/supplierbuyer/supplierbuyer/messageadmin.html");
    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/edit_request", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    // ---- Parse request ID safely ----
    char id_str[32] = {0};
    if (ri->query_string)
        mg_get_var(ri->query_string, (int)strlen(ri->query_string), "id", id_str, sizeof(id_str));

    if (strlen(id_str) == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nMissing request ID");
        return 400;
    }

    std::string requester = getUsernameFromCookie(conn);
    if (requester.empty()) {
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /login\r\n\r\n");
        return 302;
    }

    // ---- Open DB safely ----
    sqlite3 *db = safe_open(PRODUCTS_DB_PATH);
    if (!db) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nCannot open database");
        return 500;
    }

    const char *sql =
        "SELECT destination, target_price, suppliers_from, req_date, payment_terms, description "
        "FROM requests WHERE id=? AND requester=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "Edit_request prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nSQL prepare failed");
        return 500;
    }

    sqlite3_bind_int(stmt, 1, atoi(id_str));
    sqlite3_bind_text(stmt, 2, requester.c_str(), -1, SQLITE_TRANSIENT);

    std::string destination, suppliers_from, req_date, payment_terms, description;
    double target_price = 0.0;
    bool found = false;

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        found = true;
        const unsigned char *dest = sqlite3_column_text(stmt, 0);
        const unsigned char *sup = sqlite3_column_text(stmt, 2);
        const unsigned char *date = sqlite3_column_text(stmt, 3);
        const unsigned char *pay = sqlite3_column_text(stmt, 4);
        const unsigned char *desc = sqlite3_column_text(stmt, 5);
        target_price = sqlite3_column_double(stmt, 1);

        destination = dest ? reinterpret_cast<const char*>(dest) : "";
        suppliers_from = sup ? reinterpret_cast<const char*>(sup) : "";
        req_date = date ? reinterpret_cast<const char*>(date) : "";
        payment_terms = pay ? reinterpret_cast<const char*>(pay) : "";
        description = desc ? reinterpret_cast<const char*>(desc) : "";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (!found) {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\nRequest not found or permission denied");
        return 404;
    }

    // ---- HTML Output (no raw C pointers) ----
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Edit Request</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#f4f4f4;margin:20px;}"
        ".container{background:#fff;padding:20px;border-radius:8px;"
        "box-shadow:0 2px 8px rgba(0,0,0,0.1);max-width:800px;margin:auto;}"
        "input,textarea{width:100%%;padding:8px;margin:5px 0 15px 0;border:1px solid #ccc;border-radius:4px;}"
        "label{font-weight:bold;}"
        "button{background:#28a745;color:#fff;border:none;padding:10px 16px;border-radius:4px;cursor:pointer;}"
        "button:hover{background:#218838;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h2>Edit Request</h2>"
        "<form method='POST' action='/update_request'>"
        "<input type='hidden' name='id' value='%s'>"
        "<label>Destination:</label><input type='text' name='destination' value='%s' required>"
        "<label>Target Price:</label><input type='number' step='0.01' name='target_price' value='%.2f'>"
        "<label>Suppliers From:</label><input type='text' name='suppliers_from' value='%s'>"
        "<label>Date:</label><input type='date' name='req_date' value='%s'>"
        "<label>Payment Terms:</label><input type='text' name='payment_terms' value='%s'>"
        "<label>Description:</label><textarea name='description' rows='4'>%s</textarea>"
        "<button type='submit'>Update Request</button>"
        "</form>"
        "<p><a href='/request_product.html'>&larr; Back to Request Page</a></p>"
        "</div></body></html>",
        id_str,
        destination.c_str(),
        target_price,
        suppliers_from.c_str(),
        req_date.c_str(),
        payment_terms.c_str(),
        description.c_str()
    );

    return 200;
}, nullptr);

    // Keep the server running until interrupted
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Stop CivetWeb server if running
    if (ctx) {
        mg_stop(ctx);
    }

    return 0;
} // <-- This is the final closing brace for main()