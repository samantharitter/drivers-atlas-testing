/*
 * Copyright 2020 MongoDB Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mongoc/mongoc.h>
#include <stdio.h>

typedef struct {
    bool signaled;
    bson_mutex_t mutex;
} _signal_control_t;

static _signal_control_t global_control;

void
astrolabe_signal(int signal)
{
    printf ("caught signal %d\n", signal);
    // TODO set a flag of some sort.
}

bson_t*
parse_json (const char *json_blob)
{
    bson_t *json;
    long length;
    bson_error_t error;

    json = bson_new_from_json ((const uint8_t *) json_blob, length, &error);
    if (!json) {
	printf ("Error parsing json: %s\n", error.message);
    }

    return json;
}

bool
signaled()
{
    bool signal;

    bson_mutex_lock (global_control.mutex);
    signal = global_control.signaled;
    bson_mutex_unlock (global_control.mutex);

    return signal;
}

typedef struct {
    int num_errors;
    int num_failures;
    int num_successes;
} test_data_t;

void
write_output (test_data_t *results)
{
    File *f;
    bson_t bson;
    char *str;

    bson_init (&bson);
    bson_append_int32 (&bson, "numErrors", -1, results.num_errors);
    bson_append_int32 (&bson, "numFailures", -1, results.num_failures);
    bson_append_int32 (&bson, "numSuccesses", -1, results.num_successes);

    str = bson_as_json (&bson, NULL);

    // Write to a file named results.json
    f = fopen ("results.json", "rw");
    if (!f) {
	printf ("Error: could not open file to write results.\n");
    } else {
        fputs (str, f);
        fclose (f);
    }

    bson_free (str);
    bson_destroy (&bson);
}

void
run_tests (mongoc_client_t *client, bson_t *workload)
{
    test_data_t results;
    bson_iter_t iter;
    bool signaled;
    bool res;

    while (true) {
	/* Check signal flag */
	if (signaled()) {
	    write_output (&results);
	    return;
	}

	for (operation : workload) {
	    bool res = run_opreation (db, coll, opration);
	    if (res) {
		results.num_successes++;
	    } else {
		// determine if it was an error or a failure.
	    }
	}
    }
}

int
main (int argc, char *argv[])
{
    mongoc_client_t *client;
    mongoc_uri_t *uri;
    const char *uri_str;
    const char *json_blob;
    bson_error_t error;

    /* Install signal handler. */
#if defined(_WIN64) || defined(_WIN32)
    signal (SIGBREAK, astrolabe_signal);
#else
    signal (SIGINT, astrolabe_signal);
#endif

    /* Parse command-line arguments. */
    if (argc < 3) {
	printf ("Error: Usage: workload-executor connection_string JSON\n");
	return EXIT_FAILURE;
    }

    uri_str = argv[1];
    json_blob = argv[2];

    /* Initialize and connect to the driver. */
    mongoc_init();

    uri = mongoc_uri_new (uri_str);
    if (!uri) {
	printf ("Error: invalid connection string\n");
	return EXIT_FAILURE;
    }

    client = mongoc_client_new_from_uri (uri);

    /* Clean up before exiting. */
    mongoc_client_destroy (client);
    mongoc_uri_destroy (uri);

    return EXIT_SUCCESS;
}
