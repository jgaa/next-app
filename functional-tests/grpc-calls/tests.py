import json
import pytest
import time
from datetime import datetime, timedelta
import os
import random

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
    event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
    req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE
    
def test_activate_next_work_session(gd):
    req = nextapp_pb2.CreateWorkReq(actionId=gd['test_action'])
    status = gd['stub'].CreateWorkSession(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.action == gd['test_action']
    assert status.work.state == nextapp_pb2.WorkSession.State.ACTIVE
    work_id = status.work.id

    req = nextapp_pb2.CreateWorkReq(actionId=gd['test_action'])
    status = gd['stub'].CreateWorkSession(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.action == gd['test_action']
    assert status.work.state == nextapp_pb2.WorkSession.State.ACTIVE
    work_id2 = status.work.id

    req = nextapp_pb2.Empty()
    status = gd['stub'].ListCurrentWorkSessions(req)
    assert status.error == nextapp_pb2.Error.OK
    assert len(status.workSessions.sessions) == 2
    assert status.workSessions.sessions[0].id == work_id2 
    assert status.workSessions.sessions[0].state == nextapp_pb2.WorkSession.State.ACTIVE
    assert status.workSessions.sessions[1].id == work_id 
    assert status.workSessions.sessions[1].state == nextapp_pb2.WorkSession.State.PAUSED

    # End the work
    event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
    req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE

    event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
    req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id2, event=event)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE

    req = nextapp_pb2.Empty()
    status = gd['stub'].ListCurrentWorkSessions(req)
    assert status.error == nextapp_pb2.Error.OK
    assert len(status.workSessions.sessions) == 0
    
def test_stopped_work_activates_most_recent_paused_work_session(gd):
    req = nextapp_pb2.CreateWorkReq(actionId=gd['test_action'])
    status = gd['stub'].CreateWorkSession(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.action == gd['test_action']
    assert status.work.state == nextapp_pb2.WorkSession.State.ACTIVE
    work_id = status.work.id

    req = nextapp_pb2.CreateWorkReq(actionId=gd['test_action'])
    status = gd['stub'].CreateWorkSession(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.action == gd['test_action']
    assert status.work.state == nextapp_pb2.WorkSession.State.ACTIVE
    work_id2 = status.work.id

    req = nextapp_pb2.Empty()
    status = gd['stub'].ListCurrentWorkSessions(req)
    assert status.error == nextapp_pb2.Error.OK
    assert len(status.workSessions.sessions) == 2
    assert status.workSessions.sessions[0].id == work_id2 
    assert status.workSessions.sessions[0].state == nextapp_pb2.WorkSession.State.ACTIVE
    assert status.workSessions.sessions[1].id == work_id 
    assert status.workSessions.sessions[1].state == nextapp_pb2.WorkSession.State.PAUSED

    # End the work
    event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
    req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id2, event=event)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE

    req = nextapp_pb2.Empty()
    status = gd['stub'].ListCurrentWorkSessions(req)
    assert status.error == nextapp_pb2.Error.OK
    assert len(status.workSessions.sessions) == 1
    assert status.workSessions.sessions[0].id == work_id 
    assert status.workSessions.sessions[0].state == nextapp_pb2.WorkSession.State.ACTIVE

    event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
    req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event)
    status = gd['stub'].AddWorkEvent(req)
    assert status.error == nextapp_pb2.Error.OK
    assert status.work.state == nextapp_pb2.WorkSession.State.DONE

def test_add_many_work_sessions(gd):
    parent_node_id = ""
    start_time = datetime.now() - timedelta(days=10)

    for n in range(5):
        # create nodes
        node = nextapp_pb2.Node(kind=nextapp_pb2.Node.Kind.FOLDER, name='work-node {}'.format(n), parent=parent_node_id)
        req = nextapp_pb2.CreateNodeReq(node=node)
        status = gd['stub'].CreateNode(req)
        assert status.error == nextapp_pb2.Error.OK
        parent_node_id = status.node.uuid
        node_id = status.node.uuid

        for a in range(10):
            # create actions
            req = nextapp_pb2.Action(name='TestAction-{}-{}'.format(n, a), 
                                     node=node_id, priority=nextapp_pb2.ActionPriority.PRI_NORMAL, 
                                     difficulty=nextapp_pb2.ActionDifficulty.NORMAL)
            status = gd['stub'].CreateAction(req)
            assert status.error == nextapp_pb2.Error.OK
            action_id = status.action.id

            for w in range((a + 1) * 5):
                # create work sessions
                req = nextapp_pb2.CreateWorkReq(actionId=action_id)
                status = gd['stub'].CreateWorkSession(req)
                assert status.error == nextapp_pb2.Error.OK
                work_id = status.work.id

                # create an work-event to correct the start time
                event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.CORRECTION, start=int(time.mktime(start_time.timetuple())))
                req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event)
                status = gd['stub'].AddWorkEvent(req)
                assert status.error == nextapp_pb2.Error.OK

                # create an work-event to stop the work
                event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.STOP)
                req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event)
                status = gd['stub'].AddWorkEvent(req)
                assert status.error == nextapp_pb2.Error.OK

                # Create a work-event to correct the end time
                end_time = start_time + timedelta(seconds = random.randint(60, 2400))
                event = nextapp_pb2.WorkEvent(kind=nextapp_pb2.WorkEvent.Kind.CORRECTION, end=int(time.mktime(end_time.timetuple())))
                req = nextapp_pb2.AddWorkEventReq(workSessionId=work_id, event=event) 
                status = gd['stub'].AddWorkEvent(req)
                assert status.error == nextapp_pb2.Error.OK

                # set start-time at a random number of seconds into the future
                start_time = end_time + timedelta(seconds = random.randint(60, 1000))
    