#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

#include <openssl/sha.h>

#include "AuthManager.h"

// ======================== 생성자 / 소멸자 ========================

AuthManager::AuthManager() {}

AuthManager::~AuthManager() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ======================== 초기화 ========================

bool AuthManager::init(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[Auth] DB open failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // users 테이블 생성 (없으면)
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS users ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username  TEXT    UNIQUE NOT NULL,"
        "  email     TEXT    UNIQUE NOT NULL,"
        "  password  TEXT    NOT NULL,"
        "  salt      TEXT    NOT NULL,"
        "  role      TEXT    DEFAULT 'pending',"
        "  created   DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[Auth] Table creation failed: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    std::cout << "[Auth] Database initialized: " << db_path << std::endl;

    // 테스트용 초기 데이터 삽입 (DB가 비어있을 때만)
    seedTestData();

    return true;
}

// ======================== 회원가입 ========================

json AuthManager::registerUser(const std::string& username, const std::string& email, const std::string& password) {
    // 입력 검증
    if (username.empty() || email.empty() || password.empty()) {
        return {{"success", false}, {"error", "모든 필드를 입력해주세요."}};
    }

    // salt 생성 + 비밀번호 해싱
    std::string salt = generateSalt();
    std::string hashed = hashPassword(password, salt);

    // DB 저장
    const char* sql = "INSERT INTO users (username, email, password, salt) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {{"success", false}, {"error", "DB 오류"}};
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, hashed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, salt.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        // UNIQUE 제약 위반 체크
        if (err.find("UNIQUE") != std::string::npos) {
            return {{"success", false}, {"error", "이미 사용 중인 아이디 또는 이메일입니다."}};
        }
        return {{"success", false}, {"error", "회원가입 실패"}};
    }

    std::cout << "[Auth] User registered: " << username << " (pending)" << std::endl;
    return {{"success", true}, {"message", "회원가입 완료. 관리자 승인을 기다려주세요."}};
}

// ======================== 로그인 ========================

json AuthManager::loginUser(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        return {{"success", false}, {"error", "아이디와 비밀번호를 입력해주세요."}};
    }

    // DB에서 유저 조회
    const char* sql = "SELECT password, salt, role FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {{"success", false}, {"error", "DB 오류"}};
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return {{"success", false}, {"error", "존재하지 않는 아이디입니다."}};
    }

    std::string stored_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    sqlite3_finalize(stmt);

    // 비밀번호 검증
    std::string input_hash = hashPassword(password, salt);
    if (input_hash != stored_hash) {
        return {{"success", false}, {"error", "비밀번호가 일치하지 않습니다."}};
    }

    // 역할 확인
    if (role == "pending") {
        return {{"success", false}, {"error", "관리자 승인 대기 중입니다."}};
    }

    std::cout << "[Auth] Login success: " << username << " (role: " << role << ")" << std::endl;
    return {{"success", true}, {"state", role}, {"username", username}};
}

// ======================== 관리자 기능 ========================

json AuthManager::listPendingUsers() {
    const char* sql = "SELECT username, email, created FROM users WHERE role = 'pending';";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {{"success", false}, {"error", "DB 오류"}};
    }

    json users = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json user;
        user["username"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        user["email"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user["created"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        users.push_back(user);
    }
    sqlite3_finalize(stmt);

    return {{"success", true}, {"users", users}};
}

json AuthManager::approveUser(const std::string& username) {
    const char* sql = "UPDATE users SET role = 'user' WHERE username = ? AND role = 'pending';";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {{"success", false}, {"error", "DB 오류"}};
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return {{"success", false}, {"error", "승인 실패"}};
    }

    int changes = sqlite3_changes(db_);
    if (changes == 0) {
        return {{"success", false}, {"error", "해당 유저가 없거나 이미 승인되었습니다."}};
    }

    std::cout << "[Auth] User approved: " << username << std::endl;
    return {{"success", true}, {"message", username + " 승인 완료"}};
}

// ======================== 헬퍼 함수 ========================

std::string AuthManager::generateSalt(int length) {
    // /dev/urandom에서 랜덤 바이트 읽기
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    std::vector<unsigned char> buf(length);
    urandom.read(reinterpret_cast<char*>(buf.data()), length);

    // hex 문자열로 변환
    std::ostringstream oss;
    for (unsigned char c : buf) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return oss.str();
}

std::string AuthManager::hashPassword(const std::string& password, const std::string& salt) {
    std::string combined = password + salt;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);

    // hex 문자열로 변환
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ======================== 테스트 데이터 ========================

void AuthManager::seedTestData() {
    // DB에 유저가 이미 있으면 스킵
    const char* count_sql = "SELECT COUNT(*) FROM users;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (count > 0) return; // 이미 데이터가 있으면 삽입하지 않음

    std::cout << "[Auth] Seeding test data..." << std::endl;

    // role을 직접 지정해서 삽입하는 헬퍼 람다
    auto insertUser = [this](const std::string& username, const std::string& email,
                             const std::string& password, const std::string& role) {
        std::string salt = generateSalt();
        std::string hashed = hashPassword(password, salt);

        const char* sql = "INSERT INTO users (username, email, password, salt, role) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_, sql, -1, &s, nullptr);
        sqlite3_bind_text(s, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, hashed.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 4, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 5, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_finalize(s);
    };

    // admin 계정
    insertUser("admin_alpha", "admin_alpha@google.com", "pass_admin_alpha", "admin");

    // user 계정 (승인 완료)
    insertUser("user_alpha", "user_alpha@alpha.com", "pass_user_alpha", "user");
    insertUser("user_beta",  "user_beta@google.com",  "pass_user_beta",  "user");
    insertUser("user_gamma", "user_gamma@google.com", "pass_user_gamma", "user");

    // pending 계정 (승인 대기)
    insertUser("wait_alpha", "wait_alpha@google.com", "pass_wait_alpha", "pending");
    insertUser("wait_beta",  "wait_beta@google.com",  "pass_wait_beta",  "pending");
    insertUser("wait_gamma", "wait_gamma@google.com", "pass_wait_gamma", "pending");

    std::cout << "[Auth] Test data seeded: 1 admin, 3 users, 3 pending" << std::endl;
}
