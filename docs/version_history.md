# DeviceApp 버전 관리 히스토리

프로젝트: `DeviceApp` — 라즈베리파이 기반 카메라 관리 + Qt 클라이언트 중계 시스템  
리포: `https://github.com/veda-Anomap/DeviceApp`  
작성 기준: 2026-03-12

---

## 아키텍처 개요

```
Sub-Pi (AI카메라) ──UDP 비콘 + TCP 5000──┐
                                         ├→ DeviceServer (:8554 RTSP, :30000 TCP)
한화 카메라 ──ONVIF 멀티캐스트──────────┘         │
                                                  │ TCP :30000
                                         ClientServer (:20000 TCP)
                                                  │
                                         Qt 클라이언트 (VLC/관제)
```

---

## 커밋 히스토리 (최신순)

### 브랜치: `main` (현재 HEAD)

| 커밋 | 이슈 | 날짜 | 내용 |
|------|------|------|------|
| — | AN-104 | 2026-03-13 | **SUNAPI 실패 차단 임계값 2회→1회 변경** |
| — | AN-103 | 2026-03-13 | **InternalClient Request/Response → Push 전환 + 데드락 수정** |
| — | AN-102 | 2026-03-12 | **스레드 누수 해결 및 방어적 코드 개선** |
| `68367a8` | AN-101 | 2026-03-11 | **docs: 향후 개선 로드맵 추가 및 Docker 체크리스트 보완** |
| `bcf36d5` | AN-100 | 2026-03-10 | **docs 폴더 추가 및 ARP 스캔 fping 유니캐스트 전환** |
| `6ef5a87` | AN-99 | 2026-03-11 | **send/recv 안정성 개선 (partial write, Push 혼선, 락 분리)** |

---

### 브랜치: `AN-96-motor-control`

| 커밋 | 이슈 | 날짜 | 내용 |
|------|------|------|------|
| `15fc64c` | AN-98 | 2026-03-10 | **META(0x09) 센서 데이터 즉시 전달 파이프라인 (Sub-Pi → Qt)** |
| `9c65612` | AN-97 | 2026-03-09 | **모터 제어 신호 전달 파이프라인 구현 (Qt → Sub-Pi)** |

---

### 브랜치: `AN-90-camera-health-check`

| 커밋 | 이슈 | 날짜 | 내용 |
|------|------|------|------|
| `a6f6ddc` | AN-95 | 2026-03-09 | **SUNAPI 2회 실패 IP 재시도 차단** |
| `d00e66d` | AN-95 | 2026-03-09 | **IGMP Snooping 환경 대응 — 멀티캐스트 그룹 가입 + ARP 폴백 스캔** |
| `500db3a` | AN-94 | 2026-03-06 | **한화 카메라 TCP 554 헬스체크 IP별 캐싱 최적화** |
| `0d5d736` | AN-94 | 2026-03-06 | **카메라 생존 감지 및 is_online 플래그 전환** |

---

### 브랜치: `AN-84-protocol-refactor` (→ main 머지 완료)

| 커밋 | 이슈 | 날짜 | 내용 |
|------|------|------|------|
| `9b68334` | AN-93 | 2026-03-06 | **AVAILABLE 권한별 분리 (broadcastByRole 추가)** |
| `9c66e1e` | AN-93 | 2026-03-05 | **CAMERA 캐시 갱신 누락 핫픽스** |
| `670fddc` | AN-93 | 2026-03-05 | **SubCam AVAILABLE 수신/전달 파이프라인 (SubCam → Qt)** |
| `87cc947` | AN-84 | 2026-03-04 | **IMAGE(0x0a) 수신/전달 파이프라인 구현 (SubCam → Qt)** |
| `55943dc` | AN-84 | 2026-03-04 | **Qt 통신 엔디언 정책 변경 (Big Endian → 리틀 엔디언)** |
| `b394f3f` | AN-84 | 2026-03-04 | **카메라 리스트에 `ip` 필드 추가 및 AVAILABLE 필드 정리** |

### 브랜치: `AN-83-auth-system` (→ main 머지 완료)

| 커밋 | 이슈 | 날짜 | 내용 |
|------|------|------|------|
| `2b1b276` | AN-83 | 2026-03-03 | **AuthManager 멀티스레드 DB 동시 접근 보호** |
| `1c3eb37` | AN-83 | 2026-03-03 | **users 테이블에 `is_active` 컬럼 추가** |
| `11e8827` | AN-83 | 2026-03-02 | **role 기반 브로드캐스트 구현** |
| `068f4e8` | AN-83 | 2026-03-02 | **테스트용 초기 DB 데이터 삽입** |
| `ad289ac` | AN-83 | 2026-02-28 | **SQLite 기반 회원가입/로그인 시스템 구현** |

### 브랜치: `main`

| 커밋 | 이슈 | 내용 |
|------|------|------|
| `752c766` | AN-81 | **ClientServer 종료 데드락 수정 (`cleanupFinishedThreads` + 소켓 shutdown)** |
| `8a34f99` | AN-81 | **Ctrl+C 무한 대기 버그 수정 (`condition_variable` 적용)** |
| `f8f68c2` | AN-80 | **RTSP 재접속 시 503 에러 및 헤더 누락 수정** |
| `f1eb443` | AN-78 | **.gitignore 실행파일 추적 해제 및 Sub-Pi 중복 탐색 버그 수정** |
| `07c7dcf` | AN-78 | **DeviceManager 클래스 분리 (콜백 기반)** |
| `d6fde7b` | AN-76 | Sub-Pi 연결 끊김 시 RTSP 릴레이 정리 + AI 리스너 안정화 |
| `acf104f` | AN-74 | 카메라 리스트에서 불필요한 `id`, `ip` 필드 제거 |
| `b1cb8c0` | AN-72 | Sub-Pi 카메라 리스트에 릴레이 RTSP URL 추가 |
| `afe7937` | AN-70 | AI 이벤트 파이프라인 구현 (Sub-Pi → Qt) |
| `c9be208` | AN-66 | monitorLoop 장치 제거 시 뮤텍스 누락 버그 수정 |
| `aca1cd6` | AN-57 | README 상세 업데이트 |
| `2d95e38` | AN-57 | SystemMonitor AVAILABLE 브로드캐스트 실기 테스트 완료 |
| `08d9316` | — | SystemMonitor 추가 |
| `363d026` | — | AuthServer→DeviceServer, StreamServer→ClientServer 이름 변경 |
| `9914834` | — | Controller 클래스 분리 |
| `1251143` | — | 마이크로서비스 분리 (AuthServer + StreamServer) |
| `a90494e` | — | Qt에 리스트 전달을 위한 프로토콜에 Camera 추가 |
| `019303d` | — | Qt 관제 프로그램 TCP 통신 서버 구현 |
| `d6087e4` | — | TLS/DTLS 암호화 추가 |
| `8551444` | — | mutex 범위 수정 |
| `a272805` | — | LAN 접속 가능 카메라 탐지 기능 구현 |
| `4524834` | — | UDP포트, ONVIF 기능 추가 |
| `c6fc833` | — | RTSP 서버 초기 구조 |
| `78b8f20` | — | 테스트 코드 작성 |
| `22cc493` | — | Initial commit |

---

## 각 커밋 상세

### AN-104: SUNAPI 실패 차단 임계값 변경 (2026-03-13)
**변경 파일**: `OnvifScanner.cpp`

- SUNAPI 실패 차단 임계값 `>= 2` → `>= 1` (4곳)
- 1회 실패 시 즉시 해당 IP 재시도 차단 (다른 팀 카메라 잠금 방지 강화)

### AN-103: InternalClient Request/Response → Push 전환 + 데드락 수정 (2026-03-13)
**변경 파일**: `InternalServer.h/cpp`, `DeviceController.cpp`, `DeviceManager.h/cpp`, `InternalClient.h/cpp`, `ClientController.cpp`

- `InternalServer`: 새 클라이언트 접속 시 CAMERA+AVAILABLE 환영 Push 즉시 전송
- `broadcastCameraList()`/`broadcastDeviceStatus()` 신규 — 장치 변경 시 모든 ClientServer에 Push
- `DeviceController`: 장치 등록/제거 콜백에서 자동 브로드캐스트
- `InternalClient`: `waitForResponse`, `requestCameraList`, `requestDeviceStatus` 완전 제거
- `connectionLoop`: 5초 요청 루프 → 순수 `handleIncoming()` 수신 루프
- `dispatchPushEvent`: CAMERA/AVAILABLE Push 수신 시 캐시 자동 갱신
- `DeviceManager`: 모든 콜백(`on_device_registered_`, `on_device_removed_`)을 `device_mutex_` 해제 후 호출하도록 리팩토링 (데드락 방지)
- `on_device_changed_` 콜백 신규 — 장치 변경 시 락 해제 후 CAMERA/AVAILABLE Push
- `ClientController`: 폴링 루프 tick 카운터 제거, `wait_for(5초)` 단순화

### AN-102: 스레드 누수 해결 및 방어적 코드 개선 (2026-03-12)
**변경 파일**: `InternalServer.h/cpp`, `SubPiManager.h/cpp`, `QtCommServer.cpp`, `Common.h`

- `InternalServer`: `std::thread::detach()` 제거 → `client_threads_` map 보관 + `cleanupFinishedThreads()`
- `SubPiManager`: `listener_threads_` vector→map 전환, 종료된 리스너 `finished_ids_` 등록 + 주기적 정리
- `stop()`에서 모든 스레드 안전 `join()` 후 종료
- `QtCommServer::broadcastImage`: `client_roles_[fd]` → `find()` 변경 (map 자동 삽입 방지)
- `Common.h`: `DeviceInfo` 멤버 기본값 추가 (`is_online=false`, `command_socket_fd=-1`)

### AN-99: send/recv 안정성 개선 (2026-03-11)
**변경 파일**: `common/NetUtil.h`(신규), `SubPiManager.cpp`, `InternalServer.cpp`, `DeviceManager.h/cpp`, `InternalClient.h/cpp`, `QtCommServer.cpp`, 헤더 4개

- `sendExact()`/`recvExact()` 공통 inline 함수 도입 (4곳 중복 제거)
- `InternalClient`: `waitForResponse()` + `dispatchPushEvent()` — 요청 대기 중 Push 이벤트 혼선 해결 (3초 타임아웃)
- `DeviceManager::monitorLoop()`: 락 내 `checkTcpPort()` 블로킹 제거 (3단계 분리)
- `DeviceManager::sendMotorCommand()`: `send_mutex_` 도입 (동시 send 경쟁 방지)

### AN-98: META 센서 데이터 즉시 전달 파이프라인 (2026-03-10)
**변경 파일**: `SubPiManager.h/cpp`, `DeviceManager.h/cpp`, `DeviceController.cpp`, `InternalServer.h/cpp`, `InternalClient.h/cpp`, `ClientController.cpp`

**`15fc64c`** — META(0x09) Push 전달:
- Sub-Pi → DeviceServer → ClientServer → Qt 경로로 센서 배치 데이터 즉시 전달
- AI 이벤트와 동일한 Push 패턴 — 캠싱 없이 투명 프록시
- JSON 구조: `{"sensor_batch": [{"tmp", "hum", "light", "tilt", "dir", "ts"}, ...]}` (5개 샘플/배치)
- AVAILABLE(폴링)과 달리 실시간 전달 — 센서 긴급 경보 확장 가능

### AN-97: 모터 제어 신호 전달 파이프라인 (2026-03-09)
**변경 파일**: `ClientController.cpp`, `InternalClient.h/cpp`, `InternalServer.h/cpp`, `DeviceController.cpp`, `DeviceManager.h/cpp`

**`9c65612`** — DEVICE(0x04) 모터 제어 전달:
- Qt → ClientServer → DeviceServer → Sub-Pi 경로로 모터 명령(w/a/s/d/auto/unauto) 전달
- `InternalClient::sendDeviceCommand()`: DeviceServer로 DEVICE 패킷 전달
- `InternalServer`: DEVICE 수신 → `on_device_command_` 콜백 호출
- `DeviceManager::sendMotorCommand()`: IP로 Sub-Pi 조회 → `command_socket_fd`로 TCP 전송

### AN-95: IGMP Snooping 환경 대응 + SUNAPI 실패 IP 차단 (2026-03-09)
**변경 파일**: [OnvifScanner.h](file:///home/changjoh/Anomap_project/DeviceApp/DeviceServer/include/OnvifScanner.h), [OnvifScanner.cpp](file:///home/changjoh/Anomap_project/DeviceApp/DeviceServer/src/OnvifScanner.cpp)

**`a6f6ddc`** — SUNAPI 2회 실패 IP 재시도 차단:
- `sunapi_fail_count_` 맵 추가 — IP별 SUNAPI 실패 횟수 추적
- 2회 이상 실패한 IP는 `runScan()` 및 `arpFallbackScan()` 모두에서 스킵
- **목적**: 다른 팀 소유의 한화 카메라에 반복 인증 시도 방지

**`d00e66d`** — 멀티캐스트 그룹 가입 + ARP 폴백 스캔:
- `IP_ADD_MEMBERSHIP`으로 WS-Discovery 멀티캐스트 그룹(239.255.255.250) 명시적 가입
- `arpFallbackScan()`: 브로드캐스트 ping → `/proc/net/arp` → 한화 MAC(`e4:30:22`) 필터 → 미등록 IP만 curl
- **배경**: 기업 네트워크(IGMP Snooping ON, Querier 미설정) 환경 대응

### AN-90: RTSP 503 해결 시도 — 파이프라인 즉시 구동 (2026-03-09, ❌ 되돌림)
**변경 파일**: `RtspServer.h`, `RtspServer.cpp`, `Makefile` (모두 원복)

- `addRelayPath()`에서 `gst_rtsp_media_factory_construct()` + `gst_rtsp_media_prepare()`로 파이프라인 즉시 구동 시도
- **결과**: UDP 포트가 사전 구동 파이프라인에 의해 선점되어, VLC 접속 시 GStreamer가 새 파이프라인을 만들 수 없어 VLC 접속 불가
- `git checkout`으로 원래 코드로 되돌림

### AN-94: 카메라 생존 감지 및 is_online 플래그 전환 (2026-03-06)
**변경 파일**: [DeviceManager.h/cpp](file:///home/changjoh/Anomap_project/DeviceApp/DeviceServer/src/DeviceManager.cpp), `SubPiManager.cpp`

**`500db3a`** — 한화 TCP 헬스체크 IP별 캐싱:
- 같은 IP의 다채널(CH_0~3)은 1번만 TCP 체크 후 결과 공유
- `ip_check_cache` 맵으로 루프 내 중복 `connect` 방지

**`0d5d736`** — is_online 전환 + 한화 헬스체크:
- `monitorLoop()`: 장치 삭제(`erase`) → `is_online = false` 변경
- 한화 카메라 TCP 554 포트 헬스체크 추가 (3초 주기)
- Sub-Pi/Hanwha 재발견 시 `is_online = true` 복구 + 소켓 fd 갱신
- `checkTcpPort()` 헬퍼 함수 신규
- `is_registered_` 콜백: 온라인 장치만 중복 판정

### AN-93: SubCam AVAILABLE + broadcastByRole (2026-03-05~06)
**변경 파일**: `SubPiManager.h/cpp`, `DeviceManager.h/cpp`, `InternalServer.h/cpp`, `InternalClient.h/cpp`, `QtCommServer.h/cpp`, `DeviceController.cpp`, `ClientController.cpp`

**`9b68334`** — broadcastByRole 추가:
- `QtCommServer::broadcastToRole()`: admin/user 역할별 브로드캐스트

**`9c66e1e`** — CAMERA 캐시 갱신 핫픽스:
- 카메라 캐시를 `requestDeviceStatus()` 호출 전에 즉시 갱신

**`670fddc`** — SubCam AVAILABLE 파이프라인:
- SubCam → DeviceServer → ClientServer → Qt 전달 경로 구현

**`87cc947`** — IMAGE 수신/전달:
- `SubPiManager`: IMAGE 타입 수신 + JPEG 바이너리 추가 수신
- `DeviceManager` → `InternalServer` → `InternalClient` → `QtCommServer` 콜백 체인
- `sendImageMessage()`: PacketHeader + JSON + JPEG 바이너리 전송

### AN-84: Qt 통신 엔디언 변경 + 카메라 리스트 개선
**`55943dc`** — 리틀 엔디언 전환:
- `QtCommServer.cpp`에서 `ntohl()`/`htonl()` 제거
- Qt ↔ ClientServer 구간만 리틀 엔디언, 서버 간 통신은 빅 엔디언 유지

**`b394f3f`** — ip 필드 추가:
- `buildCameraListJson()`에 `ip` 필드 추가 (Qt에서 IP 기준 멀티채널 그룹핑 가능)
- AVAILABLE에서 `cameras_connected`, `clients_connected` 제거

### AN-83: SQLite 기반 인증 시스템 구현
**변경 파일**: `AuthManager.h/cpp`, `ClientController.cpp`, [Common.h](file:///home/changjoh/Anomap_project/DeviceApp/common/Common.h)

**`ad289ac`** — 핵심 인증 로직:
- `AuthManager` 클래스 신규: SQLite DB 초기화, `registerUser()`, `loginUser()`
- SHA-256 + salt 방식 비밀번호 해싱 (`/dev/urandom` + OpenSSL)
- 입력 검증, UNIQUE 제약 위반 처리, role 기반 로그인 제어

**`068f4e8`** — 테스트 데이터:
- `seedTestData()`: DB 비어있을 때 자동 삽입
- admin 1명(`admin_alpha`), user 3명, pending 3명

**`11e8827`** — role 기반 권한 분리:
- `QtCommServer::broadcastToRole()`: 역할별 브로드캐스트 (`user` = user+admin, `admin` = admin만)
- `setClientRole()`: 로그인 성공 시 TCP fd에 role 태깅
- `ClientController::onQtMessage()`: `ASSIGN(0x08)` 메시지 내 `action` 필드로 분기 (register/list_pending/approve)
- 관리자 기능: `listPendingUsers()`, `approveUser()`

**`1c3eb37`** — DB 스키마 확장:
- `is_active INTEGER DEFAULT 1` 컬럼 추가

**`2b1b276`** — 멀티스레드 DB 접근 보호:
- `AuthManager.h`에 `std::mutex db_mutex_` 추가
- 4개 public 메서드에 `std::lock_guard` 적용

### AN-81: ClientServer 종료 데드락 수정
**변경 파일**: `QtCommServer.h/cpp`, `InternalClient.h/cpp`, `ClientController.h/cpp`

- `cleanupFinishedThreads()` 3중 데드락 수정
- `client_threads_`: `vector<thread>` → `map<int,thread>` 변경
- `InternalClient`: `shutdown(current_fd_)` 소켓 강제 종료
- `ClientController`: `stop()` 순서 변경, 메인 루프 `condition_variable` 적용

### AN-80: RTSP 재접속 시 503 에러 수정
**변경 파일**: `RtspServer.cpp`

- `config-interval=1` 추가 (SPS/PPS 헤더 주기적 전송)
- `suspend_mode` → `NONE` (파이프라인 상시 구동)

### AN-78: DeviceManager 클래스 분리
**변경 파일**: `DeviceManager.h/cpp`, `SubPiManager.h/cpp`, `OnvifScanner.h/cpp`

- 581줄 → 3개 클래스 분리 (단일 책임 원칙)
- 콜백 기반 데이터 동기화
- 중복 체크 누락 버그 수정

### AN-76: Sub-Pi 연결 정리 + AI 리스너 안정화
**변경 파일**: `RtspServer.h/cpp`, `DeviceManager.h/cpp`, `DeviceController.cpp`, `Common.h`

- `removeRelayPath()` 추가, `on_device_removed_` 콜백
- `SO_RCVTIMEO` → `select()` 방식, JSON 파싱 에러 처리 강화
- `ASSIGN(0x08)`, `META(0x09)` 메시지 타입 추가
