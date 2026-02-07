#define _POSIX_C_SOURCE 200809L
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>  // Added for sleep_for
#include <chrono>  // Added for seconds

// System Headers
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/types.h>
#endif

// Third-party Headers
#include "CivetServer.h"
#include "json.hpp"

using json = nlohmann::json;

static const char* PRODUCT_FILE = "products.json";

// --- Helper Functions ---

std::string get_current_log_time() {
    std::time_t now = std::time(nullptr);
    std::tm* now_tm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(now_tm, "%d/%b/%Y:%H:%M:%S %z");
    return oss.str();
}

// --- Handler Classes ---

// 1. Favicon Handler
class FaviconHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        mg_printf(conn, "HTTP/1.1 204 No Content\r\n\r\n");
        return true;
    }
};

// 2. Root Handler
class RootHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::ofstream logfile("access.log", std::ios::app);
        if (logfile.is_open()) {
            logfile << ri->remote_addr << " - - [" << get_current_log_time() << "] \"GET / HTTP/1.1\" 302\n";
            logfile.close();
        }
        mg_printf(conn, "HTTP/1.1 302 Found\r\n"
                        "Location: /supplierbuyer/supplierbuyer.html\r\n"
                        "Content-Length: 0\r\n\r\n");
        return true;
    }
};

class ProductsHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        json products = json::array();
        std::ifstream in(PRODUCT_FILE);
        if (in.is_open()) {
            try { in >> products; } catch (...) { products = json::array(); }
        }
        std::string body = products.dump();
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n\r\n%s",
                  body.size(), body.c_str());
        return true;
    }
};

class UploadHandler : public CivetHandler {
private:
    std::string extractFilename(const std::string& header) {
        size_t pos = header.find("filename=\"");
        if (pos == std::string::npos) return "";
        pos += 10;
        return header.substr(pos, header.find("\"", pos) - pos);
    }
    std::string extractFieldName(const std::string& header) {
        size_t pos = header.find("name=\"");
        if (pos == std::string::npos) return "";
        pos += 6;
        return header.substr(pos, header.find("\"", pos) - pos);
    }
    std::string generateSimpleFilename(const std::string&, const std::string& ext) {
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

        char pName[256] = {0}, pPrice[64] = {0}, pDesc[512] = {0};
        std::vector<std::string> imageUrls;
        std::string data(post_data.begin(), post_data.end());
        size_t pos = data.find(boundary);

        while (pos != std::string::npos && pos < data.length()) {
            pos += boundary.length();
            if (data.substr(pos, 2) == "--") break;
            pos += 2;
            size_t header_end = data.find("\r\n\r\n", pos);
            if (header_end == std::string::npos) break;

            std::string header = data.substr(pos, header_end - pos);
            std::string field = extractFieldName(header);
            std::string fname = extractFilename(header);
            pos = header_end + 4;
            size_t next_b = data.find(boundary, pos);
            if (next_b == std::string::npos) break;

            std::string content = data.substr(pos, next_b - pos - 2);

            if (!fname.empty()) {
                std::string ext = fname.substr(fname.find_last_of(".") + 1);
                std::string safeName = generateSimpleFilename(fname, ext);
                std::string fullPath = "uploads/" + safeName;
                std::ofstream file(fullPath, std::ios::binary);
                if (file.is_open()) {
                    file.write(content.c_str(), content.size());
                    file.close();
                    imageUrls.push_back("/uploads/" + safeName);
                }
            } else {
                if (field == "name") strncpy(pName, content.c_str(), 255);
                else if (field == "price") strncpy(pPrice, content.c_str(), 63);
                else if (field == "description") strncpy(pDesc, content.c_str(), 511);
            }
            pos = next_b;
        }

        json products = json::array();
        std::ifstream in(PRODUCT_FILE);
        if (in.is_open()) in >> products;

        json newProd = {
            {"name", pName},
            {"price", std::atof(pPrice)},
            {"description", pDesc},
            {"image", imageUrls.empty() ? "" : imageUrls[0]},
            {"images", imageUrls}
        };

        products.push_back(newProd);
        std::ofstream out(PRODUCT_FILE);
        out << products.dump(2);

        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\nContent-Length: 0\r\n\r\n");
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

        while (pos != std::string::npos && pos < data.length()) {
            pos += boundary.length();
            if (data.substr(pos, 2) == "--") break; 
            pos += 2; 

            size_t header_end = data.find("\r\n\r\n", pos);
            if (header_end == std::string::npos) break;

            std::string header = data.substr(pos, header_end - pos);
            
            size_t npos = header.find("name=\"");
            if (npos == std::string::npos) { pos = header_end + 4; continue; }
            size_t nend = header.find("\"", npos + 6);
            std::string field = header.substr(npos + 6, nend - (npos + 6));

            size_t fnpos = header.find("filename=\"");
            bool isFile = (fnpos != std::string::npos);

            pos = header_end + 4;
            size_t next_b = data.find(boundary, pos);
            if (next_b == std::string::npos) break;
            
            std::string content = data.substr(pos, next_b - pos - 2);

            if (isFile && !content.empty()) {
                std::string safeName = "updated_" + std::to_string(time(0)) + ".jpg";
                std::ofstream file("uploads/" + safeName, std::ios::binary);
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

        json products;
        std::ifstream in("products.json");
        if (in.is_open()) in >> products;

        if (pIndex >= 0 && pIndex < (int)products.size()) {
            products[pIndex]["name"] = pName;
            products[pIndex]["price"] = std::atof(pPrice.c_str());
            products[pIndex]["description"] = pDesc;
            if (!newImageUrl.empty()) products[pIndex]["image"] = newImageUrl;

            std::ofstream out("products.json");
            out << products.dump(4);
        }

        std::string response = "{\"success\":true}";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", response.size(), response.c_str());
        return true;
    }
};

class UploadFormHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        std::string html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Upload Product</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f6f9; padding: 20px; display: flex; justify-content: center; }
        .container { background: #fff; padding: 40px; border-radius: 10px; max-width: 500px; width: 100%; }
        h1 { text-align: center; color: #0077cc; }
        input, textarea, button { width: 100%; padding: 10px; margin-top: 10px; box-sizing: border-box; }
        button { background: #0077cc; color: white; border: none; cursor: pointer; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Upload New Product</h1>
        <form method="POST" action="/api/upload_product" enctype="multipart/form-data">
            <input type="text" name="name" placeholder="Product Name" required>
            <input type="number" name="price" step="0.01" placeholder="Price" required>
            <textarea name="description" placeholder="Description"></textarea>
            <input type="file" name="image" accept=".png,.jpg,.jpeg" multiple required>
            <button type="submit">Upload Product</button>
        </form>
        <div style="text-align:center; margin-top:15px;"><a href="/supplierbuyer/supplierbuyerdash.html">Back</a></div>
    </div>
</body>
</html>
)HTML";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n%s", html.size(), html.c_str());
        return true;
    }
};

class AdminProfileHandler : public CivetHandler {
private:
    const std::string USER_FILE = "users.json";
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
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", body.size(), body.c_str());
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

        users[0]["username"] = getField(body, "username");
        users[0]["description"] = getField(body, "description");
        std::string newP = getField(body, "password");
        if (!newP.empty()) users[0]["password"] = newP;

        size_t filePos = body.find("filename=\"");
        if (filePos != std::string::npos) {
            size_t start = body.find("\r\n\r\n", filePos) + 4;
            std::string boundary = body.substr(0, body.find("\r\n"));
            size_t end = body.find(boundary, start) - 4;
            if (end > start) {
                std::string fileData = body.substr(start, end - start);
                std::string fileName = "profile_" + users[0]["username"].get<std::string>() + ".jpg";
                std::ofstream outFile("uploads/" + fileName, std::ios::binary);
                outFile.write(fileData.data(), fileData.size());
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

class UpdateProductsHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        std::string body;
        char buf[1024];
        int n;
        while ((n = mg_read(conn, buf, sizeof(buf))) > 0) body.append(buf, n);

        try {
            auto updatedProducts = json::parse(body);
            std::ofstream out("products.json");
            out << updatedProducts.dump(4);
            std::string response = "{\"success\": true}";
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", response.size(), response.c_str());
        } catch (...) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        }
        return true;
    }
};

class UploadsHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::string uri = ri->request_uri;
        if (uri.find("/uploads/") == 0) uri = uri.substr(9);

        std::string filepath = "uploads/" + uri;
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return true;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        file.read(buffer.data(), size);

        std::string content_type = "application/octet-stream";
        if (uri.find(".jpg") != std::string::npos || uri.find(".jpeg") != std::string::npos) content_type = "image/jpeg";
        else if (uri.find(".png") != std::string::npos) content_type = "image/png";

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n", content_type.c_str(), size);
        mg_write(conn, buffer.data(), size);
        return true;
    }
};

class CSSHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::string uri = ri->request_uri;
        std::string filepath_suffix = uri;
        if (uri.find("/supplierbuyer/") == 0) filepath_suffix = uri.substr(15);

        std::string full_path = (filepath_suffix == "loginpage.html") 
                              ? "supplierbuyer/auth/loginpage.html" 
                              : "supplierbuyer/" + filepath_suffix;

        std::ifstream file(full_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return true;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        file.read(buffer.data(), size);

        std::string content_type = "text/plain";
        if (full_path.find(".css") != std::string::npos) content_type = "text/css";
        else if (full_path.find(".html") != std::string::npos) content_type = "text/html";
        else if (full_path.find(".js") != std::string::npos) content_type = "application/javascript";
        else if (full_path.find(".jpg") != std::string::npos) content_type = "image/jpeg";

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n", content_type.c_str(), size);
        mg_write(conn, buffer.data(), size);
        return true;
    }
};

class RegisterHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        char post_data[2048];
        int dlen = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (dlen <= 0) return false;
        post_data[dlen] = '\0';

        char username[100], password[100], question[200], answer[200];
        mg_get_var(post_data, dlen, "username", username, sizeof(username));
        mg_get_var(post_data, dlen, "password", password, sizeof(password));
        mg_get_var(post_data, dlen, "security_question", question, sizeof(question));
        mg_get_var(post_data, dlen, "security_answer", answer, sizeof(answer));

        std::ofstream userFile("users.txt", std::ios::app);
        if (userFile.is_open()) {
            userFile << username << ":" << password << ":" << question << ":" << answer << "\n";
            userFile.close();
        }
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/loginpage.html\r\nContent-Length: 0\r\n\r\n");
        return true;
    }
};

class LoginHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        char post_data[1024];
        int dlen = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (dlen <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
            return true;
        }
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
                std::string u, p;
                if (std::getline(ss, u, ':') && std::getline(ss, p, ':')) {
                    if (u == input_user && p == input_pass) { authenticated = true; break; }
                }
            }
        }

        if (authenticated) mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyerdash.html\r\nContent-Length: 0\r\n\r\n");
        else mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/loginpage.html?error=invalid\r\nContent-Length: 0\r\n\r\n");
        return true;
    }
};

class LogoutHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/supplierbuyer.html\r\nContent-Length: 0\r\n\r\n");
        return true;
    }
};

class ResetPasswordHandler : public CivetHandler {
public:
    bool handlePost(CivetServer*, struct mg_connection* conn) override {
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
                if (u == user && q == q_input && a == a_input) {
                    lines.push_back(u + ":" + new_pass + ":" + q + ":" + a);
                    success = true;
                } else lines.push_back(line);
            }
        }

        if (success) {
            std::ofstream outFile("users.txt");
            for (const auto& l : lines) outFile << l << "\n";
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/loginpage.html\r\n\r\n");
        } else {
            mg_printf(conn, "HTTP/1.1 302 Found\r\nLocation: /supplierbuyer/auth/resetpassword.html?error=mismatch\r\n\r\n");
        }
        return true;
    }
};

class MessageHandler : public CivetHandler {
private:
    std::string extractParam(const std::string& body, const std::string& key) {
        size_t start = body.find(key + "=");
        if (start == std::string::npos) return "";
        start += key.length() + 1;
        size_t end = body.find('&', start);
        std::string val = body.substr(start, end - start);
        std::replace(val.begin(), val.end(), '+', ' ');
        return val;
    }
public:
    bool handleGet(CivetServer*, struct mg_connection* conn) override {
        std::ifstream in("messages.json");
        json messages = json::array();
        if (in.is_open()) in >> messages;
        std::string body = messages.dump();
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", body.size(), body.c_str());
        return true;
    }

    bool handlePost(CivetServer*, struct mg_connection* conn) override {
        char buffer[1024];
        int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        buffer[dlen] = '\0';
        std::string bStr(buffer);

        json newMessage = {
            {"product_id", extractParam(bStr, "product_id")},
            {"sender", extractParam(bStr, "sender")},
            {"receiver", extractParam(bStr, "receiver")},
            {"text", extractParam(bStr, "message")},
            {"timestamp", (long long)time(nullptr) * 1000}
        };

        json messages = json::array();
        std::ifstream in("messages.json");
        if (in.is_open()) in >> messages;
        messages.push_back(newMessage);

        std::ofstream out("messages.json");
        out << messages.dump(4);
        std::string response = "{\"success\":true}";
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", response.size(), response.c_str());
        return true;
    }
};

// --- Main Server Setup ---

int main() {
    const char* options[] = {
        "document_root", ".",
        "listening_ports", "8080",
        "error_log_file", "error.log",
        "enable_directory_listing", "no",
        nullptr
    };

    CivetServer server(options);

    // Initialize Handler Instances
    RootHandler rootHandler;
    FaviconHandler faviconHandler;
    ProductsHandler productsHandler;
    UploadHandler uploadHandler;
    UpdateProductsHandler updateHandler;
    UploadsHandler uploadsHandler;
    CSSHandler cssHandler;
    LoginHandler loginHandler;
    RegisterHandler registerHandler;
    LogoutHandler logoutHandler;
    UpdateProductWithImageHandler updateWithImageHandler;
    AdminProfileHandler adminHandler;
    UploadFormHandler uploadFormHandler;
    MessageHandler messageHandler;
    ResetPasswordHandler resetHandler;

    // --- Route Registration ---
    
    // 1. Root & Favicon
    server.addHandler("/", rootHandler);
    server.addHandler("/favicon.ico", faviconHandler);

    // 2. API Routes
    server.addHandler("/api/products", productsHandler);
    server.addHandler("/api/upload_product", uploadHandler);
    server.addHandler("/api/update_products", updateHandler);
    server.addHandler("/api/update_product_with_image", updateWithImageHandler);
    server.addHandler("/upload_product", uploadFormHandler);
    server.addHandler("/api/messages", messageHandler);
    server.addHandler("/api/admin_profile", adminHandler);
    server.addHandler("/api/update_admin", adminHandler);

    // 3. Auth Routes
    server.addHandler("/supplierbuyer/login", loginHandler);
    server.addHandler("/supplierbuyer/register", registerHandler);
    server.addHandler("/supplierbuyer/logout", logoutHandler);
    server.addHandler("/reset_password", resetHandler);
    
    // 4. Static Files
    server.addHandler("/supplierbuyer/", cssHandler);
    server.addHandler("/uploads/", uploadsHandler);

    std::cout << "Server started on port 8080!\n";
    std::cout << "Access via Tor: http://bdvzrechjf2pkx6pemuwcc4htizigz3iosmu2g75ti76awgwg26nwwyd.onion/\n";

    while (true) {
        // FIXED: Replaced platform-specific sleep(1) with standard C++11
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}