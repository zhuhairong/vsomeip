#include <vsomeip/vsomeip.hpp>
#include <iostream>

static const vsomeip::service_t      SERVICE_ID      = 0x1113;
static const vsomeip::instance_t     INSTANCE_ID     = 0x2222;
static const vsomeip::event_t        EVENT_ID        = 0x8002;
static const vsomeip::eventgroup_t   EVENTGROUP_ID   = 0x7002;
static const vsomeip::method_t       GET_METHOD_ID   = 0x0001;
static const vsomeip::method_t       SET_METHOD_ID   = 0x0002;

std::shared_ptr<vsomeip::application> g_app;

void on_message(const std::shared_ptr<vsomeip::message>& msg) {
    auto pl = msg->get_payload();
    std::string val((const char*)pl->get_data(), pl->get_length());

    if (msg->get_method() == EVENT_ID && msg->get_client() == 0) {
        std::cout << "[FIELD_CLIENT] Event notification: \"" << val << "\"" << std::endl;
        return;  // notification from subscribe, handled separately
    }

    if (msg->get_message_type() == vsomeip::message_type_e::MT_RESPONSE &&
        msg->get_return_code() == vsomeip::return_code_e::E_OK) {
        if (msg->get_method() == GET_METHOD_ID) {
            std::cout << "[FIELD_CLIENT] GET response: \"" << val << "\"" << std::endl;
            // After GET, send SET
            std::string new_val = "Hello vsomeip!";
            auto req = vsomeip::runtime::get()->create_request();
            req->set_service(SERVICE_ID);
            req->set_instance(INSTANCE_ID);
            req->set_method(SET_METHOD_ID);
            auto pl2 = vsomeip::runtime::get()->create_payload();
            pl2->set_data(std::vector<vsomeip::byte_t>(new_val.begin(), new_val.end()));
            req->set_payload(pl2);
            g_app->send(req);
            std::cout << "[FIELD_CLIENT] SET -> \"" << new_val << "\"" << std::endl;
        } else if (msg->get_method() == SET_METHOD_ID) {
            std::cout << "[FIELD_CLIENT] SET response: \"" << val << "\"" << std::endl;
            g_app->stop();
        }
    }
}

void on_availability(vsomeip::service_t svc, vsomeip::instance_t inst, bool avail) {
    if (svc == SERVICE_ID && inst == INSTANCE_ID && avail) {
        std::cout << "[FIELD_CLIENT] Service available" << std::endl;
        std::set<vsomeip::eventgroup_t> groups{EVENTGROUP_ID};
        g_app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                             vsomeip::event_type_e::ET_FIELD);
        g_app->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);

        // Send GET immediately
        auto req = vsomeip::runtime::get()->create_request();
        req->set_service(SERVICE_ID);
        req->set_instance(INSTANCE_ID);
        req->set_method(GET_METHOD_ID);
        g_app->send(req);
        std::cout << "[FIELD_CLIENT] GET -> initial value" << std::endl;
    }
}

void on_state(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED)
        g_app->request_service(SERVICE_ID, INSTANCE_ID);
}

int main() {
    g_app = vsomeip::runtime::get()->create_application("field_client");
    g_app->init();
    g_app->register_state_handler(on_state);
    g_app->register_message_handler(vsomeip::ANY_SERVICE, INSTANCE_ID,
                                    vsomeip::ANY_METHOD, on_message);
    g_app->register_availability_handler(SERVICE_ID, INSTANCE_ID, on_availability);
    g_app->start();
    // After app_->start() returns (stop is called elsewhere), exit
    return 0;
}
