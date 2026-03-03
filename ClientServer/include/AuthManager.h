#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <string>
#include <sqlite3.h>

#include "json.hpp"

using json = nlohmann::json;

class AuthManager {
public:
    AuthManager();
    ~AuthManager();

    // DB 열기 + users 테이블 생성
    bool init(const std::string& db_path);

    // 회원가입: salt 생성 → SHA-256(pw + salt) → DB 저장 (role = 'pending')
    json registerUser(const std::string& username, const std::string& email, const std::string& password);

    // 로그인: DB에서 salt 조회 → 해시 비교 → role 확인
    json loginUser(const std::string& username, const std::string& password);

    // 관리자: 승인 대기 중인 유저 목록
    json listPendingUsers();

    // 관리자: 유저 승인 (pending → user)
    json approveUser(const std::string& username);

private:
    // 16바이트 랜덤 salt 생성 (hex 문자열)
    std::string generateSalt(int length = 16);

    // SHA-256(password + salt) → hex 문자열
    std::string hashPassword(const std::string& password, const std::string& salt);

    sqlite3* db_ = nullptr;
};

#endif // AUTH_MANAGER_H
