#pragma once
#include <iostream>
#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <vector>

/**
 * @file tls.hpp
 * @brief OpenSSL 기반 메모리 BIO(Memory BIO)를 활용한 비동기 TLS 세션 제어 모듈
 *
 * 외부의 TCP 네트워크 통신(Asio 등) 계층과 결합하여 사용할 수 있도록, 네트워크
 * I/O와 실제 암복호화/핸드셰이크 처리 로직을 완전히 분리한 구조입니다. 소켓
 * I/O는 사용자가 직접 수행하며, 수신된 데이터는 `decrypt()`로 넣고 송신할
 * 데이터는 `encrypt()` 혹은 `Handshake()`/`getHandshakeData()`를 통해
 * 추출합니다.
 */
namespace TLS {
constexpr int BufferSize = 4096;

/**
 * @param certfile 서버 인증서 경로 (PEM)
 * @param keyfile 서버 개인키 경로 (PEM)
 * @param cafile mTLS(Peer 검증)를 수행하기 위한 CA 인증서 경로
 * @return SSL_CTX* 할당된 서버 컨텍스트
 * @note 애플리케이션 종료 시 SSL_CTX_free()로 직접 해제 필요
 */
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

/**
 * @param certfile 클라이언트 인증서 경로 (mTLS 필요 시)
 * @param keyfile 클라이언트 개인키 경로
 * @param cafile 서버 인증서 검증용 CA 경로
 * @return SSL_CTX* 할당된 클라이언트 컨텍스트
 * @note SSL_CTX_free()로 해제 책임 호출자에게 있음
 */
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

/**
 * @brief 단일 TLS 세션의 핸드셰이크 및 데이터 암/복호화를 담당하는 클래스
 *
 * **표준 사용 방법론:**
 * 1. **초기화**: `Session session(ctx, isServer)`
 * 2. **Client 최초 동작**: 클라이언트의 경우 즉시 `Handshake()`를 호출해 반환된
 * `ClientHello` 바이트 배열을 네트워크로 전송
 * 3. **데이터 수신 루프 (핸드셰이크 & 페이로드 모두 포함)**:
 *    - 네트워크에서 데이터 수신 시 `plainText = session.decrypt(data, len);`
 * 호출
 *    - `auto out = session.getHandshakeData();` 호출 후 반환값이 비어있지
 * 않다면 즉시 네트워크로 전송 (핸드셰이크 응답 등)
 *    - 본 루틴을 `isHandshakeDone() == true`가 될 때까지 지속하며, 완료 후엔
 * `plainText`가 실제 수신 애플리케이션 데이터
 * 4. **애플리케이션 데이터 송신**:
 *    - `auto out = session.encrypt(appData, appLen);`
 *    - 반환된 `out`을 네트워크로 전송
 */
class Session {
  SSL *ssl;
  BIO *readBio, *writeBio;

  void cleanup() {
    if (ssl) {
      SSL_free(ssl);
      // SSL_free가 BIO도 해제하므로 별도 해제 불필요
      ssl = nullptr;
      readBio = writeBio = nullptr;
    }
  }

  /**
   * @return std::vector<uint8_t> writeBio에 보관 중인 암호화 데이터
   * @note 내부 flush 용도로 사용되며, 추출 후 버퍼는 비워짐
   */
  std::vector<uint8_t> flushWriteBio() {
    std::vector<uint8_t> out;
    int pending = BIO_pending(writeBio);
    if (pending > 0) {
      out.resize(pending);
      int read = BIO_read(writeBio, out.data(), pending);
      if (read > 0)
        out.resize(read);
      else
        out.clear();
    }
    return out;
  }

public:
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  ~Session() { cleanup(); }

  /**
   * @param ctx 초기화된 SSL_CTX 포인터
   * @param isServer 서버 모드 여부 (true: Accept, false: Connect)
   * @note 내부적으로 메모리 BIO를 생성하고 EOF 반환값을 -1로 설정함
   */
  Session(SSL_CTX *ctx, bool isServer = true)
      : ssl(nullptr), readBio(nullptr), writeBio(nullptr) {
    if (!ctx) {
      std::cerr << "Session: ctx가 nullptr" << std::endl;
      return;
    }
    ssl = SSL_new(ctx);
    if (!ssl) {
      std::cerr << "SSL_new 실패" << std::endl;
      return;
    }
    readBio = BIO_new(BIO_s_mem());
    writeBio = BIO_new(BIO_s_mem());
    if (!readBio || !writeBio) {
      std::cerr << "BIO_new 실패" << std::endl;
      if (readBio)
        BIO_free(readBio);
      if (writeBio)
        BIO_free(writeBio);
      SSL_free(ssl);
      ssl = nullptr;
      readBio = writeBio = nullptr;
      return;
    }
    BIO_set_mem_eof_return(readBio, -1);
    BIO_set_mem_eof_return(writeBio, -1);
    SSL_set_bio(ssl, readBio, writeBio);

    if (isServer)
      SSL_set_accept_state(ssl);
    else
      SSL_set_connect_state(ssl);
  }

  Session(Session &&other) noexcept
      : ssl(other.ssl), readBio(other.readBio), writeBio(other.writeBio) {
    other.ssl = nullptr;
    other.readBio = other.writeBio = nullptr;
  }

  Session &operator=(Session &&other) noexcept {
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

  /**
   * @return bool SSL 객체 유효 여부
   * @note nullptr 체크를 통해 세션 정상 생성 여부 판단
   */
  bool isValid() const { return ssl != nullptr; }

  /**
   * @return bool 핸드셰이크 프로세스 완료 여부
   * @note true일 경우에만 실제 애플리케이션 데이터 암복호화 가능
   */
  bool isHandshakeDone() const { return ssl && SSL_is_init_finished(ssl); }

  /**
   * @return std::vector<uint8_t> 생성된 핸드셰이크 패킷 (ClientHello 등)
   * @note 클라이언트는 세션 생성 직후 이 함수를 호출해 첫 패킷을 전송해야 함
   */
  std::vector<uint8_t> Handshake() {
    if (!ssl)
      return {};

    if (!SSL_is_init_finished(ssl)) {
      int res = SSL_do_handshake(ssl);
      if (res <= 0) {
        int err = SSL_get_error(ssl, res);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
          std::cerr << "Handshake error: " << err << std::endl;
      }
    }
    return flushWriteBio();
  }

  /**
   * @param buffer 수신된 암호화 데이터 버퍼
   * @param len 데이터 길이
   * @return std::vector<uint8_t> 복호화된 평문 데이터
   * @note 핸드셰이크 중에는 빈 결과를 반환하며 내부 상태만 갱신함
   */
  std::vector<uint8_t> decrypt(const char *buffer, int len) {
    std::vector<uint8_t> plainText;
    if (!ssl)
      return plainText;

    int written = BIO_write(readBio, buffer, len);
    if (written <= 0) {
      std::cerr << "decrypt BIO_write 실패" << std::endl;
      return plainText;
    }

    // 핸드셰이크가 아직 진행 중이면 진행
    if (!SSL_is_init_finished(ssl)) {
      int res = SSL_do_handshake(ssl);
      if (res <= 0) {
        int err = SSL_get_error(ssl, res);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
          std::cerr << "handshake error: " << err << std::endl;
      }
      // 핸드셰이크가 이번 호출로 완료됐더라도 아래 SSL_read로 이어짐
      if (!SSL_is_init_finished(ssl))
        return plainText; // 아직 진행 중이면 데이터 없음
    }

    // 핸드셰이크 완료 — 복호화된 데이터 읽기
    uint8_t buf[BufferSize];
    while (true) {
      int read = SSL_read(ssl, buf, sizeof(buf));
      if (read > 0) {
        plainText.insert(plainText.end(), buf, buf + read);
      } else {
        int err = SSL_get_error(ssl, read);
        if (err == SSL_ERROR_ZERO_RETURN) {
          // 상대방이 정상적으로 연결 종료
          break;
        }
        if (err != SSL_ERROR_WANT_READ)
          std::cerr << "SSL_read error: " << err << std::endl;
        break;
      }
    }
    return plainText;
  }

  /**
   * @param buffer 송신할 평문 데이터 버퍼
   * @param len 평문 길이
   * @return std::vector<uint8_t> 네트워크로 전송할 암호화된 레코드
   * @note 핸드셰이크 완료 전에 호출 시 에러 로그 출력 및 빈 결과 반환
   */
  std::vector<uint8_t> encrypt(const char *buffer, int len) {
    std::vector<uint8_t> cipherText;
    if (!ssl)
      return cipherText;
    if (!SSL_is_init_finished(ssl)) {
      std::cerr << "핸드셰이크 완료 전 암호화 시도" << std::endl;
      return cipherText;
    }
    int written = SSL_write(ssl, buffer, len);
    if (written <= 0) {
      int err = SSL_get_error(ssl, written);
      std::cerr << "SSL_write error: " << err << std::endl;
      return cipherText;
    }
    return flushWriteBio();
  }

  /**
   * @return std::vector<uint8_t> 전송 대기 중인 핸드셰이크 응답 데이터
   * @note 서버 모드에서 decrypt() 호출 후 응답을 추출할 때 주로 사용
   */
  std::vector<uint8_t> getHandshakeData() {
    if (!ssl)
      return {};
    return flushWriteBio();
  }
};
} // namespace TLS

/**
 * [TLS Workflow Guide]
 * 1. 환경 설정: TLS::ServerContext() 또는 TLS::ClientContext() 호출하여 SSL_CTX
 * 생성
 * 2. 세션 생성: 네트워킹(Accept/Connect) 시점에 TLS::Session(ctx, isServer)
 * 인스턴스화
 * 3. 초기 핸드셰이크:
 *    - Client: Handshake()를 즉시 호출하여 생성된 ClientHello 패킷을 서버로
 * 전송
 *    - Server: 최초의 데이터(ClientHello) 수신 대기
 * 4. 데이터 교환 루프:
 *    - 수신: decrypt(recvData) 호출. 반환된 평문이 있으면 처리.
 *           동시에 getHandshakeData()를 확인하여 반환값이 있으면 즉시 네트워크
 * 전송
 *    - 송신: encrypt(plainData) 호출하여 반환된 암호화 레코드를 네트워크 전송
 * 5. 완료 판정: isHandshakeDone()이 true가 된 이후부터 실제 애플리케이션 데이터
 * 통신 가능
 */