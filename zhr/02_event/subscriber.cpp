#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <cstring>

static const vsomeip::service_t      SERVICE_ID      = 0x1112;
static const vsomeip::instance_t     INSTANCE_ID     = 0x2222;
static const vsomeip::event_t        EVENT_ID        = 0x8001;
static const vsomeip::eventgroup_t   EVENTGROUP_ID   = 0x7001;

std::shared_ptr<vsomeip::application> g_app;

void on_message(const std::shared_ptr<vsomeip::message>& msg) {
    if (msg->get_method() != EVENT_ID) return;
    auto pl = msg->get_payload();
    if (pl->get_length() < 4) return;
    uint32_t counter;
    std::memcpy(&counter, pl->get_data(), sizeof(counter));
    std::cout << "[SUBSCRIBER] Received event: counter = " << counter << std::endl;
}

void on_availability(vsomeip::service_t svc, vsomeip::instance_t inst, bool avail) {
    if (svc == SERVICE_ID && inst == INSTANCE_ID && avail) {
        std::cout << "[SUBSCRIBER] Service available, subscribing..." << std::endl;
        std::set<vsomeip::eventgroup_t> groups{EVENTGROUP_ID};
        g_app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                             vsomeip::event_type_e::ET_EVENT);
        g_app->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);
    }
}

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED)
        g_app->request_service(SERVICE_ID, INSTANCE_ID);
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("subscriber");
    g_app->init();
    g_app->register_state_handler(on_state);
    g_app->register_message_handler(vsomeip::ANY_SERVICE, INSTANCE_ID,
                                    vsomeip::ANY_METHOD, on_message);
    g_app->register_availability_handler(SERVICE_ID, INSTANCE_ID, on_availability);
    g_app->start();
    return 0;
}
