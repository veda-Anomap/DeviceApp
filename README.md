# DeviceApp

라즈베리 파이 4B 기반 **하이브리드 영상 관제 시스템**의 메인 서버입니다.  
Sub-Pi(AI 카메라)와 한화 비전 카메라를 자동 탐색하여 Qt 관제 클라이언트에 통합 제공합니다.

## 아키텍처

```
Sub-Pi ──[UDP 비콘]──▶ DeviceServer ──[TCP 30000]──▶ ClientServer ──[TCP 20000]──▶ Qt 클라이언트
         [UDP 영상]──▶ RtspServer ──[RTSP 8554]──────────────────────────────────▶ Qt/VLC
한화 카메라 ──[ONVIF]──▶ DeviceServer
```

## 프로젝트 구조

```
DeviceApp/
├── common/          공유 헤더 (프로토콜, 데이터 구조체, NetUtil)
├── DeviceServer/    장치 탐색 + RTSP 릴레이 + Push 이벤트 서버
├── ClientServer/    Qt 관제 통신 + 인증 + 시스템 모니터링
├── crypt/           암호화 모듈
└── docs/            프로토콜 명세, 버전 히스토리, ADR
```

## 서버 구성

### DeviceServer

| 클래스 | 역할 |
|:---|:---|
| `DeviceManager` | 장치 통합 관리 (등록/상태/헬스체크 모니터링) |
| `SubPiManager` | Sub-Pi UDP 비콘 수신 + TCP 핸드셰이크 + AI/IMAGE/META/AVAILABLE 리스너 |
| `OnvifScanner` | 한화 카메라 ONVIF 멀티캐스트 + ARP 폴백 스캔 |
| `RtspServer` | GStreamer UDP→RTSP 릴레이 (H.264, buffer-size 2MB, queue 분리) |
| `InternalServer` | ClientServer 접속 수락 + Push 이벤트 브로드캐스트 |

### ClientServer

| 클래스 | 역할 |
|:---|:---|
| `ClientController` | 진입점, 콜백 연결, 5초 주기 상태 브로드캐스트 |
| `InternalClient` | DeviceServer Push 수신 (CAMERA/AVAILABLE 캐싱, AI/IMAGE/META 패스스루) |
| `QtCommServer` | Qt TCP 서버 + role 기반 브로드캐스트 (admin/user/guest) |
| `AuthManager` | SQLite 인증 (회원가입, 로그인, 승인) |
| `SystemMonitor` | CPU/메모리/온도/가동시간 수집 |

## 통신 프로토콜

```
패킷: [Type 1B] [BodyLength 4B] [JSON Body]

서버 간 (SubPi↔DeviceServer↔ClientServer): Big Endian
Qt↔ClientServer: Little Endian
```

| Type | 이름 | 방향 | 전송 방식 |
|:---:|:---|:---|:---|
| `0x01` | LOGIN | Qt→서버 | 요청 |
| `0x02` | SUCCESS | 서버→Qt | 응답 |
| `0x03` | FAIL | 서버→Qt | 응답 |
| `0x04` | DEVICE | Qt→서버 | 모터 제어 (패스스루) |
| `0x05` | AVAILABLE | 서버→Qt | 5초 주기, admin 전용 |
| `0x06` | AI | 서버→Qt | 즉시 Push (이벤트 발생 시) |
| `0x07` | CAMERA | 서버→Qt | 5초 주기, user+admin |
| `0x08` | ASSIGN | Qt→서버 | 회원가입/승인 |
| `0x09` | META | 서버→Qt | 즉시 Push (5초 배치) |
| `0x0a` | IMAGE | 서버→Qt | 즉시 Push (JSON+JPEG) |

> 상세 프로토콜 명세: [`docs/qt_protocol_reference.md`](docs/qt_protocol_reference.md)

## 포트 구성

| 포트 | 프로토콜 | 용도 |
|:---:|:---:|:---|
| 10001 | UDP | Sub-Pi 비콘 수신 |
| 5000 | TCP | Sub-Pi START_STREAM 명령 |
| 15001~ | UDP | Sub-Pi 영상 수신 (동적 할당) |
| 8554 | RTSP | GStreamer RTSP 릴레이 서버 |
| 30000 | TCP | DeviceServer ↔ ClientServer 내부 통신 |
| 20000 | TCP | ClientServer ↔ Qt 클라이언트 통신 |

## 빌드

```bash
# 크로스 컴파일 (aarch64)
cd DeviceServer && make
cd ClientServer && make
```

## 의존성

- nlohmann/json v3.11.3
- GStreamer 1.x + RTSP Server
- libcurl (ONVIF SUNAPI)
- SQLite3 (인증)
- Linux: pthreads, socket API

## 문서

- [`docs/qt_protocol_reference.md`](docs/qt_protocol_reference.md) — Qt 통신 프로토콜 전체 명세
- [`docs/version_history.md`](docs/version_history.md) — 커밋 히스토리 (AN-99 ~ AN-105)
- [`docs/architecture_decisions.md`](docs/architecture_decisions.md) — ADR (설계 결정 기록)
