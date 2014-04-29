/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#include "list.h"
#include <stdlib.h>

List *list_append(List *list, void *user_data)
{
    List *link;
    List *tail;

    // malloc the new link:
    link = list_new();
    if (link == NULL) { return NULL; }

    // Set user_data on the new link:
    link->user_data = user_data;

    // Now link it at the end of the list:
    if (list == NULL) {
        // This new link is the first element of an empty list.
        // It is the new head of the list; return it.
        list = link;
    } else {
        // There are items in this list; append to the end.
        tail = list_last(list);
        tail->next = link;
        link->prev = tail;
    }

    return list;
}


List *list_delete_link(List *list, List *link)
{
    // Unlink the 'link' node:
    list = list_remove_link(list, link);

    // Free it:
    list_free_link(link);

    // Return the (possibly changed) new head:
    return list;
}


List *list_last(List *list)
{
    if (list == NULL) { return NULL; }

    // Roll to the end of the list, return the tail node:
    while (list->next != NULL) {
        list = list->next;
    }

    return list;
}

// supporting the two above:
List *list_new(void)
{
    return (List *)calloc(1, sizeof(List));
}


void list_free_link(List *link)
{
    if (link == NULL) { return; }
    free(link);
}


List *list_remove_link(List *list, List *link)
{
    if (link == NULL) {
        // Invalid link; take no action, just return the head.
        return list;
    }

    if (link->prev != NULL) {
        link->prev->next = link->next;
    }

    if (link->next != NULL) {
        link->next->prev = link->prev;
    }

    // If the link removed was the head, return the new head
    // as the next link in the chain.
    if (list == link) {
        list = list->next;
    }

    link->next = NULL;
    link->prev = NULL;

    return list;
}


List *list_prepend(List *list, void *user_data)
{
    List *link;

    link = list_new();
    link->user_data = user_data;

    if (list != NULL) {

        if (list->prev != NULL) {
            // Insert the new link for the 'list->prev' node:
            list->prev->next = link;
            link->prev = list->prev;
        }

        // Put the new link ahead of 'list':
        list->prev = link;
        link->next = list;
    }

    // Return as the new head of the list.
    return link;
}

