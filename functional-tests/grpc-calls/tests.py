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
    test_node = None
    test_action = None
    return {'stub': stub}

def test_add_root_node(gd):
    node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name='first')
    req = nextapp_pb2.CreateNodeReq(node=node)
    status = gd['stub'].CreateNode(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.node.name == 'first'
    gd['test_node'] = status.node.uuid

def test_add_child_node(gd):
    node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name='second')
    req = nextapp_pb2.CreateNodeReq(node=node)
    status = gd['stub'].CreateNode(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.node.name == 'second'

    parent = status.node.uuid
    assert parent != ""
    print ("parent is {}".format(parent))
    node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name='child-of-second', parent=parent)
    req = nextapp_pb2.CreateNodeReq(node=node)
    status = gd['stub'].CreateNode(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.node.name == 'child-of-second'
    assert status.node.parent == parent

def test_add_child_tree(gd):
    name = "third"
    node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name=name)
    req = nextapp_pb2.CreateNodeReq(node=node)
    status = gd['stub'].CreateNode(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.node.name == name
    parent = status.node.uuid
    assert parent != ""

    for i in range(20):
        name = "third-{}".format(i)
        node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name=name, parent=parent)
        req = nextapp_pb2.CreateNodeReq(node=node)
        status = gd['stub'].CreateNode(req)
        assert status.error == nextapp_pb2.Error.OK
        assert status.node.name == name
        assert status.node.parent == parent

        iiparent = status.node.uuid
        for ii in range(8):
            name = "third-{}-{}".format(i, ii)
            node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name=name, parent=iiparent)
            req = nextapp_pb2.CreateNodeReq(node=node)
            status = gd['stub'].CreateNode(req)
            assert status.error == nextapp_pb2.Error.OK
            assert status.node.name == name
            assert status.node.parent == iiparent


def test_add_tenant(gd):
    template = nextapp_pb2.Tenant(kind=nextapp_pb2.Tenant.Kind.Regular, name='dogs')
    req = nextapp_pb2.CreateTenantReq(tenant=template)
    status = gd['stub'].CreateTenant(req)
    assert status.error == nextapp_pb2.Error.OK


def test_add_tenant_with_user(gd):
    template = nextapp_pb2.Tenant(kind=nextapp_pb2.Tenant.Kind.Regular, name='cats')
    req = nextapp_pb2.CreateTenantReq(tenant=template)
    req.users.extend([nextapp_pb2.User(kind=nextapp_pb2.User.Kind.Regular, name='kitty', email='kitty@example.com')])

    status = gd['stub'].CreateTenant(req)
    assert status.error == nextapp_pb2.Error.OK

    # Todo. Fetch the user to validate

def test_get_nodes(gd):
    req = nextapp_pb2.GetNodesReq()
    nodes = gd['stub'].GetNodes(req)

def test_add_action(gd):
    assert gd['test_node'] != None

    req = nextapp_pb2.Action(name='TestAction', node=gd['test_node'], priority=nextapp_pb2.ActionPriority.PRI_NORMAL, difficulty=nextapp_pb2.ActionDifficulty.NORMAL)
    status = gd['stub'].CreateAction(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.action.name == 'TestAction'
    assert status.action.node == gd['test_node']
    assert status.action.status == nextapp_pb2.ActionStatus.ACTIVE
    gd['test_action'] = status.action.id

def test_add_work(gd):
    req = nextapp_pb2.CreateWorkReq(actionId=gd['test_action'])
    status = gd['stub'].CreateWorkSession(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.action == gd['test_action']
    assert status.work.state == nextapp_pb2.WorkSession.State.ACTIVE
    work_id = status.work.id

    # End the work
    req = nextapp_pb2.WorkEvent(session=work_id, kind=nextapp_pb2.WorkEvent.Kind.STOP)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE
    
    # # Pause the work
    # req = nextapp_pb2.WorkEvent(session=work_id, kind=nextapp_pb2.WorkEvent.Kind.PAUSE)
    # status = gd['stub'].WorkEvent(req)
    # assert status.error == nextapp_pb2.Error.OK
    # assert status.work.state == nextapp_pb2.WorkSession.State.PAUSED