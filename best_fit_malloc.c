#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Interfaces to get memory pages from OS
void *mmap_from_system(size_t size);
void munmap_to_system(void *ptr, size_t size);

// Functions in common
int max(int a, int b);

// Struct definitions
typedef struct best_fit_metadata_t {
  size_t size;
  struct best_fit_metadata_t *left;
  struct best_fit_metadata_t *right;
  int height;
} best_fit_metadata_t;

typedef struct best_fit_tree_t {
  best_fit_metadata_t *free_head;
  best_fit_metadata_t dummy;
} best_fit_tree_t;

// Static variables (DO NOT ADD ANOTHER STATIC VARIABLES!)
best_fit_tree_t best_fit_tree;

// Helper functions
best_fit_metadata_t *best_fit_balance_tree(best_fit_metadata_t *tree) {
  int left_height = tree->left ? tree->left->height : 0;
  int right_height = tree->right ? tree->right->height : 0;
  tree->height = 1 + max(left_height, right_height);

  if (left_height + 1 < right_height) {
    best_fit_metadata_t *right = tree->right;
    best_fit_metadata_t *right_left = right->left;
    right->left = tree;
    tree->right = right_left;
    return right;
  } else if (right_height + 1 < left_height) {
    best_fit_metadata_t *left = tree->left;
    best_fit_metadata_t *left_right = left->right;
    left->right = tree;
    tree->left = left_right;
    return left;
  }
  return tree;
}

best_fit_metadata_t *best_fit_insert_recursive(best_fit_metadata_t *metadata,
      best_fit_metadata_t *tree) {
  if (!tree)
    return metadata;
  else if (metadata->size < tree->size)
    tree->left = best_fit_insert_recursive(metadata, tree->left);
  else
    tree->right = best_fit_insert_recursive(metadata, tree->right);
  
  return best_fit_balance_tree(tree);
}

best_fit_metadata_t *best_fit_remove_recursive(best_fit_metadata_t *metadata,
      best_fit_metadata_t *tree) {
  if (metadata == tree) {
    if (!tree->left) {
      return tree->right;
    }
    if (!tree->right) {
      return tree->left;
    }
    best_fit_metadata_t *root = tree->right;
    while (root->left)
      root = root->left;
    root->right = best_fit_remove_recursive(root, tree->right);
    root->left = tree->left;
    return root;
  }

  if (tree->size < metadata->size) {
    tree->right = best_fit_remove_recursive(metadata, tree->right);
  } else {
    tree->left = best_fit_remove_recursive(metadata, tree->left);
  }
  
  return best_fit_balance_tree(tree);
}

void best_fit_insert_to_tree(best_fit_metadata_t *metadata) {
  best_fit_tree.free_head = best_fit_insert_recursive(metadata, best_fit_tree.free_head);
}

void best_fit_remove_from_tree(best_fit_metadata_t *metadata) {
  best_fit_tree.free_head = best_fit_remove_recursive(metadata, best_fit_tree.free_head);
}

// This is called at the beginning of each challenge.
void best_fit_initialize() {
  best_fit_tree.free_head = &best_fit_tree.dummy;
  best_fit_tree.dummy.size = 0;
  best_fit_tree.dummy.left = NULL;
  best_fit_tree.dummy.right = NULL;
  best_fit_tree.dummy.height = 1;
}

// best_fit_malloc() is called every time an object is allocated.
// |size| is guaranteed to be a multiple of 8 bytes and meets 8 <= |size| <=
// 4000. You are not allowed to use any library functions other than
// mmap_from_system() / munmap_to_system().
void *best_fit_malloc(size_t size) {
  best_fit_metadata_t *metadata = best_fit_tree.free_head;
  best_fit_metadata_t *best = NULL;
  // Find the first free slot the object fits.
  while (metadata) {
    if (metadata->size < size) {
      metadata = metadata->right;
    } else {
      best = metadata;
      metadata = metadata->left;
    }
  }
  // now, metadata points to the first free slot

  if (!best) {
    // There was no free slot available. We need to request a new memory region
    // from the system by calling mmap_from_system().
    //
    //     | metadata | free slot |
    //     ^
    //     metadata
    //     <---------------------->
    //            buffer_size
    size_t buffer_size = 4096;
    metadata = (best_fit_metadata_t *)mmap_from_system(buffer_size);
    metadata->size = buffer_size - sizeof(best_fit_metadata_t);
    metadata->left = NULL;
    metadata->right = NULL;
    metadata->height = 1;
    // Add the memory region to the free list.
    best_fit_insert_to_tree(metadata);
    // Now, try best_fit_malloc() again. This should succeed.
    return best_fit_malloc(size);
  }

  // |ptr| is the beginning of the allocated object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  void *ptr = best + 1;
  size_t remaining_size = best->size - size;
  // Remove the free slot from the free list.
  best_fit_remove_from_tree(best);
  best->left = NULL;
  best->right = NULL;
  best->height = 1;

  if (remaining_size > sizeof(best_fit_metadata_t)) {
    // Shrink the metadata for the allocated object
    // to separate the rest of the region corresponding to remaining_size.
    // If the remaining_size is not large enough to make a new metadata,
    // this code path will not be taken and the region will be managed
    // as a part of the allocated object.
    best->size = size;
    // Create a new metadata for the remaining free slot.
    //
    // ... | metadata | object | metadata | free slot | ...
    //     ^          ^        ^
    //     metadata   ptr      new_metadata
    //                 <------><---------------------->
    //                   size       remaining size
    best_fit_metadata_t *new_metadata = (best_fit_metadata_t *)((char *)ptr + size);
    new_metadata->size = remaining_size - sizeof(best_fit_metadata_t);
    new_metadata->left = NULL;
    new_metadata->right = NULL;
    new_metadata->height = 1;
    // Add the remaining free slot to the free list.
    best_fit_insert_to_tree(new_metadata);
  }
  return ptr;
}

// This is called every time an object is freed.  You are not allowed to
// use any library functions other than mmap_from_system / munmap_to_system.
void best_fit_free(void *ptr) {
  // Look up the metadata. The metadata is placed just prior to the object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  best_fit_metadata_t *metadata = (best_fit_metadata_t *)ptr - 1;
  // Add the free slot to the free list.
  best_fit_insert_to_tree(metadata);
}

// This is called at the end of each challenge.
void best_fit_finalize() {}

void test() {}
