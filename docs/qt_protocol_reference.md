# Qt 클라이언트 ↔ ClientServer 통신 프로토콜

ClientServer 주소: `TCP 20000번 포트`  
작성 기준: 2026-03-10

---

## 1. 패킷 구조

모든 메시지는 **5바이트 헤더 + JSON 본문** 형식입니다.

```
[Type: 1바이트] [BodyLength: 4바이트, 리틀 엔디언] [JSON Body: N바이트]
```

> [!IMPORTANT]
> `BodyLength`는 **리틀 엔디언 (변환 없이 그대로)** 전송합니다.
> 내부망 환경이므로 네트워크 바이트 오더 변환(`htonl`/`ntohl`)을 사용하지 않습니다.

> [!WARNING]
> **서버 간 통신**(SubCam ↔ DeviceServer ↔ ClientServer)은 **Big Endian**(`htonl`/`ntohl`) 사용합니다.
> Qt ↔ ClientServer 구간만 리틀 엔디언입니다.

---

## 2. MessageType 목록

| 값 | 이름 | 방향 | 용도 |
|----|------|------|------|
| `0x01` | LOGIN | Qt → 서버 | 로그인 요청 |
| `0x02` | SUCCESS | 서버 → Qt | 성공 응답 |
| `0x03` | FAIL | 서버 → Qt | 실패 응답 |
| `0x05` | AVAILABLE | 서버 → Qt | 서버 + 디바이스 시스템 상태 *(admin 전용)* |
| `0x06` | AI | 서버 → Qt | AI 이벤트 *(낙상 감지 등)* |
| `0x07` | CAMERA | 양방향 | 카메라 리스트 |
| `0x08` | ASSIGN | Qt → 서버 | 회원가입 / 승인 대기 목록 / 유저 승인 |
| `0x0a` | IMAGE | 서버 → Qt | 이벤트 녹화 이미지 (JSON 메타 + JPEG) |

### DEVICE(0x04) 모터 제어 명령

Qt에서 Sub-Pi의 모터를 제어합니다. 서버는 이 패킷을 그대로 Sub-Pi에 전달합니다.

**요청** (Qt → 서버, Type `0x04`):
```json
{"device": "192.168.0.43", "motor": "w"}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `device` | string | Sub-Pi IP 주소 |
| `motor` | string | 모터 명령: `w`, `a`, `s`, `d`, `auto`, `unauto` |

> [!NOTE]
> 응답 패킷은 없습니다. 서버는 수신 즉시 Sub-Pi로 전달합니다.

### META(0x09) 센서 배치 데이터

Sub-Pi의 STM32 센서에서 수집한 환경 데이터를 5초 주기로 전달합니다. 서버는 캠싱 없이 즉시 Push합니다.

**수신** (서버 → Qt, Type `0x09`):
```json
{
  "device_id": "SubPi_192.168.0.43",
  "sensor_batch": [
    {"tmp": 23.92, "hum": 34.26, "light": 200.0, "tilt": 90.0, "dir": "R", "ts": 1773104377},
    {"tmp": 23.92, "hum": 34.61, "light": 201.0, "tilt": 90.0, "dir": "R", "ts": 1773104378}
  ]
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `device_id` | string | Sub-Pi 식별자 (서버가 추가) |
| `sensor_batch` | array | 센서 샘플 배열 (5개/배치) |
| `tmp` | float | 온도 (°C) |
| `hum` | float | 습도 (%) |
| `light` | float | 조도 (lux) |
| `tilt` | float | 기울기 (도) |
| `dir` | string | 방향 (R/L 등) |
| `ts` | int | Unix 타임스탬프 |

### ASSIGN(0x08) action 분기 요약

| action | 기능 | 필수 필드 |
|--------|------|----------|
| `"register"` | 회원가입 | `id`, `email`, `pw` |
| `"list_pending"` | 승인 대기 목록 조회 | 없음 |
| `"approve"` | 유저 승인 | `target_id` |

### 포트 구성

| 포트 | 프로토콜 | 용도 |
|------|----------|------|
| 10001 | UDP | Sub-Pi 비콘 수신 |
| 5000 | TCP | Sub-Pi에 START_STREAM 명령 |
| 15001~ | UDP | Sub-Pi 영상 수신 (동적 할당) |
| 8554 | RTSP | GStreamer RTSP 릴레이 서버 |
| 30000 | TCP | DeviceServer ↔ ClientServer 내부 통신 |
| 20000 | TCP | ClientServer ↔ Qt 클라이언트 통신 |

---

## 3. 로그인

### 요청 (Qt → 서버, Type `0x01`)

```json
{
    "id": "user_alpha",
    "pw": "pass_user_alpha"
}
```

### 응답 — 성공 (Type `0x02`)
```json
{
    "success": true,
    "state": "user",
    "username": "user_alpha"
}
```
- `state`는 `"user"` 또는 `"admin"`
- 성공 직후 **카메라 리스트**(`0x07`)도 자동 전송됨

### 응답 — 실패 (Type `0x03`)
```json
{
    "success": false,
    "error": "비밀번호가 일치하지 않습니다."
}
```

**실패 에러 메시지 종류:**
| error | 원인 |
|-------|------|
| `"아이디와 비밀번호를 입력해주세요."` | 빈 입력 |
| `"존재하지 않는 아이디입니다."` | DB에 없는 username |
| `"비밀번호가 일치하지 않습니다."` | 해시 불일치 |
| `"관리자 승인 대기 중입니다."` | role이 pending |

---

## 4. 회원가입

### 요청 (Qt → 서버, Type `0x08`)

```json
{
    "action": "register",
    "id": "new_user",
    "email": "new_user@example.com",
    "pw": "my_password"
}
```

### 응답 — 성공 (Type `0x02`)
```json
{
    "success": true,
    "message": "회원가입 완료. 관리자 승인을 기다려주세요."
}
```

### 응답 — 실패 (Type `0x03`)
```json
{
    "success": false,
    "error": "이미 사용 중인 아이디 또는 이메일입니다."
}
```

> [!NOTE]
> 회원가입 후 role은 `"pending"`입니다. 관리자가 승인해야 로그인 가능합니다.

---

## 5. 관리자 — 승인 대기 목록 조회

### 요청 (Qt → 서버, Type `0x08`)
```json
{
    "action": "list_pending"
}
```

### 응답 (Type `0x08`)
```json
{
    "success": true,
    "users": [
        { "username": "wait_alpha", "email": "wait_alpha@google.com", "created": "2026-03-04 ..." },
        { "username": "wait_beta",  "email": "wait_beta@google.com",  "created": "2026-03-04 ..." }
    ]
}
```

---

## 6. 관리자 — 유저 승인

### 요청 (Qt → 서버, Type `0x08`)
```json
{
    "action": "approve",
    "target_id": "wait_alpha"
}
```

### 응답 — 성공 (Type `0x02`)
```json
{
    "success": true,
    "message": "wait_alpha 승인 완료"
}
```

---

## 7. 카메라 리스트 (CAMERA, `0x07`)

### 전송 시점
| 시점 | 대상 |
|------|------|
| 로그인 성공 직후 (1회) | 로그인한 클라이언트만 |
| 이후 약 5초마다 주기적 | user + admin 전체 |

### JSON 형식 (Type `0x07`, JSON 배열)

> [!NOTE]
> 오프라인 장치(`is_online: false`)도 리스트에 포함됩니다.
> Qt에서 오프라인 카메라를 회색으로 표시하거나 비활성화 처리하는 것을 권장합니다.

```json
[
    {
        "is_online": true,
        "ip": "192.168.0.10",
        "source_url": "rtsp://192.168.0.10:554/profile1/media.smp",
        "type": "HANWHA"
    },
    {
        "is_online": true,
        "ip": "192.168.0.10",
        "source_url": "rtsp://192.168.0.10:554/profile2/media.smp",
        "type": "HANWHA"
    },
    {
        "is_online": false,
        "ip": "192.168.0.52",
        "source_url": "rtsp://192.168.0.52:8554/SubPi_abc123",
        "type": "SUB_PI"
    }
]
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `is_online` | bool | 카메라 접속 상태 (`false`인 장치도 리스트에 포함됨) |
| `ip` | string | 카메라 IP (같은 IP = 같은 물리 카메라의 다른 채널) |
| `source_url` | string | RTSP 스트림 주소 (VLC 등에서 재생 가능) |
| `type` | string | `"HANWHA"` 또는 `"SUB_PI"` |

---

## 8. 시스템 상태 (AVAILABLE, `0x05`) — admin 전용

### 전송 시점
- 약 5초마다 주기적
- **admin만 수신** (user는 수신 안 됨)

### JSON 형식 (Type `0x05`)

> [!WARNING]
> **포맷 변경됨**: 기존 플랫 구조 → `server` + `devices` 중첩 구조로 변경되었습니다.

```json
{
    "server": {
        "cpu": 23.5,
        "memory": 61.2,
        "temp": 45.0,
        "uptime": 86400
    },
    "devices": [
        {
            "ip": "192.168.0.43",
            "cpu": 15.2,
            "memory": 42.0,
            "temp": 55.0,
            "uptime": 3600
        }
    ]
}
```

#### `server` 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `cpu` | double | 메인 서버 CPU 사용률 (%) |
| `memory` | double | 메인 서버 메모리 사용률 (%) |
| `temp` | double | 메인 서버 CPU 온도 (°C) |
| `uptime` | long | 메인 서버 가동 시간 (초) |

#### `devices` 필드 (배열 — SubCam만 해당)

| 필드 | 타입 | 설명 |
|------|------|------|
| `ip` | string | SubCam IP 주소 |
| `cpu` | double | SubCam CPU 사용률 (%) |
| `memory` | double | SubCam 메모리 사용률 (%) |
| `temp` | double | SubCam CPU 온도 (°C) |
| `uptime` | long | SubCam 가동 시간 (초) |

> [!NOTE]
> `devices` 배열에는 **AVAILABLE을 보내는 SubCam**만 포함됩니다.
> 한화 카메라는 포함되지 않습니다.

---

## 9. IMAGE (이벤트 녹화 이미지, `0x0a`)

### 패킷 구조 (일반 메시지와 다름!)

```
[Type: 0x0a, 1바이트] [BodyLength: 4바이트, LE] [JSON 메타: BodyLength바이트] [JPEG 바이너리: jpeg_size바이트]
```

> [!CAUTION]
> **일반 패킷과 다르게** JSON 뒤에 JPEG 바이너리가 추가로 이어집니다.
> `BodyLength`는 JSON 길이만 나타내며, JPEG 크기는 JSON 안의 `jpeg_size` 필드에 있습니다.

### JSON 메타 데이터 필드

```json
{
    "device_id": "SubPi_192.168.0.43",
    "track_id": 7,
    "frame_index": 0,
    "total_frames": 300,
    "timestamp_ms": 1234,
    "jpeg_size": 42500
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `device_id` | string | SubCam 장치 ID |
| `track_id` | int | 추적 대상 ID |
| `frame_index` | int | 현재 프레임 번호 (0부터 시작) |
| `total_frames` | int | 전체 프레임 수 |
| `timestamp_ms` | int | 타임스탬프 (밀리초) |
| `jpeg_size` | int | 바로 뒤에 오는 JPEG 바이너리 크기 (바이트) |

---

## 10. Qt C++ 예제 코드

### 패킷 전송
```cpp
void sendPacket(QTcpSocket* socket, uint8_t type, const QJsonObject& body) {
    QByteArray jsonData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    uint8_t headerType = type;
    uint32_t bodyLen = static_cast<uint32_t>(jsonData.size());

    socket->write(reinterpret_cast<char*>(&headerType), 1);
    socket->write(reinterpret_cast<char*>(&bodyLen), 4);
    socket->write(jsonData);
    socket->flush();
}
```

### 패킷 수신
```cpp
void onReadyRead() {
    while (socket->bytesAvailable() >= 5) {
        uint8_t type;
        uint32_t bodyLenNet;
        socket->read(reinterpret_cast<char*>(&type), 1);
        socket->read(reinterpret_cast<char*>(&bodyLenNet), 4);
        uint32_t bodyLen = bodyLenNet;  // 리틀 엔디언 그대로 사용

        while (socket->bytesAvailable() < bodyLen)
            socket->waitForReadyRead(100);

        QByteArray jsonData = socket->read(bodyLen);

        switch (type) {
            case 0x02: /* SUCCESS */ break;
            case 0x03: /* FAIL */    break;
            case 0x05: /* AVAILABLE (admin) */ break;
            case 0x07: /* CAMERA */  break;
            case 0x0a: /* IMAGE — JPEG 추가 수신 필요! */ break;
        }
    }
}
```

### IMAGE 수신 예시
```cpp
case 0x0a: { // IMAGE
    uint32_t jpegSize = body["jpeg_size"].toInt();

    while (socket->bytesAvailable() < jpegSize)
        socket->waitForReadyRead(100);

    QByteArray jpegData = socket->read(jpegSize);

    QImage image;
    image.loadFromData(jpegData, "JPEG");
    break;
}
```

### 로그인 호출
```cpp
QJsonObject body;
body["id"] = "user_alpha";
body["pw"] = "pass_user_alpha";
sendPacket(socket, 0x01, body);
```

### 회원가입 호출
```cpp
QJsonObject body;
body["action"] = "register";
body["id"] = "new_user";
body["email"] = "new@example.com";
body["pw"] = "password123";
sendPacket(socket, 0x08, body);
```

### AVAILABLE 파싱 (admin)
```cpp
case 0x05: {
    QJsonObject available = QJsonDocument::fromJson(jsonData).object();

    // 서버 상태
    QJsonObject server = available["server"].toObject();
    double serverCpu = server["cpu"].toDouble();
    double serverMem = server["memory"].toDouble();
    double serverTemp = server["temp"].toDouble();
    long serverUptime = server["uptime"].toInt();

    // 디바이스 상태 (SubCam 목록)
    QJsonArray devices = available["devices"].toArray();
    for (const QJsonValue& val : devices) {
        QJsonObject dev = val.toObject();
        QString ip = dev["ip"].toString();
        double devCpu = dev["cpu"].toDouble();
        double devMem = dev["memory"].toDouble();
        double devTemp = dev["temp"].toDouble();
        long devUptime = dev["uptime"].toInt();
    }
    break;
}
```
