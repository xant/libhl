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

static int free_value_calls = 0;

static void
test_free_value(void *value)
{
    free_value_calls++;
    free(value);
}

int main(int argc, char **argv)
{
    ut_init(basename(argv[0]));
    ut_testing("Create Graph");
    graph_t *graph = graph_create("Test", NULL);
    ut_result(graph != NULL, "Can't create a new graph");

    ut_testing("graph_errno() reports no error right after graph_create()");
    ut_result(graph_errno(graph) == EGRAPHNOERR, "A freshly created graph should report EGRAPHNOERR");

    ut_testing("Adding a new node to the graph");
    graph_node_t *node1 = graph_node_add(graph, "start_node", "test", 5);
    ut_result(node1 != NULL && strcmp(graph_node_label_get(node1), "start_node") == 0, "Can't create a new node");

    ut_testing("graph_node_add() doesn't crash and fails when label is NULL");
    graph_node_t *bad_node = graph_node_add(graph, NULL, "x", 1);
    ut_result(bad_node == NULL, "graph_node_add() should fail (not crash) when label is NULL");

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

    ut_testing("graph_node_disconnect() removes an existing connection");
    rc = graph_node_disconnect(node2, node5);
    ut_result(rc == 0, "graph_node_disconnect() failed to remove an existing connection");

    ut_testing("A disconnected node is no longer among the returned connections");
    graph_node_t *post_disconnect[3] = { NULL, NULL, NULL };
    int post_disconnect_count = graph_node_connections_get(node2, post_disconnect, 3);
    int found_node5 = 0;
    int i;
    for (i = 0; i < post_disconnect_count; i++) {
        if (post_disconnect[i] == node5)
            found_node5 = 1;
    }
    ut_result(post_disconnect_count == 2 && !found_node5, "node5 is still listed as a connection of node2 after being disconnected");

    ut_testing("graph_node_disconnect() fails when the connection doesn't exist");
    rc = graph_node_disconnect(node2, node5);
    ut_result(rc == -1, "Disconnecting an already-disconnected pair should fail");

    // restore the connection so the downstream delete test still sees node2 with 3 connections
    graph_node_connect(node2, node5, NULL, NULL);

    ut_testing("graph_node_connections_get() returns 0 for a node with no connections");
    int no_connections = graph_node_connections_get(node3, NULL, 0);
    ut_result(no_connections == 0, "A node with no outgoing connections should report 0 connections");

    ut_testing("graph_node_add() replacing an existing label releases the old value via free_value_cb");
    graph_t *vgraph = graph_create("ValueTest", test_free_value);
    graph_node_add(vgraph, "dup", strdup("value1"), 7);
    graph_node_t *dup_node = graph_node_add(vgraph, "dup", strdup("value2"), 7);
    ut_result(dup_node != NULL && free_value_calls == 1, "Old value wasn't released when overwriting an existing node");

    ut_testing("graph_node_delete() releases the node's value via free_value_cb");
    graph_node_delete(vgraph, "dup", NULL, 0);
    ut_result(free_value_calls == 2, "free_value_cb wasn't invoked when deleting a node");

    ut_testing("graph_destroy() releases remaining values via free_value_cb");
    graph_node_add(vgraph, "another", strdup("value3"), 7);
    graph_destroy(vgraph);
    ut_result(free_value_calls == 3, "free_value_cb wasn't invoked for a node still present at graph_destroy() time");

    ut_testing("graph_node_delete() doesn't crash when graph is NULL");
    int delete_ret = graph_node_delete(NULL, "start_node", NULL, 0);
    ut_result(delete_ret == -1, "Delete should fail (not crash) when graph is NULL");

    ut_testing("graph_node_delete() doesn't crash when label is NULL");
    delete_ret = graph_node_delete(graph, NULL, NULL, 0);
    ut_result(delete_ret == -1, "Delete should fail (not crash) when label is NULL");

    ut_testing("Deleting a node that was never added fails");
    delete_ret = graph_node_delete(graph, "no_such_node", NULL, 0);
    ut_result(delete_ret == -1, "Delete should fail for a label never added to the graph");

    ut_testing("graph_errno() reflects the error set by the failed delete");
    ut_result(graph_errno(graph) == EGRAPHTABLEERR, "graph_errno() didn't report the expected error code");

    ut_testing("Deleting a node returns its (possibly truncated) connections");
    graph_node_t *deleted_connections[2] = { NULL, NULL };
    delete_ret = graph_node_delete(graph, "second_node", deleted_connections, 2);
    ut_result(delete_ret == 2 && deleted_connections[0] == node3 && deleted_connections[1] == node4,
               "Delete didn't return the expected truncated list of connections");

    ut_testing("A deleted node can no longer be retrieved from the graph");
    ut_result(graph_node_get(graph, "second_node") == NULL, "The deleted node is still present in the graph");

    ut_testing("Deleting an already-deleted node fails");
    delete_ret = graph_node_delete(graph, "second_node", NULL, 0);
    ut_result(delete_ret == -1, "Deleting the same node twice should fail the second time");

    ut_testing("Deleting an existing node from the graph");
    delete_ret = graph_node_delete(graph, "start_node", NULL, 100);
    ut_result(delete_ret == 0, "Delete failed");

    ut_testing("graph_destroy() doesn't crash with dangling connections left by deleted nodes");
    graph_destroy(graph);
    ut_success();

    ut_summary();
    exit(ut_failed);
}
