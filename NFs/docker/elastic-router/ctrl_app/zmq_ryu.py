from eventlet.green import zmq
import eventlet

# https://github.com/eventlet/eventlet/tree/master/examples

CTX = zmq.Context(1)


def alarm_receiver():
    print("STARTING BOB, waiting for ALARM")
    bob = zmq.Socket(CTX, zmq.PULL)
    bob.bind("ipc:///tmp/alarm_trigger")

    while True:
        msg = bob.recv_multipart()
        print("received :", msg)

def alarm_sender():
    for i in range(0,10):
        msg = b'alarm{0}'.format(i)
        alice = zmq.Socket(CTX, zmq.PUSH)
        alice.connect("ipc:///tmp/alarm_trigger")
        alice.send_multipart([b'sub', b'alarms', msg])

        eventlet.sleep(1)


if __name__ == "__main__":

    sub_server = eventlet.spawn(alarm_receiver)

    #alice = zmq.Socket(CTX, zmq.REQ)
    #alice.connect("ipc:///tmp/alarm_subscribe")
    #alice.send_multipart([b'sub', b'alarms', b'all'])
    alarm_sender()



    #sub_server.wait()
    while True:
        eventlet.sleep(1)