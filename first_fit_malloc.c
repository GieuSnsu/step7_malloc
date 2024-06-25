#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

void *mmap_from_system(size_t size);
void munmap_to_system(void *ptr, size_t size);

// Each object or free slot has metadata just prior to it:
//
// ... | m | object | m | free slot | m | free slot | m | object | ...
//
// where |m| indicates metadata. The metadata is needed for two purposes:
//
// 1) For an allocated object:
//   *  |size| indicates the size of the object. |size| does not include
//      the size of the metadata.
//   *  |next| is unused and set to NULL.
// 2) For a free slot:
//   *  |size| indicates the size of the free slot. |size| does not include
//      the size of the metadata.
//   *  The free slots are linked with a singly linked list (we call this a
//      free list). |next| points to the next free slot.
typedef struct first_fit_metadata_t {
  size_t size;
  struct first_fit_metadata_t *next;
} first_fit_metadata_t;

// The global information of the first_fit malloc.
//   *  |free_head| points to the first free slot.
//   *  |dummy| is a dummy free slot (only used to make the free list
//      implementation first_fitr).
typedef struct first_fit_heap_t {
  first_fit_metadata_t *free_head;
  first_fit_metadata_t dummy;
} first_fit_heap_t;

first_fit_heap_t first_fit_heap;

// Add a free slot to the beginning of the free list.
void first_fit_add_to_free_list(first_fit_metadata_t *metadata) {
  assert(!metadata->next);
  metadata->next = first_fit_heap.free_head;
  first_fit_heap.free_head = metadata;
}

// Remove a free slot from the free list.
void first_fit_remove_from_free_list(first_fit_metadata_t *metadata,
                                  first_fit_metadata_t *prev) {
  if (prev) {
    prev->next = metadata->next;
  } else {
    first_fit_heap.free_head = metadata->next;
  }
  metadata->next = NULL;
}

// This is called at the beginning of each challenge.
void first_fit_initialize() {
  first_fit_heap.free_head = &first_fit_heap.dummy;
  first_fit_heap.dummy.size = 0;
  first_fit_heap.dummy.next = NULL;
}

// This is called every time an object is allocated. |size| is guaranteed
// to be a multiple of 8 bytes and meets 8 <= |size| <= 4000. You are not
// allowed to use any library functions other than mmap_from_system /
// munmap_to_system.
void *first_fit_malloc(size_t size) {
  first_fit_metadata_t *metadata = first_fit_heap.free_head;
  first_fit_metadata_t *prev = NULL;
  // First-fit: Find the first free slot the object fits.
  while (metadata && metadata->size < size) {
    prev = metadata;
    metadata = metadata->next;
  }

  if (!metadata) {
    // There was no free slot available. We need to request a new memory region
    // from the system by calling mmap_from_system().
    //
    //     | metadata | free slot |
    //     ^
    //     metadata
    //     <---------------------->
    //            buffer_size
    size_t buffer_size = 4096;
    first_fit_metadata_t *metadata =
        (first_fit_metadata_t *)mmap_from_system(buffer_size);
    metadata->size = buffer_size - sizeof(first_fit_metadata_t);
    metadata->next = NULL;
    // Add the memory region to the free list.
    first_fit_add_to_free_list(metadata);
    // Now, try first_fit_malloc() again. This should succeed.
    return first_fit_malloc(size);
  }

  // |ptr| is the beginning of the allocated object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  void *ptr = metadata + 1;
  size_t remaining_size = metadata->size - size;
  // Remove the free slot from the free list.
  first_fit_remove_from_free_list(metadata, prev);

  if (remaining_size > sizeof(first_fit_metadata_t)) {
    // Shrink the metadata for the allocated object
    // to separate the rest of the region corresponding to remaining_size.
    // If the remaining_size is not large enough to make a new metadata,
    // this code path will not be taken and the region will be managed
    // as a part of the allocated object.
    metadata->size = size;
    // Create a new metadata for the remaining free slot.
    //
    // ... | metadata | object | metadata | free slot | ...
    //     ^          ^        ^
    //     metadata   ptr      new_metadata
    //                 <------><---------------------->
    //                   size       remaining size
    first_fit_metadata_t *new_metadata = (first_fit_metadata_t *)((char *)ptr + size);
    new_metadata->size = remaining_size - sizeof(first_fit_metadata_t);
    new_metadata->next = NULL;
    // Add the remaining free slot to the free list.
    first_fit_add_to_free_list(new_metadata);
  }
  return ptr;
}

// This is called every time an object is freed.  You are not allowed to use
// any library functions other than mmap_from_system / munmap_to_system.
void first_fit_free(void *ptr) {
  // Look up the metadata. The metadata is placed just prior to the object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  first_fit_metadata_t *metadata = (first_fit_metadata_t *)ptr - 1;
  // Add the free slot to the free list.
  first_fit_add_to_free_list(metadata);
}

// This is called at the end of each challenge.
void first_fit_finalize() {}
