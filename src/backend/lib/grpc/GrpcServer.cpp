
#include "nextapp/GrpcServer.h"

using namespace std;
using namespace std::literals;

namespace nextapp::grpc {

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetServerInfo(::grpc::CallbackServerContext *ctx,
                                       const pb::Empty *,
                                       pb::ServerInfo *reply)
{
    assert(ctx);
    assert(reply);

    auto add = [&reply](string key, string value) {
        auto prop = reply->mutable_properties()->Add();
        prop->set_key(key);
        prop->set_value(value);
    };

    add("version", NEXTAPP_VERSION);

    auto* reactor = ctx->DefaultReactor();
    reactor->Finish(::grpc::Status::OK);
    return reactor;
}

GrpcServer::GrpcServer(Server &server)
    : server_{server}
{

}

void GrpcServer::start() {
    ::grpc::ServerBuilder builder;

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
    builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<NextappImpl>(*this);
    builder.RegisterService(service_.get());

    // Finally assemble the server.
    grpc_server_ = builder.BuildAndStart();
    LOG_INFO
        // Fancy way to print the class-name.
        // Useful when I copy/paste this code around ;)
        << boost::typeindex::type_id_runtime(*this).pretty_name()

        // The useful information
        << " listening on " << config().address;
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down "
             << boost::typeindex::type_id_runtime(*this).pretty_name();
    grpc_server_->Shutdown();
    grpc_server_->Wait();
}



} // ns
