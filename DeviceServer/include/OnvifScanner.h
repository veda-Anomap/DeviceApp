#ifndef ONVIF_SCANNER_H
#define ONVIF_SCANNER_H

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "Common.h"

class OnvifScanner {
public:
  OnvifScanner();
  ~OnvifScanner();

  void start(std::atomic<bool> &is_running);
  void stop();

  // 콜백 설정 (DeviceManager가 호출)
  using DeviceFoundCallback = std::function<void(const DeviceInfo &)>;
  void setOnDeviceFound(DeviceFoundCallback cb) { on_device_found_ = cb; }

  // 중복 체크 콜백 (curl 호출 전에 확인)
  using IsRegisteredCallback =
      std::function<bool(const std::string &device_id)>;
  void setIsDeviceRegistered(IsRegisteredCallback cb) { is_registered_ = cb; }

  // SUNAPI 프로필 번호 설정 (기본값: 2)
  void setProfile(int profile) { sunapi_profile_ = profile; }
  int getProfile() const { return sunapi_profile_; }

private:
  void runScan();
  std::vector<std::string> getRtspUrls(const std::string &ip);
  std::string getSingleRtspUrl(const std::string &ip);

  // ARP 폴백 스캔 (IGMP Snooping 환경 대응)
  void arpFallbackScan();
  std::vector<std::string> getHanwhaIpsFromArp();

  std::atomic<bool> *is_running_ = nullptr;
  std::thread scan_thread_;

  DeviceFoundCallback on_device_found_;
  IsRegisteredCallback is_registered_;

  // SUNAPI 프로필 번호 (1=고화질, 2=중화질, 3=저화질 등)
  int sunapi_profile_ = 5;

  // SUNAPI 실패 IP 카운터 (1회 실패 시 재시도 차단)
  std::map<std::string, int> sunapi_fail_count_;
};

#endif
