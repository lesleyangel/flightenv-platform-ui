#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rclcpp {
class Node;
}

namespace flightenv::ui::demo {

class RosJsonSubscriber final {
public:
    using Callback = std::function<void(const std::string& topic, const std::string& payload)>;

    explicit RosJsonSubscriber(const std::string& node_name);
    ~RosJsonSubscriber();

    RosJsonSubscriber(const RosJsonSubscriber&) = delete;
    RosJsonSubscriber& operator=(const RosJsonSubscriber&) = delete;

    bool ok() const;
    std::string error() const;
    void subscribe(const std::string& topic, Callback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flightenv::ui::demo
