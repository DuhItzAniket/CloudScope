// The event bus: parts of the program announce what happened, others listen, and neither knows the other.
//
//   struct CameraDisconnected { std::string camera_id; };
//
//   bus.publish(CameraDisconnected{"B0268"});
//   Connection c = bus.subscribe<CameraDisconnected>([](const CameraDisconnected& event) { ... });
//   Connection ui = bus.subscribe<CameraDisconnected>(ui_executor, [](const CameraDisconnected& event) { ... });
//
// An event is any copyable type; its type is the topic. Events are for things that happened (state changes,
// faults, telemetry samples), not for commands and not for bulk data: frames travel through FrameHub.
//
// Thread rules are those of Signal (signal.hpp): publish and subscribe from any thread; a handler subscribed
// without an executor runs on the publishing thread and must be quick.
#pragma once

#include "cloudscope/common/signal.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace cloudscope {

class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    template <class Event>
    Connection subscribe(std::function<void(const Event&)> handler)
    {
        return signal_for<Event>()->connect(std::move(handler));
    }

    template <class Event>
    Connection subscribe(IExecutor& executor, std::function<void(const Event&)> handler)
    {
        return signal_for<Event>()->connect(executor, std::move(handler));
    }

    template <class Event>
    void publish(const Event& event)
    {
        // Publishing an event nobody has ever subscribed to costs one map lookup and nothing else.
        if (const std::shared_ptr<Signal<Event>> signal = find_signal<Event>()) {
            signal->emit(event);
        }
    }

    // Number of handlers currently subscribed to an event type.
    template <class Event>
    [[nodiscard]] std::size_t subscriber_count() const
    {
        const std::shared_ptr<Signal<Event>> signal = find_signal<Event>();
        return signal ? signal->connection_count() : 0;
    }

private:
    template <class Event>
    std::shared_ptr<Signal<Event>> find_signal() const
    {
        const std::lock_guard lock(mutex_);
        const auto found = signals_.find(std::type_index(typeid(Event)));
        return found == signals_.end() ? nullptr : std::static_pointer_cast<Signal<Event>>(found->second);
    }

    template <class Event>
    std::shared_ptr<Signal<Event>> signal_for()
    {
        const std::lock_guard lock(mutex_);
        std::shared_ptr<void>& entry = signals_[std::type_index(typeid(Event))];
        if (!entry) {
            entry = std::make_shared<Signal<Event>>();
        }
        return std::static_pointer_cast<Signal<Event>>(entry);
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> signals_;
};

}  // namespace cloudscope
