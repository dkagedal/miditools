/* midiroute.c by Matthias Nagorni */

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

static bool verbose;

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xrealloc(void *ptr, size_t sz)
{
    void *p = realloc(ptr, sz);
    if (!p)
        die("realloc failed");
    return p;
}

struct client {
    int id;
    const char *name;
};

struct client_vect {
    struct client *v;
    size_t alloc;
    size_t use;
};
static struct client_vect clients;

static const struct client *add_client(int id, const char *name)
{
    if (clients.use == clients.alloc) {
        clients.alloc = clients.alloc ? clients.alloc * 2 : 10;
        clients.v = xrealloc(clients.v, clients.alloc * sizeof clients.v[0]);
    }
    clients.v[clients.use++] = (struct client){ id, strdup(name) };
    if (verbose)
        printf("+ %3d %s\n", id, name);
    return &clients.v[clients.use - 1];
}

static void remove_client(int id)
{
    for (int i = 0; i < clients.use; i++)
        if (clients.v[i].id == id) {
            if (verbose)
                printf("- %3d %s\n", clients.v[i].id, clients.v[i].name);
            clients.use--;
            free((char *)clients.v[i].name);
            memmove(&clients.v[i], &clients.v[i+1],
                    sizeof clients.v[0] * (clients.use - i));
            return;
        }
    die("nothing to remove");
}

struct addr {
    enum { Id, Name } type;
    union {
        int id;
        const char *name;
    } client;
    int port;
};

static void parse_addr(struct addr *a, const char *s)
{
    a->type = Name;
    a->client.name = strdup(s);
    char *col = strrchr(a->client.name, ':');
    if (!col)
        a->port = 0;
    else {
        a->port = atoi(col+1);
        *col = 0;
    }
}

static bool addr_match(const struct addr *a, const struct client *c)
{
    if (a->type == Id && a->client.id == c->id)
        return true;
    if (a->type == Name && strcmp(a->client.name, c->name) == 0)
        return true;
    return false;
}

struct rule {
    struct addr src, dest;
};

struct rule_vect {
    struct rule *v;
    size_t alloc;
    size_t use;
};
static struct rule_vect rules;

static void print_rule(const struct rule *rule)
{
    switch (rule->src.type) {
    case Name:
        printf("%s:%d -> ", rule->src.client.name, rule->src.port);
        break;
    case Id:
        printf("%d:%d -> ", rule->src.client.id, rule->src.port);
        break;
    }
    switch (rule->dest.type) {
    case Name:
        printf("%s:%d\n", rule->dest.client.name, rule->dest.port);
        break;
    case Id:
        printf("%d:%d\n", rule->dest.client.id, rule->dest.port);
        break;
    }
}

static const struct rule *add_rule(const char *src, const char *dest)
{
    if (rules.use == rules.alloc) {
        rules.alloc = rules.alloc ? rules.alloc * 2 : 10;
        rules.v = xrealloc(rules.v, rules.alloc * sizeof rules.v[0]);
    }
    parse_addr(&rules.v[rules.use].src, src);
    parse_addr(&rules.v[rules.use].dest, dest);
    if (verbose)
        print_rule(&rules.v[rules.use]);
    rules.use++;
    return &rules.v[rules.use - 1];
}

static void connect_clients(snd_seq_t *seq,
                            const struct client *src, int src_port,
                            const struct client *dest, int dest_port)
{
    if (verbose)
        printf("CONNECT %3d:%d %s -> %3d:%d %s\n",
               src->id, src_port, src->name,
               dest->id, dest_port, dest->name);
    snd_seq_port_subscribe_t *subs;
    snd_seq_port_subscribe_alloca(&subs);
    snd_seq_addr_t saddr = { .client = src->id, .port = src_port };
    snd_seq_port_subscribe_set_sender(subs, &saddr);
    snd_seq_addr_t daddr = { .client = dest->id, .port = dest_port };
    snd_seq_port_subscribe_set_dest(subs, &daddr);
    snd_seq_subscribe_port(seq, subs);
}

static void check_src_rule(snd_seq_t *seq, const struct client *src,
                           const struct rule *rule)
{
    for (int i = 0; i < clients.use; i++)
        if (addr_match(&rule->dest, &clients.v[i]))
            connect_clients(seq, src, rule->src.port,
                            &clients.v[i], rule->dest.port);
}

static void check_dest_rule(snd_seq_t *seq, const struct client *dest,
                            const struct rule *rule)
{
    for (int i = 0; i < clients.use; i++)
        if (addr_match(&rule->src, &clients.v[i]))
            connect_clients(seq, &clients.v[i], rule->src.port,
                            dest, rule->dest.port);
}

static void rescan(snd_seq_t *seq, const struct client *new_client)
{
    for (int i = 0; i < rules.use; i++) {
        struct rule *r = &rules.v[i];
        if (addr_match(&r->src, new_client))
            check_src_rule(seq, new_client, r);
        if (addr_match(&r->dest, new_client))
            check_dest_rule(seq, new_client, r);
    }
}

int open_seq(snd_seq_t **seq, int *port)
{
    if (snd_seq_open(seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        return -1;
    }
    snd_seq_set_client_name(*seq, "Autoconnect");
    char portname[64];
    sprintf(portname, "autoconnect");
    if ((*port = snd_seq_create_simple_port(
             *seq, portname,
             SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE|SND_SEQ_PORT_CAP_NO_EXPORT,
             SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        return -1;
    }
    return 0;
}

static void client_start(snd_seq_t *seq, snd_seq_event_t *ev)
{
    snd_seq_client_info_t *info;
    snd_seq_client_info_alloca(&info);
    snd_seq_get_any_client_info(seq, ev->data.addr.client, info);
    const char *name = snd_seq_client_info_get_name(info);
    rescan(seq, add_client(ev->data.addr.client, name));
}

static void client_exit(snd_seq_t *seq, snd_seq_event_t *ev)
{
    remove_client(ev->data.addr.client);
}

static void handle(snd_seq_t *seq)
{
    snd_seq_event_t *ev;

    do {
        snd_seq_event_input(seq, &ev);
        switch (ev->type) {
        case SND_SEQ_EVENT_CLIENT_START:
            client_start(seq, ev);
            break;
        case SND_SEQ_EVENT_CLIENT_EXIT:
            client_exit(seq, ev);
            break;
        case SND_SEQ_EVENT_CLIENT_CHANGE:
            if (verbose)
                printf("client %d:%d changed\n",
                       ev->data.addr.client, ev->data.addr.port);
            break;
        case SND_SEQ_EVENT_PORT_START:
        case SND_SEQ_EVENT_PORT_EXIT:
        case SND_SEQ_EVENT_PORT_CHANGE:
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
            break;
        default:
            if (verbose)
                printf("something else happened (event type %d)\n", ev->type);
        }
        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq, 0) > 0);
}

static void usage(void)
{
    fprintf(stderr, "usage: autoconnect src dest src dest ...\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose = true;
        memmove(argv + 1, argv + 2, sizeof argv[0] * (argc - 2));
        argc--;
    }

    argv++;
    argc--;
    while (argc > 0) {
        if (argc == 1)
            usage();
        add_rule(argv[0], argv[1]);
        argc -= 2;
        argv += 2;
    }

    snd_seq_t *seq;
    int port;
    if (open_seq(&seq, &port) < 0) {
        fprintf(stderr, "ALSA Error.\n");
        exit(1);
    }

    snd_seq_port_subscribe_t *subs;
    snd_seq_port_subscribe_alloca(&subs);
    snd_seq_addr_t sender = { .client = SND_SEQ_CLIENT_SYSTEM,
                              .port = SND_SEQ_PORT_SYSTEM_ANNOUNCE };
    snd_seq_port_subscribe_set_sender(subs, &sender);
    snd_seq_addr_t dest = { .client = snd_seq_client_id(seq),
                            .port = port };
    snd_seq_port_subscribe_set_dest(subs, &dest);
    snd_seq_subscribe_port(seq, subs);

    int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    struct pollfd pfd[npfd];
    snd_seq_poll_descriptors(seq, pfd, npfd, POLLIN);

    snd_seq_client_info_t *info;
    if (snd_seq_client_info_malloc(&info) < 0)
        die("snd_seq_client_info_malloc");

    snd_seq_client_info_set_client(info, 0);
    while (snd_seq_query_next_client(seq, info) == 0)
        rescan(seq, add_client(snd_seq_client_info_get_client(info),
                               snd_seq_client_info_get_name(info)));

    while (1) {
        if (poll(pfd, npfd, 100000) > 0) {
            handle(seq);
        }
    }
}
