#pragma once
#include <iostream>
#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <vector>
namespace tls {

inline SSL_CTX *ServerContext(const char *certfile = nullptr,
                              const char *keyfile = nullptr,
                              const char *cafile = nullptr) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
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
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

inline SSL_CTX *ClientContext(const char *certfile = nullptr,
                              const char *keyfile = nullptr,
                              const char *cafile = nullptr) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
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
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

class Session {
  SSL *ssl;
  BIO *readBio, *writeBio;

public:
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  ~Session() { cleanup(); }
  Session(SSL_CTX *ctx, bool isServer = true)
      : ssl(nullptr), readBio(nullptr), writeBio(nullptr) {
    ssl = SSL_new(ctx);
    if (!ssl) {
      std::cerr << "SSL_new 실패 " << std::endl;
      return;
    }
    if (isServer) {
      SSL_set_accept_state(ssl);
    } else {
      SSL_set_connect_state(ssl);
    }
    readBio = BIO_new(BIO_s_mem());
    writeBio = BIO_new(BIO_s_mem());
    if (!readBio || !writeBio) {
      std::cerr << "Bio new fail" << std::endl;
    }
    SSL_set_bio(ssl, readBio, writeBio);
  }
  void cleanup() {
    if (ssl) {
      SSL_free(ssl);
      ssl = nullptr;
      readBio = writeBio = nullptr;
    }
  }
  std::vector<uint8_t> encrypt(const char *buffer, int len) {
    std::vector<uint8_t> cipherText;
    if (!SSL_is_init_finished(ssl)) {
      std::cerr << "암호화를 복호화 전에 시도함" << std::endl;
      return cipherText;
    }
    int written = SSL_write(ssl, buffer, len);
    if (written < 0) {
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
  std::vector<uint8_t> decrypt(char *buffer, int len) {
    std::vector<uint8_t> plainText;
    plainText.reserve(len);
    int written = BIO_write(readBio, buffer, len);
    if (written <= 0) {
      std::cerr << "decrypt BIO_write Fail" << std::endl;
    }
    if (!SSL_is_init_finished(ssl)) {
      int res = SSL_do_handshake(ssl);
      if (res <= 0) {
        int err = SSL_get_error(ssl, res);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
          std::cerr << "handshake Error : " << err << std::endl;
        }
      }
      return plainText;
    }
    uint8_t buf[4096];
    while (true) {
      int read = SSL_read(ssl, buf, sizeof(buf));
      if (read > 0) {
        plainText.insert(plainText.end(), buf, buf + read);
        if (SSL_pending(ssl) == 0) {
          break;
        } else
          break;
      }
    }
    return plainText;
  }
  std::vector<uint8_t> Handshake() {
    std::vector<uint8_t> response;
    int pending = BIO_pending(writeBio);
    if (pending > 0) {
      response.resize(pending);
      int read = BIO_read(writeBio, response.data(), pending);
      if (read > 0)
        response.resize(read);
      else
        response.clear();
    }
    return response;
  }
};
} // namespace tls
