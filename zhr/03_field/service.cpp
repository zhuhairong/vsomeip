#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <mutex>

static const vsomeip::service_t      SERVICE_ID      = 0x1113;
static const vsomeip::instance_t     INSTANCE_ID     = 0x2222;
static const vsomeip::event_t        EVENT_ID        = 0x8002;
static const vsomeip::eventgroup_t   EVENTGROUP_ID   = 0x7002;
static const vsomeip::method_t       GET_METHOD_ID   = 0x0001;
static const vsomeip::method_t       SET_METHOD_ID   = 0x0002;

std::shared_ptr<vsomeip::application> g_app;
std::shared_ptr<vsomeip::payload> g_payload;
std::mutex g_mutex;

void on_get(const std::shared_ptr<vsomeip::message>& req) {
    auto resp = vsomeip::runtime::get()->create_response(req);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        resp->set_payload(g_payload);
    }
    g_app->send(resp);
    std::cout << "[FIELD_SERVICE] GET -> current value returned" << std::endl;
}

void on_set(const std::shared_ptr<vsomeip::message>& req) {
    auto resp = vsomeip::runtime::get()->create_response(req);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_payload = req->get_payload();
        resp->set_payload(g_payload);
    }
    g_app->send(resp);
    g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_ID, g_payload);
    std::cout << "[FIELD_SERVICE] SET -> notified new value" << std::endl;
}

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        std::set<vsomeip::eventgroup_t> groups{EVENTGROUP_ID};
        g_app->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                           vsomeip::event_type_e::ET_FIELD,
                           std::chrono::milliseconds::zero(),
                           false, true, nullptr,
                           vsomeip::reliability_type_e::RT_UNKNOWN);
        g_app->offer_service(SERVICE_ID, INSTANCE_ID);
        std::cout << "[FIELD_SERVICE] Offered field service" << std::endl;
    }
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("field_service");
    g_app->init();
    g_app->register_state_handler(on_state);
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, GET_METHOD_ID, on_get);
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, SET_METHOD_ID, on_set);

    // Initial field value: "Hello"
    std::string init = "Hello";
    g_payload = vsomeip::runtime::get()->create_payload();
    g_payload->set_data(std::vector<vsomeip::byte_t>(init.begin(), init.end()));

    g_app->start();
    return 0;
}
