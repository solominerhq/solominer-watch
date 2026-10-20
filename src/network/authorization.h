#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
namespace network {
enum class AuthReply { Unrelated, Accepted, Rejected };
inline AuthReply authorizationReply(const char* line, uint32_t requestId) {
  StaticJsonDocument<1024> reply;
  if (deserializeJson(reply, line)) return AuthReply::Unrelated;
  if (!requestId || !reply["id"].is<uint32_t>() || reply["id"].as<uint32_t>() != requestId)
    return AuthReply::Unrelated;
  return reply["error"].isNull() && reply["result"].is<bool>() && reply["result"].as<bool>()
      ? AuthReply::Accepted : AuthReply::Rejected;
}
}
