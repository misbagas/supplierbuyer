#define _POSIX_C_SOURCE 200809L
#include <iostream>
#include <fstream>
#include <algorithm>
#include <windows.h>
#include "CivetServer.h"
#include "json.hpp"
#include <cstring>
#include <vector>
#include "platform.h"
#include <unistd.h>

using json = nlohmann::json;

static const char* PRODUCT_FILE = "products.json";

/* ================= PRODUCTS API ================= */
class ProductsHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        json products = json::array();

        std::ifstream in(PRODUCT_FILE);
        if (in.is_open()) {
            try {
                in >> products;
            } catch (...) {
                products = json::array();
            }
        }

        std::string body = products.dump();

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %zu\r\n\r\n%s",
            body.size(), body.c_str());

        return true;
    }
};

/* ================= UPLOAD API ================= */
class UploadHandler : public CivetHandler {
private:
    std::string extractFilename(const std::string& header) {
        size_t pos = header.find("filename=\"");
        if (pos == std::string::npos) return "";
        pos += 10;
        size_t endPos = header.find("\"", pos);
        return header.substr(pos, endPos - pos);
    }

    std::string extractFieldName(const std::string& header) {
        size_t pos = header.find("name=\"");
        if (pos == std::string::npos) return "";
        pos += 6;
        size_t endPos = header.find("\"", pos);
        return header.substr(pos, endPos - pos);
    }

    std::string generateSimpleFilename(const std::string& originalName, const std::string& ext) {
        return "product_" + std::to_string(time(0)) + "_" + std::to_string(rand() % 1000) + "." + ext;
    }

public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        long long len = ri->content_length;
        if (len <= 0) return false;

        std::vector<char> post_data(len);
        mg_read(conn, post_data.data(), len);

        std::string ct = mg_get_header(conn, "Content-Type");
        size_t bpos = ct.find("boundary=");
        std::string boundary = "--" + ct.substr(bpos + 9);
        
        // Data Buffers
        char pName[256] = {0}, pPrice[64] = {0}, pDesc[512] = {0};
        std::vector<std::string> imageUrls;

        std::string data(post_data.begin(), post_data.end());
        size_t pos = data.find(boundary);

        while (pos != std::string::npos && pos < data.length()) {
            pos += boundary.length();
            if (data.substr(pos, 2) == "--") break; // End of data
            pos += 2; // Skip CRLF

            size_t header_end = data.find("\r\n\r\n", pos);
            if (header_end == std::string::npos) break;

            std::string header = data.substr(pos, header_end - pos);
            std::string field = extractFieldName(header);
            std::string fname = extractFilename(header);
            pos = header_end + 4;

            size_t next_b = data.find(boundary, pos);
            if (next_b == std::string::npos) break;
            
            size_t c_end = next_b - 2; // Remove CRLF
            std::string content = data.substr(pos, c_end - pos);

            if (!fname.empty()) {
                // It's a file
                std::string ext = fname.substr(fname.find_last_of(".") + 1);
                std::string safeName = generateSimpleFilename(fname, ext);
                std::string fullPath = "C:\\Users\\Guntur\\OneDrive\\Desktop\\supplierbuyer\\uploads\\" + safeName;
                
                std::ofstream file(fullPath, std::ios::binary);
                if (file.is_open()) {
                    file.write(content.c_str(), content.size());
                    file.close();
                    imageUrls.push_back("/uploads/" + safeName);
                }
            } else {
                // It's a text field
               if (field == "name") {
    strncpy(pName, content.c_str(), sizeof(pName) - 1);
    pName[sizeof(pName) - 1] = '\0';
}
else if (field == "price") {
    strncpy(pPrice, content.c_str(), sizeof(pPrice) - 1);
    pPrice[sizeof(pPrice) - 1] = '\0';
}
else if (field == "description") {
    strncpy(pDesc, content.c_str(), sizeof(pDesc) - 1);
    pDesc[sizeof(pDesc) - 1] = '\0';
}

            }
            pos = next_b;
        }

        // Save to products.json
        json products = json::array();
        std::ifstream in(PRODUCT_FILE);
        if (in.is_open()) { in >> products; in.close(); }

        json newProd;
        newProd["name"] = pName;
        newProd["price"] = std::atof(pPrice);
        newProd["description"] = pDesc;
        newProd["image"] = imageUrls.empty() ? "" : imageUrls[0];
        newProd["images"] = imageUrls; // Store all image paths in an array

        products.push_back(newProd);
        std::ofstream out(PRODUCT_FILE);
        out << products.dump(2);
        
        // Redirect back
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\n\r\n");
        return true;
    }
};

class UpdateProductWithImageHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        long long len = ri->content_length;
        std::vector<char> post_data(len);
        mg_read(conn, post_data.data(), len);

        std::string ct = mg_get_header(conn, "Content-Type");
        size_t bpos = ct.find("boundary=");
        std::string boundary = "--" + ct.substr(bpos + 9);

        int pIndex = -1;
        std::string pName, pPrice, pDesc, newImageUrl;
        std::string data(post_data.begin(), post_data.end());
        size_t pos = data.find(boundary);

        // --- Parsing Logic ---
        while (pos != std::string::npos && pos < data.length()) {
            pos += boundary.length();
            if (data.substr(pos, 2) == "--") break;
            pos += 2; 

            size_t header_end = data.find("\r\n\r\n", pos);
            if (header_end == std::string::npos) break;

            std::string header = data.substr(pos, header_end - pos);
            size_t npos = header.find("name=\"");
            size_t nend = header.find("\"", npos + 6);
            std::string field = header.substr(npos + 6, nend - (npos + 6));
            
            size_t fnpos = header.find("filename=\"");
            bool isFile = (fnpos != std::string::npos);

            pos = header_end + 4;
            size_t next_b = data.find(boundary, pos);
            std::string content = data.substr(pos, next_b - pos - 2);

            if (isFile && !content.empty()) {
                std::string safeName = "updated_" + std::to_string(time(0)) + ".jpg";
                std::ofstream file("C:\\Users\\Guntur\\OneDrive\\Desktop\\supplierbuyer\\uploads\\" + safeName, std::ios::binary);
                file.write(content.c_str(), content.size());
                newImageUrl = "/uploads/" + safeName;
            } else {
                if (field == "index") pIndex = std::stoi(content);
                else if (field == "name") pName = content;
                else if (field == "price") pPrice = content;
                else if (field == "description") pDesc = content;
            }
            pos = next_b;
        }

// Inside UpdateProductWithImageHandler::handlePost
json products;
std::ifstream in("products.json");
if (in.is_open()) {
    in >> products;
    in.close();
}

if (pIndex >= 0 && pIndex < (int)products.size()) {
    products[pIndex]["name"] = pName;
    products[pIndex]["price"] = std::atof(pPrice.c_str());
    products[pIndex]["description"] = pDesc;
    
    // 🔹 Only update image if a new one was actually uploaded
    if (!newImageUrl.empty()) {
        products[pIndex]["image"] = newImageUrl;
    }

    std::ofstream out("products.json");
    out << products.dump(4);
    out.close();
}

        std::string response = "{\"success\":true}";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", response.size(), response.c_str());
        return true;
    }
};
class UploadFormHandler : public CivetHandler {
public:
    bool handleGet(CivetServer *server, struct mg_connection *conn) override {
        std::string html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Upload Product</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f6f9; margin: 0; padding: 20px; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
        .container { background: #fff; padding: 40px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); width: 100%; max-width: 500px; }
        h1 { text-align: center; color: #0077cc; margin-bottom: 30px; }
        label { display: block; margin-top: 15px; font-weight: bold; color: #333; }
        input, textarea { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ccc; border-radius: 6px; font-size: 14px; box-sizing: border-box; }
        textarea { min-height: 100px; resize: vertical; }
        .file-input-wrapper { position: relative; overflow: hidden; display: inline-block; width: 100%; }
        .file-input-wrapper input[type=file] { position: absolute; left: -9999px; }
        .file-input-label { display: block; padding: 20px; background: #f0f0f0; border: 2px dashed #0077cc; border-radius: 6px; text-align: center; cursor: pointer; margin-top: 5px; color: #0077cc; }
        .file-input-label:hover { background: #e8f4fd; }
        #fileNameDisplay { margin-top: 10px; color: #28a745; font-size: 14px; white-space: pre-wrap; }
        button { width: 100%; padding: 12px; margin-top: 20px; background: #0077cc; color: white; border: none; border-radius: 6px; font-size: 16px; cursor: pointer; transition: background 0.3s; }
        button:hover { background: #005fa3; }
        .back-link { text-align: center; margin-top: 15px; }
        .back-link a { color: #0077cc; text-decoration: none; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Upload New Product</h1>
        <form method="POST" action="/api/upload_product" enctype="multipart/form-data">
            <label>Product Name *</label>
            <input type="text" name="name" required>
            <label>Price *</label>
            <input type="number" name="price" step="0.01" required>
            <label>Description</label>
            <textarea name="description"></textarea>
            <label>Product Images (PNG or JPG) *</label>
            <div class="file-input-wrapper">
                <input type="file" name="image" id="imageInput" accept=".png,.jpg,.jpeg" multiple required>
                <label for="imageInput" class="file-input-label">Click to select images or drag & drop</label>
                <div id="fileNameDisplay"></div>
            </div>
            <button type="submit">Upload Product</button>
        </form>
        <div class="back-link">
            <a href="/supplierbuyer/supplierbuyerdash.html">Back to Dashboard</a>
        </div>
    </div>
    <script>
        const fileInput = document.getElementById('imageInput');
        const fileNameDisplay = document.getElementById('fileNameDisplay');
        fileInput.addEventListener('change', function() {
            if (this.files.length > 0) {
                let names = Array.from(this.files).map(f => '✓ ' + f.name).join('\n');
                fileNameDisplay.textContent = names;
            }
        });
    </script>
</body>
</html>
)HTML";

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n%s", html.size(), html.c_str());
        return true;
    }
};
/* ================= ADMIN PROFILE API ================= */
class AdminProfileHandler : public CivetHandler {
private:
    const std::string USER_FILE = "users.json";

    // Helper to extract text fields from multipart
    std::string getField(const std::string& body, const std::string& key) {
        std::string searchKey = "name=\"" + key + "\"";
        size_t pos = body.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = body.find("\r\n\r\n", pos);
        if (pos == std::string::npos) return "";
        pos += 4;
        return body.substr(pos, body.find("\r\n", pos) - pos);
    }

public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        std::ifstream in(USER_FILE);
        json users = json::array();
        if (in.is_open()) in >> users;
        
        json currentUser = (!users.empty()) ? users[0] : json::object({{"username", "Admin"}, {"description", ""}, {"photo", ""}});
        currentUser.erase("password");
        std::string body = currentUser.dump();

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n\r\n%s", 
                  body.size(), body.c_str());
        return true;
    }

    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::vector<char> buffer(ri->content_length);
        mg_read(conn, buffer.data(), ri->content_length);
        std::string body(buffer.begin(), buffer.end());

        std::ifstream in(USER_FILE);
        json users = json::array();
        if (in.is_open()) in >> users;
        if (users.empty()) users.push_back(json::object());

        // Update Text Fields
        users[0]["username"] = getField(body, "username");
        users[0]["description"] = getField(body, "description");
        std::string newP = getField(body, "password");
        if (!newP.empty()) users[0]["password"] = newP;

        // --- NEW: SAVE IMAGE LOGIC ---
        size_t filePos = body.find("filename=\"");
        if (filePos != std::string::npos) {
            size_t start = body.find("\r\n\r\n", filePos) + 4;
            // Find the end of the file by looking for the boundary marker
            std::string boundary = body.substr(0, body.find("\r\n"));
            size_t end = body.find(boundary, start) - 4; // -4 for \r\n--

            if (end > start) {
                std::string fileData = body.substr(start, end - start);
                std::string fileName = "profile_" + users[0]["username"].get<std::string>() + ".jpg";
                std::string fullPath = "uploads/" + fileName;

                std::ofstream outFile(fullPath, std::ios::binary);
                outFile.write(fileData.data(), fileData.size());
                outFile.close();

                users[0]["photo"] = "/uploads/" + fileName;
            }
        }

        std::ofstream out(USER_FILE);
        out << users.dump(4);
        
        std::string resp = "{\"success\":true}";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", resp.c_str());
        return true;
    }
};
/* ================= UPDATE PRODUCTS API ================= */
class UpdateProductsHandler : public CivetHandler {
public:
    bool handlePost(CivetServer* server, struct mg_connection* conn) override {
        std::string dummy;
        std::string body = "";
        char buf[1024];
        int n;
        // Read the entire JSON body sent from the dashboard
        while ((n = mg_read(conn, buf, sizeof(buf))) > 0) {
            body.append(buf, n);
        }

        try {
            // Validate that the body is valid JSON
            auto updatedProducts = json::parse(body);
            
            // Overwrite products.json with the new list
            std::ofstream out("products.json");
            out << updatedProducts.dump(4);
            out.close();

            std::string response = "{\"success\": true}";
            mg_printf(conn, "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\n\r\n%s",
                            response.size(), response.c_str());
        } catch (...) {
            std::string response = "{\"success\": false, \"message\": \"Invalid JSON\"}";
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\n\r\n%s",
                            response.size(), response.c_str());
        }
        return true;
    }
};
/* ================= UPLOADS STATIC FILE HANDLER ================= */
class UploadsHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::string uri = ri->request_uri;
        
        // Remove /uploads/ prefix
        if (uri.find("/uploads/") == 0) {
            uri = uri.substr(9);  // Remove "/uploads/"
        }

        char filepath[512];
        snprintf(filepath, sizeof(filepath),
            "C:\\Users\\Guntur\\OneDrive\\Desktop\\supplierbuyer\\uploads\\%s",
            uri.c_str());

        fprintf(stderr, "📥 Serving file request: %s\n", filepath);

        // Try to open the file
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            fprintf(stderr, "⚠️ File not found: %s\n", filepath);
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return true;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file content
        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        file.close();

        // Determine content type
        std::string content_type = "application/octet-stream";
        if (uri.find(".jpg") != std::string::npos || uri.find(".jpeg") != std::string::npos) {
            content_type = "image/jpeg";
        } else if (uri.find(".png") != std::string::npos) {
            content_type = "image/png";
        }

        fprintf(stderr, "✅ Sending file: %s (%zu bytes) as %s\n", filepath, size, content_type.c_str());

        // Send response with proper headers
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: public, max-age=3600\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n",
            content_type.c_str(), size);

        mg_write(conn, buffer.data(), size);

        return true;
    }
};
/* ================= CSS STATIC FILE HANDLER ================= */
class CSSHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::string uri = ri->request_uri;
        
        // Remove /supplierbuyer/ prefix
        std::string filepath_suffix = uri;
        if (uri.find("/supplierbuyer/") == 0) {
            filepath_suffix = uri.substr(15);  // Remove "/supplierbuyer/"
        }

        char filepath[512];
        sprintf_s(filepath, sizeof(filepath),
            "C:\\Users\\Guntur\\OneDrive\\Desktop\\supplierbuyer\\supplierbuyer\\%s",
            filepath_suffix.c_str());

        fprintf(stderr, "📥 Serving request: %s\n", filepath);

        // Try to open the file
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            fprintf(stderr, "⚠️ File not found: %s\n", filepath);
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return true;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file content
        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        file.close();

        // Determine content type
        std::string content_type = "application/octet-stream";
        if (filepath_suffix.find(".css") != std::string::npos) {
            content_type = "text/css";
        } else if (filepath_suffix.find(".html") != std::string::npos) {
            content_type = "text/html";
        } else if (filepath_suffix.find(".js") != std::string::npos) {
            content_type = "application/javascript";
        } else if (filepath_suffix.find(".jpg") != std::string::npos || filepath_suffix.find(".jpeg") != std::string::npos) {
            content_type = "image/jpeg";
        } else if (filepath_suffix.find(".png") != std::string::npos) {
            content_type = "image/png";
        }

        fprintf(stderr, "✅ Sending file: %s (%zu bytes) as %s\n", filepath, size, content_type.c_str());

        // Send response with proper headers
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: public, max-age=3600\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n",
            content_type.c_str(), size);

        mg_write(conn, buffer.data(), size);

        return true;
    }
};
class RegisterHandler : public CivetHandler {
public:
    bool handlePost(CivetServer* server, struct mg_connection* conn) override {
        char post_data[2048];
        int dlen = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (dlen <= 0) return false;
        post_data[dlen] = '\0';

        char username[100], password[100], question[200], answer[200];
        mg_get_var(post_data, dlen, "username", username, sizeof(username));
        mg_get_var(post_data, dlen, "password", password, sizeof(password));
        mg_get_var(post_data, dlen, "security_question", question, sizeof(question));
        mg_get_var(post_data, dlen, "security_answer", answer, sizeof(answer));

        // Save Format: user:pass:question:answer
        std::ofstream userFile("users.txt", std::ios::app);
        if (userFile.is_open()) {
            userFile << username << ":" << password << ":" << question << ":" << answer << "\n";
            userFile.close();
        }

       mg_printf(conn,
            "HTTP/1.1 302 Found\r\n"
            "Location: /supplierbuyer/supplierbuyerdash.html\r\n"
            "Content-Length: 0\r\n\r\n");
        return true;
    }
};
/* ================= LOGIN HANDLER ================= */
class LoginHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        char post_data[1024];
        int dlen = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (dlen <= 0) return false;
        post_data[dlen] = '\0';

        char input_user[100], input_pass[100];
        mg_get_var(post_data, dlen, "username", input_user, sizeof(input_user));
        mg_get_var(post_data, dlen, "password", input_pass, sizeof(input_pass));

        std::ifstream userFile("users.txt");
        std::string line;
        bool authenticated = false;

        if (userFile.is_open()) {
            while (std::getline(userFile, line)) {
                std::stringstream ss(line);
                std::string stored_user, stored_pass, q, a;
                
                // Parse the colon-separated line
                if (std::getline(ss, stored_user, ':') && std::getline(ss, stored_pass, ':')) {
                    if (stored_user == input_user && stored_pass == input_pass) {
                        authenticated = true;
                        break;
                    }
                }
            }
            userFile.close();
        }

        if (authenticated) {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\n\r\n");
        } else {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/loginpage.html?error=invalid\r\n\r\n");
        }
        return true;
    }
};
/* ================= LOGOUT HANDLER ================= */
class LogoutHandler : public CivetHandler {
public:
    bool handleGet(CivetServer* server, struct mg_connection* conn) override {
        // Redirect to the landing page
        mg_printf(conn,
            "HTTP/1.1 302 Found\r\n"
            "Location: /supplierbuyer/supplierbuyer.html\r\n"
            "Content-Length: 0\r\n\r\n");
        return true;
    }
};
class ResetPasswordHandler : public CivetHandler {
public:
    bool handlePost(CivetServer* server, struct mg_connection* conn) override {
        char post_data[2048];
        int dlen = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (dlen <= 0) return false;
        post_data[dlen] = '\0';

        char user[100], q_input[200], a_input[200], new_pass[100];
        mg_get_var(post_data, dlen, "username", user, sizeof(user));
        mg_get_var(post_data, dlen, "security_question", q_input, sizeof(q_input));
        mg_get_var(post_data, dlen, "security_answer", a_input, sizeof(a_input));
        mg_get_var(post_data, dlen, "new_password", new_pass, sizeof(new_pass));

        std::ifstream inFile("users.txt");
        std::vector<std::string> lines;
        std::string line;
        bool success = false;

        if (inFile.is_open()) {
            while (std::getline(inFile, line)) {
                std::stringstream ss(line);
                std::string u, p, q, a;
                std::getline(ss, u, ':'); std::getline(ss, p, ':');
                std::getline(ss, q, ':'); std::getline(ss, a, ':');

                // Check if user, question, AND answer match
                if (u == user && q == q_input && a == a_input) {
                    lines.push_back(u + ":" + new_pass + ":" + q + ":" + a);
                    success = true;
                } else {
                    lines.push_back(line);
                }
            }
            inFile.close();
        }

        if (success) {
            std::ofstream outFile("users.txt");
            for (const auto& l : lines) outFile << l << "\n";
            outFile.close();
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/loginpage.html\r\n\r\n");
        } else {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/resetpassword.html?error=mismatch\r\n\r\n");
        }
        return true;
    }
};

/* ================= MESSAGE SYSTEM ================= */
struct Message {
    std::string product_id;
    std::string sender;
    std::string text;
    std::string timestamp;
};

/* ================= MESSAGING API ================= */
class MessageHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        std::ifstream in("messages.json");
        json messages = json::array();
        if (in.is_open()) { in >> messages; }

        std::string body = messages.dump();
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Content-Length: %zu\r\n\r\n%s", body.size(), body.c_str());
        return true;
    }

    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        char buffer[1024];
        int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        buffer[dlen] = '\0';

        std::string product_id = extractParam(buffer, "product_id");
        std::string sender = extractParam(buffer, "sender");
        std::string receiver = extractParam(buffer, "receiver");
        std::string text = extractParam(buffer, "message");

        json newMessage = {
            {"product_id", product_id},
            {"sender", sender},
            {"receiver", receiver},
            {"text", text},
            {"timestamp", (long long)time(nullptr) * 1000} 
        };

        json messages = json::array();
        std::ifstream in("messages.json");
        if (in.is_open()) { in >> messages; }
        messages.push_back(newMessage);

        std::ofstream out("messages.json");
        out << messages.dump(4);

        std::string response = "{\"success\":true}";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Content-Length: %zu\r\n\r\n%s", response.size(), response.c_str());
        return true;
    }

private:
    std::string extractParam(const std::string& body, const std::string& key) {
        size_t start = body.find(key + "=");
        if (start == std::string::npos) return "";
        start += key.length() + 1;
        size_t end = body.find('&', start);
        std::string val = body.substr(start, end - start);
        // Basic URL decoding for spaces
        std::replace(val.begin(), val.end(), '+', ' ');
        return val;
    }
};

// In your main() function, ensure this is added:
// server.addHandler("/api/messages", new MessageHandler());
int main() {
    const char* options[] = {
        "document_root", "C:\\Users\\Guntur\\OneDrive\\Desktop\\supplierbuyer",
        "listening_ports", "8080",
        "enable_directory_listing", "no",
        "index_files", "supplierbuyer/supplierbuyer.html",
        nullptr
    };

    CivetServer server(options);

    ProductsHandler productsHandler;
    UploadHandler uploadHandler;
    UploadFormHandler uploadFormHandler;
    UpdateProductsHandler updateHandler;
    UploadsHandler uploadsHandler;
    CSSHandler cssHandler;
    LoginHandler loginHandler;
    RegisterHandler registerHandler; // <--- Add this
    LogoutHandler logoutHandler; // <--- Add this
    ResetPasswordHandler resetHandler;
    UpdateProductWithImageHandler updateWithImageHandler;
    AdminProfileHandler adminHandler;

    // Prefix all routes with /root/supplierbuyer
    server.addHandler("/root/supplierbuyer/api/products", productsHandler);
    server.addHandler("/root/supplierbuyer/api/upload_product", uploadHandler);
    server.addHandler("/root/supplierbuyer/api/update_products", updateHandler);
    server.addHandler("/root/supplierbuyer/api/messages", new MessageHandler());
    server.addHandler("/root/supplierbuyer/api/admin_profile", adminHandler);
    server.addHandler("/root/supplierbuyer/api/update_admin", adminHandler);
    
    // Auth Routes
    server.addHandler("/root/supplierbuyer/login", loginHandler);
    server.addHandler("/root/supplierbuyer/register", registerHandler);
    server.addHandler("/root/supplierbuyer/logout", logoutHandler);

    // Static Files (HTML/CSS/Images)
    // Note: Ensure your local folder structure matches or adjust the handler
    server.addHandler("/root/supplierbuyer/", cssHandler); 
    server.addHandler("/root/supplierbuyer/uploads/", uploadsHandler);

    std::cout << "Server running at http://localhost:8080/root/supplierbuyer/supplierbuyerhome.html\n";

    while (true) { Sleep(1); }

    while (true) {
        Sleep(1);
    }
}
