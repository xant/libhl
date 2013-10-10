#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <linklist.h>

int iterator_callback(void *item, unsigned long idx, void *user) {
    int *failed = (int *)user;
    char *val = (char *)item;
    char test[100];
    sprintf(test, "test%lu", idx+1);
    if (strcmp(test, val) != 0) {
        t_failure("Value at index %d doesn't match %s  (%s)", idx, test, val);
        *failed = 1;
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    t_init();
    t_section("Value-based API");

    t_testing("Create list");
    linked_list_t *list = create_list();
    t_result(list != NULL, "Can't create a new list");

    t_testing("push_value() return value on success");
    t_result(push_value(list, "test1") == 0, "Can't push value");

    push_value(list, "test2");
    push_value(list, "test3");

    t_testing("list_count() after push");
    t_result(list_count(list) == 3, "Length is not 3 after 3 pushes");

    t_testing("pick_value()");
    t_validate_string(pick_value(list, 1), "test2");

    t_testing("shift_value()");
    t_validate_string(shift_value(list), "test1"); 

    t_testing("list_count() after shift");
    t_result(list_count(list) == 2, "Length is not 2 after shifting one value");

    t_testing("unshift_value()");
    unshift_value(list, "test1");
    t_validate_string(pick_value(list, 0), "test1"); 

    t_testing("push_value() accepts NULL");
    t_result(push_value(list, NULL) == 0, "push_value didn't accept a NULL value");

    t_testing("length updated after pushing NULL");
    t_result(list_count(list) == 4, "Length is not 4 after pushing the NULL value");

    t_testing("pop_value()");
    void *val = pop_value(list);
    t_result(list_count(list) == 3, "Length is not 3 after popping a value from the list");
    t_testing("pop_value() returned last element");
    t_result(val == NULL, "Value is not NULL");

    t_testing("still pop_value() return value");
    val = pop_value(list);
    t_validate_string(val, "test3");

    push_value(list, "test3");
    t_testing("list_count() consistent");
    t_result(list_count(list) == 3, "Length is not 3 after pushing back the 'test3' value");

    t_testing("pushing 100 values to the list");
    int i;
    for (i = 4; i <= 100; i++) {
        char *val = malloc(100);
        sprintf(val, "test%d", i);
        push_value(list, val);
    }
    t_result(list_count(list) == 100, "Length is not 100"); 

    t_testing("Order is preserved");
    int failed = 0;
    for (i = 0; i < 100; i++) {
        char test[100];
        sprintf(test, "test%d", i+1);
        char *val = pick_value(list, i);
        if (strcmp(val, test) != 0) {
            t_failure("Value at index %d doesn't match %s  (%s)", i, test, val);
            failed = 1;
            break;
        }
    }
    if (!failed)
        t_success();

    t_testing("Value iterator");
    failed = 0;
    foreach_list_value(list, iterator_callback, &failed);

    if (!failed)
        t_success();

    t_summary();

}
