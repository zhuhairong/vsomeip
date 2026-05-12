#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <string>

static const vsomeip::service_t   SERVICE_ID   = 0x1111;
static const vsomeip::instance_t  INSTANCE_ID  = 0x2222;
static const vsomeip::method_t    METHOD_ID    = 0x3333;

std::shared_ptr<vsomeip::application> g_app;

void on_message(const std::shared_ptr<vsomeip::message> &resp) {
    if (resp->get_service()     == SERVICE_ID  &&
        resp->get_instance()    == INSTANCE_ID &&
        resp->get_message_type() == vsomeip::message_type_e::MT_RESPONSE &&
        resp->get_return_code()  == vsomeip::return_code_e::E_OK) {
        auto len = resp->get_payload()->get_length();
        std::string msg((const char*)resp->get_payload()->get_data(), len);
        std::cout << "[CLIENT] Received: " << msg << std::endl;
        g_app->stop();
    }
}

void on_availability(vsomeip::service_t service, vsomeip::instance_t instance, bool available) {
    if (service == SERVICE_ID && instance == INSTANCE_ID && available) {
        std::cout << "[CLIENT] Service available, sending request..." << std::endl;
        auto req = vsomeip::runtime::get()->create_request();
        req->set_service(SERVICE_ID);
        req->set_instance(INSTANCE_ID);
        req->set_method(METHOD_ID);
        std::string payload_str = "World";
        auto pl = vsomeip::runtime::get()->create_payload();
        pl->set_data(std::vector<vsomeip::byte_t>(payload_str.begin(), payload_str.end()));
        req->set_payload(pl);
        g_app->send(req);
        std::cout << "[CLIENT] Sent: " << payload_str << std::endl;
    }
}

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        g_app->request_service(SERVICE_ID, INSTANCE_ID);
    }
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("demo_client");
    g_app->init();
    g_app->register_state_handler(on_state);
    g_app->register_message_handler(vsomeip::ANY_SERVICE, INSTANCE_ID, vsomeip::ANY_METHOD, on_message);
    g_app->register_availability_handler(SERVICE_ID, INSTANCE_ID, on_availability);
    g_app->start();
    return 0;
}
