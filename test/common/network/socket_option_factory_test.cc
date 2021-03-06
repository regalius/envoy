#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionFactoryTest : public testing::Test {
public:
  SocketOptionFactoryTest() = default;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{[this]() {
    // Before injecting OsSysCallsImpl, make sure validateIpv{4,6}Supported is called so the static
    // bool is initialized without requiring to mock ::socket and ::close. :( :(
    std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
    std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
    return &os_sys_calls_mock_;
  }()};

protected:
  testing::NiceMock<MockListenSocket> socket_mock_;
  Api::MockOsSysCalls os_sys_calls_mock_;

  void SetUp() override { socket_mock_.local_address_.reset(); }
  void makeSocketV4() {
    socket_mock_.local_address_ = std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
  }
  void makeSocketV6() {
    socket_mock_.local_address_ = std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
  }
};

#define CHECK_OPTION_SUPPORTED(option)                                                             \
  if (!option.has_value()) {                                                                       \
    return;                                                                                        \
  }

// TODO(klarose): Simplify these tests once https://github.com/envoyproxy/envoy/pull/5351 is merged.

TEST_F(SocketOptionFactoryTest, TestBuildSocketMarkOptions) {

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildSocketMarkOptions(100);

  const auto expected_option = ENVOY_SOCKET_SO_MARK;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .WillOnce(Invoke([type, option](int, int input_type, int input_option, const void* optval,
                                      socklen_t) -> int {
        EXPECT_EQ(100, *static_cast<const int*>(optval));
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv4TransparentOptions) {
  makeSocketV4();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IP_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int, int input_type, int input_option,
                                            const void* optval, socklen_t) -> int {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_BOUND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv6TransparentOptions) {
  makeSocketV6();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IPV6_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int, int input_type, int input_option,
                                            const void* optval, socklen_t) -> int {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_BOUND));
}

TEST_F(SocketOptionFactoryTest, TestBuildLiteralOptions) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::SocketOption> socket_options_proto;
  Envoy::Protobuf::TextFormat::Parser parser;
  envoy::api::v2::core::SocketOption socket_option_proto;
  static const char linger_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    buf_value: "\x01\x00\x00\x00\x80\x0d\x00\x00"
  )proto";
  auto linger_option = absl::StrFormat(linger_option_format, SOL_SOCKET, SO_LINGER);
  ASSERT_TRUE(parser.ParseFromString(linger_option, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;
  static const char keepalive_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
  )proto";
  auto keepalive_option = absl::StrFormat(keepalive_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(keepalive_option, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  auto socket_options = SocketOptionFactory::buildLiteralOptions(socket_options_proto);
  EXPECT_EQ(2, socket_options->size());
  auto option_details = socket_options->at(0)->getOptionDetails(
      socket_mock_, envoy::api::v2::core::SocketOption::STATE_PREBIND);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(SOL_SOCKET, option_details->name_->first);
  EXPECT_EQ(SO_LINGER, option_details->name_->second);
  EXPECT_EQ(sizeof(struct linger), option_details->value_.size());
  const struct linger* linger_ptr =
      reinterpret_cast<const struct linger*>(option_details->value_.data());
  EXPECT_EQ(1, linger_ptr->l_onoff);
  EXPECT_EQ(3456, linger_ptr->l_linger);

  option_details = socket_options->at(1)->getOptionDetails(
      socket_mock_, envoy::api::v2::core::SocketOption::STATE_PREBIND);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(SOL_SOCKET, option_details->name_->first);
  EXPECT_EQ(SO_KEEPALIVE, option_details->name_->second);
  EXPECT_EQ(sizeof(int), option_details->value_.size());
  const int* flag_ptr = reinterpret_cast<const int*>(option_details->value_.data());
  EXPECT_EQ(1, *flag_ptr);
}

} // namespace
} // namespace Network
} // namespace Envoy
