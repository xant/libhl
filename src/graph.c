#include <stdlib.h>

#include "graph.h"
#include "hashtable.h"
#include "bsd_queue.h"

static char *graph_error_messages[] = {
    "No error",
    "No node has been found",
    "Label not present",
    "Allocation failed (OOM?)",
    "The connection hasn't been found"
};

struct _graph_s {
    char *label;
    hashtable_t *nodes;
    graph_free_value_callback_t free_value_cb;
    int errno;
};

struct _graph_node_connection_s {
    graph_node_t *destination;
    graph_node_chooser_callback_t chooser;
    void *context;
    TAILQ_ENTRY(_graph_node_connection_s) next;
};

struct _graph_node_s {
    char *label;
    graph_t *graph;
    void *value;
    size_t vlen;
    TAILQ_HEAD(, _graph_node_connection_s) connections;
};

typedef struct _graph_node_connection_s graph_node_connection_t;

static inline void
graph_node_destroy(graph_node_t *node)
{
    if (node->value && node->graph && node->graph->free_value_cb)
        node->graph->free_value_cb(node->value);

    graph_node_connection_t *connection, *tmp;
    TAILQ_FOREACH_SAFE(connection, &node->connections, next, tmp) {
        TAILQ_REMOVE(&node->connections, connection, next);
        free(connection);
    }

    free(node->label);
    free(node);
}

graph_t *
graph_create(char *label, graph_free_value_callback_t free_value_cb)
{
    graph_t *graph = calloc(1, sizeof(graph_t));
    if (!graph)
        return NULL;

    if (label)
        graph->label = strdup(label);

    graph->free_value_cb = free_value_cb;

    graph->nodes = ht_create(8, 1<<16, (ht_free_item_callback_t)graph_node_destroy);
    if (!graph->nodes) {
        free(graph->label);
        free(graph);
        return NULL;
    }

    graph_error_reset(graph);

    return graph;
}

void
graph_destroy(graph_t *graph)
{
    free(graph->label);
    ht_destroy(graph->nodes);
    free(graph);
}

int
graph_node_delete(graph_t *graph, char *label __attribute__ ((unused)), graph_node_t **connections, int max_connections)
{
    graph_node_t *node = NULL;
    int rc = ht_delete(graph->nodes, node->label, strlen(node->label), (void **)&node, NULL);
    if (rc != 0) {
        graph->errno = EGRAPHTABLEERR;
        return -1;
    } else if (!node) {
        graph->errno = EGRAPHNONODE;
        return -1;
    }

    int num_connections = graph_node_connections_get(node, connections, max_connections);

    graph_node_destroy(node);

    return num_connections;
}

static inline graph_node_t *
graph_node_create(graph_t *graph, char *label, void *value, size_t vlen)
{
    if (!label) { 
        graph->errno = EGRAPHNOLABEL;
        return NULL;
    }

    graph_node_t *node = calloc(1, sizeof(graph_node_t));
    if (!node) {
        graph->errno = EGRAPHNOMEM;
        return NULL;
    }

    node->label = strdup(label);

    node->value = value;
    node->vlen = vlen;
    return node;
}

graph_node_t *
graph_node_add(graph_t *graph, char *label, void *value, size_t vsize)
{
    // first check if we already have a node with the same name
    graph_node_t *node = graph_node_create(graph, label, value, vsize);
    node->graph = graph;
    TAILQ_INIT(&node->connections);
    if (ht_set(graph->nodes, label, strlen(label), node, sizeof(graph_node_t)) != 0) {
        graph->errno = EGRAPHTABLEERR;
        graph_node_destroy(node);
        return NULL;
    }
    return node;
}

int
graph_node_connect(graph_node_t *node1,
                   graph_node_t *node2,
                   graph_node_chooser_callback_t chooser,
                   void *context)
{
    graph_node_connection_t *connection = malloc(sizeof(graph_node_connection_t));
    if (!connection) {
        node1->graph->errno = EGRAPHNOMEM;
        return -1;
    }
    connection->destination = node2;
    connection->context = context;
    connection->chooser = chooser;
    TAILQ_INSERT_TAIL(&node1->connections, connection, next);
    return 0;
}

int
graph_node_disconnect(graph_node_t *node1, graph_node_t *node2)
{
    graph_node_connection_t *connection, *tmp;
    TAILQ_FOREACH_SAFE(connection, &node1->connections, next, tmp) {
        if (connection->destination == node2) {
            TAILQ_REMOVE(&node1->connections, connection, next);
            free(connection);
            return 0;
        }
    }
    node1->graph->errno = EGRAPHCONNECTIONNOTFOUND;
    return -1;
}


graph_node_t *
graph_node_next(graph_node_t *node)
{
    graph_node_connection_t *connection;
    graph_node_t *selected_destination = NULL;
    int weight = 0;
    TAILQ_FOREACH(connection, &node->connections, next) {
        if (connection->chooser) {
            int w = connection->chooser(node, connection->context);
            if (w > weight) {
                weight = w;
                selected_destination = connection->destination;
            }
        } else if (!weight) {
            selected_destination = connection->destination;
        } 
    }
    return selected_destination;
}

const char *
graph_node_label_get(graph_node_t *node)
{
    return strdup(node->label);
}

graph_node_t *
graph_node_get(graph_t *graph, char *label)
{
    return ht_get(graph->nodes, label, strlen(label), NULL);
}

int
graph_node_connections_get(graph_node_t *node, graph_node_t **connections, int max_connections)
{
    int num_connections = 0;    
    if (connections) {
        graph_node_connection_t *connection;
        TAILQ_FOREACH(connection, &node->connections, next) {
            if (num_connections == max_connections)
                break;
            connections[num_connections++] = connection->destination;
        }
    }
    return num_connections;

}

char *
graph_strerror(graph_t *graph)
{
    int index = graph->errno - 600;
    if (index >= 0 && index < (int)(sizeof(graph_error_messages) / sizeof(char *)))
        return graph_error_messages[index];
    return NULL;
}

void
graph_error_reset(graph_t *graph) {
    graph->errno = EGRAPHNOERR;
}
