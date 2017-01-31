#ifndef HT_GRAPH_H
#define HT_GRAPH_H

#include <sys/types.h>

#define EGRAPHNOERR              600
#define EGRAPHNONODE             601
#define EGRAPHNOLABEL            602
#define EGRAPHNOMEM              603
#define EGRAPHTABLEERR           604
#define EGRAPHCONNECTIONNOTFOUND 605

typedef struct _graph_s graph_t;
typedef struct _graph_node_s graph_node_t;

typedef void (*graph_free_value_callback_t)(void *);

/**
 * @brief Create a new graph with a given name
 * @param label The name of the new graph
 * @param free_value_cb If defined this callback will be called when a node
 *        is being destroyed to allow releasing resources used by the value
 * @return A pointer to a valid and initialized graph structure
 */
graph_t *graph_create(char *label, graph_free_value_callback_t free_value_cb);
void graph_destroy(graph_t *graph);


/**
 * @brief Select a node by its label
 * @param graph A pointer to a valid and initialized graph structure
 * @param label The label of the node to be retrieved
 * @return The selected node if found; NULL otherwise
 */
graph_node_t *graph_node_get(graph_t *graph, char *label);

/**
 * @brief Create a new node and add it to the graph
 * @param graph A pointer to a valid and initialized graph structure
 * @param label The label of the new node
 * @param value The value to store in the new node
 * @param vlen The size of the stored value
 * @return A pointer to a valid and initialized node on success;
 *         NULL in case of failure
 */
graph_node_t *graph_node_add(graph_t *graph,
                             char *label,
                             void *value,
                             size_t vlen);

/**
 * @brief Get all the connections from a given node
 * @param node A pointer to a valid node
 * @param connections A pointer to the output array where to store references
 *                    to all the connections for a given node
 * @param max_connections The size of the output array
 * @return The number of connections stored in the output array;\n
 *         0 if no connections are present; -1 in case of error
 */
int graph_node_connections_get(graph_node_t *node,
                               graph_node_t **connections,
                               int max_connections);

/**
 * @brief Remove a node from the graph and release its resources
 * @param graph A pointer to a valid and initialized graph structure
 * @param label The label of the node to remove
 * @param connections A pointer to the output array where to store references
 *                    to all the connections for a given node
 * @param max_connections The size of the output array
 * @return The number of connections stored in the output array;\n
 *         0 if no connections are present; -1 in case of error
 */ 
int graph_node_delete(graph_t *graph,
                      char *label,
                      graph_node_t **connections,
                      int max_connections);
/**
 * @brief Callback used to choose a connection from a given node
 * @param node A valid pointer to an initialized graph_node_t structure
 * @param context An optional pointer to some private context which can be
 *                used by the caller to determine if the node should be
 *                selected or not when calling graph_node_connection_select()
 * @return A positive integer if the connection can be chosen; 0 otherwise
 * @note If multiple chooser returns a positive value the greatest will be selected
 */
typedef int (*graph_node_chooser_callback_t)(graph_node_t *node, void *context);


/**
 * @brief Create a new connection between two nodes in the graph
 * @param node1 A pointer to a valid graph_node_t structure representing the
 *              start of the connection
 * @param node2 A pointer to a valid graph_node_t structure representing the
 *              end of the connection
 * @param chooser If provided the callback will be called to determine if the
 *                connection is selectable 
 */
int graph_node_connect(graph_node_t *node1,
                        graph_node_t *node2,
                        graph_node_chooser_callback_t chooser,
                        void *context);

/**
 * @brief Returns the current errno for a given graph
 * @param graph A pointer to a valid and initialized graph structure
 */
int graph_errno(graph_t *graph);

/**
 * @brief Returns a text description for the current error
 * @param graph A pointer to a valid and initialized graph structure
 */
char *graph_strerror(graph_t *graph);

/**
 * @brief Select and return the next node in the graph
 */
graph_node_t *graph_node_next(graph_node_t *node);

/**
 * @brief Returns a copy of the label for a given node
 * @note The caller is responsible of releasing the resources
 *       used to store the returned copy of the label
 */
const char *graph_node_label_get(graph_node_t *node);


/**
 * @brief Reset the internal error number
 */
void graph_error_reset(graph_t *graph);

#endif
