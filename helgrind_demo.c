#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int shared_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t first_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t second_mutex = PTHREAD_MUTEX_INITIALIZER;

static void pause_briefly(void)
{
    const struct timespec delay = {0, 10000000L};
    (void)nanosleep(&delay, NULL);
}

static void *race_bad_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100000; i++)
    {
        shared_counter++; /* BUG: no mutex protects the shared value. */
    }
    return NULL;
}

static void *race_fixed_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100000; i++)
    {
        (void)pthread_mutex_lock(&counter_mutex);
        shared_counter++; /* FIX: protected by counter_mutex. */
        (void)pthread_mutex_unlock(&counter_mutex);
    }
    return NULL;
}

static void *deadlock_bad_worker_one(void *arg)
{
    (void)arg;
    (void)pthread_mutex_lock(&first_mutex);
    pause_briefly();
    (void)pthread_mutex_lock(&second_mutex); /* BUG: first -> second. */
    (void)pthread_mutex_unlock(&second_mutex);
    (void)pthread_mutex_unlock(&first_mutex);
    return NULL;
}

static void *deadlock_bad_worker_two(void *arg)
{
    (void)arg;
    (void)pthread_mutex_lock(&second_mutex);
    pause_briefly();
    (void)pthread_mutex_lock(&first_mutex); /* BUG: second -> first. */
    (void)pthread_mutex_unlock(&first_mutex);
    (void)pthread_mutex_unlock(&second_mutex);
    return NULL;
}

static void *deadlock_fixed_worker(void *arg)
{
    (void)arg;
    (void)pthread_mutex_lock(&first_mutex);
    pause_briefly();
    (void)pthread_mutex_lock(&second_mutex);
    (void)pthread_mutex_unlock(&second_mutex);
    (void)pthread_mutex_unlock(&first_mutex);
    return NULL;
}

static int run_race_test(void *(*worker)(void *))
{
    pthread_t thread_one;
    pthread_t thread_two;
    (void)pthread_create(&thread_one, NULL, worker, NULL);
    (void)pthread_create(&thread_two, NULL, worker, NULL);
    (void)pthread_join(thread_one, NULL);
    (void)pthread_join(thread_two, NULL);
    (void)printf("race result: shared_counter=%d\n", shared_counter);
    return EXIT_SUCCESS;
}

static int run_deadlock_test(void *(*worker_one)(void *),
                             void *(*worker_two)(void *))
{
    pthread_t thread_one;
    pthread_t thread_two;
    (void)pthread_create(&thread_one, NULL, worker_one, NULL);
    (void)pthread_create(&thread_two, NULL, worker_two, NULL);
    (void)pthread_join(thread_one, NULL);
    (void)pthread_join(thread_two, NULL);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if ((argc != 2) ||
        ((strcmp(argv[1], "race-bad") != 0) &&
         (strcmp(argv[1], "race-fixed") != 0) &&
         (strcmp(argv[1], "deadlock-bad") != 0) &&
         (strcmp(argv[1], "deadlock-fixed") != 0)))
    {
        (void)fprintf(stderr,
                      "Usage: %s race-bad|race-fixed|deadlock-bad|deadlock-fixed\n",
                      argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "race-bad") == 0)
    {
        return run_race_test(race_bad_worker);
    }
    if (strcmp(argv[1], "race-fixed") == 0)
    {
        return run_race_test(race_fixed_worker);
    }
    if (strcmp(argv[1], "deadlock-bad") == 0)
    {
        return run_deadlock_test(deadlock_bad_worker_one,
                                 deadlock_bad_worker_two);
    }
    return run_deadlock_test(deadlock_fixed_worker, deadlock_fixed_worker);
}
