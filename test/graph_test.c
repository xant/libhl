#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ut.h>
#include <graph.h>
#include <libgen.h>

static int
connection_chooser_test(graph_node_t *node, void *context)
{
    int *choose = (int *)context;
    if (choose)
        return *choose;
    return 0;
}

int main(int argc, char **argv)
{
    ut_init(basename(argv[0]));
    ut_testing("Create Graph");
    graph_t *graph = graph_create("Test", NULL);
    ut_result(graph != NULL, "Can't create a new graph");

    ut_testing("Adding a new node to the graph");
    graph_node_t *node1 = graph_node_add(graph, "start_node", "test", 5);
    ut_result(node1 != NULL && strcmp(graph_node_label_get(node1), "start_node") == 0, "Can't create a new node");

    ut_testing("Adding a new node and connect (unconditionally) the first node to the new node");
    graph_node_t *node2 = graph_node_add(graph, "second_node", "test2", 6);

    int rc = graph_node_connect(node1, node2, NULL, NULL); // unconditional connection
    if (rc == 0)
        ut_success();
    else
        ut_failure("graph_node_connect() returned an error");

    ut_testing("graph_node_next with only 1 unconditional connection");
    graph_node_t *test_node = graph_node_next(node1);
    ut_result(test_node == node2, "Can't get the unconditional connection");

    int select = 0;
    ut_testing("Add a conditional connection");
    graph_node_t *node3 = graph_node_add(graph, "third_node", "test3", 6);
    rc = graph_node_connect(node2, node3, connection_chooser_test, &select);
    if (rc == 0)
        ut_success();
    else
        ut_failure("graph_node_connect() returned an error");

    ut_testing("When the chooser returns false the new node is not selected");
    test_node = graph_node_next(node2);
    ut_result(test_node == NULL, "The node was returned even if the chooser returned false");

    select = 1;
    ut_testing("When the chooser returns true the new node is not selected");
    test_node = graph_node_next(node2);
    ut_result(test_node == node3, "The node has been selected when the chooser returned true");

    ut_testing("A node can be retrieved by label using graph_node_get()");
    test_node = graph_node_get(graph, "second_node");
    ut_result(test_node == node2, "Can't retrieve the node");

    ut_testing("Represent and access multiple connections from a node");
    graph_node_t *node4 = graph_node_add(graph, "fourth_node", "test4", 6);
    graph_node_t *node5 = graph_node_add(graph, "fifth_node", "test5", 6);

    int select2 = 0;
    graph_node_connect(node2, node4, connection_chooser_test, &select2);
    graph_node_connect(node2, node5, NULL, NULL); // unconditional connection

    graph_node_t **connections = malloc(sizeof(graph_node_t *) * 3);
    int num_connections = graph_node_connections_get(node2, connections, 3);
    ut_validate_int(num_connections, 3);
    ut_testing("Validate returned connections");
    if (connections[0] == node3 || connections[1] == node4 || connections[2] == node5)
        ut_success();
    else
        ut_failure("Returned nodes don't match");

    ut_testing("Connection selection");
    test_node = graph_node_next(node2);
    select = 0;
    select2 = 1;
    graph_node_t *test_node2 = graph_node_next(node2);
    select2 = 0;
    graph_node_t *test_node3 = graph_node_next(node2);

    ut_result(test_node == node3 && test_node2 == node4 && test_node3 == node5, "Node selection doesn't behave as expected");

    ut_testing("Chooser weight is honored");
    select = 2;
    select2 = 1;
    test_node = graph_node_next(node2);
    select2 = 3;
    test_node2 = graph_node_next(node2);
    ut_result(test_node == node3 && test_node2 == node4, "Node selection doesn't honor weights returned by the chooser");

    graph_destroy(graph);

    ut_summary();
    exit(ut_failed);
}
