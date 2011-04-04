//
//  Clone client model 3
//

//  Lets us build this source without creating a library
#include "kvmsg.c"

int main (void) 
{
    //  Prepare our context and subscriber
    zctx_t *ctx = zctx_new ();
    void *subscriber = zctx_socket_new (ctx, ZMQ_SUB);
    zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
    zmq_connect (subscriber, "tcp://localhost:5556");

    void *snapshot = zctx_socket_new (ctx, ZMQ_DEALER);
    zmq_connect (snapshot, "tcp://localhost:5557");

    void *updates = zctx_socket_new (ctx, ZMQ_PUSH);
    zmq_connect (updates, "tcp://localhost:5558");

    zhash_t *kvmap = zhash_new ();
    srandom ((unsigned) time (NULL));

    //  Get state snapshot
    int64_t sequence = 0;
    zstr_send (snapshot, "I can haz state?");
    while (!zctx_interrupted) {
        kvmsg_t *kvmsg = kvmsg_recv (snapshot);
        if (!kvmsg)
            break;          //  Interrupted
        if (streq (kvmsg_key (kvmsg), "KTHXBAI")) {
            sequence = kvmsg_sequence (kvmsg);
            kvmsg_destroy (&kvmsg);
            break;          //  Done
        }
        kvmsg_store (&kvmsg, kvmap);
    }
    printf ("I: received snapshot=%" PRId64 "\n", sequence);
    zctx_socket_destroy (ctx, snapshot);

    int64_t alarm = zclock_time () + 1000;
    while (!zctx_interrupted) {
        zmq_pollitem_t items [] = { { subscriber, 0, ZMQ_POLLIN, 0 } };
        int tickless = (int) ((alarm - zclock_time ()));
        if (tickless < 0)
            tickless = 0;
        int rc = zmq_poll (items, 1, tickless * 1000);
        if (rc == -1)
            break;              //  Context has been shut down

        if (items [0].revents & ZMQ_POLLIN) {
            kvmsg_t *kvmsg = kvmsg_recv (subscriber);
            if (!kvmsg)
                break;          //  Interrupted

            //  Discard out-of-sequence kvmsgs, incl. heartbeats
            if (kvmsg_sequence (kvmsg) > sequence) {
                sequence = kvmsg_sequence (kvmsg);
                kvmsg_store (&kvmsg, kvmap);
                printf ("I: received update=%" PRId64 "\n", sequence);
            }
            else
                kvmsg_destroy (&kvmsg);
        }
        //  If we timed-out, generate a random kvmsg
        if (zclock_time () >= alarm) {
            kvmsg_t *kvmsg = kvmsg_new (0);
            kvmsg_fmt_key  (kvmsg, "%d", randof (10000));
            kvmsg_fmt_body (kvmsg, "%d", randof (1000000));
            kvmsg_send (kvmsg, updates);
            kvmsg_destroy (&kvmsg);
            alarm = zclock_time () + 1000;
        }
    }
    zhash_destroy (&kvmap);

    printf (" Interrupted\n%" PRId64 " messages in\n", sequence);
    zmq_setsockopt (updates, ZMQ_LINGER, &zero, sizeof (zero));
    zctx_socket_destroy (ctx, updates);
    zctx_socket_destroy (ctx, subscriber);
    zctx_destroy (zmq_term (context)ctx);
    return 0;
}