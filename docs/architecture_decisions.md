# DeviceApp 설계 결정 기록 (Architecture Decision Records)

프로젝트 개발 과정에서 내린 주요 설계 결정과 그 배경을 기록합니다.  
작성 기준: 2026-03-19

---

## GStreamer RTSP 릴레이 파이프라인 개선 (2026-03-19)

**상태**: 채택됨 · **관련 이슈**: AN-105

- **문제**: UDP 수신 버퍼 부족으로 I-Frame burst 시 패킷 유실 → 화면 깨짐. Caps negotiation 지연으로 최초 접속 시 수초 블랙스크린. 수신/송신 스레드 미분리로 역류 압력 발생. 중간 접속 시 SPS/PPS 누락으로 디코딩 실패.
- **결정**: 파이프라인에 `buffer-size=2097152`(2MB), `queue` 2개(3개 스레드 독립 구동, 200buf/10MB 상한), 명시적 caps, `h264parse config-interval=-1`(모든 키프레임 앞 SPS/PPS 삽입) 추가
- **기각**: `udpsrc timeout=5s`는 Sub-Pi 영상 미수신 시 EOS를 발생시켜 `SUSPEND_MODE_NONE`과 충돌 → 검토 후 제거
- **추가 수정**: `stop()`에서 `mounts_`/`server_` GObject `unref` 추가, `addRelayPath()`에 `removeRelayPath()` 후 재등록 방어 코드 삽입

---

## Sub-Pi 이벤트 패킷 IP 필드 주입 (2026-03-18)

**상태**: 채택됨 · **관련 이슈**: AN-104

- **문제**: AI/IMAGE/META 패킷에 `device_id`(`SubPi_192.168.0.43`)만 있고 `ip` 필드가 없어 Qt에서 파싱 불편
- **결정**: `SubPiManager`에서 수신 시 `device_id`에서 IP를 추출하여 `"ip"` 필드를 명시적으로 주입
- **근거**: ClientServer는 패스스루만 하므로 DeviceServer에서 주입하는 것이 적합. `device_id` 포맷(`SubPi_` 접두어)이 고정되어 있으므로 `substr(6)`으로 안전 추출 가능

---

## RTSP 재접속 503 해결: Suspend Mode NONE + config-interval (2026-02-27)

**상태**: 채택됨 · **관련 이슈**: AN-80

- **문제**: VLC 재접속 시 503 에러 — GStreamer가 클라이언트 0명일 때 suspend → UDP 포트 해제 딜레이 → 바인딩 충돌
- **결정**: **`GST_RTSP_SUSPEND_MODE_NONE` + `config-interval=1`** 조합
- **근거**: 라즈베리파이에서 udpsrc 상시 구동 CPU 부하 ~0.5%로 무시 가능, SPS/PPS 1초마다 전송으로 중간 접속자 디코딩 실패도 동시 해결

---

## DeviceManager 클래스 분리: 콜백 기반 설계 (2026-02-27)

**상태**: 채택됨 · **관련 이슈**: AN-78

- **문제**: `DeviceManager.cpp` 581줄, 4가지 역할(UDP 비콘, ONVIF 스캔, 모니터링, AI 리스너)
- **결정**: **콜백 기반 3개 클래스 분리** (`SubPiManager`, `OnvifScanner`, `DeviceManager` 오케스트레이터)
- **근거**: `devices_`와 `device_mutex_`는 DeviceManager만 소유 → 락 관리 단일화, 하위 클래스는 "발견 알림"만 콜백 전달

> [!CAUTION]
> 분리 시 **중복 체크 로직**이 비용이 큰 작업(curl, TCP) **전에** 실행되는지 반드시 검증. 누락 시 1초마다 무한 curl/TCP 연결 버그 발생.

---

## ClientServer 종료 메커니즘: CV + Socket Shutdown (2026-02-27)

**상태**: 채택됨 · **관련 이슈**: AN-81

- **문제**: Ctrl+C 시 `sleep_for(3s)` 블로킹 + `cleanupFinishedThreads()` 3중 데드락
- **결정**: **`condition_variable::wait_for` + `shutdown(fd, SHUT_RDWR)`** 조합
- **근거**: `wait_for`로 즉시 깨움, `shutdown()`으로 `recv()` 블로킹 해제, `map<int,thread>`로 fd별 개별 join

---

## IMAGE 프로토콜: Containerless Chunking (2026-02-27)

**상태**: 채택됨 · **관련 이슈**: AN-84

- **문제**: Sub-Pi 낙상 감지 시 ~150장 JPEG를 Qt까지 전달, 번들 전송 시 15MB TCP 블로킹
- **결정**: **각 프레임을 기존 5바이트 헤더(IMAGE 0x0A)에 담아 독립 패킷으로 전송**
- **근거**: TCP 블로킹 없음(프레임 사이에 다른 메시지 가능), Progressive Rendering(수신 즉시 표시), 1MB 제한 회피, 실패 시 받은 프레임까지 보존

---

## STM 제어 보드 연결: Sub-Pi UART 직결 (2026-03-03)

**상태**: 채택됨 · **관련 이슈**: 아키텍처 설계

- **문제**: Sub-Pi와 STM이 메인 서버에 개별 연결 → 어떤 STM이 어떤 Sub-Pi 것인지 매칭 불가
- **결정**: **Sub-Pi에 STM을 UART(USB-UART)로 직결**
- **근거**: 물리 연결 = 자동 페어링, 제어 지연 최소화(로컬 UART), 메인 서버 단순화

```
메인 서버 ── TCP ── Sub-Pi A ──(UART)── STM A
           └ TCP ── Sub-Pi B ──(UART)── STM B
```

---

## 인증 DB: SQLite 채택 (2026-03-03)

**상태**: 채택됨 · **관련 이슈**: AN-83

- **결정**: MySQL 대신 **SQLite** 채택
- **근거**: 단일 라즈베리파이 + 소수 관제 요원, 외부 의존성 없음, Docker volume mount 영속화 가능

---

## 비밀번호 해싱: 서버 사이드 솔팅 (2026-03-03)

**상태**: 채택됨 · **관련 이슈**: AN-83

- **결정**: 평문 전송 + **서버에서 Salt + SHA-256 해싱**
- **근거**: 클라이언트 해싱 시 Pass-the-Hash 공격 취약, 폐쇄망이므로 SSL/TLS 생략, `/dev/urandom` 16바이트 salt로 레인보우 테이블 무력화

---

## role 기반 브로드캐스트 권한 분리 (2026-03-03)

**상태**: 채택됨 · **관련 이슈**: AN-83

- **결정**: fd별 role 맵 + `broadcastToRole()` 구현

| 상태 | CAMERA | AVAILABLE |
|:---:|:---:|:---:|
| 미로그인 | ❌ | ❌ |
| user | ✅ | ❌ |
| admin | ✅ | ✅ |

---

## Qt ↔ ClientServer 엔디언: 리틀 엔디언 직접 전송 (2026-03-04)

**상태**: 채택됨 · **관련 이슈**: AN-84

- **문제**: Qt가 리틀 엔디언 전송, 서버가 `ntohl()` 적용 → body_length 뒤집힘 → 738MB로 해석
- **결정**: `QtCommServer.cpp`에서 **`ntohl()`/`htonl()` 제거**, 서버 간 통신은 빅 엔디언 유지
- **근거**: 내부망, 양쪽 리틀 엔디언 아키텍처

---

## IMAGE 패킷 전달: 투명 프록시 콜백 체인 (2026-03-04)

**상태**: 채택됨 · **관련 이슈**: AN-84

- **결정**: AI 이벤트와 동일한 **콜백 체인 패턴** (`SubPiManager → DeviceManager → InternalServer → InternalClient → QtCommServer`)
- **근거**: 기존 패턴 재활용, JSON만 파싱 + JPEG는 `vector<char>` 통과, 12MB(300프레임) 메모리 감당 가능

---

## SubCam AVAILABLE: Push + 캐싱 + 5초 폴링 (2026-03-05)

**상태**: 채택됨 · **관련 이슈**: AN-93

- **결정**: SubCam이 Push → DeviceManager 캐싱 → ClientServer 5초 폴링으로 서버 상태와 합쳐 전송
- **근거**: 모니터링 데이터는 5초 지연 허용, 기존 CAMERA 폴링 재활용, `{"server":..., "devices":...}` 단일 JSON

---

## AVAILABLE 권한별 분리: broadcastByRole (2026-03-06)

**상태**: 채택됨 · **관련 이슈**: AN-93

- **결정**: **`broadcastByRole()` 신규 메서드** 추가 (기존 `broadcastToRole()` 유지)
- **근거**: `broadcastToRole` = 대상 필터링, `broadcastByRole` = 내용 분기 (다른 목적)

---

## 카메라 생존 감지: is_online 플래그 전환 (2026-03-06)

**상태**: 채택됨 · **관련 이슈**: AN-94

- **문제**: 기존에는 오프라인 시 `devices_` 맵에서 삭제 → Qt에서 인식 불가, 재연결 시 완전 재등록
- **결정**: 삭제 → **`is_online = false`** 전환, 재연결 시 복구
- **근거**: 오프라인 카메라 회색 표시 UX, 고정 설치 IP라 맵 무한 증가 없음

---

## 한화 카메라 헬스체크: TCP 554 connect (2026-03-06)

**상태**: 채택됨 · **관련 이슈**: AN-94

- **결정**: **TCP 554 3-way handshake**로 헬스체크, 같은 IP 다채널은 `ip_check_cache`로 1번만
- **근거**: SYN 64바이트 × 3대 = 192B/3초, RTSP OPTIONS나 ONVIF는 3초 주기에 과한 비용

---

## RTSP 릴레이: 즉시 구동 기각 → 지연 생성 유지 (2026-03-09)

**상태**: 기각됨 · **관련 이슈**: AN-90 (Issue 15)

- **시도**: `construct()` + `prepare()`로 파이프라인 사전 구동
- **실패 원인**: `udpsrc`가 포트 즉시 바인딩 → VLC 접속 시 포트 충돌 → 접속 불가
- **결정**: **지연 생성 유지**, Sub-Pi는 ICMP Port Unreachable에도 전송 지속 → 사전 구동 불필요

---

## ONVIF 카메라 탐색: ARP 폴백 스캔 (2026-03-09)

**상태**: 임시 채택 · 망 분리 완료 시 제거 예정  
**관련 이슈**: AN-95

- **문제**: 기업 네트워크(IGMP Snooping ON, Querier 미설정)에서 ONVIF 멀티캐스트 탐색 실패
- **결정**: **ARP 폴백 스캔 임시 도입** + 중기적으로 망 분리 목표
- **근거**: 망 분리가 정답이나 하드웨어 준비 필요, ARP 스캔은 30초 주기 ping 1회로 부하 최소
- **후속**: 듀얼 인터페이스(유선: 카메라망, 무선: 사내망) 구성 후 코드 제거

---

## META 센서 데이터: Push 즉시 전달 (2026-03-10)

**상태**: 채택됨 · **관련 이슈**: AN-98

- **결정**: **META(0x09) 독립 패킷으로 즉시 Push**, AVAILABLE에 합치지 않음
- **근거**:
  - 관심사 분리: AVAILABLE = IT 인프라, META = 현장 환경 센서
  - 서버가 합칠 데이터 없음 → 투명 프록시가 자연스러움
  - 향후 "센서 임계값 초과 시 즉시 경보" 확장에 코드 변경 없이 대응 가능

---

## ARP 폴백 스캔: broadcast ping → fping 유니캐스트 (2026-03-10)

**상태**: 채택됨 · **관련 이슈**: AN-95

- **문제**: `ping -b 192.168.0.255`로 ARP 테이블 갱신 시도 → 브로드캐스트 ICMP를 무시하는 장치(한화 카메라 등)는 ARP 테이블에 안 올라감
- **결정**: **`fping -a -g`로 254개 IP에 유니캐스트 ARP 병렬 전송**
- **근거**: 네트워크 환경에 따라 브로드캐스트 동작 여부가 달라지므로, 어떤 환경에서든 동작하는 방어적 코드로 fping 채택
- **비고**: `fping` 패키지 설치 필요, ARP 폴백 스캔 자체가 임시 조치(망 분리 후 제거 예정)

---

## 장치 타입 관리: 플랫 구조체 vs 상속 (현행 유지)

**상태**: ✅ 현행 유지

- **현재**: `DeviceInfo` 단일 구조체 + 타입별 `if/else` 분기. 탐색은 `SubPiManager`(Sub-Pi)와 `OnvifScanner`(한화)로 이미 분리됨.
- **대안**: `Device` 부모 클래스 + `SubPiDevice`/`HanwhaDevice` 상속, 가상 함수(`isAlive()`, `getRtspUrl()`)로 다형성 처리
- **현행 유지 이유**: 장치 타입이 2종뿐이고, 탐색 로직은 이미 별도 클래스로 분리됨. 상속 도입 시 `unique_ptr<Device>` 소유권 관리 복잡도만 증가.
- **전환 시점**: 장치 타입이 3종 이상으로 확장될 때 (예: USB 카메라, 일반 RTSP IP캠)

---

## 내부 통신: Request/Response → Push 전환 (AN-103)

**상태**: ✅ 전환 완료

- **이전**: `InternalClient`가 5초마다 `requestCameraList()` → `requestDeviceStatus()` 요청/응답. Push 이벤트(AI/IMAGE/META)가 같은 소켓에서 혼선 → `waitForResponse()` 필요.
- **전환 후**: `DeviceServer`가 접속 시 환영 Push + 장치 변경 시 자동 Push. `InternalClient`는 순수 수신자.
- **이점**: 패킷 수 절반, `waitForResponse` 제거, Sub-Pi 리스너와 동일한 패턴, 코드 ~100줄 삭제.

---

## Docker Compose 전환 (진행 예정)

**상태**: 📋 계획 중

- **목적**: 빌드·배포·자동 재시작 자동화 (크로스 컴파일 제거)
- **구조**: DeviceServer + ClientServer 2개 서비스
- **네트워크**: **`network_mode: host` 필수** — ONVIF 멀티캐스트, UDP 비콘, ARP 스캔, RTSP 동적 포트 등 물리 네트워크 직접 접근 필요
- **참고**: Docker 기본(브릿지) 네트워크는 가상 망(`172.17.x.x`)을 생성하여 멀티캐스트/브로드캐스트/ARP 접근 불가. `host` 모드는 네트워크 격리 없이 호스트 인터페이스 직접 사용

### 준비 체크리스트

- [ ] 라즈베리파이에 Docker + Docker Compose 설치
- [ ] DeviceServer Dockerfile (GStreamer, curl, fping)
- [ ] ClientServer Dockerfile (SQLite, OpenSSL)
- [ ] `docker-compose.yml` 작성
- [ ] `users.db` 경로 환경변수 전환
- [ ] 하드코딩된 설정 → 환경변수/`.env` 분리 (포트, 인증정보, 서브넷 범위, 타임아웃)
- [ ] 로깅 개선 검토 (타임스탬프 추가, `docker logs` 활용 또는 `spdlog` 도입)

---

## 향후 개선 로드맵

현재 설계 판단과 한계를 인식하고, 전환 시점을 기록합니다.

### 스레드 관리: 이벤트 루프 전환

- **현재**: thread-per-connection (클라이언트당 스레드 생성)
- **한계**: 동시 접속 50+ 시 컨텍스트 스위칭 비용 증가
- **왜 지금은 안 하는가**: CCTV 관제실 특성상 동시 접속 5~10명, 현재 규모에서 과잉
- **전환 방향**: epoll 기반 이벤트 루프 (libuv, libevent) 또는 스레드 풀
- **전환 시점**: 웹 클라이언트 추가로 동시 접속 수가 증가할 때

### DB: 용도별 분리

- **현재**: SQLite (인증 전용, 단일 파일)
- **한계**: 동시 쓰기 시 락, 이벤트 이력 저장에 부적합
- **왜 지금은 안 하는가**: 단일 Pi + 소수 관제 요원, SQLite가 최적
- **전환 방향**: 인증은 SQLite 유지, 이벤트 로그는 PostgreSQL 또는 시계열DB(InfluxDB) 분리
- **전환 시점**: AI 이벤트 영속화 또는 다중 서버 구성 시

### 배포: 컨테이너 오케스트레이션

- **현재**: 수동 크로스 컴파일 + scp 배포
- **단기 목표**: Docker Compose (위 섹션 참조)
- **장기 목표**: 다중 사이트 배포 시 K3s (경량 Kubernetes) 검토
- **왜 K8s를 바로 도입하지 않는가**: 단일 Pi에서 K8s 자체가 리소스 과다, Docker Compose로 충분
- **전환 시점**: 여러 건물/사이트에 관제 서버를 다수 배포해야 할 때

### 통신 프로토콜: REST API + WebSocket 계층 추가

- **현재**: Qt 전용 바이너리 프로토콜 (5바이트 헤더 + JSON, TCP 직접)
- **한계**: 웹 브라우저 접근 불가, 모바일 앱 연동 불가, API 테스트 도구(Postman 등) 사용 불가
- **전환 방향**: REST API (카메라 조회, 인증, 이벤트 이력) + WebSocket (AI 이벤트 Push, 실시간 상태)
- **기존 프로토콜과의 관계**: Qt 바이너리 프로토콜은 유지, REST/WS를 **추가 계층**으로 병행
- **전환 시점**: 웹 대시보드 또는 모바일 앱 개발 착수 시

### AI 이벤트 영속화

- **현재**: fire-and-forget (Qt에 Push 후 소멸, Qt가 꺼져 있으면 유실)
- **한계**: 야간 이벤트 사후 분석 불가, 이벤트 통계/추이 분석 불가
- **전환 방향**: 이벤트 DB 저장 + IMAGE 파일시스템 저장 + 이력 조회 API
- **전환 시점**: REST API 추가(위 항목)와 동시 진행이 자연스러움
