import json
import pytest
# import time
import os
# import uuid

import grpc
import nextapp_pb2
import nextapp_pb2_grpc

@pytest.fixture(scope = 'module')
def gd():
    channel = grpc.insecure_channel(os.getenv('NA_GRPC', '127.0.0.1:10321'))
    stub = nextapp_pb2_grpc.NextappStub(channel)
    return {'stub': stub}


def test_add_tenant(gd):
    template = nextapp_pb2.Tenant(kind=nextapp_pb2.Tenant.Kind.regular, name='dogs')
    req = nextapp_pb2.CreateTenantReq(tenant=template)
    status = gd['stub'].CreateTenant(req)
    assert status.error == nextapp_pb2.Error.OK


def test_add_tenant_with_user(gd):
    template = nextapp_pb2.Tenant(kind=nextapp_pb2.Tenant.Kind.regular, name='cats')
    req = nextapp_pb2.CreateTenantReq(tenant=template)
    req.users.extend([nextapp_pb2.User(kind=nextapp_pb2.User.Kind.regular, name='kitty', email='kitty@example.com')])

    status = gd['stub'].CreateTenant(req)
    assert status.error == nextapp_pb2.Error.OK

    # Todo. Fetch the user to validate




