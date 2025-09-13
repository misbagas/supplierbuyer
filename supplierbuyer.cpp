#include "civetweb.h"
#include "CivetServer.h"
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
#include <chrono>
#include <csignal>

#include "sqlite3.h"
#ifndef FORM_FIELD_STORAGE_GET
#define FORM_FIELD_STORAGE_GET 1
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
sqlite3 *db = nullptr;
const char *USERS_DB_PATH = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer.db";
const char *PRODUCTS_DB_PATH = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/products.db";
volatile sig_atomic_t running = 1;
// ---------------- Signal Handler ----------------
void signal_handler(int) {
    running = false;
}

// ---------------- Form Data Struct ----------------
struct FormData {
    std::string id;     
    std::string name;
    std::string description;
    std::string filename;
    std::string productGrade;
    std::string minPrice;
    std::string maxPrice;
    std::string priceUnits;
    std::string incoterms;
    std::string sellerCode;
    std::string productStandards;
    std::vector<unsigned char> imageData;
};
// ---------------- Callbacks ----------------
// Global variables to manage the file stream and filename during the upload
static std::ofstream outputFileStream;
static std::string uploadedFilename;

// Modified field_found_cb function
static int field_found_cb(const char *key, const char *filename,
                          char *path, size_t pathlen, void *user_data) {
    if (strcmp(key, "image") == 0 && filename) {
        // We found an uploaded file field.
        const std::string uploadDir = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/uploads/";
        std::string ext = ".jpg";
        auto dot = std::string(filename).find_last_of('.');
        if (dot != std::string::npos) {
            ext = std::string(filename).substr(dot);
        }
        uploadedFilename = "product_" + std::to_string(time(nullptr)) + ext;
        std::string filepath = uploadDir + uploadedFilename;
        outputFileStream.open(filepath, std::ios::binary);

        if (!outputFileStream.is_open()) {
            return -1; // Indicate a file-open error
        }

        // Return FORM_FIELD_STORAGE_GET to signal that CivetWeb should
        // use field_get_cb to process the file's content.
        return FORM_FIELD_STORAGE_GET;
    }

    return FORM_FIELD_STORAGE_GET;
}

// Modified field_get_cb function
static int field_get_cb(const char *key, const char *value, size_t valuelen, void *user_data) {
    FormData *data = (FormData *)user_data;

    // First, check if this is the 'image' field, which contains the file data.
    if (strcmp(key, "image") == 0) {
        if (outputFileStream.is_open()) {
            outputFileStream.write(value, valuelen);
        }
        return 0; // Return 0 to continue processing
    }

    // Original logic for other form fields
    if (strcmp(key, "id") == 0) {
        data->id.assign(value, valuelen);
    } else if (strcmp(key, "name") == 0 || strcmp(key, "productName") == 0) {
        data->name.assign(value, valuelen);
    } else if (strcmp(key, "description") == 0 || strcmp(key, "productDescription") == 0) {
        data->description.assign(value, valuelen);
    } else if (strcmp(key, "minPrice") == 0) {
        data->minPrice.assign(value, valuelen);
    } else if (strcmp(key, "maxPrice") == 0) {
        data->maxPrice.assign(value, valuelen);
    } else if (strcmp(key, "price") == 0) {
        data->minPrice.assign(value, valuelen);
        data->maxPrice.assign(value, valuelen);
    } else if (strcmp(key, "priceUnits") == 0) {
        data->priceUnits.assign(value, valuelen);
    } else if (strcmp(key, "incoterms") == 0) {
        data->incoterms.assign(value, valuelen);
    } else if (strcmp(key, "sellerCode") == 0) {
        data->sellerCode.assign(value, valuelen);
    } else if (strcmp(key, "productStandards") == 0) {
        data->productStandards.assign(value, valuelen);
    } else if (strcmp(key, "productGrade") == 0) {
        data->productGrade.assign(value, valuelen);
    }

    return 0;
}


static int handle_upload_product(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") == 0) {
        // Serve the styled upload form with the updated HTML
        mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Upload Product</title></head><body>"
        "<h1>Upload New Product</h1>"
        "<form method='POST' action='/upload_product' enctype='multipart/form-data'>"
        "Name: <input type='text' name='name' required/><br/>"
        "Description: <textarea name='description' required></textarea><br/>"
        "Price: <input type='number' step='0.01' name='price' required/><br/>"
        "Unit: <select name='priceUnits'>"
        "<option value='unit'>unit</option>"
        "<option value='packs'>packs</option>"
        "<option value='ton'>ton</option>"
        "<option value='gram'>gram</option>"
        "<option value='kilo'>kilo</option>"
        "</select><br/>"
        "Image: <input type='file' name='image' accept='image/*' required/><br/>"
        "<button type='submit'>Upload</button>"
        "</form>"
        "</body></html>");
        return 200;
    }

    if (strcmp(ri->request_method, "POST") == 0) {
        FormData formData;
        mg_form_data_handler fdh{};
        fdh.field_found = field_found_cb;
        fdh.field_get = field_get_cb;
        fdh.user_data = &formData;
        mg_handle_form_request(conn, &fdh);

        // Close the file stream after the upload is complete
        if (outputFileStream.is_open()) {
            outputFileStream.close();
        }

        if (formData.name.empty() || uploadedFilename.empty()) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<h1>Missing product name or image</h1>");
            return 400;
        }

        // Use the globally set `uploadedFilename` instead of formData.filename
        std::string relativePath = "/admin/uploads/" + uploadedFilename;

        sqlite3 *pdb = nullptr;
        int rc = sqlite3_open(PRODUCTS_DB_PATH, &pdb);
        if (rc != SQLITE_OK) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nDB open failed: %s", sqlite3_errmsg(pdb));
            if (pdb) sqlite3_close(pdb);
            return 500;
        }

        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(pdb,
                                "INSERT INTO products (name, description, minPrice, maxPrice, image, priceUnits) VALUES (?, ?, ?, ?, ?, ?);",
                                -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nPrepare failed: %s", sqlite3_errmsg(pdb));
            sqlite3_close(pdb);
            return 500;
        }

        double price = 0.0;
        if (!formData.minPrice.empty()) {
            price = atof(formData.minPrice.c_str());
        }

        sqlite3_bind_text(stmt, 1, formData.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, formData.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, price);
        sqlite3_bind_double(stmt, 4, price);
        sqlite3_bind_text(stmt, 5, relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, formData.priceUnits.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            const char *err = sqlite3_errmsg(pdb);
            sqlite3_finalize(stmt);
            sqlite3_close(pdb);
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nInsert failed: %s", err);
            return 500;
        }

        sqlite3_finalize(stmt);
        sqlite3_close(pdb);

        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return 302;
    }
    mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nOnly GET and POST are supported.");
    return 405;
}
// ---------------- Serve Products ----------------

static int handle_upload_page(struct mg_connection *conn, void *cbdata) {
    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    // Corrected SQL query to select minPrice and priceUnits
    sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image, priceUnits FROM products;", -1, &stmt, nullptr);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
              "<!DOCTYPE html><html><head><title>Uploaded Products</title></head><body>"
              "<h1>Uploaded Products</h1><ul>"
    );

    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCount++;
        const char *prodName = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc = (const char*)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);
        const char *priceUnits = (const char*)sqlite3_column_text(stmt, 4); // Get the new column

        mg_printf(conn,
                  "<li><b>Name:</b> %s<br>"
                  "<b>Description:</b> %s<br>"
                  "<b>Price:</b> $%.2f / %s<br>" // Updated display format
                  "<img src='%s' width='200'><br><br></li>",
                  prodName ? prodName : "",
                  desc ? desc : "",
                  price,
                  priceUnits ? priceUnits : "unit", // Display the unit
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
            mg_printf(conn,
                      "HTTP/1.1 302 Found\r\n"
                      "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                      "Content-Length: 0\r\n"
                      "Connection: close\r\n\r\n");
        } else if (rc == SQLITE_CONSTRAINT) {
            mg_printf(conn,
                      "HTTP/1.1 302 Found\r\n"
                      "Location: /auth/joinpage.html?error=userexists\r\n"
                      "Content-Length: 0\r\n"
                      "Connection: close\r\n\r\n");
        } else {
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
                mg_printf(conn,
                          "HTTP/1.1 302 Found\r\n"
                          "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n\r\n");
            } else {
                mg_printf(conn,
                          "HTTP/1.1 302 Found\r\n"
                          "Location: /auth/loginpage.html?error=invalid\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n\r\n");
            }
        } else {
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

//------------- handle edit product form ----------------

// Corrected handle_edit_product_form function
static int handle_edit_product(struct mg_connection *conn, void *cbdata) {
    const mg_request_info *ri = mg_get_request_info(conn);

    char id[32] = {0};
    mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));
    int id_num = atoi(id);

    if (id_num == 0) { // Check for a valid numeric ID
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing or invalid product ID");
        return 400;
    }

    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image, priceUnits FROM products WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id_num);

    const char *name = "";
    const char *desc = "";
    double price = 0.0;
    const char *image = "";
    const char *unit = ""; // New variable

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        name = (const char*)sqlite3_column_text(stmt, 0);
        desc = (const char*)sqlite3_column_text(stmt, 1);
        price = sqlite3_column_double(stmt, 2);
        image = (const char*)sqlite3_column_text(stmt, 3);
        unit = (const char*)sqlite3_column_text(stmt, 4); // Get the new column
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nProduct not found");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 404;
    }

mg_printf(conn,
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
    "<!DOCTYPE html><html><head><title>Edit Product</title></head><body>"
    "<h1>Edit Product</h1>"
    "<form method='POST' action='/update_product' enctype='multipart/form-data'>"
    "<input type='hidden' name='id' value='%d'/>"
    "Name: <input type='text' name='name' value='%s' required/><br/>"
    "Description: <textarea name='description' required>%s</textarea><br/>"
    "Price: <input type='number' step='0.01' name='price' value='%.2f' required/><br/>"
    "Unit: <input type='text' name='priceUnits' value='%s' required/><br/>" // A text input is used for simplicity, but a dropdown could be used here as well
    "Current Image:<br><img src='%s' width='150'/><br/>"
    "Upload New Image: <input type='file' name='image' accept='image/*'/><br/>"
    "<button type='submit'>Update</button>"
    "</form>"
    "</body></html>",
    id_num, name, desc, price, unit, image // Note the new 'unit' variable
);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 200;
}
// ---------------- Update Product Handler ----------------

static int handle_update_product(mg_connection *conn, void *cbdata) {
    FormData formData;
    mg_form_data_handler fdh{};
    fdh.field_found = field_found_cb;
    fdh.field_get = field_get_cb;
    fdh.user_data = &formData;
    mg_handle_form_request(conn, &fdh);

    char id[32] = {0};
    if (!formData.id.empty()) {
        strncpy(id, formData.id.c_str(), sizeof(id) - 1);
    }
    int id_num = atoi(id);

    if (id_num == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing or invalid product ID");
        return 400;
    }

    std::string currentImage;
    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);
    sqlite3_stmt *select_stmt;
    sqlite3_prepare_v2(db, "SELECT image FROM products WHERE id=?;", -1, &select_stmt, nullptr);
    sqlite3_bind_int(select_stmt, 1, id_num);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const char *img = (const char*)sqlite3_column_text(select_stmt, 0);
        if (img) currentImage = img;
    }
    sqlite3_finalize(select_stmt);

    std::string imagePath = currentImage;
    if (!formData.imageData.empty()) {
        const std::string uploadDir = "C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/uploads/";
#ifdef _WIN32
        _mkdir("C:/Users/misba/OneDrive/Desktop/supplierbuyer/admin/");
        _mkdir(uploadDir.c_str());
#else
        mkdir(uploadDir.c_str(), 0777);
#endif
        std::string ext = ".jpg";
        auto dot = formData.filename.find_last_of('.');
        if (dot != std::string::npos) {
            ext = formData.filename.substr(dot);
        }
        std::string basename = "product_" + std::string(id) + "_" + std::to_string(time(nullptr)) + ext;
        std::string filepath = uploadDir + basename;
        std::ofstream out(filepath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(formData.imageData.data()), formData.imageData.size());
            out.close();
            imagePath = "/admin/uploads/" + basename;
        }
    }

    std::string sql = "UPDATE products SET name=?, description=?, minPrice=?, maxPrice=?, image=?, priceUnits=? WHERE id=?;";
    sqlite3_stmt *stmt;
    int prepare_rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (prepare_rc != SQLITE_OK) {
        fprintf(stderr, "SQL Prepare Error: %s\n", sqlite3_errmsg(db));
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to prepare statement");
        sqlite3_close(db);
        return 500;
    }

    double price_val = 0.0;
    if (!formData.minPrice.empty()) {
        price_val = atof(formData.minPrice.c_str());
    }

    sqlite3_bind_text(stmt, 1, formData.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, formData.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, price_val);
    sqlite3_bind_double(stmt, 4, price_val);
    sqlite3_bind_text(stmt, 5, imagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, formData.priceUnits.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, id_num);

    int step_rc = sqlite3_step(stmt);
    if (step_rc != SQLITE_DONE) {
        fprintf(stderr, "SQL Update Error: %s\n", sqlite3_errmsg(db));
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to update product");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 500;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 302 Found\r\n"
        "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
        "\r\n"
    );
    return 302;
}
// ---------------- Main ----------------
#include <signal.h> // For signal handling
#include <thread>   // For sleep_for

// Forward declarations of your existing functions
// (These need to be defined elsewhere in your code)
// These are placeholders for the functions you've referenced but not provided.
int handle_upload_product(mg_connection* conn, void* data);
int handle_upload_page(mg_connection* conn, void* data);
int handle_register(mg_connection* conn, void* data);
int handle_login(mg_connection* conn, void* data);
int handle_edit_product_form(mg_connection* conn, void* data);
int handle_update_product(mg_connection* conn, void* data);

// Global variables for SQLite connections (as used in the original code)
// Global variables for SQLite connections

int main() {
    
    // Register signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize databases
    if (sqlite3_open(USERS_DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open users database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_exec(db,
                 "CREATE TABLE IF NOT EXISTS users ("
                 "username TEXT PRIMARY KEY, "
                 "password TEXT);",
                 0, 0, 0);

    sqlite3 *pdb = nullptr;
    if (sqlite3_open(PRODUCTS_DB_PATH, &pdb) != SQLITE_OK) {
        fprintf(stderr, "Cannot open products database: %s\n", sqlite3_errmsg(pdb));
        sqlite3_close(db);
        return 1;
    }
    sqlite3_exec(pdb,
                    "CREATE TABLE IF NOT EXISTS products ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT NOT NULL, "
                    "description TEXT, "
                    "minPrice REAL, "
                    "maxPrice REAL, "
                    "image TEXT,"
                    "priceUnits TEXT" // New column
                    ");",
                    nullptr, nullptr, nullptr);
        sqlite3_close(pdb);
 // Close the products DB connection, it will be opened/closed per-request

    const char *options[] = {
        "document_root", "C:/Users/misba/OneDrive/Desktop/supplierbuyer",
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

    mg_context *ctx = mg_start(&callbacks, 0, options);
    if (!ctx) {
        printf("Failed to start CivetWeb!\n");
        sqlite3_close(db);
        return 1;
    }
    mg_set_request_handler(ctx, "/update_product", handle_update_product, nullptr);

    mg_set_request_handler(ctx, "/api/products", [](mg_connection *conn, void *) -> int {
    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, description, COALESCE(minPrice, 0), image FROM products;", -1, &stmt, nullptr);

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

        char buf[2048];
        snprintf(buf, sizeof(buf),
            R"({"id":%d,"name":"%s","description":"%s","price":%.2f,"image":"%s"})",
            id,
            name ? name : "",
            desc ? desc : "",
            price,
            image ? image : ""
        );
        json += buf;
    }
    json += "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n" // allow frontend fetch
        "Content-Length: %zu\r\n\r\n%s",
        json.size(), json.c_str());

    return 200;
}, nullptr);

mg_set_request_handler(ctx, "/supplierbuyerhome.html", [](mg_connection *conn, void *) -> int {
    mg_send_file(conn, "C:\\Users\\misba\\OneDrive\\Desktop\\supplierbuyer\\supplierbuyer\\supplierbuyerhome.html");
    return 200;
}, nullptr);



    // --- Web request handlers ---
    mg_set_request_handler(ctx, "/supplierbuyer.css", [](mg_connection *conn, void *) -> int {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.css");
        return 200;
    }, nullptr);

    mg_set_request_handler(ctx, "/", [](mg_connection *conn, void *) -> int {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        if (strcmp(ri->local_uri, "/") == 0) {
            mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyer.html");
            return 200;
        }
        return 0;
    }, nullptr);

    mg_set_request_handler(ctx, "/upload_product", handle_upload_product, nullptr);
    mg_set_request_handler(ctx, "/uploadpage.html", handle_upload_page, nullptr);
    mg_set_request_handler(ctx, "/register", handle_register, nullptr);
    mg_set_request_handler(ctx, "/login", handle_login, nullptr);
    mg_set_request_handler(ctx, "/edit_product_form", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    char id[32] = {0};
    // Get the product ID from the URL's query string
    mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));
    int id_num = atoi(id);

    if (id_num == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing or invalid product ID");
        return 400;
    }

    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    // Select the product details from the database using minPrice
    sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image FROM products WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id_num);

    const char *name = "";
    const char *desc = "";
    double price = 0.0;
    const char *image = "";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        name = (const char*)sqlite3_column_text(stmt, 0);
        desc = (const char*)sqlite3_column_text(stmt, 1);
        price = sqlite3_column_double(stmt, 2);
        image = (const char*)sqlite3_column_text(stmt, 3);
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nProduct not found");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 404;
    }

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Edit Product</title></head><body>"
        "<h1>Edit Product</h1>"
        // This form's action points to your /update_product handler
        "<form method='POST' action='/update_product' enctype='multipart/form-data'>"
        "<input type='hidden' name='id' value='%d'/>"
        "Name: <input type='text' name='name' value='%s' required/><br/>"
        "Description: <textarea name='description' required>%s</textarea><br/>"
        "Price: <input type='number' step='0.01' name='price' value='%.2f' required/><br/>"
        "Current Image:<br><img src='%s' width='150'/><br/>"
        "Upload New Image: <input type='file' name='image' accept='image/*'/><br/>"
        "<button type='submit'>Update</button>"
        "</form>"
        "</body></html>",
        id_num, name, desc, price, image
    );

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 200;
}, nullptr);
    mg_set_request_handler(ctx, "/product", [](mg_connection *conn, void *) -> int {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        const char *qs = ri->query_string;

        char idStr[64] = {0};
        if (qs) {
            mg_get_var(qs, strlen(qs), "id", idStr, sizeof(idStr));
        }

        if (strlen(idStr) == 0) {
            mg_printf(conn,
                      "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\n"
                      "Missing product ID");
            return 400;
        }

        sqlite3 *db;
        sqlite3_open(PRODUCTS_DB_PATH, &db);

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image FROM products WHERE id=?;", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, atoi(idStr));

        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const char *prodName = (const char*)sqlite3_column_text(stmt, 0);
            const char *desc = (const char*)sqlite3_column_text(stmt, 1);
            double price = sqlite3_column_double(stmt, 2);
            const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);

            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                      "<!DOCTYPE html><html><head><title>%s</title></head><body>"
                      "<h1>%s</h1>"
                      "<img src='%s' width='300'><br>"
                      "<p><b>Description:</b> %s</p>"
                      "<p><b>Price:</b> $%.2f</p>"
                      "<p><a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a></p>"
                      "</body></html>",
                      prodName,
                      prodName,
                      imagePath ? imagePath : "",
                      desc ? desc : "",
                      price
            );
        } else {
            mg_printf(conn,
                      "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n"
                      "Product not found");
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 200;
    }, nullptr);

    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyer/supplierbuyerproduct.html",
                           [](mg_connection *conn, void *) -> int {
                               mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerproduct.html");
                               return 200;
                           }, nullptr);

    mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.css", [](mg_connection *conn, void *) -> int {
        mg_send_file(conn, "C:/Users/misba/OneDrive/Desktop/supplierbuyer/supplierbuyer/supplierbuyerdash.css");
        return 200;
    }, nullptr);


mg_set_request_handler(ctx, "/supplierbuyer/supplierbuyerdash.html", [](mg_connection *conn, void *) -> int {
    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, description, minPrice, image, priceUnits FROM products;", -1, &stmt, nullptr);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
              "<!DOCTYPE html><html lang='en'><head>"
              "<meta charset='UTF-8'/>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
              "<title>SupplierBuyer Dashboard</title>"
              "<link rel='stylesheet' href='/supplierbuyer/supplierbuyerdash.css'>"
              "<style>"
              "body{font-family:Arial,sans-serif;background:#fff;margin:0;color:#222;font-size:14px;}"
              ".navbar{display:flex;justify-content:space-between;align-items:center;background:#00a6e1;color:#fff;padding:12px 24px;}"
              ".navbar .logo{font-weight:700;font-size:18px;}"
              ".navbar .nav-links a{color:#fff;margin-left:20px;text-decoration:none;font-weight:500;}"
              ".navbar .nav-links a:hover{text-decoration:underline;}"
              ".content{padding:30px;}"
              ".container{max-width:900px;margin:20px auto;}"
              ".tab-label{display:inline-block;background:#00a6e1;color:#fff;font-weight:600;padding:8px 16px;border-radius:3px 3px 0 0;"
              "border:1px solid #00a6e1;border-bottom:none;margin-bottom:0;}"
              ".product-border{border-top:2px solid #00a6e1;margin-top:-1px;margin-bottom:20px;}"
              ".product-card{background:#f7f7f7;border:1px solid #c8d7df;border-top:none;border-radius:0 0 4px 4px;"
              "padding:20px 30px 24px;box-sizing:border-box;position:relative;font-size:14px;line-height:1.5;color:#444;margin-bottom:20px;}"
              ".product-card .content-area{display:flex;gap:20px;}"
              ".image-box{background:#e9ecee;border:1px solid #d9d9d9;width:160px;height:160px;display:flex;align-items:center;justify-content:center;}"
              ".image-box img{max-width:160px;max-height:160px;}"
              ".product-info{flex:1;display:flex;flex-direction:column;gap:10px;font-size:14px;}"
              ".product-title{font-weight:600;font-size:15px;margin:0;}"
              ".price{color:#222;font-weight:700;font-size:17px;}"
              ".unit{font-weight:400;font-size:12px;color:#666;margin-left:6px;}"
              ".desc p{margin:4px 0;color:#444;font-size:14px;line-height:1.5;}"
              ".footer-row{margin-top:20px;font-size:11px;color:#555;display:flex;justify-content:space-between;align-items:center;}"
              ".id-text{opacity:.6;}"
              ".btn-group{display:flex;gap:16px;}"
              ".btn{cursor:pointer;font-size:14px;font-weight:600;display:flex;align-items:center;gap:6px;color:#2c9405;border:none;background:transparent;padding:4px 6px;border-radius:2px;}"
              ".btn:hover{background:#d7f1ca;}"
              "</style></head><body>"
              "<div class='navbar'>"
              "<div class='logo'>SupplierBuyer Admin</div>"
              "<div class='nav-links'>"
              "<a href='/supplierbuyer/supplierbuyerdash.html'>Dashboard</a>"
              "<a href='/upload_product'>Upload Product</a>"
              "<a href='/view_products'>View Products</a>"
              "<a href='/logout'>Logout</a>"
              "</div></div>"
              "<div class='content'>"
              "<div class='container'>"
              "<div class='tab-label'>Live Products</div>"
              "<div class='product-border'></div>"
    );

    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCount++;
        int prodId = sqlite3_column_int(stmt, 0);
        const char *prodName = (const char*)sqlite3_column_text(stmt, 1);
        const char *desc = (const char*)sqlite3_column_text(stmt, 2);
        double price = sqlite3_column_double(stmt, 3);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 4);
        const char *priceUnits = (const char*)sqlite3_column_text(stmt, 5);

        mg_printf(conn,
                  "<div class='product-card'>"
                  "<div class='content-area'>"
                  "<div class='image-box'>"
                  "<img src='%s' alt='Product Image'/>"
                  "</div>"
                  "<div class='product-info'>"
                  "<h2 class='product-title'><a href='/product?id=%d'>%s</a></h2>"
                  "<div class='price'>$%.2f <span class='unit'>/ %s</span></div>"
                  "<div class='desc'><p>%s</p></div>"
                  "</div>"
                  "</div>"
                  "<div class='footer-row'>"
                  "<div class='id-text'>Product ID: %d</div>"
                  "<div class='btn-group'>"
                  "<form method='POST' action='/delete_product' style='display:inline;'>"
                  "<input type='hidden' name='id' value='%d'/>"
                  "<button class='btn delete' type='submit' onclick='return confirm(\"Are you sure you want to delete this product?\")'>Delete</button>"
                  "</form>"
                  "<form method='GET' action='/edit_product' style='display:inline;'>"
                  "<input type='hidden' name='id' value='%d'/>"
                  "<button class='btn edit' type='submit'>Edit</button>"
                  "</form>"
                  "</div>"
                  "</div>"
                  "</div>",
                  imagePath ? imagePath : "",
                  prodId,
                  prodName ? prodName : "Untitled",
                  price,
                  priceUnits ? priceUnits : "unit",
                  desc ? desc : "",
                  prodId,
                  prodId,
                  prodId
        );
    }

    if (rowCount == 0) {
        mg_printf(conn, "<p><i>No products uploaded yet.</i></p>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    mg_printf(conn, "</div></div></body></html>");
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

    sqlite3 *db;
    if (sqlite3_open(PRODUCTS_DB_PATH, &db) != SQLITE_OK) {
        mg_printf(conn,
                  "HTTP/1.1 500 Internal Server Error\r\n"
                  "Content-Type: text/plain\r\n\r\n"
                  "Database error: %s", sqlite3_errmsg(db));
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
        std::string full_path = "C:/Users/misba/OneDrive/Desktop/supplierbuyer" + image_path_str;
        
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
}, nullptr); // <-- The semicolon should be here.
    // Product Display for Detail

    // Handler for the product detail page
mg_set_request_handler(ctx, "/product", [](mg_connection *conn, void *) -> int {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    char id_str[32] = {0};
    mg_get_var(ri->query_string, strlen(ri->query_string), "id", id_str, sizeof(id_str));
    int id = atoi(id_str);

    if (id == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<h1>Product ID is missing or invalid.</h1>");
        return 400;
    }

    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image, priceUnits FROM products WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        const char *desc = (const char*)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt, 2);
        const char *imagePath = (const char*)sqlite3_column_text(stmt, 3);
        const char *priceUnits = (const char*)sqlite3_column_text(stmt, 4);

        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                  "<!DOCTYPE html><html lang='en'><head>"
                  "<meta charset='UTF-8'/>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                  "<title>Product Detail</title>"
                  "<style>"
                  "body { font-family: Arial, sans-serif; background: #f4f4f4; margin: 0; padding: 20px; }"
                  ".container { max-width: 800px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
                  ".product-image img { max-width: 100%; height: auto; border-radius: 4px; }"
                  ".product-details h1 { color: #00a6e1; }"
                  ".price-tag { font-size: 24px; font-weight: bold; color: #333; }"
                  "</style>"
                  "</head><body>"
                  "<div class='container'>"
                  "<div class='product-image'>"
                  "<img src='%s' alt='%s' />"
                  "</div>"
                  "<div class='product-details'>"
                  "<h1>%s</h1>"
                  "<p class='price-tag'>Price: $%.2f / %s</p>"
                  "<p>%s</p>"
                  "<a href='/supplierbuyer/supplierbuyerdash.html'>Back to Dashboard</a>"
                  "</div>"
                  "</div>"
                  "</body></html>",
                  imagePath ? imagePath : "",
                  name ? name : "",
                  name ? name : "",
                  price,
                  priceUnits ? priceUnits : "unit",
                  desc ? desc : ""
        );
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>Product not found.</h1>");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 200;
}, nullptr);

    mg_set_request_handler(ctx, "/edit_product", [](mg_connection *conn, void *) -> int {
    const mg_request_info *ri = mg_get_request_info(conn);

    char id[32] = {0};
    mg_get_var(ri->query_string, strlen(ri->query_string), "id", id, sizeof(id));
    int id_num = atoi(id);

    if (id_num == 0) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing or invalid product ID");
        return 400;
    }

    sqlite3 *db;
    sqlite3_open(PRODUCTS_DB_PATH, &db);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, description, minPrice, image, priceUnits FROM products WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id_num);

    const char *name = "";
    const char *desc = "";
    double price = 0.0;
    const char *image = "";
    const char *unit = "";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        name = (const char*)sqlite3_column_text(stmt, 0);
        desc = (const char*)sqlite3_column_text(stmt, 1);
        price = sqlite3_column_double(stmt, 2);
        image = (const char*)sqlite3_column_text(stmt, 3);
        unit = (const char*)sqlite3_column_text(stmt, 4);
    } else {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nProduct not found");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 404;
    }

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
              "<!DOCTYPE html><html><head><title>Edit Product</title></head><body>"
              "<h1>Edit Product</h1>"
              "<form method='POST' action='/update_product' enctype='multipart/form-data'>"
              "<input type='hidden' name='id' value='%d'/>"
              "Name: <input type='text' name='name' value='%s' required/><br/>"
              "Description: <textarea name='description' required>%s</textarea><br/>"
              "Price: <input type='number' step='0.01' name='price' value='%.2f' required/><br/>"
              "Unit: <select name='priceUnits'>"
              "<option value='unit' %s>unit</option>"
              "<option value='packs' %s>packs</option>"
              "<option value='ton' %s>ton</option>"
              "<option value='gram' %s>gram</option>"
              "<option value='kilo' %s>kilo</option>"
              "</select><br/>"
              "Current Image:<br><img src='%s' width='150'/><br/>"
              "Upload New Image: <input type='file' name='image' accept='image/*'/><br/>"
              "<button type='submit'>Update</button>"
              "</form>"
              "</body></html>",
              id_num,
              name,
              desc,
              price,
              (unit && strcmp(unit, "unit") == 0) ? "selected" : "",
              (unit && strcmp(unit, "packs") == 0) ? "selected" : "",
              (unit && strcmp(unit, "ton") == 0) ? "selected" : "",
              (unit && strcmp(unit, "gram") == 0) ? "selected" : "",
              (unit && strcmp(unit, "kilo") == 0) ? "selected" : "",
              image
    );

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 200;
}, nullptr);

    // --- Main server loop ---
    printf("Server starting on http://localhost:8080\n");
    printf("Press Ctrl+C to stop the server.\n");
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // --- Shutdown sequence ---
    printf("\nShutting down server...\n");
    mg_stop(ctx);
    sqlite3_close(db); // Close the users DB connection here
    printf("Server stopped.\n");

    return 0;
}