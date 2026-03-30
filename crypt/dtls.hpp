#pragma once

/**
 * @file MediaDTLS.hpp
 * @brief DTLS 세션 및 컨텍스트 관리 엔진 (Security Layer)
 *
 * 이 파일은 UDP 기반의 비신뢰성 네트워크 환경에서 보안성을 확보하기 위한
 * DTLS(Datagram TLS) 엔진을 구현합니다. 핵심 디자인 패턴으로 **Memory BIO(I/O를
 * 메모리 버퍼와 분리)**를 채택하여, 네트워크 전송 로직이나 외부 프레임워크(예:
 * Boost.Asio)에 종속되지 않고 순수 데이터의 암호화/복호화만 독립적으로
 * 수행합니다.
 */

#include <cstdint>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <vector>

namespace DTLS {

/**
 * @param certfile 서버용 공개 키 인증서 파일 경로 (PEM 형식)
 * @param keyfile  서버용 비밀 키 파일 경로 (PEM 형식)
 * @param cafile   클라이언트 검증을 위한 CA 인증서 경로 (상호 인증 시 사용)
 * @return SSL_CTX* 서버용 DTLS 컨텍스트 포인터
 * @note 실패 시 nullptr을 반환하며, 내부적으로 applyCipherSuites를 호출하여
 *       현대적인 보안 수준(DTLS 1.3/1.2)을 설정함.
 */
inline SSL_CTX *ServerContext(const char *certfile = nullptr,
                              const char *keyfile = nullptr,
                              const char *cafile = nullptr);

/**
 * @param certfile 클라이언트용 공개 키 인증서 파일 경로 (선택 사항)
 * @param keyfile  클라이언트용 비밀 키 파일 경로 (선택 사항)
 * @param cafile   서버 신뢰성 검증을 위한 Root CA 경로
 * @return SSL_CTX* 클라이언트용 DTLS 컨텍스트 포인터
 * @note 실패 시 nullptr을 반환함. 세션 생성 전 한 번만 생성하여 여러 세션에서
 * 공유 가능함.
 */
inline SSL_CTX *ClientContext(const char *certfile = nullptr,
                              const char *keyfile = nullptr,
                              const char *cafile = nullptr);

/**
 * @class Session
 * @brief DTLS 세션 관리 클래스
 *
 * **Standard Usage Methodology:**
 * 1. **Initialization**: `SSL_CTX`를 사용하여 `Session` 객체를 생성합니다
 * (isServer에 따라 모드 결정).
 * 2. **Handshake**: 첫 패킷 수신 시 `Handshake()`를 호출하며, 리턴된 데이터가
 * 있다면 소켓을 통해 전송합니다. `isHandshakeDone()`이 true가 될 때까지 수신
 * 패킷을 지속적으로 `Handshake()`에 투입합니다.
 * 3. **Data Exchange**: 핸드셰이크 완료 후 `encrypt()`로 평문을 암호화하여
 * 전송하고, 수신된 패킷은 `decrypt()`를 통해 평문으로 변환합니다.
 */
class Session {
public:
  Session(SSL_CTX *ctx, bool isServer = true);
  ~Session();

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  Session(Session &&other) noexcept;
  Session &operator=(Session &&other) noexcept;

  bool isValid() const { return ssl != nullptr; }

  /**
   * @return bool 핸드셰이크(키 합의 및 인증) 완료 여부
   */
  bool isHandshakeDone() const;

  /**
   * @param incoming 상대방에게서 수신한 원본 UDP 패킷 (없으면 빈 벡터)
   * @return std::vector<uint8_t> 네트워크로 전송해야 할 DTLS 핸드셰이크 패킷
   * @note 내부 상태를 한 단계 진전시키며, 키 갱신이나 인증 과정에서 발생하는
   *       응답 패킷이 리턴됨. `isHandshakeDone()`과 함께 루프 내에서 체크해야
   * 함.
   */
  std::vector<uint8_t> Handshake(const std::vector<uint8_t> &incoming = {});

  /**
   * @return std::string 협상된 암호화 알고리즘 (예: TLS_AES_256_GCM_SHA384)
   * @note 핸드셰이크 완료 전에 호출 시 "Not Negotiated Yet"을 반환함.
   */
  std::string getNegotiatedCipher() const;

  /**
   * @param buffer  전송할 평문(Plaintext) 데이터 포인터
   * @param len     평문 데이터의 바이트 길이
   * @return std::vector<uint8_t> 네트워크로 즉시 전송 가능한 암호화된 DTLS
   * 레코드 패킷
   * @note 핸드셰이크 미완료 시 빈 벡터를 반환함. 생성된 조각은 MTU(1200)에 맞춰
   * 단편화될 수 있음.
   */
  std::vector<uint8_t> encrypt(const char *buffer, int len);

  /**
   * @param buffer  네트워크(recvfrom)를 통해 수신한 원본 DTLS 암호문 패킷
   * @param len     수신된 패킷의 길이
   * @return std::vector<uint8_t> 복호화된 평문 데이터
   * @note 핸드셰이크 패킷 투입 시 내부 상태만 변경하고 빈 결과를 반환함
   * (Side-effect: 상태 전이). 호출 후 핸드셰이크 완료 여부에 따라 로직 대응
   * 필요.
   */
  std::vector<uint8_t> decrypt(const char *buffer, int len);

private:
  /**
   * @note SSL 자원을 해제하고 BIO 핸들을 초기화함. 소멸자에서 자동 호출됨.
   */
  void cleanup();

  /**
   * @return std::vector<uint8_t> SSL 엔진의 출력 버퍼(writeBio)에 쌓인 데이터
   * @note 이 포인터의 데이터를 소켓으로 전송해야 상대방에게 DTLS 패킷이 전달됨.
   */
  std::vector<uint8_t> flushWriteBio();

  SSL *ssl = nullptr;
  BIO *readBio = nullptr;
  BIO *writeBio = nullptr;

  static constexpr int BufferSize = 4096;
};

} // namespace MediaDTLS

#include <iostream>

namespace MediaDTLS {

// ─────────────────────────────────────────────
// 공통 cipher suite (TLS 1.3 / DTLS 1.3 우선, DTLS 1.2 fallback)
// ─────────────────────────────────────────────
static inline void applyCipherSuites(SSL_CTX *ctx) {
  // DTLS 1.3 / TLS 1.3
  SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:"
                                "TLS_CHACHA20_POLY1305_SHA256:"
                                "TLS_AES_128_GCM_SHA256");

  // DTLS 1.2 fallback — ECDHE + GCM/Poly1305 만 허용
  SSL_CTX_set_cipher_list(
      ctx, "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
           "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
           "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
}

// ─────────────────────────────────────────────
// 컨텍스트 생성
// ─────────────────────────────────────────────
inline SSL_CTX *ServerContext(const char *certfile, const char *keyfile,
                              const char *cafile) {
  SSL_CTX *ctx = SSL_CTX_new(DTLS_server_method());
  if (!ctx)
    return nullptr;

  applyCipherSuites(ctx);

  if (certfile &&
      SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM) != 1) {
    std::cerr << "DTLS: 인증서 로드 실패\n";
    ERR_print_errors_fp(stderr);
  }
  if (keyfile &&
      SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM) != 1) {
    std::cerr << "DTLS: 개인키 로드 실패\n";
    ERR_print_errors_fp(stderr);
  }

  if (cafile) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       nullptr);
    SSL_CTX_load_verify_locations(ctx, cafile, nullptr);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

inline SSL_CTX *ClientContext(const char *certfile, const char *keyfile,
                              const char *cafile) {
  SSL_CTX *ctx = SSL_CTX_new(DTLS_client_method());
  if (!ctx)
    return nullptr;

  applyCipherSuites(ctx);

  if (certfile &&
      SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM) != 1) {
    std::cerr << "DTLS Client: 인증서 로드 실패\n";
    ERR_print_errors_fp(stderr);
  }
  if (keyfile &&
      SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM) != 1) {
    std::cerr << "DTLS Client: 개인키 로드 실패\n";
    ERR_print_errors_fp(stderr);
  }

  if (cafile) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_load_verify_locations(ctx, cafile, nullptr);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

// ─────────────────────────────────────────────
// Session
// ─────────────────────────────────────────────
inline Session::Session(SSL_CTX *ctx, bool isServer) {
  ssl = SSL_new(ctx);
  if (!ssl)
    return;

  readBio = BIO_new(BIO_s_mem());
  writeBio = BIO_new(BIO_s_mem());
  BIO_set_mem_eof_return(readBio, -1);
  BIO_set_mem_eof_return(writeBio, -1);
  SSL_set_bio(ssl, readBio, writeBio);

  // UDP 환경 MTU 힌트 — IP 단편화 방지
  DTLS_set_link_mtu(ssl, 1200);

  if (isServer)
    SSL_set_accept_state(ssl);
  else
    SSL_set_connect_state(ssl);
}

inline Session::~Session() { cleanup(); }

inline Session::Session(Session &&other) noexcept
    : ssl(other.ssl), readBio(other.readBio), writeBio(other.writeBio) {
  other.ssl = nullptr;
  other.readBio = other.writeBio = nullptr;
}

inline Session &Session::operator=(Session &&other) noexcept {
  if (this != &other) {
    cleanup();
    ssl = other.ssl;
    readBio = other.readBio;
    writeBio = other.writeBio;
    other.ssl = nullptr;
    other.readBio = other.writeBio = nullptr;
  }
  return *this;
}

inline void Session::cleanup() {
  if (ssl) {
    SSL_free(ssl); // SSL_free가 BIO도 함께 해제
    ssl = nullptr;
    readBio = writeBio = nullptr;
  }
}

inline bool Session::isHandshakeDone() const {
  return ssl && SSL_is_init_finished(ssl);
}

inline std::string Session::getNegotiatedCipher() const {
  if (!isHandshakeDone())
    return "Not Negotiated Yet";
  const SSL_CIPHER *c = SSL_get_current_cipher(ssl);
  return c ? SSL_CIPHER_get_name(c) : "Unknown";
}

inline std::vector<uint8_t> Session::flushWriteBio() {
  std::vector<uint8_t> out;
  int pending = BIO_pending(writeBio);
  if (pending > 0) {
    out.resize(pending);
    int n = BIO_read(writeBio, out.data(), pending);
    if (n > 0)
      out.resize(n);
    else
      out.clear();
  }
  return out;
}

inline std::vector<uint8_t>
Session::Handshake(const std::vector<uint8_t> &incoming) {
  if (!ssl)
    return {};
  if (!incoming.empty())
    BIO_write(readBio, incoming.data(), static_cast<int>(incoming.size()));
  if (!SSL_is_init_finished(ssl)) {
    int ret = SSL_do_handshake(ssl);
    if (ret <= 0) {
      int err = SSL_get_error(ssl, ret);
      if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
        std::cerr << "DTLS Handshake error: " << err << "\n";
        ERR_print_errors_fp(stderr);
      }
    }
  }
  return flushWriteBio();
}

inline std::vector<uint8_t> Session::encrypt(const char *buffer, int len) {
  if (!ssl || !SSL_is_init_finished(ssl) || !buffer || len <= 0)
    return {};
  int n = SSL_write(ssl, buffer, len);
  if (n <= 0) {
    int err = SSL_get_error(ssl, n);
    if (err != SSL_ERROR_WANT_WRITE) {
      std::cerr << "DTLS encrypt SSL_write error: " << err << "\n";
      ERR_print_errors_fp(stderr);
    }
  }
  return flushWriteBio();
}

inline std::vector<uint8_t> Session::decrypt(const char *buffer, int len) {
  std::vector<uint8_t> plainText;
  if (!ssl || !buffer || len <= 0)
    return plainText;

  BIO_write(readBio, buffer, len);

  if (!SSL_is_init_finished(ssl)) {
    SSL_do_handshake(ssl);
    if (!SSL_is_init_finished(ssl))
      return plainText; // 핸드셰이크 진행 중
  }

  uint8_t buf[BufferSize];
  while (true) {
    int n = SSL_read(ssl, buf, sizeof(buf));
    if (n > 0) {
      plainText.insert(plainText.end(), buf, buf + n);
    } else {
      int err = SSL_get_error(ssl, n);
      if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_ZERO_RETURN) {
        std::cerr << "DTLS decrypt SSL_read error: " << err << "\n";
        ERR_print_errors_fp(stderr);
      }
      break;
    }
  }
  return plainText;
}

} // namespace MediaDTLS

/**
 * @section workflow MediaDTLS Workflow Guide
 *
 * 프로젝트의 송수신 루프(Send/Recv Loop) 통합 지침:
 *
 * 1. Recv/Dispatch Loop:
 *    - UDP 소켓으로부터 패킷 수신 (recvfrom)
 *    - session.isHandshakeDone() 여부에 따라 분기
 *      a) !done: session.Handshake(packet) 호출 -> 리턴된 조각이 있다면 즉시
 * 소켓 전송 (Handshake Response) b) done: session.decrypt(packet) 호출 ->
 * 리턴된 평문 데이터를 상위 레이어로 전달
 *
 * 2. Send Loop:
 *    - 상위 레이어에서 보낼 평문 데이터 발생
 *    - session.encrypt(plain) 호출 -> 리턴된 암호화된 벡터 전체를 순회하며 소켓
 * 전송
 *
 * @note Memory BIO 방식이므로 소켓 I/O(sendto/recvfrom)는 외부(Boost.Asio
 * 등)에서 직접 관리해야 함.
 */
