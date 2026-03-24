#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <vector>

/**
 * @file dtls.hpp
 * @brief OpenSSL 기반 메모리 BIO를 활용한 DTLS (Datagram TLS) 세션 제어 모듈
 *
 * UDP 통신에서 사용할 수 있는 비연결형 DTLS 암호화 모듈입니다.
 * DTLS의 특성 상 세션 탈취 방지를 위한 Cookie 생성/검증과 `peer` (상대방 고유
 * 식별자, 예: IP:Port 문자열) 바인딩 로직이 포함되어 있습니다. 네트워크 I/O는
 * 사용자가 별도로 관리해야 합니다.
 */
namespace DTLS {

constexpr int BufferSize = 4096;
inline std::vector<uint8_t> SecretCookie(32);
inline std::once_flag cookie_flag;

/**
 * @note RAND_bytes를 이용해 32바이트 보안 쿠키 시드를 1회 초기화함
 */
inline void InitCookie() {
  std::call_once(cookie_flag, []() {
    if (RAND_bytes(SecretCookie.data(), SecretCookie.size()) <= 0) {
      std::cerr << "Fail to Init Cookie" << std::endl;
    }
  });
}

/**
 * @param peer 상대방 식별 데이터 (IP 등)
 * @param cookie 출력될 쿠키 버퍼
 * @param len 출력된 쿠키 길이
 * @return int 성공 시 1
 * @note HMAC-SHA256을 사용하여 통신 상대별 고유 쿠키 생성
 */
static int GenerateCookieWithPeer(const std::vector<uint8_t> &peer,
                                  unsigned char *cookie, unsigned int *len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  HMAC(EVP_sha256(), SecretCookie.data(), SecretCookie.size(), peer.data(),
       static_cast<int>(peer.size()), result, &resultlength);
  memcpy(cookie, result, resultlength);
  *len = resultlength;
  return 1;
}

/**
 * @param peer 상대방 식별 데이터
 * @param cookie 검증할 쿠키
 * @param len 쿠키 길이
 * @return int 일치 시 1, 불일치 시 0
 * @note 수신된 쿠키가 현재 시드와 Peer 정보를 조합한 결과와 맞는지 확인
 */
static int VerifyCookieWithPeer(const std::vector<uint8_t> &peer,
                                const unsigned char *cookie, unsigned int len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  HMAC(EVP_sha256(), SecretCookie.data(), SecretCookie.size(), peer.data(),
       static_cast<int>(peer.size()), result, &resultlength);
  if (len == resultlength && memcmp(result, cookie, resultlength) == 0)
    return 1;
  return 0;
}

/**
 * @param ssl SSL 객체
 * @param cookie 출력 쿠키 버퍼
 * @param len 출력 길이
 * @return int 성공 시 1
 * @note SSL_CTX 콜백용 (내부적으로 빈 Peer 정보를 사용함)
 */
int GenerateCookie(SSL *ssl, unsigned char *cookie, unsigned int *len) {
  std::vector<uint8_t> emptyPeer;
  return GenerateCookieWithPeer(emptyPeer, cookie, len);
}

/**
 * @param ssl SSL 객체
 * @param cookie 검증할 쿠키
 * @param len 쿠키 길이
 * @return int 일치 시 1
 * @note SSL_CTX 콜백용
 */
int VerifyCookie(SSL *ssl, const unsigned char *cookie, unsigned int len) {
  std::vector<uint8_t> emptyPeer;
  return VerifyCookieWithPeer(emptyPeer, cookie, len);
}

/**
 * @param certfile 서버 인증서 경로 (PEM)
 * @param keyfile 서버 개인키 경로 (PEM)
 * @param cafile mTLS 수행용 CA 인증서 경로
 * @return SSL_CTX* 생성된 DTLS 서버 컨텍스트
 * @note Cookie 생성을 위한 콜백과 SSL_OP_COOKIE_EXCHANGE 옵션이 자동 설정됨.
 * 반환된 포인터는 SSL_CTX_free()로 해제 필요.
 */
inline SSL_CTX *ServerContext(const char *certfile = nullptr,
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
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  SSL_CTX_set_cookie_generate_cb(ctx, GenerateCookie);
  SSL_CTX_set_cookie_verify_cb(ctx, VerifyCookie);
  SSL_CTX_set_options(ctx, SSL_OP_COOKIE_EXCHANGE);
  return ctx;
}

/**
 * @param certfile 클라이언트 인증서 경로 (PEM)
 * @param keyfile 클라이언트 개인키 경로 (PEM)
 * @param cafile 서버 검증용 CA 경로
 * @return SSL_CTX* 생성된 DTLS 클라이언트 컨텍스트
 * @note DTLS_client_method()를 사용함. SSL_CTX_free()로 해제 필요.
 */
inline SSL_CTX *ClientContext(const char *certfile = nullptr,
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
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  return ctx;
}

/**
 * @param data 수신된 패킷 포인터
 * @param len 데이터 길이
 * @return std::string 추출된 8바이트 CID (실패 시 빈 문자열)
 * @note DTLS 연결 식별을 위한 고정 길이 ID 추출 헬퍼
 */
inline std::string getCID(const char *data, size_t len) {
  constexpr size_t CID_len = 8;
  if (len < CID_len)
    return "";
  return std::string(data, CID_len);
}

/**
 * @brief 단일 DTLS 세션을 관리하는 클래스
 *
 * **표준 사용 방법론:**
 * 1. **초기화**: `Session session(ctx, isServer, peerIdent)`
 *    - UDP는 연결 기반이 아니므로 `peer` (상대 식별자) 지정 권장
 * 2. **Client 최초 동작**: 즉시 `Handshake()` 호출해 반환된 `ClientHello`를
 * 서버(UDP EndPoint)로 전송
 * 3. **데이터 수신 및 핸드셰이크 진행**:
 *    - 데이터 수신 시 `auto plainText = session.decrypt(data, dataLen);`
 *    - `auto hData = session.getHandshakeData();` 후 반환값이 있으면 전송
 * (Server의 HelloVerifyRequest 응답 등)
 *    - `isHandshakeDone()`이 참이면 `plainText`부터 진짜 UDP 애플리케이션
 * 데이터
 * 4. **애플리케이션 데이터 송신**: `auto out = session.encrypt(appData,
 * appLen);` 후 네트워크 송신
 */
class Session {
  SSL *ssl;
  BIO *readBio, *writeBio;
  std::vector<uint8_t> peer;

  void cleanup() {
    if (ssl) {
      SSL_free(ssl); // SSL_free가 BIO도 해제
      ssl = nullptr;
      readBio = writeBio = nullptr;
    }
  }

  /**
   * @return std::vector<uint8_t> writeBio 내에 쌓인 암호화 패킷
   * @note 내부 flush 용도. UDP 전송을 위해 한 번에 한 패킷씩 읽어가는 구조 권장
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

  Session(Session &&other) noexcept
      : ssl(other.ssl), readBio(other.readBio), writeBio(other.writeBio),
        peer(std::move(other.peer)) {
    other.ssl = nullptr;
    other.readBio = other.writeBio = nullptr;
  }

  Session &operator=(Session &&other) noexcept {
    if (this != &other) {
      cleanup();
      ssl = other.ssl;
      readBio = other.readBio;
      writeBio = other.writeBio;
      peer = std::move(other.peer);
      other.ssl = nullptr;
      other.readBio = other.writeBio = nullptr;
    }
    return *this;
  }

  /**
   * @param ctx SSL_CTX 포인터
   * @param isServer 서버 여부
   * @param peerIdent 초기 Peer 식별 데이터
   * @note DTLS 특성에 맞춰 BIO_set_mem_eof_return(-1)이 설정됨
   */
  Session(SSL_CTX *ctx, bool isServer = true,
          std::vector<uint8_t> peerIdent = {})
      : ssl(nullptr), readBio(nullptr), writeBio(nullptr),
        peer(std::move(peerIdent)) {
    if (!ctx) {
      std::cerr << "Session: ctx가 nullptr" << std::endl;
      return;
    }
    ssl = SSL_new(ctx);
    if (!ssl) {
      std::cerr << "SSL_new 실패" << std::endl;
      return;
    }
    if (isServer)
      SSL_set_accept_state(ssl);
    else
      SSL_set_connect_state(ssl);

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
  }

  ~Session() { cleanup(); }

  /**
   * @return bool SSL 객체 유효 여부
   */
  bool isValid() const { return ssl != nullptr; }

  /**
   * @return bool 핸드셰이크 완료 여부
   */
  bool isHandshakeDone() const { return ssl && SSL_is_init_finished(ssl); }

  /**
   * @param peerIdent 새로운 Peer 식별 정보 (IP 등)
   * @note Cookie 생성 및 검증 시 재료로 사용됨
   */
  void setPeer(std::vector<uint8_t> peerIdent) { peer = std::move(peerIdent); }

  /**
   * @return std::vector<uint8_t> 생성된 핸드셰이크 패킷
   * @note 클라이언트의 경우 최초 1회 호출하여 Hello 패킷을 전송해야 함
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
   * @param buffer 수신된 DTLS 패킷 버퍼
   * @param size 패킷 길이
   * @return std::vector<uint8_t> 복호화된 평문 데이터
   * @note 핸드셰이크 중에는 빈 결과를 반환하며 내부 상태를 전진시킴
   */
  std::vector<uint8_t> decrypt(const char *buffer, size_t size) {
    std::vector<uint8_t> plainText;
    if (!ssl)
      return plainText;

    int written = BIO_write(readBio, buffer, static_cast<int>(size));
    if (written <= 0) {
      std::cerr << "decrypt BIO_write 실패" << std::endl;
      return plainText;
    }

    // 핸드셰이크 진행 중이면 처리 후 완료 여부 확인
    if (!SSL_is_init_finished(ssl)) {
      int res = SSL_do_handshake(ssl);
      if (res <= 0) {
        int err = SSL_get_error(ssl, res);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
          std::cerr << "Handshake error: " << err << std::endl;
      }
      // 아직 진행 중이면 데이터 없음 — 완료됐으면 SSL_read로 이어짐
      if (!SSL_is_init_finished(ssl))
        return plainText;
    }

    // 핸드셰이크 완료 — 복호화된 데이터 읽기
    uint8_t buf[BufferSize];
    while (true) {
      int read = SSL_read(ssl, buf, sizeof(buf));
      if (read > 0) {
        plainText.insert(plainText.end(), buf, buf + read);
      } else {
        int err = SSL_get_error(ssl, read);
        if (err == SSL_ERROR_ZERO_RETURN)
          break; // 정상 종료
        if (err != SSL_ERROR_WANT_READ)
          std::cerr << "SSL_read error: " << err << std::endl;
        break;
      }
    }
    return plainText;
  }

  /**
   * @param buffer 송신할 평문 데이터
   * @param size 평문 길이
   * @return std::vector<uint8_t> 네트워크(UDP)로 전송할 DTLS 레코드
   * @note 핸드셰이크 완료 전에 호출 시 빈 결과 반환
   */
  std::vector<uint8_t> encrypt(const char *buffer, size_t size) {
    std::vector<uint8_t> cipherText;
    if (!ssl)
      return cipherText;
    if (!SSL_is_init_finished(ssl)) {
      std::cerr << "핸드셰이크 완료 전 암호화 시도" << std::endl;
      return cipherText;
    }
    int written = SSL_write(ssl, buffer, static_cast<int>(size));
    if (written <= 0) {
      int err = SSL_get_error(ssl, written);
      std::cerr << "SSL_write error: " << err << std::endl;
      return cipherText;
    }
    return flushWriteBio();
  }

  /**
   * @return std::vector<uint8_t> 전송 대기 중인 핸드셰이크 응답 패킷
   * @note 서버 모드에서 decrypt() 호출 후 매번 확인하여 응답 송신 권장
   */
  std::vector<uint8_t> getHandshakeData() {
    if (!ssl)
      return {};
    return flushWriteBio();
  }
};

} // namespace DTLS

/**
 * [DTLS Workflow Guide]
 * 1. 초기화: DTLS::InitCookie()를 앱 시작 시 1회 호출
 * 2. 컨텍스트: DTLS::ServerContext() 또는 DTLS::ClientContext()로 SSL_CTX 생성
 * 3. 세션: 통신 대상별로 DTLS::Session(ctx, isServer, peerIdent) 인스턴스화
 * 4. 핸드셰이크 개시:
 *    - Client: Handshake()를 즉시 호출하여 생성된 패킷을 UDP로 전송
 *    - Server: 최초 패킷 수신 시 decrypt()에 투입
 * 5. 패킷 교환 루프:
 *    - 수신 시마다 decrypt(data)를 호출하고, getHandshakeData()를 확인하여 응답
 * 패킷이 있으면 UDP 전송
 *    - isHandshakeDone() == true 이후부터 encrypt()를 통한 보호된 데이터 전송
 * 가능
 * 6. 주의: UDP의 비신뢰성으로 인해 패킷 손실 시 핸드셰이크 재시작 또는 재전송
 * 설계가 필요할 수 있음
 */