/**
 * Copyright (C) 2016, 2017  Sergey Morozov <sergey@morozov.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "log.h"
#include "conf.h"
#include "queue.h"
#include "policy.h"
#include "ops.h"

/* a helper structure that unites two arbitrary entities */
typedef struct {
        void *first;
        void *second;
} pair_t;

/**
 * TODO: implement thread monitoring
 */
static void monitor_threads(pthread_t *scan_fs_thread,
                            pthread_t *dowload_file_thread,
                            pthread_t *upload_file_thread) {
        /* TODO: implement thread monitoring */
        for(;;){}
}

/**
 * @brief transfer_files_loop Common code for download_file_routine()
 *                            and upload_file_routine() routines. Pops elements
 *                            from the queues based on queue priority and
 *                            performs requested action on the popped element.
 *
 * @note This function never returns.
 *
 * @param[in] pair        A pair of primary and secondary queues.
 * @param[in] action      A pointer to the function to be invoked with popped
 *                        element as an argument.
 * @param[in] action_name Human-readable name of the action (for logging).
 */
static void *transfer_files_loop(pair_t *pair,
                                 int (*action)(const char *),
                                 const char *action_name) {
        const size_t path_max_size = get_conf()->path_max;
        char path[path_max_size];
        unsigned long long failure_counter = 0;

        queue_t *primary_queue   = pair->first;
        queue_t *secondary_queue = pair->second;

        size_t path_size;
        int pop_res;
        for (;;) {
                pop_res = -1;
                path_size = path_max_size;

                if (primary_queue != NULL) {
                    pop_res = queue_try_pop(primary_queue,
                                            path,
                                            &path_size);
                }

                if (pop_res == -1 && secondary_queue != NULL) {
                        pop_res = queue_try_pop(secondary_queue,
                                                path,
                                                &path_size);
                }

                if (pop_res == -1) {
                        /* both queues are empty */
                        pthread_testcancel();
                        continue;
                }

                if (action(path) == -1) {
                        /* continue execution even on failure */

                        if ((++failure_counter % 1024) == 0) {
                                /* periodically report about failures */

                                LOG(DEBUG,
                                    "failures of %s [counter: %llu]",
                                    action_name,
                                    failure_counter);
                        }
                }

                pthread_testcancel();
        }

        return "unreachable place";
}

/**
 * @brief download_file_routine Routine responsible for scheduling and
 *                              execution of download operations.
 *
 * @note This function never returns.
 *
 * @param[in] args A pair of primary and secondary dowload queues.
 */
static void *download_file_routine(void *args) {
        return transfer_files_loop((pair_t *)args,
                                   download_file,
                                   "download file");
}

/**
 * @brief upload_file_routine Routine responsible for scheduling and
 *                            execution of upload operations.
 *
 * @note This function never returns.
 *
 * @param[in] args A pair of primary and secondary upload queues.
 */
static void *upload_file_routine(void *args) {
        return transfer_files_loop((pair_t *)args,
                                   upload_file,
                                   "upload file");
}

/**
 * @brief scan_fs_routine Routine responsible for file system scanning.
 *
 * @note This function never returns.
 *
 * @param[in] args A pair of upload and download queue pairs.
 */
static void *scan_fs_routine(void *args) {
        pair_t *dow_upl_pair = args;
        pair_t *dow_queue_pair = dow_upl_pair->first;
        pair_t *upl_queue_pair = dow_upl_pair->second;

        queue_t *download_queue = dow_queue_pair->second;
        queue_t *upload_queue   = upl_queue_pair->second;

        unsigned long long failure_counter = 0;
        for (;;) {
                if (scan_fs(download_queue, upload_queue) == -1) {
                        /* continue execution even on failure */

                        if ((++failure_counter % 1024) == 0) {
                                /* periodically report about failures */

                                LOG(DEBUG,
                                    "failures of %s [counter: %llu]",
                                    "scan_fs",
                                    failure_counter);
                        }
                }
        }

        return "unreachable place";
}

/**
 * @brief init_data Initialization of global valuables and establishment of
 *                  the connection to the remote storage.
 *
 * @param[out] dow_queue_pair A pair of primary and secondary download queues
 *                            to be initialized.
 * @param[out] upl_queue_pair A pair of primary and secondary upload queues
 *                            to be initialized.
 *
 * @return  0: both data structures have successfully been initialized
 *         -1: error happen during data structures initialization
 */
static int init_data(pair_t *dow_queue_pair,
                     pair_t *upl_queue_pair) {
        conf_t *conf = get_conf();
        if (conf == NULL) {
                LOG(ERROR,
                    "get_conf() unexpectedly returned NULL; unable to start");
                return -1;
        }

        if (queue_init((queue_t **)&(dow_queue_pair->first),
                       conf->primary_download_queue_max_size,
                       conf->path_max,
                       QUEUE_SHM_OBJ) == -1) {
                LOG(ERROR,
                    "unable to allocate memory for primary download queue");
                return -1;
        }

        if (queue_init((queue_t **)&(dow_queue_pair->second),
                       conf->secondary_download_queue_max_size,
                       conf->path_max,
                       NULL) == -1) {
                LOG(ERROR,
                    "unable to allocate memory for secondary download queue");

                /* cleanup already allocated queues */
                queue_destroy(dow_queue_pair->first);

                return -1;
        }

        /* today there are no situations where hierarchy of queues
           established for upload action */
        upl_queue_pair->first = NULL;

        if (queue_init((queue_t **)&(upl_queue_pair->second),
                       conf->secondary_upload_queue_max_size,
                       conf->path_max,
                       NULL) == -1) {
                LOG(ERROR,
                    "unable to allocate memory for secondary upload queue");

                /* cleanup already allocated queues */
                queue_destroy(dow_queue_pair->first);
                queue_destroy(dow_queue_pair->second);
                queue_destroy(upl_queue_pair->first);

                return -1;
        }

        if (get_ops()->connect() == -1) {
                LOG(ERROR, "unable to establish connection to remote storage");

                queue_destroy(dow_queue_pair->first);
                queue_destroy(dow_queue_pair->second);
                queue_destroy(upl_queue_pair->first);
                queue_destroy(upl_queue_pair->second);

                return -1;
        }

        return 0;
}

/**
 * @brief start_routines Start threads for (1) file system scanner,
 *                       (2) download operations and (3) upload operations.
 *
 * @param[in] dow_queue_pair       A pair of primary and secondary
 *                                 download queues.
 * @param[in] upl_queue_pair       A pair of primary and secondary
 *                                 upload queues.
 * @param[in] scan_fs_thread       Thread id for file system scanner.
 * @param[in] download_file_thread Thread id for download operations.
 * @param[in] upload_file_thread   Thread id for upload operations.
 *
 * @return  0: when all threads have been successfully started
 *         -1: when at least one thread was not started
 */
static int start_routines(pair_t    *dow_queue_pair,
                          pair_t    *upl_queue_pair,
                          pthread_t *scan_fs_thread,
                          pthread_t *download_file_thread,
                          pthread_t *upload_file_thread) {
        int ret;

        pair_t dow_upl_pair = {
                .first = dow_queue_pair,
                .second = upl_queue_pair,
        };
        ret = pthread_create(scan_fs_thread,
                             NULL,
                             scan_fs_routine,
                             &dow_upl_pair);
        if (ret != 0) {
                /* ret is errno in this case */
                LOG(ERROR,
                    "pthread_create for scan_fs_routine failed "
                    "[reason: %s]",
                    strerror(ret));
                return -1;
        }

        ret = pthread_create(download_file_thread,
                             NULL,
                             download_file_routine,
                             dow_queue_pair);
        if (ret != 0) {
                /* ret is errno in this case */
                LOG(ERROR,
                    "pthread_create for download_file_routine failed "
                    "[reason: %s]",
                    strerror(ret));
                return -1;
        }

        ret = pthread_create(upload_file_thread,
                             NULL,
                             upload_file_routine,
                             upl_queue_pair);
        if (ret != 0) {
                /* ret is errno in this case */
                LOG(ERROR,
                    "pthread_create for upload_file_routine failed "
                    "[reason: %s]",
                    strerror(ret));
                return -1;
        }

        return 0;
}

/**
 * Entrypoint.
 */
int main(int argc, char *argv[]) {
        /* TODO: add check that extended attributes are supported */
        /* TODO: consider the increase of RLIMIT_NOFILE */
        /* TODO: use futimens() to leave files access and modification
                 times untouched */
        /* TODO: consider using S3 multipart upload and operations
                 on object parts as an optimization */
        /* TODO: notify client process which enqueued file via signal
                 SIGUSR1 or SIGUSR2 about the successful download event */

        pair_t dow_queue_pair;
        pair_t upl_queue_pair;

        /* thread that should scan file system and add elements to download
           and upload queues */
        pthread_t scan_fs_thread;

        /* thread that should move files from remote storage to local */
        pthread_t download_file_thread;

        /* thread that should move files from local storage to remote */
        pthread_t upload_file_thread;

        /* validate number of input arguments */
        if (argc != 2) {
                fprintf(stderr,
                        "1 argument was expected but %d provided",
                        argc - 1);
                return EXIT_FAILURE;
        }

        /* read configuration file specified via input argument */
        if (read_conf(argv[1])) {
                fprintf(stderr,
                        "failed to read configuration file %s",
                        argv[1]);
                return EXIT_FAILURE;
        }

        /* here configuration has successfully been read and logger
           structure has been initialized */
        OPEN_LOG(argv[0]);

        /* initialize variables that will be used in whole program */
        if (init_data(&dow_queue_pair, &upl_queue_pair) == -1) {
                return EXIT_FAILURE;
        }

        /* start all routines composing business logic of this program */
        if (start_routines(&dow_queue_pair,
                           &upl_queue_pair,
                           &scan_fs_thread,
                           &download_file_thread,
                           &upload_file_thread) == -1) {
                return EXIT_FAILURE;
        }

        monitor_threads(&scan_fs_thread,
                        &download_file_thread,
                        &upload_file_thread);

        /* this place is unreachable */
        return EXIT_FAILURE;
}
