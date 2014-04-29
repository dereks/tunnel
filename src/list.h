/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#ifndef LIST_H
#define LIST_H

typedef struct List {
    struct List *next;
    struct List *prev;
    void *user_data;
} List;

// Allocate a new link node pointing to 'user_data' and add it to 
// the head of the list.
// Returns: The new list head.  (Assign the return value to your list pointer.)
List *list_prepend(List *list, void *user_data);

// Allocate a new link node pointing to 'user_data' and add it to 
// the end of the list.
// Returns: The (possibly new) head of the list.
List *list_append(List *list, void *user_data);

// Return the last item in the list:
List *list_last(List *list);

// Delete: list_remove_link(link) then list_free_link(link).
// Returns: The (possibly new) head of the list, which is NULL if the list is 
// empty.
List *list_delete_link(List *list, List *link);

// Unlink the given 'link' from the list, thus making it
// the head of a new single-element list (containing only itself).
//
// Warning: It is up to the caller to eventually free 'link'.
//
// Returns: The (possibly new) head of the list.
List *list_remove_link(List *list, List *link);

// Allocate a new link.
List *list_new(void);
// Free the link.
void list_free_link(List *link);


// Convenience macros, for cleaner encapsulation:
#define list_next(link) (link->next)
#define list_prev(link) (link->prev)
#define list_user_data(link) (link->user_data)

#endif  // LIST_H
