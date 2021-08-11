/**
 * \author Beiyang Li
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dplist.h"
#include <string.h>
/*
 * definition of error codes
 * */
#define DPLIST_NO_ERROR 0
#define DPLIST_MEMORY_ERROR 1 // error due to mem alloc failure
#define DPLIST_INVALID_ERROR 2 //error due to a list operation applied on a NULL list 

#ifdef DEBUG
#define DEBUG_PRINTF(...) 									                                        \
        do {											                                            \
            fprintf(stderr,"\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__);	    \
            fprintf(stderr,__VA_ARGS__);								                            \
            fflush(stderr);                                                                         \
                } while(0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif


#define DPLIST_ERR_HANDLER(condition, err_code)                         \
    do {                                                                \
            if ((condition)) DEBUG_PRINTF(#condition " failed\n");      \
            assert(!(condition));                                       \
        } while(0)


/*
 * The real definition of struct list / struct node
 */

struct dplist_node {
    dplist_node_t *prev, *next;
    void *element;
};

struct dplist {
    //fiset list node has index 0
    dplist_node_t *head;

    void *(*element_copy)(void *src_element);

    void (*element_free)(void **element);

    int (*element_compare)(void *x, void *y);
};


dplist_t *dpl_create(// callback functions
        void *(*element_copy)(void *src_element),
        void (*element_free)(void **element),
        int (*element_compare)(void *x, void *y)
) {
    dplist_t* list = (dplist_t *) malloc(sizeof(struct dplist));
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_MEMORY_ERROR);
    list->head = NULL;
    //just create
    list->element_copy = element_copy;
    list->element_free = element_free;
    list->element_compare = element_compare;
    return list;
}

void dpl_free(dplist_t **list, bool free_element) {
    //if free_element == true, then call element_free() on the element of the list node to remove
    //if not, don't call
    assert(*list != NULL);
    while (dpl_size(*list)>0)
    {
        dpl_remove_at_index(*list, 0, free_element);
    }
    free(*list);//free the pointer
    *list = NULL;//set to NULL, clear it    

}

dplist_t *dpl_insert_at_index(dplist_t *list, void *element, int index, bool insert_copy) {
    //if insert_copy == true, use the copy in the new list node
    //if not, without taking a copy
    //if index <= 0, inserted at the start of list
    //bigger than size, inserted at the end of list
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    dplist_node_t *ref_at_index, *list_node;
    
    list_node = malloc(sizeof(dplist_node_t));
    DPLIST_ERR_HANDLER(list_node == NULL, DPLIST_MEMORY_ERROR);
    if(insert_copy)
    {
        //deepcopy
        list_node->element = list->element_copy(element);
    }
    else//just assign pointer
    {
        list_node->element = element;
    }
    if(list->head == NULL)//case1: empty list
    {
        list_node->prev = NULL;
        list_node->next = NULL;
        list->head = list_node;
    }
    else if (index <= 0)//case2: insert to head
    {
        list_node->prev = NULL;
        list_node->next = list->head;
        list->head->prev = list_node;
        list->head = list_node;
    }
    
    else //case3: mid and end
    {
        ref_at_index = dpl_get_reference_at_index(list, index);
        assert(ref_at_index != NULL);//just make sure
        if (index < dpl_size(list))//mid
        {
            //first change list_node
            list_node->prev = ref_at_index->prev;
            list_node->next = ref_at_index;
            //then change ref at index and previous one, don't forget it!
            ref_at_index->prev->next = list_node;
            ref_at_index->prev = list_node;
        }
        else//end
        {
            assert(ref_at_index->next == NULL);
            list_node->next = NULL;
            list_node->prev = ref_at_index;
            ref_at_index->next = list_node;
        }
        
        
    }
    return list;
}

dplist_t *dpl_remove_at_index(dplist_t *list, int index, bool free_element) {

    //if free_element == true, call element_free()
    //free list node
    if (list == NULL || list->head == NULL)
    {
        return list;//don't need remove
    }
    dplist_node_t *temp = dpl_get_reference_at_index(list, index);//pointer
    if(temp == NULL)
    {
        return list;
    }
    //
    dplist_node_t* t_prev = temp->prev;
    dplist_node_t* t_next = temp->next;
    if(t_prev == NULL)//head
    {
        if(t_next != NULL)
        {
            t_next->prev = NULL;
            list->head = t_next;
        }
        else//empty list
        {
            list->head = NULL;
        }
        
    }
    else//normal
    {
        //change prev and next node
        t_prev->next = t_next;
        if (t_next != NULL)
        {
            t_next->prev = t_prev;
        }
        else
        {
            //t_prev->next = NULL;
        }        
    }
    if (free_element)
    {
        list->element_free((void*)&(temp->element));//void (*element_free)(void **element);
        free(temp);
    }
    return list;
    
    

}

int dpl_size(dplist_t *list) {
    if (list == NULL)
    {
        return -1;
    }
    
    int size = 0;
    dplist_node_t *size_temp = list->head;
    while(size_temp != NULL){
        size_temp = size_temp->next;
        size++;
    }
    return size;

}

void *dpl_get_element_at_index(dplist_t *list, int index) {

    //if list empty, return (void *)0
    if(list == NULL || list->head == NULL)
    {
        return (void *)0;
    }
    if(index <= 0)
    {
        //return the element of the first list node
        return list->head->element;
    }
    int count = 0;
    dplist_node_t *temp = list->head;
    while (temp->next != NULL)
    {
        if(count == index)
        return temp->element;
        
        temp = temp->next;
        count++;
    }
    return temp->element;//the last one

}

int dpl_get_index_of_element(dplist_t *list, void *element) {

    //element_compare() to search 'element' in the list;
    int index = 0;
    dplist_node_t *temp = list->head;
    while (temp != NULL)
    {
        if(list->element_compare(element, temp->element) == 0)
        return index;

        temp = temp->next;
        index++;
    }
    return -1;//not found
}

dplist_node_t *dpl_get_reference_at_index(dplist_t *list, int index) {

    int count;
    dplist_node_t *dummy;
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    if (list->head == NULL)
        return NULL;
    for (dummy = list->head, count = 0; dummy->next != NULL; dummy = dummy->next, count++)
    {
        if (count >= index)
            return dummy;
    }
    return dummy;

}

void *dpl_get_element_at_reference(dplist_t *list, dplist_node_t *reference) {

    if(list == NULL || list->head == NULL){
        return NULL;
    }
    dplist_node_t* temp = list->head;
    
    while (temp->next != NULL)
    {
        if(temp == reference){
            return temp->element;
        }
        temp = temp->next;
    }

    if(reference == NULL){//NULL
        return NULL;
    }
    
    else if(temp==reference){
        return temp->element; 
    }
    else{
        return NULL;
    }

}


/** Inserts a new list node containing 'element' in the sorted list and returns a pointer to the new list.
 * - The list must be sorted or empty before calling this function.
 * - The sorting is done in ascending order according to a comparison function.
 * - If two members compare as equal, their order in the sorted array is undefined.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param element a pointer to an element
 * \param insert_copy if true use element_copy() to make a copy of 'element' and use the copy in the new list node, otherwise the given element pointer is added to the list
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_insert_sorted(dplist_t *list, void *element, bool insert_copy)
{
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    dplist_node_t* dummy,* list_node;
    list_node = (dplist_node_t*)malloc(sizeof(dplist_node_t));
    DPLIST_ERR_HANDLER(list_node == NULL, DPLIST_MEMORY_ERROR);

    if(insert_copy)
    {
        list_node->element=list->element_copy(element);
    }
    else
    {
        list_node->element=element;
    }
    if (list->head == NULL)
    {
        list_node->prev = NULL;
        list_node->next = NULL;
        list->head = list_node;
    }
    else
    {
        dummy = list->head;
        if (list->element_compare(dummy->element, element) >= 0)
        {
            list_node->prev = NULL;
            list_node->next = list->head;
            list->head->prev = list_node;
            list->head = list_node;
            return list;
        }
        while (dummy->next != NULL)
        {
            if (list->element_compare(dummy->next->element, element) >= 0)
            {
                list_node->prev = dummy;
                list_node->next = dummy->next;
                dummy->next = list_node;
                list_node->next->prev = list_node;
                return list;
            }
            else
                dummy = dummy->next;
        }
        //to the end of the list
        list_node->prev = dummy;
        list_node->next = NULL;
        dummy->next = list_node;
        return list;
    }
    return list;
}

dplist_node_t *dpl_get_reference_of_element(dplist_t *list, void *element)
{
    if(list == NULL || list->head == NULL){
        // empty list
        return NULL;
    }
    int index = dpl_get_index_of_element(list, element);
    if(index<0){
        return NULL;    // element not found
    }
    else{
        return dpl_get_reference_at_index(list, index);
    }
}