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

volatile sig_atomic_t stop;

void
astrolabe_signal(int signal)
{
    printf ("Caught signal %d\n", signal);
    stop = 1;
}

bson_t*
parse_json (const char *json_blob)
{
    bson_t *json;
    bson_error_t error;

    json = bson_new_from_json ((const uint8_t *) json_blob, -1, &error);
    if (!json) {
	printf ("Error parsing json: %s\n", error.message);
    }

    return json;
}

bool
signaled()
{
    return (stop == 1);
}

typedef struct {
    int num_errors;
    int num_failures;
    int num_successes;
} test_data_t;

void
write_output (test_data_t *results)
{
    FILE *f;
    bson_t bson;
    char *str;

    bson_init (&bson);
    bson_append_int32 (&bson, "numErrors", -1, results->num_errors);
    bson_append_int32 (&bson, "numFailures", -1, results->num_failures);
    bson_append_int32 (&bson, "numSuccesses", -1, results->num_successes);

    str = bson_as_json (&bson, NULL);

    /* Write to a file named results.json */
    f = fopen ("results.json", "w");
    if (!f) {
	printf ("Error: could not open file to write results, errno: %d\n", errno);
    } else {
        fputs (str, f);
        fclose (f);
    }

    bson_free (str);
    bson_destroy (&bson);
}

bool
doc_in_array (const bson_t *array, const bson_t *doc)
{
    bson_iter_t iter;

    bson_iter_init (&iter, array);
    while (bson_iter_next (&iter)) {
	bson_t array_doc;
	const uint8_t *data;
	uint32_t len;

	if (!BSON_ITER_HOLDS_DOCUMENT (&iter)) {
	    printf ("Error: expected array to hold only documents\n");
	    return false;
	}

	bson_iter_document (&iter, &len, &data);
	if (!bson_init_static (&array_doc, data, len)) {
	    printf ("Error initializing array document\n");
	    return false;
	}

	/* Check if this is the document we want */
	if (bson_equal (doc, &array_doc)) {
	    return true;
	}
    }

    return false;
}

/* --------------------- */
/* Database operations   */
/* --------------------- */

bool
run_database_op (mongoc_database_t *db,
		 const char *name,
		 bson_t *arguments,
		 bson_t *operation)
{
    /* No database commands are yet supported. */
    printf ("Error: unsupported database command '%s'\n", name);
    return false;
}

/* --------------------- */
/* Collection operations */
/* --------------------- */

bool
run_find (mongoc_collection_t *coll,
	  bson_t *arguments,
	  bson_t *operation)
{
    mongoc_cursor_t *cursor;
    const uint8_t *results_data;
    const uint8_t *filter_data;
    const uint8_t *sort_data;
    uint32_t results_len;
    uint32_t filter_len;
    uint32_t sort_len;
    int cursor_len = 0;
    int num_results = 0;
    bson_t results = BSON_INITIALIZER;
    bson_t filter = BSON_INITIALIZER;
    bson_t sort = BSON_INITIALIZER;
    bson_iter_t iter;
    bson_t opts;
    const bson_t *doc;

    /* Parse out find arguments. */
    bson_iter_init (&iter, arguments);
    while (bson_iter_next (&iter)) {
	const char *arg;

	arg = bson_iter_key (&iter);
	if (strcmp (arg, "filter") == 0) {
	    /* Filter. */
	    if (!BSON_ITER_HOLDS_DOCUMENT (&iter)) {
		printf ("Error: expected filter to be a document.\n");
		return false;
	    }

	    bson_iter_document (&iter, &filter_len, &filter_data);
	    if (!bson_init_static (&filter, filter_data, filter_len)) {
		printf ("Error: could not initialize filter\n");
		return false;
	    }
	} else if (strcmp (arg, "sort") == 0) {
	    /* Sort. */
	    if (!BSON_ITER_HOLDS_DOCUMENT (&iter)) {
		printf ("Error: expected sort to be a document.\n");
		return false;
	    }

	    bson_iter_document (&iter, &sort_len, &sort_data);
	    if (!bson_init_static (&sort, sort_data, sort_len)) {
		printf ("Error: could not initialize sort\n");
		return false;
	    }
	} else {
	    printf ("Warning: skipping unsupported find argument '%s'\n", arg);
	}
    }

    bson_init (&opts);
    bson_append_document (&opts, "sort", -1, &sort);

    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);
    if (!cursor) {
	printf ("Error running find\n");
	return false;
    }

    if (!bson_iter_init_find (&iter, operation, "result") ||
	!BSON_ITER_HOLDS_ARRAY (&iter)) {
	/* No results array, return without checking. */
	mongoc_cursor_destroy (cursor);
	return true;
    }

    /* Pull results out into its own bson_t, for comparing against cursor. */
    bson_iter_array (&iter, &results_len, &results_data);
    if (!bson_init_static (&results, results_data, results_len)) {
	printf ("Error: could not initialize results\n");
	mongoc_cursor_destroy (cursor);
	return false;
    }

    /* Check that all resutls from cursor are in the results array. */
    while (mongoc_cursor_next (cursor, &doc)) {
	if (!doc_in_array (&results, doc)) {
	    mongoc_cursor_destroy (cursor);
	    return false;
	}
	cursor_len++;
    }

    mongoc_cursor_destroy (cursor);

    /* All docs from cursor are in results, now check that there aren't any extras we missed. */
    bson_iter_init (&iter, &results);
    while (bson_iter_next (&iter)) {
	num_results++;
    }

    if (cursor_len != num_results) {
	return false;
    }

    return true;
}

bool
run_insert_one (mongoc_collection_t *coll,
		bson_t *arguments,
		bson_t *operation)
{
    // TODO insert one
    //bson
    // parse out insert one arguments
    // document
    return true;
}

bool
run_collection_op (mongoc_collection_t *coll,
		   const char *name,
		   bson_t *arguments,
		   bson_t *operation)
{
    if (strcmp (name, "find") == 0) {
	return run_find (coll, arguments, operation);
    } else if (strcmp (name, "insertOne") == 0) {
	return run_insert_one (coll, arguments, operation);
    } else {
	printf ("Error: unsupported collection command '%s'\n", name);
	return false;
    }
}

/* ---------------- */
/* Main test loop   */
/* ---------------- */

void
run_tests (mongoc_client_t *client, bson_t *workload)
{
    mongoc_collection_t *coll = NULL;
    mongoc_database_t *db = NULL;
    bson_t *operations = NULL;
    const uint8_t *array_data;
    test_data_t results;
    uint32_t array_len;
    bson_iter_t iter;
    bool res;

    results.num_successes = 0;
    results.num_errors = 0;
    results.num_failures = 0;

    /* Parse out database */
    if (!bson_iter_init_find (&iter, workload, "database") ||
	!BSON_ITER_HOLDS_UTF8 (&iter)) {
	printf ("Error: could not find string database\n");
	goto cleanup;
    }

    db = mongoc_client_get_database (client, bson_iter_utf8 (&iter, NULL));

    /* Parse out collection */
    if (!bson_iter_init_find (&iter, workload, "collection") ||
	!BSON_ITER_HOLDS_UTF8 (&iter)) {
	printf ("Error: could not find string collection\n");
	goto cleanup;
    }

    coll = mongoc_database_get_collection (db, bson_iter_utf8 (&iter, NULL));

    /* Parse out operations */
    if (!bson_iter_init_find (&iter, workload, "operations") ||
	!BSON_ITER_HOLDS_ARRAY (&iter)) {
	printf ("Error: could not find operations array\n");
	goto cleanup;
    }

    bson_iter_array (&iter, &array_len, &array_data);
    operations = bson_new_from_data (array_data, array_len);

    /* Run through all operations until signaled by astrolable. */
    while (true) {
	/* Check signal flag - must check here in case of empty operations array. */
	if (signaled()) {
	    write_output (&results);
	    goto cleanup;
	}

	bson_iter_init (&iter, operations);
	while (bson_iter_next (&iter)) {
	    uint32_t len;
	    uint32_t args_len;
	    const uint8_t *data;
	    const uint8_t *args_data;
	    bson_t operation;
	    bson_t arguments;
	    const char *object;
	    const char *name;
	    bson_error_t error;

	    /* Check signal flag */
	    if (signaled()) {
		write_output (&results);
		goto cleanup;
	    }

	    if (!BSON_ITER_HOLDS_DOCUMENT (&iter)) {
		printf ("Error: expected operation type to be a document\n");
		goto cleanup;
	    }

	    bson_iter_document (&iter, &len, &data);
	    if (!bson_init_static (&operation, data, len)) {
		printf ("Error: could not parse operation document\n");
		goto cleanup;
	    }

	    /* Each operation is a document with the following fields: */
	    /* Object (string): either “database” or “collection”. */
	    if (!bson_iter_init_find (&iter, &operation, "object") ||
		!BSON_ITER_HOLDS_UTF8 (&iter)) {
		printf ("Error: could not parse object field\n");
		goto cleanup;
	    }
	    object = bson_iter_utf8 (&iter, NULL);

	    /* Name (string): name of the operation. */
	    if (!bson_iter_init_find (&iter, &operation, "name") ||
		!BSON_ITER_HOLDS_UTF8 (&iter)) {
		printf ("Error: could not parse name field\n");
		goto cleanup;
	    }
	    name = bson_iter_utf8 (&iter, NULL);

	    /* Arguments (document): the names and values of arguments to be passed to the operation. */
	    if (!bson_iter_init_find (&iter, &operation, "arguments") ||
		!BSON_ITER_HOLDS_DOCUMENT (&iter)) {
		printf ("Error: could not parse arguments field\n");
		goto cleanup;
	    }

	    bson_iter_document (&iter, &args_len, &args_data);
	    if (!bson_init_static (&arguments, args_data, args_len)) {
		printf ("Error: could not parse arguments document\n");
		goto cleanup;
	    }

	    printf ("operation: %s\n", bson_as_json (&operation, NULL));

	    if (strcmp (object, "database") == 0) {
		/* Run on database. */
		res = run_database_op (db, name, &arguments, &operation);
		
	    } else {
		/* Run on collection. */
		res = run_collection_op (coll, name, &arguments, &operation);
	    }

	    if (res) {
		results.num_successes++;
	    } else {
		// TODO: determine if it was an error or a failure.
		results.num_failures++;
	    }
	}
    }

 cleanup:
    mongoc_collection_destroy (coll);
    mongoc_database_destroy (db);
}

int
main (int argc, char *argv[])
{
    mongoc_client_t *client;
    mongoc_uri_t *uri;
    const char *uri_str;
    const char *json_blob;
    bson_error_t error;
    bson_t *workload;

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

    /* Parse JSON input */
    printf ("json blob is %s\n", json_blob);
    workload = parse_json (json_blob);
    if (!workload) {
	mongoc_uri_destroy (uri);
	return EXIT_FAILURE;
    }

    client = mongoc_client_new_from_uri (uri);

    run_tests (client, workload);

    /* Clean up before exiting. */
    mongoc_client_destroy (client);
    mongoc_uri_destroy (uri);

    return EXIT_SUCCESS;
}
