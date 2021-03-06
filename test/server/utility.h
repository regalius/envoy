#pragma once

#include <string>

#include "envoy/api/v2/lds.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace {

inline envoy::api::v2::Listener parseListenerFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::Listener listener;
  MessageUtil::loadFromYaml(yaml, listener);
  return listener;
}

} // namespace
} // namespace Server
} // namespace Envoy
