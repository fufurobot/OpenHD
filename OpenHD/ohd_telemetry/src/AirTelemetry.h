//
// Created by consti10 on 13.04.22.
//

#ifndef OPENHD_TELEMETRY_AIRTELEMETRY_H
#define OPENHD_TELEMETRY_AIRTELEMETRY_H

#include "endpoints/SerialEndpoint.h"
#include "endpoints/UDPEndpoint.h"
#include "internal/InternalTelemetry.h"
#include "openhd-platform.hpp"
#include <string>

/**
 * OpenHD Air telemetry. Assumes a Ground instance running on the ground pi.
 */
class AirTelemetry {
 public:
  explicit AirTelemetry(std::string fcSerialPort);
  /**
   * Telemetry will run infinite in its own threads until an error occurs.
   * @param enableExtendedLogging be really verbose on logging.
   */
  void loopInfinite(const bool enableExtendedLogging = false);
 private:
  // send a mavlink message to the flight controller connected to the air unit via UART
  void sendMessageFC(MavlinkMessage &message);
  // called every time a message from the flight controller bus is received
  void onMessageFC(MavlinkMessage &message);
  // send a message to the ground pi
  void sendMessageGroundPi(MavlinkMessage &message);
  // called every time a message from the ground pi is received
  void onMessageGroundPi(MavlinkMessage &message);
 private:
  static constexpr auto M_SYS_ID = OHD_SYS_ID_AIR;
  std::unique_ptr<SerialEndpoint> serialEndpoint;
  // For now, use UDP endpoint and rely on another service for starting the rx/tx links
  //std::unique_ptr<WBEndpoint> wifibroadcastEndpoint;
  std::unique_ptr<UDPEndpoint> wifibroadcastEndpoint;
  InternalTelemetry ohdTelemetryGenerator{true};
};

#endif //OPENHD_TELEMETRY_AIRTELEMETRY_H