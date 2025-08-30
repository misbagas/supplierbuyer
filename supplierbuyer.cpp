#include "civetweb.h"
#include <unordered_set>
#include <iostream>
#include <string>
#include <ctime>
#include <cstdio>
#include "sqlite3.h"
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <vector>
#include "CivetServer.h"
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

sqlite3 *db;

const char *PRODUCTS_DB_PATH = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/products.db";
const char *USERS_DB_PATH    = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer.db";


// ---------------- Form Data Struct ----------------
struct FormData {
    std::string name;
    std::string description;
    std::string price;
    std::vector<unsigned char> imageData; // raw image bytes
};

// ---------------- Callbacks ----------------
static int field_found_cb(const char *key, const char *filename,
                          char *path, size_t pathlen, void *user_data) {
    return 1; // Always capture into memory
}

static int field_get_cb(const char *key, const char *value, size_t valuelen, void *user_data) {
    FormData *data = (FormData *)user_data;

    if (strcmp(key, "name") == 0) {
        data->name.assign(value, valuelen);
    } else if (strcmp(key, "description") == 0) {
        data->description.assign(value, valuelen);
    } else if (strcmp(key, "price") == 0) {
        data->price.assign(value, valuelen);
    } else if (strcmp(key, "image") == 0) {
        data->imageData.assign(value, value + valuelen);
    }
    return 0;
}

// ---------------- Upload Handler ----------------
static int handle_upload_product(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<!DOCTYPE html><html><head><title>Upload Product</title></head><body>"
            "<h1>Upload New Product</h1>"
            "<form action='/upload_product' method='POST' enctype='multipart/form-data'>"
            "<label>Product Name:</label><br>"
            "<input type='text' name='name' required><br><br>"
            "<label>Description:</label><br>"
            "<textarea name='description' required></textarea><br><br>"
            "<label>Price:</label><br>"
            "<input type='number' step='0.01' name='price' required><br><br>"
            "<label>Product Image:</label><br>"
            "<input type='file' name='image' accept='image/*' required><br><br>"
            "<button type='submit'>Upload Product</button>"
            "</form></body></html>"
        );
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        FormData formData;

        mg_form_data_handler fdh{};
        fdh.field_found = field_found_cb;   // keep capture-in-memory
        fdh.field_get   = field_get_cb;     // fills FormData
        fdh.user_data   = &formData;

        mg_handle_form_request(conn, &fdh);

        if (formData.name.empty() || formData.imageData.empty()) {
            mg_printf(conn,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<h1>Missing product name or image</h1>");
            return 400;
        }

        // ---- Save file to your requested folder ----
        const std::string uploadDir = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/uploads/";
        #ifdef _WIN32
            _mkdir("C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/");
            _mkdir(uploadDir.c_str()); // creates uploads/ if missing (OK if exists)
        #else
            mkdir("C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/", 0777);
            mkdir(uploadDir.c_str(), 0777);
        #endif

        // Use timestamp for uniqueness; keep .jpg for simplicity
        std::string basename = formData.name + "_" + std::to_string(time(nullptr)) + ".jpg";
        std::string filepath = uploadDir + basename;

        std::ofstream out(filepath, std::ios::binary);
        if (!out) {
            mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Cannot write file at: %s", filepath.c_str());
            return 500;
        }
        out.write(reinterpret_cast<const char*>(formData.imageData.data()), formData.imageData.size());
        out.close();

        // IMPORTANT: store path relative to document_root so the browser can load it
        std::string relativePath = "/admin/uploads/" + basename;

        // ---- Insert into DB (products.db) ----
        sqlite3 *pdb = nullptr;
        int rc = sqlite3_open(PRODUCTS_DB_PATH, &pdb);
        if (rc != SQLITE_OK) {
            mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "DB open failed: %s", sqlite3_errmsg(pdb));
            if (pdb) sqlite3_close(pdb);
            return 500;
        }

        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(pdb,
            "INSERT INTO products (name, description, price, image) VALUES (?, ?, ?, ?);",
            -1, &stmt, nullptr);

        if (rc != SQLITE_OK) {
            mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Prepare failed: %s", sqlite3_errmsg(pdb));
            sqlite3_close(pdb);
            return 500;
        }

        sqlite3_bind_text(stmt, 1, formData.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, formData.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, atof(formData.price.c_str()));
        sqlite3_bind_text(stmt, 4, relativePath.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            const char *err = sqlite3_errmsg(pdb);
            sqlite3_finalize(stmt);
            sqlite3_close(pdb);
            mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                "Insert failed: %s", err);
            return 500;
        }

        sqlite3_finalize(stmt);
        sqlite3_close(pdb);

        // ---- Render a simple list (or redirect to dashboard if you prefer) ----
        sqlite3_open("products.db", &pdb);
        sqlite3_prepare_v2(pdb, "SELECT name, description, price, image FROM products ORDER BY id DESC;", -1, &stmt, nullptr);

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<!DOCTYPE html><html><head><title>Uploaded Products</title></head><body>"
            "<h1>Uploaded Products</h1><ul>"
        );

        bool hasProducts = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            hasProducts = true;
            const char *prodName  = (const char*)sqlite3_column_text(stmt, 0);
            const char *desc      = (const char*)sqlite3_column_text(stmt, 1);
            double price          = sqlite3_column_double(stmt, 2);
            const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);

            mg_printf(conn,
                "<li><b>Name:</b> %s<br>"
                "<b>Description:</b> %s<br>"
                "<b>Price:</b> %.2f<br>"
                "<img src='%s' width='200'><br><br></li>",
                prodName ? prodName : "",
                desc ? desc : "",
                price,
                imagePath ? imagePath : ""
            );
        }

        if (!hasProducts) {
            mg_printf(conn, "<p>No products found in database.</p>");
        }

        sqlite3_finalize(stmt);
        sqlite3_close(pdb);

        mg_printf(conn,
            "</ul>"
            "<p><a href='/upload_product'>Upload another product</a> | "
            "<a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a></p>"
            "</body></html>"
        );

        return 200;
    }

    return 405;
}

// ---------------- Serve Products ----------------
static int handle_upload_page(struct mg_connection *conn, void *cbdata) {
    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, description, price, image FROM products;", -1, &stmt, nullptr);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Uploaded Products</title></head><body>"
        "<h1>Uploaded Products</h1><ul>"
    );

    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCount++;
        const char *prodName  = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc      = (const char*)sqlite3_column_text(stmt, 1);
        double price          = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);

        mg_printf(conn,
            "<li><b>Name:</b> %s<br>"
            "<b>Description:</b> %s<br>"
            "<b>Price:</b> %.2f<br>"
            "<img src='%s' width='200'><br><br></li>",
            prodName ? prodName : "",
            desc ? desc : "",
            price,
            imagePath ? imagePath : ""
        );
    }

    if (rowCount == 0) {
        mg_printf(conn, "<li><i>No products found in database.</i></li>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "</ul><p><a href='/upload_product'>Upload another product</a></p></body></html>");
    return 200;
}

// ---------------- Register & Login ----------------

static int handle_register(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/auth/joinpage.html");
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        long long len = ri->content_length;
        std::vector<char> post_data(len + 1);
        mg_read(conn, post_data.data(), (size_t)len);
        post_data[len] = '\0';

        char username[128], password[128];
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,
            "INSERT INTO users (username, password) VALUES (?, ?);",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            // ✅ Success → redirect to dashboard
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n");
        } else if (rc == SQLITE_CONSTRAINT) {
            // ⚠️ Username already exists
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: /auth/joinpage.html?error=userexists\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n");
        } else {
            // ❌ Some other DB error → show in logs
            fprintf(stderr, "SQLite error on register: %s\n", sqlite3_errmsg(db));
            mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Registration failed: %s",
                sqlite3_errmsg(db));
        }

        return 200;
    }

    return 405;
}


static int handle_login(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/auth/loginpage.html");
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        long long len = ri->content_length;
        std::vector<char> post_data(len + 1);
        mg_read(conn, post_data.data(), (size_t)len);
        post_data[len] = '\0';

        char username[128], password[128];
        mg_get_var(post_data.data(), len, "username", username, sizeof(username));
        mg_get_var(post_data.data(), len, "password", password, sizeof(password));

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT password FROM users WHERE username=?;", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const char *storedPass = (const char*)sqlite3_column_text(stmt, 0);
            if (storedPass && strcmp(storedPass, password) == 0) {
                // Success
                mg_printf(conn,
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n\r\n");
            } else {
                // Wrong password
                mg_printf(conn,
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /auth/loginpage.html?error=invalid\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n\r\n");
            }
        } else {
            // User not found
            mg_printf(conn,
                "HTTP/1.1 302 Found\r\n"
                "Location: /auth/loginpage.html?error=usernotfound\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n");
        }

        sqlite3_finalize(stmt);
        return 200;
    }

    return 405;
}


// ---------------- Main ----------------

// Define a global constant for DB path


int main() {
sqlite3_open(USERS_DB_PATH, &db);

sqlite3_exec(db,
    "CREATE TABLE IF NOT EXISTS users ("
    "username TEXT PRIMARY KEY, "
    "password TEXT);",
    0, 0, 0);

        // Create products table if it does not exist
    sqlite3 *pdb = nullptr;
    sqlite3_open(PRODUCTS_DB_PATH, &pdb);
    sqlite3_exec(pdb,
        "CREATE TABLE IF NOT EXISTS products ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "price REAL, "
        "image TEXT"
        ");",
        nullptr, nullptr, nullptr);


    const char *options[] = {
        "document_root", "C:/Users/misba/OneDrive/Desktop/supplierbuyer",
        "listening_ports", "8080",
        "enable_directory_listing", "no",
        "extra_mime_types", ".js=application/javascript,.css=text/css,.jpg=image/jpeg,.png=image/png",
        "index_files", "supplierbuyer.html",
        
        0
    };

    mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = [](const struct mg_connection *, const char *msg) {
        printf("CivetWeb log: %s\n", msg);
        return 0;
    };

    mg_context *ctx = mg_start(&callbacks, 0, options);
    if (!ctx) {
        printf("Failed to start CivetWeb!\n");
        return 1;
    }

    mg_set_request_handler(ctx, "/supplierbuyer.css", [](mg_connection *conn, void *) -> int {
    mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.css");
    return 200;
}, nullptr);


mg_set_request_handler(ctx, "/", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    // only serve homepage if it's the exact root "/"
    if (strcmp(ri->local_uri, "/") == 0) {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.html");
        return 200;
    }

    // otherwise, let CivetWeb try normal routing
    return 0; 
}, nullptr);


    mg_set_request_handler(ctx, "/upload_product", handle_upload_product, nullptr);
    mg_set_request_handler(ctx, "/uploadpage.html", handle_upload_page, nullptr);
    mg_set_request_handler(ctx, "/register", handle_register, nullptr);
    mg_set_request_handler(ctx, "/login", handle_login, nullptr);

    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.html", [](mg_connection *conn, void *) -> int {
    sqlite3 *db;
    sqlite3_open("products.db", &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, description, price, image FROM products;", -1, &stmt, nullptr);

    // Start page
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Admin Dashboard</title>"
        "<style>"
        "body { font-family: Arial; padding:20px; }"
        ".gallery { display:flex; flex-wrap:wrap; gap:20px; }"
        ".card { border:1px solid #ccc; padding:10px; border-radius:8px; width:220px; text-align:center; }"
        ".card img { width:200px; height:auto; border-radius:4px; }"
        "</style>"
        "</head><body>"
        "<h1>Admin Dashboard</h1>"
        "<p><a href='/upload_product'>Upload New Product</a></p>"
        "<h2>Uploaded Products</h2>"
        "<div class='gallery'>"
    );

    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCount++;
        const char *prodName  = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc      = (const char*)sqlite3_column_text(stmt, 1);
        double price          = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);

        mg_printf(conn,
            "<div class='card'>"
            "<img src='%s'><br>"
            "<b>%s</b><br>"
            "<small>%s</small><br>"
            "<b>$%.2f</b>"
            "</div>",
            imagePath ? imagePath : "",
            prodName ? prodName : "",
            desc ? desc : "",
            price
        );
    }

    if (rowCount == 0) {
        mg_printf(conn, "<p><i>No products uploaded yet.</i></p>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // End page
    mg_printf(conn, "</div></body></html>");
    return 200;
}, nullptr);



    printf("Server started on http://localhost:8080\n");
    getchar();
    mg_stop(ctx);
    return 0;
}
