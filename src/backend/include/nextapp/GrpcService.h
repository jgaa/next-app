#pragma once

#include <queue>

#include <grpcpp/server.h>

#include "nextapp/Server.h"
#include "nextapp/nextapp.h"
#include "nextapp/config.h"
#include "nextapp.pb.h"

namespace nextapp::grpc {

class GrpcServer {
public:
    GrpcServer(Server& server);



};

} // ns
