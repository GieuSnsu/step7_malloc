#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

// [First fit malloc]
void first_fit_initialize();
void *first_fit_malloc(size_t size);
void first_fit_free(void *ptr);
void first_fit_finalize();

// [Best fit malloc]
void best_fit_initialize();
void *best_fit_malloc(size_t size);
void best_fit_free(void *ptr);
void best_fit_finalize();

// [Best malloc]
void best_initialize();
void *best_malloc(size_t size);
void best_free(void *ptr);
void best_finalize();

// Vector
typedef struct object_t {
  void *ptr;
  size_t size;
  char tag;  // A tag to check the object is not broken.
} object_t;

typedef struct vector_t {
  size_t size;
  size_t capacity;
  object_t *buffer;
} vector_t;

vector_t *vector_create() {
  vector_t *vector = (vector_t *)malloc(sizeof(vector_t));
  vector->capacity = 0;
  vector->size = 0;
  vector->buffer = NULL;
  return vector;
}

void vector_push(vector_t *vector, object_t object) {
  if (vector->size >= vector->capacity) {
    vector->capacity = vector->capacity * 2 + 128;
    vector->buffer = (object_t *)realloc(vector->buffer,
                                         vector->capacity * sizeof(object_t));
  }
  vector->buffer[vector->size] = object;
  vector->size++;
}

size_t vector_size(vector_t *vector) { return vector->size; }

object_t vector_at(vector_t *vector, size_t i) {
  assert(i < vector->size);
  return vector->buffer[i];
}

void vector_clear(vector_t *vector) {
  free(vector->buffer);
  vector->capacity = 0;
  vector->size = 0;
  vector->buffer = NULL;
}

void vector_destroy(vector_t *vector) {
  free(vector->buffer);
  free(vector);
}

// Return the current time in seconds.
double get_time(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec * 1e-6;
}

// Return a random number in [0, 1).
double urand() { return rand() / ((double)RAND_MAX + 1); }

// Return an object size. The returned size is a random number in
// [min_size, max_size] that follows an exponential distribution.
// |min_size| needs to be a multiple of 8 bytes.
size_t get_object_size(size_t min_size, size_t max_size) {
  const int alignment = 8;
  assert(min_size <= max_size);
  assert(min_size % alignment == 0);
  const double lambda = 1;
  const double threshold = 6;
  double tau = -lambda * log(urand());
  if (tau >= threshold) {
    tau = threshold;
  }
  size_t result = (size_t)((max_size - min_size) * tau / threshold) + min_size;
  result = result / alignment * alignment;
  assert(min_size <= result);
  assert(result <= max_size);
  return result;
}

// Return an object lifetime. The returned lifetime is a random number in
// [min_epoch, max_epoch] that follows an exponential distribution.
unsigned get_object_lifetime(unsigned min_epoch, unsigned max_epoch) {
  const double lambda = 1;
  const double threshold = 6;
  double tau = -lambda * log(urand());
  if (tau >= threshold) {
    tau = threshold;
  }
  unsigned result =
      (unsigned)((max_epoch - min_epoch) * tau / threshold + min_epoch);
  assert(min_epoch <= result);
  assert(result <= max_epoch);
  return result;
}

typedef void (*initialize_func_t)();
typedef void *(*malloc_func_t)(size_t size);
typedef void (*free_func_t)(void *ptr);
typedef void (*finalize_func_t)();

// Record the statistics of each challenge.
typedef struct stats_t {
  double begin_time;
  double end_time;
  size_t mmap_size;
  size_t munmap_size;
  size_t allocated_size;
  size_t freed_size;
} stats_t;

stats_t stats;
FILE *trace_fp;

// Run one challenge.
// |min_size|: The min size of an allocated object
// |max_size|: The max size of an allocated object
// |*_func|: Function pointers to initialize / malloc / free.
void run_challenge(const char *trace_file_name, size_t min_size,
                   size_t max_size, initialize_func_t initialize_func,
                   malloc_func_t malloc_func, free_func_t free_func,
                   finalize_func_t finalize_func) {
  trace_fp = NULL;
#ifdef ENABLE_MALLOC_TRACE
  if (trace_file_name) {
    trace_fp = fopen(trace_file_name, "wb");
    if (!trace_fp) {
      fprintf(stderr, "Failed to open a trace file: %s\n", trace_file_name);
      exit(EXIT_FAILURE);
    }
  }
  const int epochs_per_cycle = 10;
  const int objects_per_epoch_small = 25;
  const int objects_per_epoch_large = 50;
#else
  const int epochs_per_cycle = 100;
  const int objects_per_epoch_small = 100;
  const int objects_per_epoch_large = 2000;
#endif
  const int cycles = 10;
  char tag = 0;
  // The last entry of the vector is used to store objects that are never freed.
  vector_t *objects[epochs_per_cycle + 1];
  for (int i = 0; i < epochs_per_cycle + 1; i++) {
    objects[i] = vector_create();
  }
  initialize_func();
  stats.mmap_size = stats.munmap_size = 0;
  stats.allocated_size = stats.freed_size = 0;
  stats.begin_time = get_time();
  for (int cycle = 0; cycle < cycles; cycle++) {
    for (int epoch = 0; epoch < epochs_per_cycle; epoch++) {
      size_t allocated = 0;
      size_t freed = 0;

      // Allocate |objects_per_epoch| objects.
      int objects_per_epoch = objects_per_epoch_small;
      if (epoch == 0) {
        // To simulate a peak memory usage, we allocate a larger number of
        // objects from time to time.
        objects_per_epoch = objects_per_epoch_large;
      }
      for (int i = 0; i < objects_per_epoch; i++) {
        size_t size = get_object_size(min_size, max_size);
        int lifetime = get_object_lifetime(1, epochs_per_cycle);
        stats.allocated_size += size;
        allocated += size;
        void *ptr = malloc_func(size);
        if (trace_fp) {
          fprintf(trace_fp, "a %llu %ld\n", (unsigned long long)ptr, size);
        }
        memset(ptr, tag, size);
        object_t object = {ptr, size, tag};
        tag++;
        if (tag == 0) {
          // Avoid 0 for tagging since it is not distinguishable from fresh
          // mmaped memory.
          tag++;
        }
        if (urand() < 0.04) {
          // 4% of objects are set as never freed.
          vector_push(objects[epochs_per_cycle], object);
        } else {
          vector_push(objects[(epoch + lifetime) % epochs_per_cycle], object);
        }
      }
      // Free objects that are expected to be freed in this epoch.
      vector_t *vector = objects[epoch];
      for (size_t i = 0; i < vector_size(vector); i++) {
        object_t object = vector_at(vector, i);
        stats.freed_size += object.size;
        freed += object.size;
        // Check that the tag is not broken.
        if (((char *)object.ptr)[0] != object.tag ||
            ((char *)object.ptr)[object.size - 1] != object.tag) {
          assert(0);
        }
        if (trace_fp) {
          fprintf(trace_fp, "f %llu %ld\n", (unsigned long long)object.ptr,
                  object.size);
        }
        free_func(object.ptr);
      }

#if 0
      // Debug print
      printf("epoch = %d, allocated = %ld bytes, freed = %ld bytes\n",
             cycle * epochs_per_cycle + epoch, allocated, freed);
      printf("allocated = %.2f MB, freed = %.2f MB, mmap = %.2f MB, munmap = %.2f MB, utilization = %d%%\n",
             stats.allocated_size / 1024.0 / 1024.0,
             stats.freed_size / 1024.0 / 1024.0,
             stats.mmap_size / 1024.0 / 1024.0,
             stats.munmap_size / 1024.0 / 1024.0,
             (int)(100.0 * (stats.allocated_size - stats.freed_size)
                   / (stats.mmap_size - stats.munmap_size)));
#endif
      vector_clear(vector);
      // printf("cycle done %d\n", cycle);
    }
  }
  stats.end_time = get_time();
  for (int i = 0; i < epochs_per_cycle + 1; i++) {
    vector_destroy(objects[i]);
  }
  finalize_func();
  if (trace_fp) {
    fclose(trace_fp);
    trace_fp = NULL;
  }
}

#define FIRST_CHALLENGE_INDEX 1
#define LAST_CHALLENGE_INDEX 5

int best_malloc_time_ms[LAST_CHALLENGE_INDEX + 1];
int best_malloc_utilization_percentage[LAST_CHALLENGE_INDEX + 1];

// Print stats
void print_stats(int challenge_index, stats_t first_fit_stats, stats_t best_fit_stats,
                 stats_t best_stats) {
  assert(FIRST_CHALLENGE_INDEX <= challenge_index &&
         challenge_index <= LAST_CHALLENGE_INDEX);
  printf("==========================================================================\n");
  printf("Challenge #%d    | %16s => %16s => %16s\n", challenge_index, "first_fit_malloc",
         "best_fit_malloc", "best_malloc");
  printf("%-16s+ %16s => %16s => %16s\n", "---------------", "----------------",
         "----------------", "----------------");
  int first_fit_time_ms = (first_fit_stats.end_time - first_fit_stats.begin_time) * 1000;
  int best_fit_time_ms = (best_fit_stats.end_time - best_fit_stats.begin_time) * 1000;
  int best_time_ms = (best_stats.end_time - best_stats.begin_time) * 1000;
  int first_fit_utilization_percentage =
      (int)(100.0 * (first_fit_stats.allocated_size - first_fit_stats.freed_size) /
            (first_fit_stats.mmap_size - first_fit_stats.munmap_size));
  int best_fit_utilization_percentage =
      (int)(100.0 * (best_fit_stats.allocated_size - best_fit_stats.freed_size) /
            (best_fit_stats.mmap_size - best_fit_stats.munmap_size));
  int best_utilization_percentage =
      (int)(100.0 * (best_stats.allocated_size - best_stats.freed_size) /
            (best_stats.mmap_size - best_stats.munmap_size));

  printf("%16s| %16d => %16d => %16d\n", "Time [ms]", first_fit_time_ms, best_fit_time_ms,
         best_time_ms);
  printf("%16s| %16d => %16d => %16d\n", "Utilization [%] ",
         first_fit_utilization_percentage, best_fit_utilization_percentage,
         best_utilization_percentage);

  best_malloc_time_ms[challenge_index] = best_time_ms;
  best_malloc_utilization_percentage[challenge_index] = best_utilization_percentage;
}

// run challenges with differnt algorithm
void run_challenges_n(int n, size_t min_size, size_t max_size) {
  stats_t first_fit_stats, best_fit_stats, best_stats;
  char file[22];

  snprintf(file, 22, "trace%d_first_fit.txt", n);
  run_challenge(file, min_size, max_size, first_fit_initialize, first_fit_malloc,
                first_fit_free, first_fit_finalize);
  first_fit_stats = stats;

  snprintf(file, 21, "trace%d_best_fit.txt", n);
  run_challenge(file, min_size, max_size, best_fit_initialize, best_fit_malloc,
                best_fit_free, best_fit_finalize);
  best_fit_stats = stats;

  snprintf(file, 18, "trace%d_best.txt", n);
  run_challenge(file, min_size, max_size, best_initialize, best_malloc, best_free,
                best_finalize);
  best_stats = stats;

  print_stats(n, first_fit_stats, best_fit_stats, best_stats);
}

void print_score_data() {
  printf("\nChallenge done!\n");
  printf("Please copy & paste the following data in the score sheet!\n");
  for (int i = FIRST_CHALLENGE_INDEX; i <= LAST_CHALLENGE_INDEX; i++) {
    printf("%d,%d,", best_malloc_time_ms[i], best_malloc_utilization_percentage[i]);
  }
  printf("\n");
}

// Run challenges
void run_challenges() {

#ifdef ENABLE_MALLOC_TRACE
  printf(
      "!!! WARNING - MALLOC_TRACE is enabled.\n"
      "The result will be different compare to normal builds.\n");
#endif

  // Warm up run.
  run_challenge(NULL, 128, 128, first_fit_initialize, first_fit_malloc, first_fit_free,
                first_fit_finalize);

  // Run scored challenges
  run_challenges_n(1, 128, 128);
  run_challenges_n(2, 16, 16);
  run_challenges_n(3, 16, 128);
  run_challenges_n(4, 256, 4000);
  run_challenges_n(5, 8, 4000);

#ifdef ENABLE_MALLOC_TRACE
  printf(
      "!!! WARNING - MALLOC_TRACE is enabled.\n"
      "The result will be different compare to normal builds.\n");
#endif

#ifndef ENABLE_MALLOC_TRACE
  print_score_data();
#endif
}

// Allocate a memory region from the system. |size| needs to be a multiple of
// 4096 bytes.
void *mmap_from_system(size_t size) {
  assert(size % 4096 == 0);
  stats.mmap_size += size;
  void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert(ptr);
  if (trace_fp) {
    fprintf(trace_fp, "m %llu %ld\n", (unsigned long long)ptr, size);
  }
  return ptr;
}

// Free a memory region [ptr, ptr + size) to the system. |ptr| and |size| needs
// to be a multiple of 4096 bytes.
void munmap_to_system(void *ptr, size_t size) {
  assert(size % 4096 == 0);
  assert((uintptr_t)(ptr) % 4096 == 0);
  stats.munmap_size += size;
  int ret = munmap(ptr, size);
  if (trace_fp) {
    fprintf(trace_fp, "u %llu %ld\n", (unsigned long long)ptr, size);
  }
  assert(ret != -1);
}

int main(int argc, char **argv) {
  srand(12);  // Set the rand seed to make the challenges non-deterministic.
  printf("Welcome to the malloc challenge!\n");
  printf("size_of(uint8_t *) = %ld\n", sizeof(uint8_t *));
  printf("size_of(size_t) = %ld\n", sizeof(size_t));
  run_challenges();
  return 0;
}
