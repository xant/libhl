#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <linklist.h>

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

    /*
    t_testing("push_value accepts NULL");
    t_result(push_value(list, NULL) == 0, "push_value didn't accept a NULL value");
    */

    t_summary();

}
