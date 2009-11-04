/* midiroute.c by Matthias Nagorni */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

static bool verbose;

static snd_seq_t *open_seq(void)
{
    snd_seq_t *seq;

    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq, "Channel Enforcer");
    return seq;
}

static int open_out_port(snd_seq_t *seq)
{
    int port = snd_seq_create_simple_port(
        seq, "OUT",
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }
    return port;
}

static int open_in_port(snd_seq_t *seq, int chan)
{
    char name[64];
    sprintf(name, "Channel %d", chan);
    int port = snd_seq_create_simple_port(
        seq, name,
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }
    return port;
}

void midi_route(snd_seq_t *seq_handle, int *chanmap, int out_port)
{
    snd_seq_event_t *ev;

    do {
        snd_seq_event_input(seq_handle, &ev);
        ev->data.control.channel = chanmap[ev->dest.port];
        snd_seq_ev_set_source(ev, out_port);
        snd_seq_ev_set_subs(ev);
        snd_seq_ev_set_direct(ev);
        if (snd_seq_event_output_direct(seq_handle, ev) < 0) {
            fprintf(stderr, "Could not write event.\n");
        }
        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
}

static void usage(void)
{
    fprintf(stderr, "usage: midichan [-v] -n nchan\n");
    fprintf(stderr, "       midichan [-v] -m c1,c2,c3,...,cn\n");
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose = true;
        memmove(argv + 1, argv + 2, sizeof argv[0] * (argc - 2));
        argc--;
    }

    if (argc != 3)
            usage();

    int nchan;
    int *chanmap;
    if (strcmp(argv[1], "-n") == 0) {
        nchan = atoi(argv[2]);
        if (nchan > 64)
            usage();
        chanmap = malloc(sizeof (int) * nchan);
        for (int i = 0; i < nchan; i++) {
            chanmap[i] = i;
        }
    } else if (strcmp(argv[1], "-m") == 0) {
        nchan = 1;
        char *c = argv[2];
        while ((c = strchr(c, ','))) {
            nchan++;
            c++;
        }
        if (nchan > 64)
            usage();
        chanmap = malloc(sizeof (int) * nchan);
        c = argv[2];
        for (int i = 0; i < nchan; i++) {
            chanmap[i] = atoi(c);
            c = strchr(c, ',') + 1;
        }
    } else {
        usage();
    }

    if (verbose) {
        printf("Channel map:\n");
        for (int i = 0; i < nchan; i++)
            printf("  port %2d -> channel %2d\n", i, chanmap[i]);
    }

    snd_seq_t *seq = open_seq();
    for (int i = 0; i < nchan; i++)
        open_in_port(seq, chanmap[i]);
    int out_port = open_out_port(seq);

    int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    struct pollfd pfd[npfd];
    snd_seq_poll_descriptors(seq, pfd, npfd, POLLIN);

    while (1) {
        if (poll(pfd, npfd, 100000) > 0) {
            midi_route(seq, chanmap, out_port);
        }
    }
}
