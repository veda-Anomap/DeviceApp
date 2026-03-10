# DeviceApp 트러블슈팅 히스토리

프로젝트 개발 과정에서 발생한 버그, 설계 문제, 해결 과정을 기록한 문서입니다.  
작성 기준: 2026-03-10

---

## Issue 1: monitorLoop 장치 제거 시 뮤텍스 누락 (AN-66) ✅

- **증상**: 서버 크래시 또는 장치 리스트 불일치
- **원인**: `monitorLoop`에서 `devices_` 맵을 수정할 때 `device_mutex_` 미적용 → 레이스 컨디션
- **해결**: `std::lock_guard<std::mutex>` 추가

---

## Issue 2: subPiListener 유휴 타임아웃으로 연결 종료 (AN-76) ✅

- **증상**: AI 이벤트가 10초간 없으면 TCP 연결이 끊어짐. Sub-Pi가 정상인데 리스너 스레드 사망.
- **원인**: `SO_RCVTIMEO` 10초 설정 → `recv()` 타임아웃 → `break` → 스레드 종료
- **해결**: `SO_RCVTIMEO` 제거, `select()` 30초 루프로 교체. 데이터 없으면 `continue` (스레드 생존 유지)

---

## Issue 3: subPiListener 잘못된 패킷으로 TCP 스트림 깨짐 (AN-76) ✅

- **증상**: Sub-Pi가 JSON이 아닌 평문 전송 시 서버 크래시 또는 이후 패킷 파싱 실패
- **원인**: 잘못된 `body_len` 값으로 이후 데이터를 엉뚱한 위치에서 읽음 (스트림 비동기화)
- **해결**:
  - `body_len` 범위 검증 (0 또는 1MB 초과 시 `continue`)
  - JSON 파싱 `try-catch` + 3연속 실패 시에만 연결 종료
  - 디버그 로그: 헤더 type/body_len, 본문 앞 50자 출력

---

## Issue 4: Sub-Pi 연결 끊김 시 RTSP 릴레이 미정리 (AN-76) ✅

- **증상**: Sub-Pi가 꺼져도 GStreamer RTSP 릴레이(udpsrc 파이프라인)가 계속 실행됨
- **원인**: `monitorLoop`이 `devices_`에서만 제거하고, `RtspServer`에 알리지 않음
- **해결**:
  - `RtspServer::removeRelayPath(device_id)` 추가
  - `DeviceManager::on_device_removed_` 콜백 추가
  - `DeviceController`에서 콜백 연결: 장치 제거 → RTSP 릴레이 제거

---

## Issue 5: Common.h ASSIGN/META 열거형 컴파일 에러 (AN-76) ✅

- **증상**: `Common.h`에서 컴파일 에러
- **원인**: 코멘트에 비표준 공백(Non-Breaking Space, `\xc2\xa0`)이 포함됨
- **해결**: 해당 문자를 일반 ASCII 공백으로 교체

---

## Issue 6: ONVIF SmartCheck 로그 무한 출력 (AN-78 리팩토링 후) ✅

- **증상**: 30초마다 모든 한화 카메라에 대해 `[SmartCheck] Found Channel...` 반복 출력. curl이 매번 호출됨.
- **원인**: `OnvifScanner`로 코드 분리 시 **중복 체크 로직이 curl 호출 이후로 밀림**
- **해결**: `OnvifScanner`에 `IsRegisteredCallback` 추가. curl 전에 등록 여부 확인.

---

## Issue 7: Sub-Pi VLC RTSP 끊김 — 1초마다 포트 재요청 (AN-78 리팩토링 후) ✅

- **증상**: VLC에서 Sub-Pi 영상이 "보이다 안보이다" 반복 또는 아예 안 나옴
- **원인**: `SubPiManager`로 코드 분리 시 **비콘 중복 체크 누락**. 매초 새 포트로 `START_STREAM` 명령 반복 발사.
- **해결**: `SubPiManager`에 `IsRegisteredCallback` 추가. 비콘 수신 시 등록 여부 확인.

---

## Issue 8: RTSP 503 Service Unavailable 및 재접속 실패 (AN-80) ✅

- **증상**: VLC에서 RTSP 접속 시 503 에러, 화면을 껐다 켤 때 무한 로딩
- **원인**:
  1. **503 Service Unavailable**: 클라이언트 0명 시 팩토리 `suspend` → UDP 포트 해제 딜레이 → 새 클라이언트 접속 시 바인딩 충돌로 503 반환
  2. **재접속 시 화면 깨짐**: 중간 접속 사용자에게 H.264 **SPS/PPS 헤더**가 없어 디코딩 실패 (IDR 프레임 전 필수 헤더 누락)
- **해결**:
  - `gst_rtsp_media_factory_set_suspend_mode(..., GST_RTSP_SUSPEND_MODE_NONE)` — 파이프라인 상시 구동
  - GStreamer 파이프라인에 `config-interval=1` 추가 — SPS/PPS 헤더 주기적 전송

---

## Issue 9: ClientServer 멈춤 현상 — Ctrl+C 종료 불가 (AN-81) ✅

- **증상**: Ctrl+C 시 "Connection failed. Retrying in 3s..." 무한 출력
- **원인**: `cleanupFinishedThreads()`에서 `finished_mutex_` 잡은 채 모든 스레드 join → 3중 데드락
- **해결**:
  - `sleep_for` → `condition_variable::wait_for` 교체
  - `client_threads_`: `vector<thread>` → `map<int,thread>` 변경
  - 뮤텍스 해제 후 해당 fd 스레드만 join
  - `InternalClient::stop()`에서 `shutdown(current_fd_)`

---

## Issue 10: users 테이블에 계정 활성화 컬럼 미존재 (AN-83) ⬜

- **증상**: 계정 삭제 요청 시 DB에서 `DELETE`해야 하므로 복구 불가
- **원인**: 초기 설계 시 계정 비활성화/복구 요구사항 미반영
- **해결**: `is_active INTEGER DEFAULT 1` 컬럼 추가
- **향후 작업**: 로그인 쿼리에 `AND is_active = 1` 조건 추가, 삭제/복구 API 구현
- **상태**: ⬜ DB 컬럼 추가 완료, 기능 구현 대기

---

## Issue 11: AuthManager 멀티스레드 DB 동시 접근 (AN-83) ✅

- **증상**: 여러 Qt 클라이언트가 동시에 로그인/회원가입 시 `sqlite3_errmsg()` 덮어쓰기 또는 잠재적 크래시
- **원인**: `QtCommServer`가 클라이언트마다 별도 스레드로 `clientHandler()`를 실행 → 같은 `db_` 포인터에 동시 접근
- **해결**: `AuthManager`에 `std::mutex db_mutex_` 추가, 4개 public 메서드에 `std::lock_guard` 적용

---

## Issue 12: Qt 패킷 body_length 엔디안 불일치 (AN-84) ✅

- **증상**: Qt 클라이언트 접속 시 `Body too large: 738197504 bytes. Disconnecting.`
- **원인**: Qt가 body_length를 리틀 엔디안 그대로 전송, 서버는 `ntohl()`로 빅→리틀 변환 → 값이 뒤집힘 (`0x2C` → `0x2C000000`)
- **해결**: 내부망 환경이므로 `QtCommServer.cpp`에서 `ntohl()`/`htonl()` 제거, Qt도 변환 없이 전송
- **비고**: 서버 간 통신(InternalClient ↔ InternalServer, SubPiManager)은 빅 엔디안 유지

---

## Issue 13: SubCam IMAGE 패킷 파싱 실패 (AN-84) ✅

- **증상**: `IMAGE JSON parse error: attempting to parse an empty input`, body 첫 바이트가 `S`(0x53)/`T`(0x54)
- **원인**: SubCam이 `body_length`에 JSON+JPEG 전체 크기를 넣고, JSON 앞에 1바이트 길이 접두사를 추가하여 전송
- **기대 포맷**: `[0x0a][json_len: 4B][JSON][JPEG]` (body_length = JSON 길이만)
- **실제 포맷**: `[0x0a][total_len: 4B][json_len_byte: 1B][JSON][JPEG]`
- **해결**: SubCam 측에 올바른 패킷 규격 전달 → SubCam 펌웨어 수정 완료

---

## Issue 14: CAMERA 패킷 Qt 미전달 — 카메라 캐시 갱신 누락 (AN-93) ✅

- **증상**: Qt 클라이언트에 AVAILABLE 패킷은 도착하나, CAMERA 패킷이 오지 않음. Wireshark에서 CAMERA 타입 패킷 미확인.
- **원인**: `InternalClient::connectionLoop()`에서 `requestDeviceStatus()` 실패 시 `break` → `cached_cameras_` 갱신 코드에 도달하지 못함 → 카메라 캐시가 초기값 `[]`로 유지
- **해결**: 카메라 캐시를 `requestDeviceStatus()` 호출 **전에** 즉시 갱신하도록 순서 변경

---

## Issue 15: RTSP 503 간헐적 발생 — GStreamer 파이프라인 타이밍 문제 (AN-90/94) 🔶 보류

- **증상**: Sub-Pi 연결 후 VLC에서 RTSP 접속 시 간헐적으로 503 에러. 연결 성공 여부가 불규칙. DeviceServer 재시작하면 정상 작동.
- **현재 상태**: 현재(2026-03-09 기준) 재현되지 않고 있어 **보류** 처리.

### 확인된 사실
- Sub-Pi는 ICMP Port Unreachable을 받아도 **UDP 전송을 멈추지 않음** (Wireshark 확인)
- `suspend_mode=NONE` + `config-interval=1`(양쪽) + `shared=TRUE` 모두 정상 적용됨
- Sub-Pi 송출 파이프라인: `appsrc → x264enc tune=zerolatency → rtph264pay config-interval=1 → udpsink` (FPS=30)
- DeviceServer 릴레이 파이프라인: `udpsrc → rtph264depay → h264parse → rtph264pay config-interval=1`

### GST_DEBUG=3 로그 분석 (2026-03-09)

두 가지 에러가 동시에 발견됨:

**① h264parse 프리롤 타임아웃 (20초)**
```
0:00:12.795 WARN  rtspstream: Passing event              ← VLC 접속 시도
0:00:32.791 WARN  rtspmedia: failed to preroll pipeline   ← 20초 후 타임아웃
0:00:32.795 ERROR rtspclient: can't prepare media
```
- `addRelayPath()`는 팩토리만 등록, 첫 VLC 접속 시 파이프라인 생성
- 팩토리 등록 ~ VLC 접속 사이에 Sub-Pi가 보낸 UDP 데이터는 수신자 없이 버려짐
- VLC 접속 후 `udpsrc` 바인딩 → 다음 IDR 프레임까지 대기 필요

**② RTSP 경로 대소문자 불일치**
```
[RELAY] Active: /SubPi_192.168.0.43     ← 등록 경로 (대문자)
ERROR: no factory for path /subpi_192.168.0.43  ← 요청 경로 (소문자!)
```
- VLC가 URL을 소문자로 정규화하여 요청 → 팩토리 매칭 실패
- Qt는 정확한 대소문자로 접속하므로 문제없음

### 시도한 해결책

| # | 시도 | 결과 |
|---|------|------|
| 1 | `suspend_mode=NONE` 설정 | ✅ 클라이언트 0명 후 재접속 503 해결 |
| 2 | `config-interval=1` 양쪽 추가 | ✅ SPS/PPS 재삽입 정상 동작 |
| 3 | `shared=TRUE` 설정 | ✅ 적용됨 |
| 4 | `construct()` + `prepare()`로 즉시 구동 | ❌ UDP 포트 선점 → VLC 접속 불가 → 되돌림 |
| 5 | `GST_DEBUG=3` 로그 분석 | 🔍 프리롤 타임아웃 + 대소문자 불일치 확인 |

### 재발 시 조치 방향
- **경로 소문자 통일**: `addRelayPath()`에서 `std::transform(path, tolower)`
- **프리롤 타임아웃 연장**: GStreamer 프리롤 타임아웃 값 늘리기 또는 Sub-Pi에 IDR 간격 단축 요청

---

## Issue 16: 기업 네트워크 통합 후 ONVIF 카메라 탐색 실패 (AN-95) ✅

- **증상**: 네트워크 통합(6개 방 → 기업 스위치) 후, 일부 한화 카메라가 ONVIF 멀티캐스트 Probe에 응답하지 않음. ping/curl은 정상.
- **원인**: 기업 스위치의 **IGMP Snooping**이 활성화되어 있으나, **IGMP Querier가 미설정** → 멀티캐스트 멤버십이 포트별로 불규칙하게 만료 → 일부 카메라에만 Probe 전달
- **핵심 구분**: ARP(브로드캐스트)는 모든 장치에 전달되지만, ONVIF(멀티캐스트 `239.255.255.250`)는 IGMP 테이블에 등록된 포트에만 전달
- **해결**:
  1. `IP_ADD_MEMBERSHIP`으로 WS-Discovery 멀티캐스트 그룹 명시적 가입
  2. ARP 폴백 스캔: 브로드캐스트 ping(1회) → `/proc/net/arp` → 한화 MAC(`e4:30:22`) 필터 → 미등록 IP만 curl
  3. SUNAPI 2회 실패 IP 재시도 차단 (`sunapi_fail_count_` 맵) — 다른 팀 카메라 잠금 방지
- **비고**: 근본 해결은 네트워크팀에 IGMP Querier 설정 요청

---

## Issue 17: Makefile 헤더 의존성 미지정으로 세그폴트 (AN-95) ✅

- **증상**: `OnvifScanner.h`에 멤버 변수 추가 후 `make` → DeviceServer 실행 시 세그폴트. `[Internal] StreamServer connected` 직후 crash.
- **원인**: Makefile이 `%.o: src/%.cpp`만 정의 → 헤더 변경 시 의존 `.o` 파일이 재컴파일되지 않음. `DeviceManager`가 `OnvifScanner`를 멤버로 소유하는데, `sizeof(OnvifScanner)`가 변경되어도 `DeviceManager.o`는 옛날 크기로 메모리 배치 → **메모리 레이아웃 불일치** → 세그폴트
- **해결**: `make clean && make`로 전체 재빌드
- **교훈**: 헤더에 멤버 변수를 추가/삭제한 뒤에는 반드시 `make clean && make` 수행

---

## Issue 18: ARP 폴백 스캔 — 브로드캐스트 ping이 ARP 테이블 갱신 실패 (AN-95) ✅

- **증상**: 라즈베리파이 재부팅 후 한화 카메라(192.168.0.49)가 `arp -an`에 나타나지 않아 ARP 폴백 스캔이 카메라를 발견하지 못함. 수동으로 `ping 192.168.0.49`를 보내면 즉시 ARP 테이블에 등록되고 카메라 발견됨.
- **원인**: `ping -b 192.168.0.255`(브로드캐스트 ICMP)를 사용했으나, **대부분의 장치는 브로드캐스트 ICMP를 무시**(리눅스 기본값 `icmp_echo_ignore_broadcasts=1`). 브로드캐스트 ICMP에 응답하지 않으면 ARP 교환도 일어나지 않아 ARP 테이블에 등록되지 않음.
- **해결**: `ping -b` → **`fping -a -g`**로 교체. 서브넷 254개 IP에 유니캐스트 ARP+ICMP를 병렬 전송 (~2초)
- **비고**: `fping` 패키지 설치 필요 (`sudo apt install fping`). 네트워크 환경에 따라 브로드캐스트 ping이 동작하는 경우도 있으므로, fping은 어떤 환경에서든 동작하는 방어적 코드

---

## 미해결 항목 요약

| # | 이슈 | 상태 | 비고 |
|---|------|------|------|
| 10 | `is_active` 기반 계정 관리 | ⬜ DB 컬럼만 추가 | 로그인 쿼리 조건, 삭제/복구 API 미구현 |
| 15 | Sub-Pi 복구 시 RTSP 503 | 🔶 보류 | 현재 미재현. 재발 시 경로 소문자 통일 + 프리롤 타임아웃 연장 검토 |
| — | Docker Compose 전환 | 📋 진행 예정 | `network_mode: host` 필수, 빌드/배포/자동 재시작 자동화 목적 |

---

## 참고: 리팩토링 시 주의사항 (교훈)

> [!CAUTION]
> 클래스 분리 리팩토링 시 **중복 체크 로직**이 빠지지 않았는지 반드시 확인할 것.  
> 특히 `devices_` 맵에 직접 접근하던 코드를 콜백으로 변환할 때, **비용이 큰 작업 전에** 중복 체크가 실행되는지 검증.

| 패턴 | ❌ 잘못된 순서 | ✅ 올바른 순서 |
|------|---------------|---------------|
| ONVIF | curl 호출 → 중복 체크 → 무시 | **중복 체크** → curl 호출 → 등록 |
| Sub-Pi | TCP 연결 → 중복 체크 → 무시 | **중복 체크** → TCP 연결 → 등록 |
