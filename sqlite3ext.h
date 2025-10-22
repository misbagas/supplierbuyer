#include "civetweb.h"
#include <unordered_set>
#include <iostream>
#include <string>claims copyright to this source code.  In place of
#include <ctime>e, here is a blessing:
#include <cstdio>
#include "sqlite3.h"d and not evil.
#include <cstring> forgiveness for yourself and forgive others.
#include <fstream>e freely, never taking more than you give.
#include <cstdlib>
#include <vector>********************************************************
#include "CivetServer.h"nes the SQLite interface for use by
#include <string.h> that want to be imported as extensions into
#include <stdlib.h>ce.  Shared libraries that intend to be loaded
#ifdef _WIN32ons by SQLite should #include this file instead of 
#include <direct.h>
#else
#include <sys/stat.h>
#endife SQLITE3EXT_H
#include <thread>.h"
#include <chrono>
/*
sqlite3 *db;wing structure holds pointers to all of the SQLite API
** routines.
const char *PRODUCTS_DB_PATH = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/products.db";
const char *USERS_DB_PATH    = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer.db";
** interfaces to the end of this structure only.  If you insert new
** interfaces in the middle of this structure, then older different
// ---------------- Form Data Struct ----------------ther's shared
struct FormData {
    std::string name;
    std::string description;{
    std::string price;ontext)(sqlite3_context*,int nBytes);
    std::string filename;   lite3_context*);
    std::vector<unsigned char> imageData; // raw image bytes(*)(void*));
};int  (*bind_double)(sqlite3_stmt*,int,double);
  int  (*bind_int)(sqlite3_stmt*,int,int);
// ---------------- Callbacks ----------------int64);
static int field_found_cb(const char *key, const char *filename,
                          char *path, size_t pathlen, void *user_data) {
    return 1; // Always capture into memory*,const char*zName);
} const char * (*bind_parameter_name)(sqlite3_stmt*,int);
  int  (*bind_text)(sqlite3_stmt*,int,const char*,int n,void(*)(void*));
static int field_get_cb(const char *key, const char *value, size_t valuelen, void *user_data) {
    FormData *data = (FormData *)user_data;t sqlite3_value*);
  int  (*busy_handler)(sqlite3*,int(*)(void*,int),void*);
    if (strcmp(key, "name") == 0) { ms);
        data->name.assign(value, valuelen);
    } else if (strcmp(key, "description") == 0) {
        data->description.assign(value, valuelen);void*,sqlite3*,
    } else if (strcmp(key, "price") == 0) {st char*));
        data->price.assign(value, valuelen);void(*)(void*,sqlite3*,
    }  else if (strcmp(key, "image") == 0) {nst void*));
        data->imageData.insert(data->imageData.end(), value, value + valuelen);
    }  (*column_bytes)(sqlite3_stmt*,int iCol);
    return 0;mn_bytes16)(sqlite3_stmt*,int iCol);
} int  (*column_count)(sqlite3_stmt*pStmt);
  const char * (*column_database_name)(sqlite3_stmt*,int);
// ---------------- Upload Handler ----------------mt*,int);
static int handle_upload_product(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
  double  (*column_double)(sqlite3_stmt*,int iCol);
    if (strcmp(ri->request_method, "GET") == 0) {
        mg_printf(conn,n_int64)(sqlite3_stmt*,int iCol);
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<!DOCTYPE html><html><head><title>Upload Product</title></head><body>"
            "<h1>Upload New Product</h1>"te3_stmt*,int);
            "<form action='/upload_product' method='POST' enctype='multipart/form-data'>"
            "<label>Product Name:</label><br>"mt*,int);
            "<input type='text' name='name' required><br><br>"
            "<label>Description:</label><br>"3_stmt*,int iCol);
            "<textarea name='description' required></textarea><br><br>"
            "<label>Price:</label><br>" iCol);
            "<input type='number' step='0.01' name='price' required><br><br>"
            "<label>Product Image:</label><br>"void*);
            "<input type='file' name='image' accept='image/*' required><br><br>"
            "<button type='submit'>Upload Product</button>"
            "</form></body></html>",const char*,int,void*,
        );                 int(*)(void*,int,const void*,int,const void*));
        return 200;lation16)(sqlite3*,const void*,int,void*,
    }                        int(*)(void*,int,const void*,int,const void*));
  int  (*create_function)(sqlite3*,const char*,int,int,void*,
    if (strcmp(ri->request_method, "POST") == 0) {ntext*,int,sqlite3_value**),
        FormData formData;void (*xStep)(sqlite3_context*,int,sqlite3_value**),
                          void (*xFinal)(sqlite3_context*));
        mg_form_data_handler fdh{};*,const void*,int,int,void*,
        fdh.field_found = field_found_cb;   // keep capture-in-memory3_value**),
        fdh.field_get   = field_get_cb;     // fills FormDatat,sqlite3_value**),
        fdh.user_data   = &formData;Final)(sqlite3_context*));
  int (*create_module)(sqlite3*,const char*,const sqlite3_module*,void*);
        mg_handle_form_request(conn, &fdh);
  sqlite3 * (*db_handle)(sqlite3_stmt*);
        if (formData.name.empty() || formData.imageData.empty()) {
            mg_printf(conn,e)(int);
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<h1>Missing product name or image</h1>");
            return 400;,const char*,sqlite3_callback,void*,char**);
        }expired)(sqlite3_stmt*);
        *finalize)(sqlite3_stmt*pStmt);
  void  (*free)(void*);
        // ---- Save file to your requested folder ----
        const std::string uploadDir = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/uploads/";
        #ifdef _WIN32a)(sqlite3_context*,int);
            _mkdir("C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/");
            _mkdir(uploadDir.c_str()); // creates uploads/ if missing (OK if exists)
        #elseerruptx)(sqlite3*);
            mkdir("C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/", 0777);
            mkdir(uploadDir.c_str(), 0777);
        #endifrsion_number)(void);
  void *(*malloc)(int);
        // Use timestamp for uniqueness; keep .jpg for simplicity
        // --- Preserve the original file extension ---
        std::string ext = ".jpg";  // default fallback in case no extension found
        auto dot = formData.filename.find_last_of('.');**,const char**);
        if (dot != std::string::npos) {*,int,sqlite3_stmt**,const void**);
            // copy extension from uploaded file (.png, .jpeg, .gif, .webp, etc.)
            ext = formData.filename.substr(dot);void*),void*);
        }*realloc)(void*,int);
  int  (*reset)(sqlite3_stmt*pStmt);
        // Build unique filename (name_timestamp.ext)nt,void(*)(void*));
        std::string basename = formData.name + "_" + std::to_string(time(nullptr)) + ext;
        std::string filepath = uploadDir + basename;,int);
  void  (*result_error16)(sqlite3_context*,const void*,int);
  void  (*result_int)(sqlite3_context*,int);
        std::ofstream out(filepath, std::ios::binary);;
        if (!out) {ll)(sqlite3_context*);
            mg_printf(conn,te3_context*,const char*,int,void(*)(void*));
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Cannot write file at: %s", filepath.c_str());id(*)(void*));
            return 500;le)(sqlite3_context*,const void*,int,void(*)(void*));
        }*result_value)(sqlite3_context*,sqlite3_value*);
        out.write(reinterpret_cast<const char*>(formData.imageData.data()), formData.imageData.size());
        out.close();zer)(sqlite3*,int(*)(void*,int,const char*,const char*,
                         const char*,const char*),void*);
        // IMPORTANT: store path relative to document_root so the browser can load it
        std::string relativePath = "/admin/uploads/" + basename;
  int  (*step)(sqlite3_stmt*);
        // ---- Insert into DB (products.db) ----ar*,const char*,const char*,
        sqlite3 *pdb = nullptr; char const**,char const**,int*,int*,int*);
        int rc = sqlite3_open(PRODUCTS_DB_PATH, &pdb);
        if (rc != SQLITE_OK) {3*);
            mg_printf(conn,void(*xTrace)(void*,const char*),void*);
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "DB open failed: %s", sqlite3_errmsg(pdb));nst*,char const*,
            if (pdb) sqlite3_close(pdb); sqlite_int64),void*);
            return 500;qlite3_context*);
        }oid * (*value_blob)(sqlite3_value*);
  int  (*value_bytes)(sqlite3_value*);
        sqlite3_stmt *stmt = nullptr;*);
        rc = sqlite3_prepare_v2(pdb,lue*);
            "INSERT INTO products (name, description, price, image) VALUES (?, ?, ?, ?);",
            -1, &stmt, nullptr);qlite3_value*);
  int  (*value_numeric_type)(sqlite3_value*);
        if (rc != SQLITE_OK) {e_text)(sqlite3_value*);
            mg_printf(conn,16)(sqlite3_value*);
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Prepare failed: %s", sqlite3_errmsg(pdb));
            sqlite3_close(pdb);lue*);
            return 500;st char*,va_list);
        }d ??? */
  int (*overload_function)(sqlite3*, const char *zFuncName, int nArg);
        sqlite3_bind_text(stmt, 1, formData.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, formData.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, atof(formData.price.c_str())); void**);
        sqlite3_bind_text(stmt, 4, relativePath.c_str(), -1, SQLITE_TRANSIENT);
  /* Added by 3.4.1 */
        rc = sqlite3_step(stmt);3*,const char*,const sqlite3_module*,void*,
        if (rc != SQLITE_DONE) {*xDestroy)(void *));
            const char *err = sqlite3_errmsg(pdb);
            sqlite3_finalize(stmt);*,int,int);
            sqlite3_close(pdb);b*);
            mg_printf(conn,_blob*);
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Insert failed: %s", err);
            return 500;te3_blob*,void*,int,int);
        }lob_write)(sqlite3_blob*,const void*,int,int);
  int (*create_collation_v2)(sqlite3*,const char*,int,void*,
        sqlite3_finalize(stmt);t(*)(void*,int,const void*,int,const void*),
        sqlite3_close(pdb);  void(*)(void*));
  int (*file_control)(sqlite3*,const char*,int,void*);
        // ---- Render a simple list (or redirect to dashboard if you prefer) ----
        sqlite3_open(PRODUCTS_DB_PATH, &pdb);
        sqlite3_prepare_v2(pdb,(int);
            "SELECT id, name, description, price, image FROM products ORDER BY id DESC;",
            -1, &stmt, nullptrutex*);
        );utex_leave)(sqlite3_mutex*);
  int (*mutex_try)(sqlite3_mutex*);
  int (*open_v2)(const char*,sqlite3**,int,const char*);
    mg_printf(conn,ory)(int);
        "HTTP/1.1 200 OK\r\n"sqlite3_context*);
        "Content-Type: text/html\r\n\r\n"text*);
        "<!DOCTYPE html><html><head><title>Uploaded Products</title></head><body>"
        "<h1>Uploaded Products</h1><ul>"
    );te3_vfs *(*vfs_find)(const char*);
  int (*vfs_register)(sqlite3_vfs*,int);
    bool hasProducts = false;e3_vfs*);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        hasProducts = true;qlite3_context*,int);
        int prodId           = sqlite3_column_int(stmt, 0);
        const char *prodName = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc     = (const char*)sqlite3_column_text(stmt, 2);
        double price         = sqlite3_column_double(stmt, 3);
        const char *imagePath= (const char*)sqlite3_column_text(stmt, 4);
  int (*limit)(sqlite3*,int,int);
    mg_printf(conn,ext_stmt)(sqlite3*,sqlite3_stmt*);
        "<div class='card'>"_stmt*);
        "<a href='/product?id=%d'>"
        "<img src='%s'><br>"e3_backup*);
        "<b>%s</b>"*backup_init)(sqlite3*,const char*,sqlite3*,const char*);
        "</a><br>"ecount)(sqlite3_backup*);
        "<small>%s</small><br>"e3_backup*);
        "<b>$%.2f</b>"qlite3_backup*,int);
        "</div>",ompileoption_get)(int);
        sqlite3_column_int(stmt, 0),  // product ID
        imagePath ? imagePath : "",*,const char*,int,int,void*,
        prodName ? prodName : "",(*xFunc)(sqlite3_context*,int,sqlite3_value**),
        desc ? desc : "",   void (*xStep)(sqlite3_context*,int,sqlite3_value**),
        price               void (*xFinal)(sqlite3_context*),
    );                      void(*xDestroy)(void*));
  int (*db_config)(sqlite3*,int,...);
    }ite3_mutex *(*db_mutex)(sqlite3*);
  int (*db_status)(sqlite3*,int,int*,int*,int);
    if (!hasProducts) {e)(sqlite3*);
        mg_printf(conn, "<p>No products found in database.</p>");
    }ite3_int64 (*soft_heap_limit64)(sqlite3_int64);
  const char *(*sourceid)(void);
        sqlite3_finalize(stmt);mt*,int,int);
        sqlite3_close(pdb);r*,const char*,int);
  int (*unlock_notify)(sqlite3*,void(*)(void**,int),void*);
        mg_printf(conn,int)(sqlite3*,int);
            "</ul>"int)(sqlite3*,const char*);
            "<p><a href='/upload_product'>Upload another product</a> | "*);
            "<a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a></p>"
            "</body></html>"*,int op,...);
        );ab_on_conflict)(sqlite3*);
  /* Version 3.7.16 and later */
        return 200;qlite3*);
    }st char *(*db_filename)(sqlite3*,const char*);
      mg_printf(conn,sqlite3*,const char*);
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "Only GET and POST are supported.");
    return 405;adonly)(sqlite3_stmt*);
  int (*stricmp)(const char*,const char*);
} int (*uri_boolean)(const char*,const char*,int);
  sqlite3_int64 (*uri_int64)(const char*,const char*,sqlite3_int64);
// ---------------- Serve Products ----------------ar*);
static int handle_upload_page(struct mg_connection *conn, void *cbdata) {
    sqlite3 *db;kpoint_v2)(sqlite3*,const char*,int,int*,int*);
    sqlite3_open(PRODUCTS_DB_PATH, &db);
  int (*auto_extension)(void(*)(void));
    sqlite3_stmt *stmt;lite3_stmt*,int,const void*,sqlite3_uint64,
    sqlite3_prepare_v2(db, "SELECT name, description, price, image FROM products;", -1, &stmt, nullptr);
  int (*bind_text64)(sqlite3_stmt*,int,const char*,sqlite3_uint64,
    mg_printf(conn,   void(*)(void*),unsigned char);
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Uploaded Products</title></head><body>"
        "<h1>Uploaded Products</h1><ul>"
    );te3_uint64 (*msize)(void*);
  void *(*realloc64)(void*,sqlite3_uint64);
    int rowCount = 0;xtension)(void);
    while (sqlite3_step(stmt) == SQLITE_ROW) { void*,sqlite3_uint64,
        rowCount++;     void(*)(void*));
        const char *prodName  = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc      = (const char*)sqlite3_column_text(stmt, 1);
        double price          = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);
  sqlite3_value *(*value_dup)(const sqlite3_value*);
        mg_printf(conn,lite3_value*);
            "<li><b>Name:</b> %s<br>"ntext*,sqlite3_uint64);
            "<b>Description:</b> %s<br>"int, sqlite3_uint64);
            "<b>Price:</b> %.2f<br>"
            "<img src='%s' width='200'><br><br></li>",
            prodName ? prodName : "",ext*,unsigned int);
            desc ? desc : "", */
            price,int,sqlite3_int64*,sqlite3_int64*,int);
            imagePath ? imagePath : ""r*,unsigned int);
        );_cacheflush)(sqlite3*);
    }Version 3.12.0 and later */
  int (*system_errno)(sqlite3*);
    if (rowCount == 0) {later */
        mg_printf(conn, "<li><i>No products found in database.</i></li>");d*);
    }r *(*expanded_sql)(sqlite3_stmt*);
  /* Version 3.18.0 and later */
    sqlite3_finalize(stmt);wid)(sqlite3*,sqlite3_int64);
    sqlite3_close(db);d later */
  int (*prepare_v3)(sqlite3*,const char*,int,unsigned int,
    mg_printf(conn, "</ul><p><a href='/upload_product'>Upload another product</a></p></body></html>");
    return 200;16_v3)(sqlite3*,const void*,int,unsigned int,
}                     sqlite3_stmt**,const void**);
  int (*bind_pointer)(sqlite3_stmt*,int,void*,const char*,void(*)(void*));
// ---------------- Register & Login ---------------- char*,void(*)(void*));
  void *(*value_pointer)(sqlite3_value*,const char*);
static int handle_register(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
  const char *(*vtab_collation)(sqlite3_index_info*,int);
    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/auth/joinpage.html");
        return 200;e)(int,const char**,int*);
    } (*keyword_check)(const char*,int);
  sqlite3_str *(*str_new)(sqlite3*);
    if (strcmp(ri->request_method, "POST") == 0) {
        long long len = ri->content_length;har *zFormat, ...);
        std::vector<char> post_data(len + 1);ar *zFormat, va_list);
        mg_read(conn, post_data.data(), (size_t)len);nt N);
        post_data[len] = '\0';3_str*, const char *zIn);
  void (*str_appendchar)(sqlite3_str*, int N, char C);
        char username[128], password[128];
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));
  char *(*str_value)(sqlite3_str*);
        sqlite3_stmt *stmt;er */
        sqlite3_prepare_v2(db,)(sqlite3*,const char*,int,int,void*,
            "INSERT INTO users (username, password) VALUES (?, ?);",e3_value**),
            -1, &stmt, nullptr); (*xFinal)(sqlite3_context*),
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);e3_value**),
                            void(*xDestroy)(void*));
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);(sqlite3_stmt*);
  /* Version 3.28.0 and later */
        if (rc == SQLITE_DONE) {stmt*);
            // ✅ Success → redirect to dashboard
            mg_printf(conn,er */
                "HTTP/1.1 302 Found\r\n"r**);
                "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                "Content-Length: 0\r\n"lite3_int64);
                "Connection: close\r\n\r\n");
        } else if (rc == SQLITE_CONSTRAINT) {*);
            // ⚠️ Username already existshar*);
            mg_printf(conn,l)(const char*);
                "HTTP/1.1 302 Found\r\n"
                "Location: /auth/joinpage.html?error=userexists\r\n",
                "Content-Length: 0\r\n"ar**);
                "Connection: close\r\n\r\n");
        } else {(*database_file_object)(const char*);
            // ❌ Some other DB error → show in logs
            fprintf(stderr, "SQLite error on register: %s\n", sqlite3_errmsg(db));
            mg_printf(conn,er */
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Registration failed: %s",
                sqlite3_errmsg(db));
        }gned int(*)(void*,const char*,unsigned int,unsigned int,unsigned int),
     void*, void(*)(void*));
        return 200; and later */
    } (*error_offset)(sqlite3*);
  int (*vtab_rhs_value)(sqlite3_index_info*,int,sqlite3_value**);
    return 405;stinct)(sqlite3_index_info*);
} int (*vtab_in)(sqlite3_index_info*,int,int);
  int (*vtab_in_first)(sqlite3_value*,sqlite3_value**);
  int (*vtab_in_next)(sqlite3_value*,sqlite3_value**);
static int handle_login(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
                     sqlite3_int64,sqlite3_int64,unsigned);
    if (strcmp(ri->request_method, "GET") == 0) { *,sqlite3_int64*,
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/auth/loginpage.html");
        return 200;name)(sqlite3*,int);
    }Version 3.40.0 and later */
  int (*value_encoding)(sqlite3_value*);
    if (strcmp(ri->request_method, "POST") == 0) {
        long long len = ri->content_length;
        std::vector<char> post_data(len + 1);
        mg_read(conn, post_data.data(), (size_t)len);
        post_data[len] = '\0';*/
  void *(*get_clientdata)(sqlite3*,const char*);
        char username[128], password[128];ar*, void*, void(*)(void*));
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));
};
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT password FROM users WHERE username=?;", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);ts.  It
** is also defined in the file "loadext.c".
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {ntry)(
            const char *storedPass = (const char*)sqlite3_column_text(stmt, 0);
            if (storedPass && strcmp(storedPass, password) == 0) {n failure. */
                // Successes *pThunk /* Extension API function pointers. */
                mg_printf(conn,
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                    "Content-Length: 0\r\n"utines so that they are
                    "Connection: close\r\n\r\n");cture.
            } else {
                // Wrong passwordby the loadext.c source file
                mg_printf(conn,rary - not an extension) so that
                    "HTTP/1.1 302 Found\r\n"ines structure
                    "Location: /auth/loginpage.html?error=invalid\r\n"
                    "Content-Length: 0\r\n" only valid if the
                    "Connection: close\r\n\r\n");
            }
        } else {ITE_CORE) && !defined(SQLITE_OMIT_LOAD_EXTENSION)
            // User not foundtext      sqlite3_api->aggregate_context
            mg_printf(conn,TED
                "HTTP/1.1 302 Found\r\n"qlite3_api->aggregate_count
                "Location: /auth/loginpage.html?error=usernotfound\r\n"
                "Content-Length: 0\r\n"sqlite3_api->bind_blob
                "Connection: close\r\n\r\n");3_api->bind_double
        }qlite3_bind_int               sqlite3_api->bind_int
#define sqlite3_bind_int64             sqlite3_api->bind_int64
        sqlite3_finalize(stmt);        sqlite3_api->bind_null
        return 200;d_parameter_count   sqlite3_api->bind_parameter_count
    }ne sqlite3_bind_parameter_index   sqlite3_api->bind_parameter_index
#define sqlite3_bind_parameter_name    sqlite3_api->bind_parameter_name
    return 405;_bind_text              sqlite3_api->bind_text
}define sqlite3_bind_text16            sqlite3_api->bind_text16
#define sqlite3_bind_value             sqlite3_api->bind_value
#define sqlite3_busy_handler           sqlite3_api->busy_handler
// ---------------- Main ----------------lite3_api->busy_timeout
#define sqlite3_changes                sqlite3_api->changes
// Define a global constant for DB pathsqlite3_api->close
#define sqlite3_collation_needed       sqlite3_api->collation_needed
#include <csignal>llation_needed16     sqlite3_api->collation_needed16
static bool running = true;            sqlite3_api->column_blob
void signal_handler(int) {es           sqlite3_api->column_bytes
    running = false;mn_bytes16         sqlite3_api->column_bytes16
}define sqlite3_column_count           sqlite3_api->column_count
int main() {te3_column_database_name   sqlite3_api->column_database_name
sqlite3_open(USERS_DB_PATH, &db);ame16 sqlite3_api->column_database_name16
#define sqlite3_column_decltype        sqlite3_api->column_decltype
sqlite3_exec(db,column_decltype16      sqlite3_api->column_decltype16
    "CREATE TABLE IF NOT EXISTS users ("qlite3_api->column_double
    "username TEXT PRIMARY KEY, "      sqlite3_api->column_int
    "password TEXT);",_int64           sqlite3_api->column_int64
    0, 0, 0);e3_column_name            sqlite3_api->column_name
#define sqlite3_column_name16          sqlite3_api->column_name16
        // Create products table if it does not existolumn_origin_name
    sqlite3 *pdb = nullptr;in_name16   sqlite3_api->column_origin_name16
    sqlite3_open(PRODUCTS_DB_PATH, &pdb);lite3_api->column_table_name
    sqlite3_exec(pdb,n_table_name16    sqlite3_api->column_table_name16
        "CREATE TABLE IF NOT EXISTS products ("api->column_text
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "pi->column_text16
        "name TEXT NOT NULL, "         sqlite3_api->column_type
        "description TEXT, "           sqlite3_api->column_value
        "price REAL, "_hook            sqlite3_api->commit_hook
        "image TEXT"lete               sqlite3_api->complete
        ");",e3_complete16             sqlite3_api->complete16
        nullptr, nullptr, nullptr);    sqlite3_api->create_collation
#define sqlite3_create_collation16     sqlite3_api->create_collation16
#define sqlite3_create_function        sqlite3_api->create_function
    const char *options[] = {on16      sqlite3_api->create_function16
        "document_root", "C:/Users/misba/OneDrive/Desktop/supplierbuyer",
        "listening_ports", "8080",     sqlite3_api->create_module_v2
        "enable_directory_listing", "no",lite3_api->data_count
        "extra_mime_types", ".js=application/javascript,.css=text/css,.jpg=image/jpeg,.png=image/png",
        "index_files", "supplierbuyer.html",e3_api->declare_vtab
        sqlite3_enable_shared_cache    sqlite3_api->enable_shared_cache
        0qlite3_errcode                sqlite3_api->errcode
    };e sqlite3_errmsg                 sqlite3_api->errmsg
#define sqlite3_errmsg16               sqlite3_api->errmsg16
    mg_callbacks callbacks;            sqlite3_api->exec
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = [](const struct mg_connection *, const char *msg) {
        printf("CivetWeb log: %s\n", msg);
        return 0;inalize               sqlite3_api->finalize
    };e sqlite3_free                   sqlite3_api->free
#define sqlite3_free_table             sqlite3_api->free_table
    mg_context *ctx = mg_start(&callbacks, 0, options);_autocommit
    if (!ctx) {_get_auxdata            sqlite3_api->get_auxdata
        printf("Failed to start CivetWeb!\n");_api->get_table
        return 1;IT_DEPRECATED
    }ne sqlite3_global_recover         sqlite3_api->global_recover
#endif
    mg_set_request_handler(ctx, "/supplierbuyer.css", [](mg_connection *conn, void *) -> int {
    mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.css");
    return 200;_libversion             sqlite3_api->libversion
}, nullptr);te3_libversion_number      sqlite3_api->libversion_number
#define sqlite3_malloc                 sqlite3_api->malloc
#define sqlite3_mprintf                sqlite3_api->mprintf
mg_set_request_handler(ctx, "/", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);
#define sqlite3_prepare                sqlite3_api->prepare
    // only serve homepage if it's the exact root "/"repare16
    if (strcmp(ri->local_uri, "/") == 0) {ite3_api->prepare_v2
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.html");
        return 200;file                sqlite3_api->profile
    }ne sqlite3_progress_handler       sqlite3_api->progress_handler
#define sqlite3_realloc                sqlite3_api->realloc
    // otherwise, let CivetWeb try normal routingi->reset
    return 0; 3_result_blob            sqlite3_api->result_blob
}, nullptr);te3_result_double          sqlite3_api->result_double
#define sqlite3_result_error           sqlite3_api->result_error
#define sqlite3_result_error16         sqlite3_api->result_error16
    mg_set_request_handler(ctx, "/upload_product", handle_upload_product, nullptr);
    mg_set_request_handler(ctx, "/uploadpage.html", handle_upload_page, nullptr);
    mg_set_request_handler(ctx, "/register", handle_register, nullptr);
    mg_set_request_handler(ctx, "/login", handle_login, nullptr);
#define sqlite3_result_text16          sqlite3_api->result_text16
 mg_set_request_handler(ctx, "/product", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);le
    const char *qs = ri->query_string; // Query string part after "?"
#define sqlite3_rollback_hook          sqlite3_api->rollback_hook
    char idStr[64] = {0};rizer         sqlite3_api->set_authorizer
    if (qs) {e3_set_auxdata            sqlite3_api->set_auxdata
        mg_get_var(qs, strlen(qs), "id", idStr, sizeof(idStr));
    }ne sqlite3_step                   sqlite3_api->step
#define sqlite3_table_column_metadata  sqlite3_api->table_column_metadata
    if (strlen(idStr) == 0) {p         sqlite3_api->thread_cleanup
        mg_printf(conn,hanges          sqlite3_api->total_changes
            "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\n"
            "Missing product ID");
        return 400;nsfer_bindings      sqlite3_api->transfer_bindings
    }f
#define sqlite3_update_hook            sqlite3_api->update_hook
    sqlite3 *db;user_data              sqlite3_api->user_data
    sqlite3_open(PRODUCTS_DB_PATH, &db);qlite3_api->value_blob
#define sqlite3_value_bytes            sqlite3_api->value_bytes
    sqlite3_stmt *stmt;ytes16          sqlite3_api->value_bytes16
    sqlite3_prepare_v2(db,le           sqlite3_api->value_double
        "SELECT name, description, price, image FROM products WHERE id=?;",
        -1, &stmt, nullptr);           sqlite3_api->value_int64
    sqlite3_bind_int(stmt, 1, atoi(idStr));te3_api->value_numeric_type
#define sqlite3_value_text             sqlite3_api->value_text
    int rc = sqlite3_step(stmt);       sqlite3_api->value_text16
    if (rc == SQLITE_ROW) {6be         sqlite3_api->value_text16be
        const char *prodName  = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc      = (const char*)sqlite3_column_text(stmt, 1);
        double price          = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);
#define sqlite3_overload_function      sqlite3_api->overload_function
        mg_printf(conn,_v2             sqlite3_api->prepare_v2
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<!DOCTYPE html><html><head><title>%s</title></head><body>"
            "<h1>%s</h1>"blob          sqlite3_api->bind_zeroblob
            "<img src='%s' width='300'><br>"e3_api->blob_bytes
            "<p><b>Description:</b> %s</p>"te3_api->blob_close
            "<p><b>Price:</b> $%.2f</p>"qlite3_api->blob_open
            "<p><a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a></p>"
            "</body></html>",          sqlite3_api->blob_write
            prodName,e_collation_v2    sqlite3_api->create_collation_v2
            prodName,control           sqlite3_api->file_control
            imagePath ? imagePath : "",sqlite3_api->memory_highwater
            desc ? desc : "",          sqlite3_api->memory_used
            priceutex_alloc            sqlite3_api->mutex_alloc
        );lite3_mutex_enter            sqlite3_api->mutex_enter
    } else {te3_mutex_free             sqlite3_api->mutex_free
        mg_printf(conn,eave            sqlite3_api->mutex_leave
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n"
            "Product not found");      sqlite3_api->open_v2
    }ne sqlite3_release_memory         sqlite3_api->release_memory
#define sqlite3_result_error_nomem     sqlite3_api->result_error_nomem
    sqlite3_finalize(stmt);r_toobig    sqlite3_api->result_error_toobig
    sqlite3_close(db);                 sqlite3_api->sleep
    return 200;_soft_heap_limit        sqlite3_api->soft_heap_limit
}, nullptr);te3_vfs_find               sqlite3_api->vfs_find
#define sqlite3_vfs_register           sqlite3_api->vfs_register
mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyer/supplierbuyerproduct.html",
    [](mg_connection *conn, void *) -> int {e3_api->xthreadsafe
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerproduct.html");
        return 200;ult_error_code      sqlite3_api->result_error_code
    }, nullptr);test_control           sqlite3_api->test_control
#define sqlite3_randomness             sqlite3_api->randomness
    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.css", [](mg_connection *conn, void *) -> int {
    mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerdash.css");
    return 200;_limit                  sqlite3_api->limit
}, nullptr);te3_next_stmt              sqlite3_api->next_stmt
#define sqlite3_sql                    sqlite3_api->sql
#define sqlite3_status                 sqlite3_api->status
mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.html", [](mg_connection *conn, void *) -> int {
    sqlite3 *db;backup_init            sqlite3_api->backup_init
    sqlite3_open(PRODUCTS_DB_PATH, &db);qlite3_api->backup_pagecount
#define sqlite3_backup_remaining       sqlite3_api->backup_remaining
    sqlite3_stmt *stmt;step            sqlite3_api->backup_step
    sqlite3_prepare_v2(db, "SELECT id, name, description, price, image FROM products;", -1, &stmt, nullptr);
#define sqlite3_compileoption_used     sqlite3_api->compileoption_used
    // Start HTML with navbaron_v2     sqlite3_api->create_function_v2
    mg_printf(conn,config              sqlite3_api->db_config
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html lang='en'><head>"api->db_status
        "<meta charset='UTF-8'/>"      sqlite3_api->extended_errcode
        "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
        "<title>SupplierBuyer Dashboard</title>"pi->soft_heap_limit64
        "<link rel='stylesheet' href='/supplierbuyer/supplierbuyerdash.css'>"
        "<style>"tmt_status            sqlite3_api->stmt_status
        "body{font-family:Arial,sans-serif;background:#fff;margin:0;color:#222;font-size:14px;}"
        ".navbar{display:flex;justify-content:space-between;align-items:center;background:#00a6e1;color:#fff;padding:12px 24px;}"
        ".navbar .logo{font-weight:700;font-size:18px;}"autocheckpoint
        ".navbar .nav-links a{color:#fff;margin-left:20px;text-decoration:none;font-weight:500;}"
        ".navbar .nav-links a:hover{text-decoration:underline;}"
        ".content{padding:30px;}"      sqlite3_api->blob_reopen
        /* --- Product card styles (shortened but same as your design) --- */
        ".container{max-width:900px;margin:20px auto;}"b_on_conflict
        ".tab-label{display:inline-block;background:#00a6e1;color:#fff;font-weight:600;padding:8px 16px;border-radius:3px 3px 0 0;"
        "border:1px solid #00a6e1;border-bottom:none;margin-bottom:0;}"
        ".product-border{border-top:2px solid #00a6e1;margin-top:-1px;margin-bottom:20px;}"
        ".product-card{background:#f7f7f7;border:1px solid #c8d7df;border-top:none;border-radius:0 0 4px 4px;"
        "padding:20px 30px 24px;box-sizing:border-box;position:relative;font-size:14px;line-height:1.5;color:#444;margin-bottom:20px;}"
        ".product-card .content-area{display:flex;gap:20px;}"
        ".image-box{background:#e9ecee;border:1px solid #d9d9d9;width:160px;height:160px;display:flex;align-items:center;justify-content:center;}"
        ".image-box img{max-width:160px;max-height:160px;}"adonly
        ".product-info{flex:1;display:flex;flex-direction:column;gap:10px;font-size:14px;}"
        ".product-title{font-weight:600;font-size:15px;margin:0;}"
        ".price{color:#222;font-weight:700;font-size:17px;}"4
        ".unit{font-weight:400;font-size:12px;color:#666;margin-left:6px;}"
        ".desc p{margin:4px 0;color:#444;font-size:14px;line-height:1.5;}"
        ".footer-row{margin-top:20px;font-size:11px;color:#555;display:flex;justify-content:space-between;align-items:center;}"
        ".id-text{opacity:.6;}"
        ".btn-group{display:flex;gap:16px;}"e3_api->auto_extension
        ".btn{cursor:pointer;font-size:14px;font-weight:600;display:flex;align-items:center;gap:6px;color:#2c9405;border:none;background:transparent;padding:4px 6px;border-radius:2px;}"
        ".btn:hover{background:#d7f1ca;}"lite3_api->bind_text64
        "</style></head><body>"ension  sqlite3_api->cancel_auto_extension
        "<div class='navbar'>"         sqlite3_api->load_extension
        "<div class='logo'>SupplierBuyer Admin</div>"alloc64
        "<div class='nav-links'>"      sqlite3_api->msize
        "<a href='/supplierbuyer/supplierbuyerdash.html'>Dashboard</a>"
        "<a href='/upload_product'>Upload Product</a>"set_auto_extension
        "<a href='/view_products'>View Products</a>"result_blob64
        "<a href='/logout'>Logout</a>" sqlite3_api->result_text64
        "</div></div>"b                sqlite3_api->strglob
        "<div class='content'>"
        "<div class='container'>"      sqlite3_api->value_dup
        "<div class='tab-label'>Live Products</div>"value_free
        "<div class='product-border'></div>"e3_api->result_zeroblob64
    );e sqlite3_bind_zeroblob64        sqlite3_api->bind_zeroblob64
/* Version 3.9.0 and later */
    int rowCount = 0;_subtype          sqlite3_api->value_subtype
    while (sqlite3_step(stmt) == SQLITE_ROW) {_api->result_subtype
        rowCount++;nd later */
        int prodId            = sqlite3_column_int(stmt, 0);
        const char *prodName  = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc      = (const char*)sqlite3_column_text(stmt, 2);
        double price          = sqlite3_column_double(stmt, 3);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 4);
/* Version 3.14.0 and later */
        mg_printf(conn,2               sqlite3_api->trace_v2
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n\r\n"rowid  sqlite3_api->set_last_insert_rowid
            "<div class='product-card'>"
            "<div class='content-area'>"qlite3_api->prepare_v3
            "<div class='image-box'>"  sqlite3_api->prepare16_v3
            "<img src='%s' alt='Product Image'/>"i->bind_pointer
            "</div>"lt_pointer         sqlite3_api->result_pointer
            "<div class='product-info'>"qlite3_api->value_pointer
            "<h2 class='product-title'><a href='/product?id=%d'>%s</a></h2>"
            "<div class='price'>$%.2f <span class='unit'>/ unit</span></div>"
            "<div class='desc'><p>%s</p></div>"api->value_nochange
            "</div>"_collation         sqlite3_api->vtab_collation
            "</div>"d later */
            "<div class='footer-row'>" sqlite3_api->keyword_count
            "<div class='id-text'>Product ID: %d</div>"word_name
            "<div class='btn-group'>"  sqlite3_api->keyword_check
            "<form method='POST' action='/delete_product' style='display:inline;'>"
            "<input type='hidden' name='id' value='%d'/>"inish
            "<button class='btn delete' type='submit' onclick='return confirm(\"Are you sure you want to delete this product?\")'>Delete</button>"
            "</form>",ppendf           sqlite3_api->str_vappendf
            "<form method='GET' action='/edit_product' style='display:inline;'>"
            "<input type='hidden' name='id' value='%d'/>"ppendall
            "<button class='btn edit' type='submit'>Edit</button>"
            "</form>"eset              sqlite3_api->str_reset
            "</div>"errcode            sqlite3_api->str_errcode
            "</div>"length             sqlite3_api->str_length
            "</div>",alue              sqlite3_api->str_value
            imagePath ? imagePath : "",
            prodId,ate_window_function sqlite3_api->create_window_function
            prodName ? prodName : "Untitled",
            price,rmalized_sql         sqlite3_api->normalized_sql
            desc ? desc : "",/
            prodIdmt_isexplain         sqlite3_api->stmt_isexplain
        );lite3_value_frombind         sqlite3_api->value_frombind
    }rsion 3.30.0 and later */
#define sqlite3_drop_modules           sqlite3_api->drop_modules
    if (rowCount == 0) {ter */
        mg_printf(conn, "<p><i>No products uploaded yet.</i></p>");64
    }ne sqlite3_uri_key                sqlite3_api->uri_key
#define sqlite3_filename_database      sqlite3_api->filename_database
    sqlite3_finalize(stmt);urnal       sqlite3_api->filename_journal
    sqlite3_close(db);me_wal           sqlite3_api->filename_wal
/* Version 3.32.0 and later */
    mg_printf(conn, "</div></div></body></html>");->create_filename
    return 200;_free_filename          sqlite3_api->free_filename
}, nullptr);te3_database_file_object   sqlite3_api->database_file_object
/* Version 3.34.0 and later */
mg_set_request_handler(ctx, "/delete_product", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);
    char id[32] = {0};s64              sqlite3_api->changes64
#define sqlite3_total_changes64        sqlite3_api->total_changes64
    // Check if it's a POST request (better for destructive operations)
    if (strcmp(ri->request_method, "POST") == 0) {->autovacuum_pages
        // Read POST dataer */
        char post_data[1024];          sqlite3_api->error_offset
        int read_len = mg_read(conn, post_data, sizeof(post_data) - 1);
        post_data[read_len] = '\0';    sqlite3_api->vtab_distinct
        sqlite3_vtab_in                sqlite3_api->vtab_in
        mg_get_var(post_data, read_len, "id", id, sizeof(id));rst
    } else if (strcmp(ri->request_method, "GET") == 0) {_in_next
        // For GET requests, get ID from query string
        if (ri->query_string) {
            mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));
        }qlite3_serialize              sqlite3_api->serialize
    }f
#define sqlite3_db_name                sqlite3_api->db_name
    if (strlen(id) == 0) {r */
        mg_printf(conn,ncoding         sqlite3_api->value_encoding
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n\r\n"_api->is_interrupted
            "Missing product ID");
        return 400;t_explain           sqlite3_api->stmt_explain
    }rsion 3.44.0 and later */
#define sqlite3_get_clientdata         sqlite3_api->get_clientdata
    sqlite3 *db;set_clientdata         sqlite3_api->set_clientdata
    if (sqlite3_open(PRODUCTS_DB_PATH, &db) != SQLITE_OK) {
        mg_printf(conn,imeout          sqlite3_api->setlk_timeout
            "HTTP/1.1 500 Internal Server Error\r\n"MIT_LOAD_EXTENSION) */
            "Content-Type: text/plain\r\n\r\n"
            "Database error: %s", sqlite3_errmsg(db));_EXTENSION)
        return 500; the file really is being compiled as a loadable 
    }extension */
# define SQLITE_EXTENSION_INIT1     const sqlite3_api_routines *sqlite3_api=0;
    // First, get the image path to delete the file
    sqlite3_stmt *select_stmt;3     \
    const char *image_path = nullptr; *sqlite3_api;
    if (sqlite3_prepare_v2(db, "SELECT image FROM products WHERE id = ?;", -1, &select_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(select_stmt, 1, atoi(id));nked into the 
        if (sqlite3_step(select_stmt) == SQLITE_ROW) {
            image_path = (const char*)sqlite3_column_text(select_stmt, 0);
        }SQLITE_EXTENSION_INIT2(v)  (void)v; /* unused parameter */
        sqlite3_finalize(select_stmt);no-op*/
    }f

    // Delete the product from database
    sqlite3_stmt *stmt;    if (sqlite3_prepare_v2(db, "DELETE FROM products WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {        mg_printf(conn,            "HTTP/1.1 500 Internal Server Error\r\n"            "Content-Type: text/plain\r\n\r\n"            "Database error: %s", sqlite3_errmsg(db));        sqlite3_close(db);        return 500;    }    sqlite3_bind_int(stmt, 1, atoi(id));    int result = sqlite3_step(stmt);    sqlite3_finalize(stmt);    // Delete the image file if it exists    if (result == SQLITE_DONE && image_path) {        std::string full_path = "C:/Users/misba/OneDrive/Desktop/supplierbuyer" + std::string(image_path);        remove(full_path.c_str());    }    sqlite3_close(db);    if (result != SQLITE_DONE) {        mg_printf(conn,            "HTTP/1.1 500 Internal Server Error\r\n"            "Content-Type: text/plain\r\n\r\n"            "Failed to delete product");        return 500;    }    // Redirect back to dashboard    mg_printf(conn,        "HTTP/1.1 303 See Other\r\n"        "Location: /supplierbuyer/supplierbuyerdash.html\r\n"        "Content-Length: 0\r\n\r\n");    return 303;}, nullptr);mg_set_request_handler(ctx, "/edit_product", [](mg_connection *conn, void *) -> int {    const mg_request_info *ri = mg_get_request_info(conn);    char id[32];    mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));    sqlite3 *db;    sqlite3_open(PRODUCTS_DB_PATH, &db);    sqlite3_stmt *stmt;    sqlite3_prepare_v2(db, "SELECT name, description, price, image FROM products WHERE id = ?;", -1, &stmt, nullptr);    sqlite3_bind_int(stmt, 1, atoi(id));    const char *name = "";    const char *desc = "";    double price = 0.0;    const char *image = "";    if (sqlite3_step(stmt) == SQLITE_ROW) {        name  = (const char*)sqlite3_column_text(stmt, 0);        desc  = (const char*)sqlite3_column_text(stmt, 1);        price = sqlite3_column_double(stmt, 2);        image = (const char*)sqlite3_column_text(stmt, 3);    }    mg_printf(conn,        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"        "<!DOCTYPE html><html><head><title>Edit Product</title></head><body>"        "<h1>Edit Product</h1>"        "<form method='POST' action='/update_product'>"        "<input type='hidden' name='id' value='%s'/>"        "Name: <input type='text' name='name' value='%s'/><br/>"        "Description: <textarea name='description'>%s</textarea><br/>"        "Price: <input type='text' name='price' value='%.2f'/><br/>"        "Image URL: <input type='text' name='image' value='%s'/><br/>"        "<button type='submit'>Update</button>"        "</form>"        "</body></html>",        id, name, desc, price, image    );    sqlite3_finalize(stmt);    sqlite3_close(db);    return 200;}, nullptr);mg_set_request_handler(ctx, "/update_product", [](mg_connection *conn, void *) -> int {    char post_data[4096];    mg_read(conn, post_data, sizeof(post_data));    char id[32], name[256], desc[512], price[64], image[256];    mg_get_var(post_data, strlen(post_data), "id", id, sizeof(id));    mg_get_var(post_data, strlen(post_data), "name", name, sizeof(name));    mg_get_var(post_data, strlen(post_data), "description", desc, sizeof(desc));    mg_get_var(post_data, strlen(post_data), "price", price, sizeof(price));    mg_get_var(post_data, strlen(post_data), "image", image, sizeof(image));    sqlite3 *db;    sqlite3_open(PRODUCTS_DB_PATH, &db);    sqlite3_stmt *stmt;    sqlite3_prepare_v2(db, "UPDATE products SET name=?, description=?, price=?, image=? WHERE id=?;", -1, &stmt, nullptr);    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);    sqlite3_bind_text(stmt, 2, desc, -1, SQLITE_TRANSIENT);    sqlite3_bind_double(stmt, 3, atof(price));    sqlite3_bind_text(stmt, 4, image, -1, SQLITE_TRANSIENT);    sqlite3_bind_int(stmt, 5, atoi(id));    sqlite3_step(stmt);    sqlite3_finalize(stmt);    sqlite3_close(db);    mg_printf(conn,        "HTTP/1.1 302 Found\r\n"        "Location: /supplierbuyer/supplierbuyerdash.html\r\n"        "\r\n"    );    return 302;}, nullptr);printf("Server started on http://localhost:8080\n");printf("Press Ctrl+C or close the terminal to stop the server...\n");while (true) {    std::this_thread::sleep_for(std::chrono::seconds(1));}// Cleanup after loop endsmg_stop(ctx);sqlite3_close(db);printf("Server stopped.\n");return 0;}