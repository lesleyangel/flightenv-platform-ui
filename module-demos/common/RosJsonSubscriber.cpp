#include "RosJsonSubscriber.h"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <atomic>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace flightenv::ui::demo {

struct RosJsonSubscriber::Impl {
    std::shared_ptr<rclcpp::Node> node;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscriptions;
    std::thread spin_thread;
    std::atomic_bool running{false};
    std::string error;
    mutable std::mutex mutex;
};

RosJsonSubscriber::RosJsonSubscriber(const std::string& node_name)
    : impl_(std::make_unique<Impl>())
{
    try {
        if (!rclcpp::ok()) {
            impl_->error = "rclcpp is not initialized";
            return;
        }

        impl_->node = std::make_shared<rclcpp::Node>(node_name);
        impl_->executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
        impl_->executor->add_node(impl_->node);
        impl_->running.store(true, std::memory_order_release);
        impl_->spin_thread = std::thread([raw = impl_.get()]() {
            try {
                raw->executor->spin();
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lock(raw->mutex);
                raw->error = ex.what();
            } catch (...) {
                std::lock_guard<std::mutex> lock(raw->mutex);
                raw->error = "unknown ROS spin exception";
            }
            raw->running.store(false, std::memory_order_release);
        });
    } catch (const std::exception& ex) {
        impl_->error = ex.what();
    }
}

RosJsonSubscriber::~RosJsonSubscriber()
{
    if (!impl_) {
        return;
    }
    try {
        if (impl_->executor) {
            impl_->executor->cancel();
        }
        if (impl_->spin_thread.joinable()) {
            impl_->spin_thread.join();
        }
        if (impl_->executor && impl_->node) {
            impl_->executor->remove_node(impl_->node);
        }
    } catch (...) {
    }
}

bool RosJsonSubscriber::ok() const
{
    return impl_ && impl_->node && impl_->executor && impl_->error.empty();
}

std::string RosJsonSubscriber::error() const
{
    if (!impl_) {
        return "subscriber not constructed";
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

void RosJsonSubscriber::subscribe(const std::string& topic, Callback callback)
{
    if (!ok() || topic.empty() || !callback) {
        return;
    }

    auto subscription = impl_->node->create_subscription<std_msgs::msg::String>(
        topic,
        rclcpp::QoS(10),
        [topic, callback = std::move(callback)](const std_msgs::msg::String::SharedPtr message) {
            callback(topic, message->data);
        });
    impl_->subscriptions.push_back(std::move(subscription));
}

} // namespace flightenv::ui::demo
