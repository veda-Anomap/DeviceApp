#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <vector>
namespace DTLS {
inline std::vector<uint8_t> SecretCookie(32);
inline std::once_flag cookie_flag;
inline void InitCookie() {
  std::call_once(cookie_flag, []() {
    if (RAND_bytes(SecretCookie.data(), SecretCookie.size()) <= 0) {
      std::cerr << "Fail to Init Cookie" << std::endl;
    }
  });
}
int GenerateCookie(SSL *ssl, unsigned char *cookie, unsigned int *len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  struct sockaddr_storage peer;
  BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);
  HMAC(EVP_sha256(), SecretCookie.data(), SecretCookie.size(),
       (unsigned char *)&peer, sizeof(peer), result, &resultlength);
  memcpy(cookie, result, resultlength);
  *len = resultlength;
  return 1;
}
int VerifyCookie(SSL *ssl, const unsigned char *cookie, unsigned int len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  struct sockaddr_storage peer;
  BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);
  HMAC(EVP_sha256(), SecretCookie.data(), SecretCookie.size(),
       (unsigned char *)&peer, sizeof(peer), result, &resultlength);
  if (len == resultlength && memcmp(result, cookie, resultlength) == 0) {
    return 1; // 성공
  }
  return 0; // 실패
}

SSL_CTX *ServerContext(const char *certfile = nullptr,
                       const char *keyfile = nullptr,
                       const char *cafile = nullptr) {
  SSL_CTX *ctx = SSL_CTX_new(DTLS_server_method());
  if (!ctx) {
    std::cerr << "SSL_CTX_new 실패" << std::endl;
    return nullptr;
  }
  if (certfile &&
      SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM) != 1)
    std::cerr << "인증서 로드 실패" << std::endl;
  if (keyfile &&
      SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM) != 1)
    std::cerr << "개인키 로드 실패" << std::endl;
  if (certfile && keyfile && SSL_CTX_check_private_key(ctx) != 1)
    std::cerr << "키 검증 실패" << std::endl;
  if (cafile) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       nullptr);
    if (SSL_CTX_load_verify_locations(ctx, cafile, nullptr) != 1)
      std::cerr << "CA 로드 실패" << std::endl;
  }
  SSL_CTX_set_cookie_generate_cb(ctx, GenerateCookie);
  SSL_CTX_set_cookie_verify_cb(ctx, VerifyCookie);
  SSL_CTX_set_options(ctx, SSL_OP_COOKIE_EXCHANGE);
  return ctx;
}

SSL_CTX *ClientContext(const char *certfile = nullptr,
                       const char *keyfile = nullptr,
                       const char *cafile = nullptr) {
  SSL_CTX *ctx = SSL_CTX_new(DTLS_client_method());
  if (!ctx)
    return nullptr;
  if (certfile)
    SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM);
  if (keyfile)
    SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM);
  if (certfile && keyfile)
    SSL_CTX_check_private_key(ctx);
  if (cafile) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_load_verify_locations(ctx, cafile, nullptr);
  } else {
    // 인증서 없이 테스트할 때
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

std::string getCID(char *data, size_t len) {
  constexpr size_t CID_len = 8;
  if (len < CID_len)
    return "";
  return std::string(data, CID_len);
}

class Session {
  SSL *ssl;
  BIO *readBio, *writeBio;

public:
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  Session(SSL_CTX *ctx, bool isServer = true)
      : ssl(nullptr), readBio(nullptr), writeBio(nullptr) {
    ssl = SSL_new(ctx);
    if (!ssl) {
      std::cerr << "SSL_new Fail" << std::endl;
    }
    if (isServer)
      SSL_set_accept_state(ssl);
    else
      SSL_set_connect_state(ssl);
    readBio = BIO_new(BIO_s_mem());
    writeBio = BIO_new(BIO_s_mem());
    if (!readBio || !writeBio) {
      std::cerr << "Bio new fail" << std::endl;
    }
    SSL_set_bio(ssl, readBio, writeBio);
    SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);
  }
  ~Session() { cleanup(); }
  void cleanup() {
    if (ssl) {
      SSL_free(ssl);
      ssl = nullptr;
      readBio = writeBio = nullptr;
    }
  }
  std::vector<uint8_t> decrypt(char *buffer, size_t size) {
    std::vector<uint8_t> plainText;
    plainText.reserve(size);
    int written = BIO_write(readBio, buffer, size);
    if (written <= 0) {
      std::cerr << "decrypt Bio_write Fail" << std::endl;
    }
    if (!SSL_is_init_finished(ssl)) {
      int res = SSL_do_handshake(ssl);
      if (res <= 0) {
        int err = SSL_get_error(ssl, res);
        // WANT_READ/WRITE는 에러가 아니라 "데이터가 더 필요함" 혹은 "보낼
        // 데이터가 있음" 상태
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
          // 정상적인 대기 상태.
          // 주의: 이때 writeBio에 핸드셰이크 응답(ServerHello 등)이 쌓여있을 수
          // 있으므로 상위 로직에서 이를 확인하여 전송해야 함.
          return plainText;
        } else {
          // 진짜 에러 발생 (인증서 오류, 프로토콜 위반 등)
          // 로그 출력 후 세션 종료 처리가 필요함
          // ERR_print_errors_fp(stderr);
          return plainText;
        }
      }
    }
    uint8_t buf[4096];
    while (true) {
      int read = SSL_read(ssl, buf, sizeof(buf)); /// 이게 복호화를 실행함
      if (read > 0) {
        plainText.insert(plainText.end(), buf, buf + read);
        if (SSL_pending(ssl) == 0) /// 더이상 읽을 데이터가 없음
          break;
      } else // 읽기 실패 혹은 WANT_READ 데이터 부족
        break;
    }
    return plainText;
  }
  std::vector<uint8_t> encrypt(char *buffer, size_t size) { /// 암호화
    std::vector<uint8_t> cipherText;
    if (!SSL_is_init_finished(ssl)) {
      std::cerr << "암호화를 복호화 전에 시도함" << std::endl;
      return cipherText;
    }
    int written = SSL_write(ssl, buffer, size);
    if (written <= 0) {
      return cipherText;
    }
    int pending = BIO_pending(writeBio);
    if (pending > 0) {
      cipherText.resize(pending);
      int read = BIO_read(writeBio, cipherText.data(), pending);
      if (read > 0) {
        cipherText.resize(read);
      } else {
        cipherText.clear();
      }
    }
    return cipherText;
  }
  std::vector<uint8_t> Handshake() {
    std::vector<uint8_t> response;
    int pending = BIO_pending(writeBio);
    response.resize(pending);
    if (pending > 0) {
      int read = BIO_read(writeBio, response.data(), pending);
      if (read > 0) {
        response.resize(read);
      } else {
        response.clear();
      }
    }
    return response;
  }
};
}
