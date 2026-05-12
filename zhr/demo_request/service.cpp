#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <string>

static const vsomeip::service_t   SERVICE_ID   = 0x1111;
static const vsomeip::instance_t  INSTANCE_ID  = 0x2222;
static const vsomeip::method_t    METHOD_ID    = 0x3333;

std::shared_ptr<vsomeip::application> g_app;

void on_message(const std::shared_ptr<vsomeip::message> &req) {
    // Read request payload
    auto len = req->get_payload()->get_length();
    std::string msg((const char*)req->get_payload()->get_data(), len);
    std::cout << "[SERVICE] Received: " << msg << std::endl;

    // Build response "Hello <msg>"
    std::string resp_str = "Hello " + msg;
    auto resp = vsomeip::runtime::get()->create_response(req);
    auto pl = vsomeip::runtime::get()->create_payload();
    pl->set_data(std::vector<vsomeip::byte_t>(resp_str.begin(), resp_str.end()));
    resp->set_payload(pl);
    g_app->send(resp);
    std::cout << "[SERVICE] Sent: " << resp_str << std::endl;
}

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        g_app->offer_service(SERVICE_ID, INSTANCE_ID);
        std::cout << "[SERVICE] Offered" << std::endl;
    }
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("demo_service");
    g_app->init();
    g_app->register_state_handler(on_state);
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_ID, on_message);
    g_app->start();
    return 0;
}
