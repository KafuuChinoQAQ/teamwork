/* Command-line entry point and worker-pool orchestration. */

#include "cfgtool.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s <elf-file> [-o output-directory] [-t threads]\n", program);
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || end == text || *end != '\0' || parsed <= 0 || parsed > 256) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }
    const char *input = NULL;
    const char *output_directory = "cfg-output";
    int thread_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output_directory = argv[++i];
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &thread_count) != 0) {
                fprintf(stderr, "invalid thread count\n"); return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); return 0;
        } else if (argv[i][0] != '-' && input == NULL) input = argv[i];
        else {
            print_usage(argv[0]); return 2;
        }
    }
    if (input == NULL) {
        print_usage(argv[0]);
        return 2;
    }
    if (thread_count == 0) {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
        thread_count = cpu_count > 0 && cpu_count < 32 ? (int)cpu_count : 4;
    }
    if (mkdir(output_directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "cannot create output directory '%s': %s\n",
                output_directory, strerror(errno));
        return 1;
    }

    ElfImage image;
    if (map_image(input, &image) != 0) return 1;
    Function *functions = NULL;
    int function_count = 0;
    if (collect_functions(&image, &functions, &function_count) != 0 || function_count == 0) {
        fprintf(stderr, "could not find functions in '%s'\n", input);
        free_functions(functions, function_count);
        unmap_image(&image);
        return 1;
    }
    if (thread_count > function_count) thread_count = function_count;
    WorkState state;
    state.image = &image;
    state.functions = functions;
    state.function_count = function_count;
    state.output_directory = output_directory;
    state.next_job = 0;
    pthread_mutex_init(&state.job_mutex, NULL);
    pthread_mutex_init(&state.stats_mutex, NULL);
    pthread_t *threads = (pthread_t *)calloc((size_t)thread_count, sizeof(pthread_t));
    if (threads == NULL) {
        fprintf(stderr, "out of memory creating worker threads\n");
        pthread_mutex_destroy(&state.stats_mutex);
        pthread_mutex_destroy(&state.job_mutex);
        free_functions(functions, function_count);
        unmap_image(&image);
        return 1;
    }
    int started = 0;
    for (int i = 0; i < thread_count; ++i) {
        if (pthread_create(&threads[started], NULL, worker_main, &state) != 0) break;
        ++started;
    }
    for (int i = 0; i < started; ++i) pthread_join(threads[i], NULL);
    if (started != thread_count) {
        fprintf(stderr, "only started %d of %d worker threads\n", started, thread_count);
    }
    printf("processed %d functions with %d threads; instructions=%lu blocks=%lu\n",
           function_count, started, g_total_instructions, g_total_blocks);
    free(threads);
    pthread_mutex_destroy(&state.stats_mutex);
    pthread_mutex_destroy(&state.job_mutex);
    free_functions(functions, function_count);
    unmap_image(&image);
    return started == thread_count ? 0 : 1;
}
