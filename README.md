# DeviceApp
라즈베리 파이 4B 기반 CCTV 관제 시스템의 메인 서버

## 프로젝트 구조
```
DeviceApp/
├── common/          공유 헤더 (프로토콜, 데이터 구조체)
├── DeviceServer/    장치 탐색 + RTSP 릴레이 서버
├── ClientServer/    Qt 관제 통신 + 서버 모니터링
├── crypt/           암호화 모듈
└── README.md
```

## 서버 구성

### DeviceServer (포트: TCP 30000)
- **DeviceManager**: UDP 비콘 수신(Sub-Pi) + ONVIF 스캔(한화 카메라) + 장치 생존 모니터링
- **RtspServer**: GStreamer 기반 UDP → RTSP 영상 릴레이
- **InternalServer**: ClientServer에 카메라 리스트 제공

### ClientServer (포트: TCP 20000)
- **QtCommServer**: Qt 관제 클라이언트와 바이너리 프로토콜 통신
- **InternalClient**: DeviceServer에서 카메라 리스트 수신
- **SystemMonitor**: CPU/메모리/온도/가동시간 수집

## 통신 프로토콜
```
패킷 구조: [Type 1B][BodyLength 4B (NBO)][JSON Body]

MessageType:
  0x00 ACK        0x01 LOGIN      0x02 SUCCESS
  0x03 FAIL       0x04 DEVICE     0x05 AVAILABLE
  0x06 AI         0x07 CAMERA
```

## 브로드캐스트 (5초 주기)
- **CAMERA (0x07)**: 연결된 카메라 리스트 (JSON 배열)
- **AVAILABLE (0x05)**: 서버 자원 상태 (CPU, 메모리, 온도, 가동시간, 카메라 수, 클라이언트 수)

## 빌드
```bash
# 크로스 컴파일 (aarch64)
cd DeviceServer && make
cd ClientServer && make
```

## 의존성
- nlohmann/json v3.11.3
- GStreamer + RTSP Server
- libcurl (ONVIF)
- Linux: pthreads, socket API
