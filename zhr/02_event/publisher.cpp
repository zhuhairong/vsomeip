#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <thread>
#include <chrono>

static const vsomeip::service_t      SERVICE_ID      = 0x1112;
static const vsomeip::instance_t     INSTANCE_ID     = 0x2222;
static const vsomeip::event_t        EVENT_ID        = 0x8001;
static const vsomeip::eventgroup_t   EVENTGROUP_ID   = 0x7001;

std::shared_ptr<vsomeip::application> g_app;
bool g_running = true;

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        std::set<vsomeip::eventgroup_t> groups{EVENTGROUP_ID};
        g_app->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                           vsomeip::event_type_e::ET_EVENT,
                           std::chrono::milliseconds::zero(),
                           false, true, nullptr,
                           vsomeip::reliability_type_e::RT_UNKNOWN);
        g_app->offer_service(SERVICE_ID, INSTANCE_ID);
        std::cout << "[PUBLISHER] Offered event service" << std::endl;
    }
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("publisher");
    g_app->init();
    g_app->register_state_handler(on_state);

    std::thread notify_thread([]{
        uint32_t counter = 0;
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!g_running) break;
            std::vector<vsomeip::byte_t> data(
                (vsomeip::byte_t*)&counter,
                (vsomeip::byte_t*)&counter + sizeof(counter));
            auto pl = vsomeip::runtime::get()->create_payload();
            pl->set_data(data);
            g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_ID, pl);
            std::cout << "[PUBLISHER] Notified counter = " << counter << std::endl;
            counter++;
        }
    });

    g_app->start();
    g_running = false;
    if (notify_thread.joinable()) notify_thread.join();
    return 0;
}
