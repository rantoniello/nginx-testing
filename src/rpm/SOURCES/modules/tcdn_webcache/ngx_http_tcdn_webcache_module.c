/**
 * @file ngx_http_tcdn_webcache_module.c
 * @author Rafael Antoniello
 * @date March, 2018
 *
 * @brief  TCDN-webcache module for Nginx.
 * - Draft -
 * TODO:
 * - Add code for pushing buckets.json (e.g. POST on some API URL) instead
 * of using thread-pool;
 * - Unitary testing.
 * - Formal functional testing.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <curl/curl.h>
#include <json-c/json.h>

/* **** Definitions **** */

/**
 * Internal redirection prefix path for all requests to be proxied.
 * The incoming request's HTTP host-header will be parsed and used to
 * accordingly redirect to a dynamically configured origin-server as follows
 * (the following code will be part of the to the Nginx configuration file):
 * @code
 * server {
 *     listen       8080;
 *     server_name  this_host.example.com;
 *     ...
 *     location ~ /proxy/(.*) {
 *         ...
 *         #resolver 8.8.8.8; # Use corresponding DNS if applicable...
 *         proxy_pass http://$1;
 *     }
 *     ...
 * }
 * @endcode
 */
#define INT_REDIR_PATH "/proxy/"

/**
 * Maximum URI path length in bytes.
 * The URI is conformed by the path and query-string concatenated to the
 * upstream <host:port> pair. For most cases, URI length will be less than this
 * value, and stack memory will be used internally to manage the request.
 * If a specific request exceeds this length, this module will use the heap
 * memory to manage it. Nevertheless, if the URI specified exceeds the
 * defined 'URI_MAX_LEN_GUARD', it will be declined and an error trace
 * logged.
 */
#define URI_MAX_LEN 1024

/**
 * Maximum URI path length guard in bytes.
 * This guard is for sanity check; we will assume no URI can exceed this
 * length. See 'URI_MAX_LEN' for details.
 */
#define URI_MAX_LEN_GUARD 16384

/**
 * Maximum internal redirection full-path length.
 * This value is supposed to cope with almost all common requests.
 * Nevertheless, some requests may exceed this value; in that case heap
 * memory may be used. See 'URI_MAX_LEN' and 'URI_MAX_LEN_GUARD'.
 */
#define INT_REDIR_PATH_MAX_LEN \
	(sizeof(INT_REDIR_PATH)+ sizeof("255.255.255.255:65535")+ URI_MAX_LEN)

/**
 * Bucket Identifier for web-caching.
 */
#define BUCKET_JSON_PLATFORM 8

/** Source code file-name without path */
#define __FILENAME__ strrchr("/" __FILE__, '/') + 1

/**
 * Logs for local debugging.
 * Un-comment 'ENABLE_DEBUG_LOGS' to trace. Disable on release version.
 */
#define ENABLE_DEBUG_LOGS //un-comment to trace logs
#ifdef ENABLE_DEBUG_LOGS
	/**
	 * Log macro for locally debugging.
	 * We directly use 'ngx_log_error' as it is used internally for debugging
	 * tracing functions (see for example 'ngx_log_debug').
	 * Un-comment 'ENABLE_DEBUG_LOGS' to trace. Disable on release version.
	 */
	#define LOGD(NGX_LOG, FMT, ...) \
	if((NGX_LOG)) { \
		ngx_log_error(NGX_LOG_ALERT, NGX_LOG, 0, "\n \x1B[33m %s:%s:%d: "\
		FMT"\x1B[0m \n", __FILENAME__, __func__, __LINE__, ##__VA_ARGS__); \
	}
#else
	/**
	 * Log macro for locally debugging.
	 * We directly use 'ngx_log_error' as it is used internally for debugging
	 * tracing functions (see for example 'ngx_log_debug').
	 * Un-comment 'ENABLE_DEBUG_LOGS' to trace. Disable on release version.
	 */
	#define LOGD(NGX_LOG, FMT, ...)
#endif

/**
 * Generic trace for tracking check-points failures.
 * Thought to be enabled even in release version.
 */
#define CHECK_DO(COND, ACTION) \
	if(!(COND)) {\
		ngx_log_error(NGX_LOG_ERR, ngx_log, 0, \
			"%s:%d: Check point failed.\n", __FILE__, __LINE__);\
		ACTION;\
	}

/**
 * Simple ASSERT implementation: does not exit the program but just outputs
 * an error trace.
 * Thought to be enabled even in release version.
 */
#define ASSERT(COND) \
	if(!(COND)) {\
		ngx_log_error(NGX_LOG_ERR, ngx_log, 0, \
			"%s:%d: Assertion failed.\n", __FILE__, __LINE__);\
	}

/**
 * TCDN-webcache module's main configuration context structure.
 * The fields in this structure are thought to be initially configured through
 * Nginx's configuration file, and lately by any other dynamic run-time mean.
 * Apart from configurations settings, this structure will also hold status
 * or data fields to be used persistently by all the request going through this
 * module.
 */
typedef struct ngx_http_tcdn_webcache_main_conf_s {

	/* **** Settings passed through commands **** */
	/**
	 * Tracker URL.
	 * This module will synchronize with the information offered by the
	 * tracker server (accessed by tracker-API calls to this URL).
	 */
	ngx_str_t tracker_url;
	/**
	 * URI (namely, path and query-string) to be concatenated to the tracker
	 * URL to request the buckets information.
	 */
	ngx_str_t bucket_uri;
	/**
	 * Specifies the refresh period, in seconds, for the buckets information.
	 */
	ngx_uint_t bucket_update_period;

	/* **** Other variables **** */
	/**
	 * Monotonic time-stamp, in seconds, indicating the date when
	 * 'buckets information' was updated for the last time. If this value is
	 * older than a specific refresh period, the bucket should be requested
	 * and parsed again.
	 */
	volatile uint64_t bucket_json_monot_ts_secs;
	/**
	 * Tracker synchronization thread mutual-exclusion lock.
	 * This lock should be acquired to access 'flag_sync_tracker_locked'.
	 */
	ngx_thread_mutex_t sync_tracker_thr_mutex;
	/**
	 * Tracker synchronization thread locked (processing) flag.
	 * If this flag is set, it means the buckets information is being updated
	 * by a parallel thread.
	 */
	volatile int flag_sync_tracker_locked;
	/*
	 * Web-caching buckets register.
	 * We work with two copies to be able to perform "ping-pong" buffering
	 * strategy to optimize parallel buckets access.
	 */
#define JOBJ_BUCKETS_CACHE_NUM 2
	struct json_object *jobj_buckets_cache[JOBJ_BUCKETS_CACHE_NUM];
	/**
	 * Web-caching buckets register current index.
	 */
	volatile int buckets_cache_idx;
	/*
	 * Web-caching buckets mutual-exclusion lock.
	 * This lock should be acquired to access 'jobj_buckets_cache[]'.
	 */
	ngx_thread_mutex_t jobj_buckets_cache_mutex;
	/**
	 * Pointer to module's main context memory pool.
	 */
	ngx_pool_t *ngx_pool;
	/**
	 * Pointer to module's thread-pool.
	 * To enable Nginx's thread pool support, Nginx core *MUST* be compiled
	 * with the configuration option '--with-threads'.
	 * The pool is used for multi-threaded blocking tasks (e.g. reading and
	 * sending of files) without blocking main worker processes.
	 * (reference: http://nginx.org/en/docs/dev/development_guide.html#threads).
	 */
	ngx_thread_pool_t *ngx_thread_pool;
	/**
	 * Tracker synchronization thread task context structure.
	 * This structure holds the thread function (handler), etc.
	 */
	ngx_thread_task_t *ngx_sync_tracker_thread_task;
} ngx_http_tcdn_webcache_main_conf_t;

/**
 * Curl memory context structure.
 * This type will be used as the private data passed to the read callback
 * function used by our libcurl's handler implementation
 * (refer to function 'curl_write_body_callback()').
 */
typedef struct curl_mem_ctx_s {
  char *data;
  size_t size;
  ngx_log_t *ngx_log;
} curl_mem_ctx_t;

/* **** Prototypes **** */

static ngx_int_t ngx_http_tcdn_webcache_init(ngx_conf_t *ngx_conf);

static void* ngx_http_tcdn_webcache_main_conf_create(ngx_conf_t *ngx_conf);
static void ngx_http_tcdn_webcache_main_conf_release(
		ngx_http_tcdn_webcache_main_conf_t **ref_main_conf,
		ngx_pool_t *ngx_pool, ngx_log_t *ngx_log);
static char* ngx_http_tcdn_webcache_set_main(ngx_conf_t *ngx_conf,
		ngx_command_t *ngx_command, void *opaque_main_conf);
static void exit_process(ngx_cycle_t *cycle);
static void exit_master(ngx_cycle_t *cycle);

static ngx_int_t ngx_http_tcdn_webcache_handler_phase0(ngx_http_request_t *r);
static ngx_int_t buckets_information_fetch_host_origin(
		ngx_http_tcdn_webcache_main_conf_t *main_conf,
		ngx_http_headers_in_t *headers_in, ngx_log_t *ngx_log,
		char **ref_orig_host, char **ref_orig_port);
static ngx_int_t buckets_information_fetch_host_origin2(
		struct json_object *jobj_buckets_cache, ngx_str_t *hdr_host,
		ngx_log_t *ngx_log, char **ref_orig_host, char **ref_orig_port);
static ngx_int_t perform_http_internal_redirect(ngx_http_request_t *r,
		ngx_log_t *ngx_log, char *orig_host, char *orig_port);

static ngx_int_t synchronize_buckets_information(
		ngx_http_tcdn_webcache_main_conf_t *main_conf, ngx_log_t *ngx_log);
static ngx_int_t synchronize_buckets_information_launch_thread(
		ngx_http_tcdn_webcache_main_conf_t *main_conf, ngx_log_t *ngx_log);

static void sync_tracker_thr(void *data, ngx_log_t *ngx_log_arg);
static size_t curl_write_body_callback(void *contents, size_t size,
		size_t nmemb, void *userp);
static void sync_tracker_thr_completion(ngx_event_t *ev);

/* **** Nginx module-specific definitions **** */

/**
 * TCDN-webcache module's directives:<br>
 * Define a set of "commands" this module will be able to handle.
 * Each command is represented by the structure type 'ngx_command_t' in which
 * it is specified:
 * <ul>
 *
 * <li>The directive's name (string with no spaces):<br>
 * it will be referenced in Nginx's configuration file, for example, as follows:
 * @code
 * location / {
 *     my_directive_name my_directive_arg;
 *     ...
 * }
 * @endcode
 * </li>
 * <li>The directive type (string with no spaces):<br>
 * The type refers to a set of flags that indicate where the directive is
 * legal (e.g. NGX_HTTP_(MAIN/SRV/LOC/...)_CONF to indicate the directive is
 * valid in the main/server/location/... configuration respectively) and how
 * many arguments the directive takes (e.g. NGX_CONF_(NOARGS/TAKE1/TAKE7/...).
 * <br>
 * Please refer to 'ngx_conf_file.h'
 * (https://github.com/nginx/nginx/blob/master/src/core/ngx_conf_file.h).
 * </li>
 * <li>The 'set' callback:<br>
 * Pointer to a function for setting up part of the module’s configuration;
 * typically this function will translate the arguments passed to this
 * directive and save an appropriate value in its configuration structure.
 * This setup function will be called when the directive is encountered.
 * </li>
 * <li>Configuration structure identifier:<br>
 * This identifier is used to indicate to the 'set' callback in which
 * configuration structure to save/store the data; namely, it tells Nginx
 * whether a value will get saved to the module’s main configuration, server
 * configuration, or location configuration (with NGX_HTTP_MAIN_CONF_OFFSET,
 * NGX_HTTP_SRV_CONF_OFFSET or NGX_HTTP_LOC_CONF_OFFSET respectively).
 * </li>
 * <li>Configuration structure offset:<br>
 * Specifies which field of the identified configuration structure to write to.
 * Is the offset of the structure-field with respect to the structure's base
 * memory address.
 * </li>
 *
 * </ul>
 */
static ngx_command_t ngx_http_tcdn_webcache_commands[]= {
		{
				ngx_string("tcdn_webcache"),
				NGX_HTTP_MAIN_CONF|NGX_CONF_NOARGS,
				ngx_http_tcdn_webcache_set_main,
				NGX_HTTP_MAIN_CONF_OFFSET,
				0,
				NULL
		},
		{
				ngx_string("tracker_url"),
				NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_str_slot,
				NGX_HTTP_MAIN_CONF_OFFSET,
				offsetof(ngx_http_tcdn_webcache_main_conf_t, tracker_url),
				NULL
		},
		{
				ngx_string("bucket_update_period"),
				NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_num_slot,
				NGX_HTTP_MAIN_CONF_OFFSET,
				offsetof(ngx_http_tcdn_webcache_main_conf_t,
						bucket_update_period),
				NULL
		},
		{
				ngx_string("bucket_uri"),
				NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_str_slot,
				NGX_HTTP_MAIN_CONF_OFFSET,
				offsetof(ngx_http_tcdn_webcache_main_conf_t, bucket_uri),
				NULL
		},
		ngx_null_command
};

/**
 * TCDN-webcache module's configuration functions context structure.
 * It defines the set of callback functions for creating the three
 * configurations (main, server and location) and merging them together.
 * (refer to 'ngx_http_config.h';
 * https://github.com/nginx/nginx/blob/master/src/http/ngx_http_config.h)
 */
static ngx_http_module_t ngx_http_tcdn_webcache_module_ctx = {
		NULL, //< preconfiguration
		ngx_http_tcdn_webcache_init, //< postconfiguration
		ngx_http_tcdn_webcache_main_conf_create, //< create main configuration
		NULL, //< init main configuration
		NULL, //< create server configuration
		NULL, //< merge server configuration
		NULL, //< create location conf.
		NULL //< merge location configuration
};

/**
 * TCDN-webcache module context structure.
 * The nomenclature is always<br>
 * 'ngx_http_<module name>_module'.<br>
 * This is where all references to the contexts and directives go, as well as
 * other callbacks (exit thread, exit process, etc.).
 * This is the public and unambiguous handler for the module instance.
 */
ngx_module_t  ngx_http_tcdn_webcache_module= {
		NGX_MODULE_V1,
		&ngx_http_tcdn_webcache_module_ctx, //< module context
		ngx_http_tcdn_webcache_commands, 	//< module directives
		NGX_HTTP_MODULE, 					//< module type
		NULL, 								//< init master
		NULL, 								//< init module
		NULL, 								//< init process
		NULL, 								//< init thread
		NULL, 								//< exit thread
		exit_process, 						//< exit process
		exit_master,						//< exit master
		NGX_MODULE_V1_PADDING
};

/* **** Implementations **** */

/**
 * Thread-pool unambiguous identification name. This name is used in the
 * Nginx's configuration file to set the thread-pool characteristics.
 * The configuration syntax is the following (set in the main context):<br>
 * thread_pool name threads=number [max_queue=number];<br>
 * For example:
 * @code
 * thread_pool tcdn_webcache_thread_pool threads=32 max_queue=65536;
 * @endcode
 */
static unsigned char *thread_pool_name_cstr= (unsigned char*)
		"tcdn_webcache_thread_pool";

/**
 * Postconfiguration callback. Refer to 'ngx_http_tcdn_webcache_module_ctx'.
 * @param ngx_conf
 * @return Status code NGX_OK on succeed, NGX_ERROR otherwise
 * (see 'ngx_core.h').
 */
static ngx_int_t ngx_http_tcdn_webcache_init(ngx_conf_t *ngx_conf)
{
	ngx_http_core_main_conf_t *core_main_conf;
	ngx_log_t *ngx_log;
    ngx_http_handler_pt *ngx_http_handler;

	/* Check arguments */
	if(ngx_conf== NULL)
		return NGX_ERROR;

	/* Get logs context */
	if((ngx_log= ngx_conf->log)== NULL)
		return NGX_ERROR;
	LOGD(ngx_log, "Registering 'tcdn_webcache' module... \n");

	core_main_conf= ngx_http_conf_get_module_main_conf(ngx_conf,
			ngx_http_core_module);
    CHECK_DO(core_main_conf!= NULL, return NGX_ERROR);

    /* Push handler to phases array */
    ngx_http_handler= ngx_array_push(&core_main_conf->
    		phases[NGX_HTTP_POST_READ_PHASE].handlers);
    CHECK_DO(ngx_http_handler!= NULL, return NGX_ERROR);

    *ngx_http_handler= ngx_http_tcdn_webcache_handler_phase0;

    LOGD(ngx_log, "Registering 'tcdn_webcache' module succeed.\n");
    return NGX_OK;
}

/**
 * Allocates and initializes main configuration context structure.
 * Refer to 'ngx_http_tcdn_webcache_module_ctx'.
 * @param ngx_conf
 * @return Main configuration context structure if succeeds, NGX_CONF_ERROR
 * otherwise (see 'ngx_conf_file.h').
 */
static void* ngx_http_tcdn_webcache_main_conf_create(ngx_conf_t *ngx_conf)
{
	ngx_int_t end_code= NGX_ERROR;
	ngx_http_tcdn_webcache_main_conf_t *main_conf= NULL, **ref_main_conf= NULL;
	ngx_log_t *ngx_log= NULL; //alias
	ngx_pool_t *main_conf_pool= NULL; //alias
	ngx_thread_pool_t *thread_pool= NULL; //alias
	ngx_thread_task_t *sync_tracker_thread_task= NULL;
	ngx_str_t thread_pool_name= {
			strlen((const char*)thread_pool_name_cstr),
			thread_pool_name_cstr
	};

	/* Check arguments */
	if(ngx_conf== NULL)
		return NGX_CONF_ERROR;

	/* Get logs context */
	if((ngx_log= ngx_conf->log)== NULL)
		return NGX_CONF_ERROR;
	LOGD(ngx_log, "Initializing 'tcdn_webcache' module main context... \n");

	/* Get memory pool */
	main_conf_pool= ngx_conf->pool;
	CHECK_DO(main_conf_pool!= NULL, goto end);

	/* Allocate main configuration context structure in the pool */
	main_conf= ngx_pcalloc(main_conf_pool, sizeof(
			ngx_http_tcdn_webcache_main_conf_t));
	CHECK_DO(main_conf!= NULL, goto end);
	LOGD(ngx_log, "(module main context pointer is= %p; memory pool pointer "
			"is= %p)\n", main_conf, main_conf_pool);

	/* **** Initialize context structure ****
	 * IMPORTANT NOTE: all the fields of the
	 * 'ngx_http_tcdn_webcache_main_conf_t' structure corresponding
	 * to commands (see 'ngx_http_tcdn_webcache_commands') **MUST** be
	 * initialized to 'NGX_CONF_UNSET' or its equivalent.
	 * Otherwise, we will receive a 'directive is duplicate ' error while
	 * parsing the configuration file when treating module's commands.
	 * This is because if is not explicitly set to 'NGX_CONF_UNSET', Nginx
	 * parser assumes that this field is already initialized, and throws the
	 * error.
	 */

	// Set by ngx_pcalloc(): main_conf->tracker_url= { 0, NULL };

	main_conf->bucket_update_period= NGX_CONF_UNSET;

	// Set by ngx_pcalloc(): main_conf->bucket_uri= { 0, NULL };

	// Set by ngx_pcalloc(): main_conf->bucket_json_monot_ts_secs= 0;

    CHECK_DO(ngx_thread_mutex_create(&main_conf->sync_tracker_thr_mutex,
    		ngx_log)==NGX_OK, goto end);

    // Set by ngx_pcalloc():  main_conf->flag_sync_tracker_locked= 0;

    // Set by ngx_pcalloc(): main_conf->jobj_buckets_cache[2]= {NULL, NULL};

    // Set by ngx_pcalloc(): main_conf->buckets_cache_idx= 0

    CHECK_DO(ngx_thread_mutex_create(&main_conf->jobj_buckets_cache_mutex,
    		ngx_log)==NGX_OK, goto end);

    main_conf->ngx_pool= main_conf_pool;

	thread_pool= ngx_thread_pool_get(ngx_conf->cycle, &thread_pool_name);
	CHECK_DO(thread_pool!= NULL, goto end);
	main_conf->ngx_thread_pool= thread_pool;

	/* Allocate Nginx generic tasks context structure (see 'ngx_thread_task_t').
	 * Note that 'ngx_thread_task_t' is extended the necessary bytes to
	 * allocate our private thread context structure; thus, 'size' parameter
	 * refers to our private context size.
	 * To later access our private structure, just get 'ngx_thread_task_s::ctx'.
	 * We will just pass the pointer to our already allocated "main context".
	 */
	sync_tracker_thread_task= ngx_thread_task_alloc(ngx_conf->pool, sizeof(
			ngx_http_tcdn_webcache_main_conf_t*));
	CHECK_DO(sync_tracker_thread_task!= NULL, goto end);
	main_conf->ngx_sync_tracker_thread_task= sync_tracker_thread_task;

	sync_tracker_thread_task->handler= sync_tracker_thr;

	/* Implementation note: 'ngx_thread_task_t::event.handler'
	 * is strictly associated with the request that launches the thread.
	 * Thus, if the request is finished, this callback will *NEVER* be called
	 * (should be used with EAGAIN and an alive request... this is the reason
	 * why we do not use it). Nevertheless, it is mandatory to define the
	 * pointer (setting it to NULL is not allowed and causes a fault).
	 */
	sync_tracker_thread_task->event.handler= sync_tracker_thr_completion;
	sync_tracker_thread_task->event.data= sync_tracker_thread_task->ctx;

	/* Initialize Nginx task structure private context */
	ref_main_conf= (ngx_http_tcdn_webcache_main_conf_t**)
			sync_tracker_thread_task->ctx;
	*ref_main_conf= main_conf;

	// Reserved for future use: initialize new fields here...

	/* We also use this space for globally initialize libcurl.
	 * Library libcurl will be used, for example, to request the buckets.json
	 * from the tracker.
	 */
	curl_global_init(CURL_GLOBAL_ALL);

	end_code= NGX_OK;
	LOGD(ngx_log, "Initialization of 'tcdn_webcache' main context succeed.\n");
end:
	if(end_code!= NGX_OK)
		ngx_http_tcdn_webcache_main_conf_release(&main_conf, main_conf_pool,
				ngx_log);
	return main_conf;
}

/**
 * Releases main configuration context structure.
 * @param ref_main_conf
 * @paral ngx_pool
 * @param ngx_log
 */
static void ngx_http_tcdn_webcache_main_conf_release(
		ngx_http_tcdn_webcache_main_conf_t **ref_main_conf,
		ngx_pool_t *ngx_pool, ngx_log_t *ngx_log)
{
	int i;
	ngx_http_tcdn_webcache_main_conf_t *main_conf;

	/* Check arguments */
	if(ref_main_conf== NULL || (main_conf= *ref_main_conf)== NULL ||
			ngx_pool== NULL || ngx_log== NULL)
		return;
	LOGD(ngx_log, "Releasing 'tcdn_webcache' module main context "
			"(context pointer= %p; pool pointer= %p)... \n",
			main_conf, ngx_pool);

	/* Release tracker synchronization thread mutual-exclusion lock */
    ASSERT(ngx_thread_mutex_destroy(&main_conf->sync_tracker_thr_mutex,
    		ngx_log)==NGX_OK);

    /* Release buckets registers */
    for(i= 0; i< JOBJ_BUCKETS_CACHE_NUM; i++) {
    	struct json_object *jobj= main_conf->jobj_buckets_cache[i];
    	if(jobj!= NULL) {
    		register int loop_guard= 100, flag_obj_freed= 0;
    		while(loop_guard> 0 && flag_obj_freed== 0) {
    			// 'json_object_put()' returns 1 if the object was freed
    			flag_obj_freed= json_object_put(jobj);
    			loop_guard--;
    		}
    		main_conf->jobj_buckets_cache[i]= NULL;
    	}
    }

	/* Release web-caching buckets mutual-exclusion lock */
    ASSERT(ngx_thread_mutex_destroy(&main_conf->jobj_buckets_cache_mutex,
    		ngx_log)==NGX_OK);

    //{ //RAL: This seems to be performed automatically by Nginx's core when
    //         signaling reload | quit (assertions always fail)
#if 0
	/* Release tracker synchronization thread task context structure */
	if(main_conf->ngx_sync_tracker_thread_task!= NULL) {
		ngx_int_t ret_code= ngx_pfree(ngx_pool,
				(void*)main_conf->ngx_sync_tracker_thread_task);
		ASSERT(ret_code == NGX_OK);
	}

	/* Finally release main configuration context structure */
	if(ngx_pfree(ngx_pool, (void*)main_conf)== NGX_OK) {
		*ref_main_conf= NULL;
		LOGD(ngx_log, "Releasing 'tcdn_webcache' module main context "
				"succeed\n");
	} else {
		ASSERT(0); // Mark that 'ngx_pfree' failed.
	}
#endif
	//}

	/* As opposite to  ibcurl's 'curl_global_init()', we clean it up */
	curl_global_cleanup();
}

/**
 * Module's command setter function.
 * @param ngx_conf
 * @param ngx_command
 * @param opaque_main_conf
 * @return NGX_CONF_OK if succeed, NGX_CONF_ERROR otherwise
 * (see 'ngx_conf_file.h').
 */
static char* ngx_http_tcdn_webcache_set_main(ngx_conf_t *ngx_conf,
		ngx_command_t *ngx_command, void *opaque_main_conf)
{
	ngx_log_t *ngx_log;

	/* Check arguments */
	if(ngx_conf== NULL || ngx_command== NULL || opaque_main_conf== NULL)
		return NGX_CONF_ERROR;

	/* Get logs context */
	if((ngx_log= ngx_conf->log)== NULL)
		return NGX_CONF_ERROR;
	LOGD(ngx_log, "Executing 'tcdn_webcache' main context setter... \n");

	// Reserved for future use...

	LOGD(ngx_log, "The 'tcdn_webcache' main context setter succeed.\n");
	return NGX_CONF_OK;
}

/**
 * Module process exit callback.
 * @param cycle
 */
static void exit_process(ngx_cycle_t *cycle)
{
	ngx_log_t *ngx_log;
	ngx_http_tcdn_webcache_main_conf_t *main_conf;

	if(cycle== NULL || (ngx_log= cycle->log)== NULL)
		return;

	LOGD(ngx_log, "Executing 'tcdn_webcache' exit process callback.\n");

	main_conf= ngx_http_cycle_get_module_main_conf(cycle,
			ngx_http_tcdn_webcache_module);
	CHECK_DO(main_conf!= NULL, return);
	LOGD(ngx_log, "(module main context pointer was= %p; WE HAVE TO RELEASE "
			"IT)\n", main_conf);

	ngx_http_tcdn_webcache_main_conf_release(&main_conf, main_conf->ngx_pool,
			ngx_log);

	LOGD(ngx_log, "The 'tcdn_webcache' exit process succeed.\n");
}

/**
 * Module master exit callback.
 * @param cycle
 */
static void exit_master(ngx_cycle_t *cycle)
{
	ngx_log_t *ngx_log;

	if(cycle!= NULL && (ngx_log= cycle->log)!= NULL)
		LOGD(ngx_log, "Executing 'tcdn_webcache' exit master callback.\n");
}

/**
 * Module's request-handler function.
 * Nginx's handlers typically do four things:
 *     -# get the main/server/location configuration,
 *     -# generate an appropriate response,
 *     -# send the response header,
 *     -# and send the body.
 *     .
 * Additionally, response can be delegated to a proxy server either by
 * using an internal redirection (see 'ngx_http_internal_redirect()') or by
 * performing a so-called sub-request ('ngx_http_subrequest').
 * @param r HTTP request context structure (includes information such as
 * request method, URI, and headers).
 * @return Status code NGX_OK on succeed. See 'ngx_core.h' for other values.
 */
static ngx_int_t ngx_http_tcdn_webcache_handler_phase0(ngx_http_request_t *r)
{
	ngx_connection_t *ngx_connection;
	ngx_log_t *ngx_log;
	ngx_http_tcdn_webcache_main_conf_t *main_conf;
	ngx_int_t ret_code;
	char *orig_host= NULL, *orig_port= NULL;

	/* Check arguments */
	if(r== NULL || (ngx_connection= r->connection)== NULL ||
			(ngx_log= ngx_connection->log)== NULL)
		return NGX_ERROR;

	LOGD(ngx_log, "Uri: '%s' (len= %d); args: '%s' (%d)\n",
			r->uri.data? r->uri.data: (u_char*)"none",
			(int)r->uri.len,
			r->args.data? r->args.data: (u_char*)"none",
			(int)r->args.len);

	/* Get module's main configuration context structure */
	main_conf= ngx_http_get_module_main_conf(r, ngx_http_tcdn_webcache_module);
	CHECK_DO(main_conf!= NULL, return NGX_ERROR);

	/* Synchronize buckets information.
	 * We just check for errors but *always* continue with request processing!
	 * Synchronization never blocks request in process (it is launched in
	 * parallel and as a detached thread).
	 */
	ret_code= synchronize_buckets_information(main_conf, ngx_log);
	ASSERT(ret_code== NGX_OK); // just check and trace if error occurred

	ret_code= buckets_information_fetch_host_origin(main_conf, &r->headers_in,
			ngx_log, &orig_host, &orig_port);
	CHECK_DO(ret_code== NGX_OK, return NGX_ERROR);

	/* Redirect internally to proxied path */
	return perform_http_internal_redirect(r, ngx_log, orig_host, orig_port);
}

/**
 * Fetch origin server corresponding to the declared HTTP host-header.
 */
static ngx_int_t buckets_information_fetch_host_origin(
		ngx_http_tcdn_webcache_main_conf_t *main_conf,
		ngx_http_headers_in_t *headers_in, ngx_log_t *ngx_log,
		char **ref_orig_host, char **ref_orig_port)
{
	ngx_table_elt_t *host;
	ngx_thread_mutex_t *p_buckets_mutex;
	ngx_int_t ret_code;

	/* Check arguments */
	if(main_conf== NULL || headers_in== NULL || ngx_log== NULL ||
			ref_orig_host== NULL || ref_orig_port== NULL)
		return NGX_ERROR;

	/* Get host-header */
	host= headers_in->host;
	CHECK_DO(host!= NULL, return NGX_ERROR);

    /* Get buckets information set */
    p_buckets_mutex= &main_conf->jobj_buckets_cache_mutex;
	ASSERT(ngx_thread_mutex_lock(p_buckets_mutex, ngx_log)== NGX_OK);
	ret_code= buckets_information_fetch_host_origin2(
			main_conf->jobj_buckets_cache[main_conf->buckets_cache_idx],
			&host->value, ngx_log, ref_orig_host, ref_orig_port);
    ASSERT(ngx_thread_mutex_unlock(p_buckets_mutex, ngx_log)== NGX_OK);

	return ret_code;
}

/**
 *
 */
static ngx_int_t buckets_information_fetch_host_origin2(
		struct json_object *jobj_buckets_cache, ngx_str_t *hdr_host,
		ngx_log_t *ngx_log, char **ref_orig_host, char **ref_orig_port)
{
	register int i;
	register size_t json_buckets_len;

	/* Check arguments */
	if(jobj_buckets_cache== NULL || hdr_host== NULL || ngx_log== NULL ||
			ref_orig_host== NULL || ref_orig_port== NULL)
		return NGX_ERROR;
	CHECK_DO(hdr_host->data!= NULL && hdr_host->len> 0, return NGX_ERROR);
	LOGD(ngx_log, "HTTP host-header input: '%s' (lenght: %d)\n",
			hdr_host->data, hdr_host->len);

	json_buckets_len= json_object_array_length(jobj_buckets_cache);
	printf("Filtered buckets.json to %d webcache buckets...\n",
			(int)json_buckets_len);

	/* Filter by HTTP header 'host' */
	for(i= 0; i< (int)json_buckets_len; i++) {
		register size_t origin_list_len;
		register json_bool flag_found;
		const char *host, *origin_host, *origin_port;
		struct json_object *jobj_bucket_cache;
		struct json_object *jobj_origin, *jobj_origin_host, *jobj_origin_port;
		struct json_object *jobj_aux1= NULL, *jobj_aux2= NULL;

		/* Get web-cache bucket */
		jobj_bucket_cache= json_object_array_get_idx(jobj_buckets_cache, i);
		CHECK_DO(jobj_bucket_cache!= NULL, exit(-1));

		/* We will filter the bucket by the field 'host' */
		flag_found= json_object_object_get_ex(jobj_bucket_cache, "host",
				&jobj_aux1);
		if(flag_found== 0 || jobj_aux1== NULL ||
				(host= json_object_get_string(jobj_aux1))== NULL ||
				strncmp(host, (const char*)hdr_host->data, hdr_host->len)!= 0)
			continue;
		LOGD(ngx_log, "'host' in bucket is: '%s'\n", host); //comment-me
		LOGD(ngx_log, "'awa' bucket is: '%s'\n", json_object_to_json_string(
				jobj_bucket_cache)); //comment-me

		/* Parse 'origin-server' host and port. JSON tree is as follows:
		 * {
		 *     ...
		 *     "awa_params": {
		 *         ...
		 *         "origins": {
		 *             ...
		 *             "origin_list":[
		 *                 {..., "host":"10.95.150.104", ..."port":80, ...},
		 *                 {...},
		 *                 ...
		 *             ]
		 *             ...
		 *         }
		 *         ...
		 *     }
		 *     ...
		 *     host: "myhost.example.com",
		 *     ...
		 * }
		 */
		// Get "awa_params"
		flag_found= json_object_object_get_ex(jobj_bucket_cache, "awa_params",
				&jobj_aux1);
		if(flag_found== 0 || jobj_aux1== NULL)
			continue;
		// We have 'jobj_aux1'<- "awa_params"; get 'origins'
		flag_found= json_object_object_get_ex(jobj_aux1, "origins", &jobj_aux2);
		if(flag_found== 0 || jobj_aux2== NULL)
			continue;
		// We have 'jobj_aux2'<- "origins"; get 'origin_list'
		flag_found= json_object_object_get_ex(jobj_aux2, "origin_list",
				&jobj_aux1);
		if(flag_found== 0 || jobj_aux1== NULL)
			continue;
		// We have 'jobj_aux1'<- "origin_list

		/* We will take the first entry available */
		origin_list_len= json_object_array_length(jobj_aux1);
		if(!origin_list_len)
			continue;
		jobj_origin= json_object_array_get_idx(jobj_aux1, 0);
		CHECK_DO(jobj_origin!= NULL, exit(-1));

		/* origin-host */
		flag_found= json_object_object_get_ex(jobj_origin, "host",
				&jobj_origin_host);
		if(flag_found!= 0) {
			origin_host= json_object_get_string(jobj_origin_host);
			if(origin_host!= NULL && strlen(origin_host)> 0) {
				LOGD(ngx_log, "origin-host: '%s'\n", origin_host);
				*ref_orig_host= strdup(origin_host);
			}
		}

		/* origin-port */
		flag_found= json_object_object_get_ex(jobj_origin, "port",
				&jobj_origin_port);
		if(flag_found!= 0) {
			origin_port= json_object_get_string(jobj_origin_port);
			if(origin_port!= NULL && strlen(origin_port)> 0) {
				LOGD(ngx_log, "origin-port: '%s'\n", origin_port);
				*ref_orig_port= strdup(origin_port);
			}
		}
		break;
	}
	return NGX_OK;
}

/**
 * Performs an internal redirection to a given configured location.
 * @param r HTTP request context structure (includes information such as
 * request method, URI, and headers).
 * @param ngx_log Log context structure.
 * @return Status code NGX_OK on succeed. See 'ngx_core.h' for other values.
 */
static ngx_int_t perform_http_internal_redirect(ngx_http_request_t *r,
		ngx_log_t *ngx_log, char *orig_host, char *orig_port)
{
	register size_t uri_args_len;
	ngx_int_t end_code= NGX_ERROR;
	int redir_max_len= INT_REDIR_PATH_MAX_LEN;
	char new_uri[INT_REDIR_PATH_MAX_LEN]= {0}, *p_new_uri= new_uri;
	char *new_uri_dyn= NULL; //release-me (heap-allocated)
	ngx_str_t ngx_str_proxy_selected= {0};

	/* Check arguments */
	if(r== NULL || ngx_log== NULL || orig_host== NULL || orig_port== NULL)
		return NGX_ERROR;

	/* Sanity checks... */
	uri_args_len= r->uri.len+ r->args.len;
	if((r->uri.len> 0 && r->uri.data== NULL) ||
			(r->args.len> 0 && r->args.data== NULL) ||
			uri_args_len> URI_MAX_LEN_GUARD) {
		CHECK_DO(0, goto end)
	}

	/* We should normally use stack-allocated URL's (this code is supposed
	 * rare to be executed)
	 */
	if(uri_args_len> URI_MAX_LEN) {
		ngx_log_error(NGX_LOG_ALERT, ngx_log, 0, "Request URI is very long "
				"(%d characters). Managing with heap.\n", (int)uri_args_len);
		redir_max_len= INT_REDIR_PATH_MAX_LEN- URI_MAX_LEN+ uri_args_len;
		new_uri_dyn= (char*)calloc(1, redir_max_len);
		CHECK_DO(new_uri_dyn!= NULL, goto end);
		p_new_uri= new_uri_dyn;
	}

	/* Print new redirection URI */
	snprintf(p_new_uri, redir_max_len- 1, "%s%s:%s%.*s?%.*s", INT_REDIR_PATH,
			orig_host, orig_port,
			(int)r->uri.len, r->uri.data!= NULL? r->uri.data: (u_char*)"",
			(int)r->args.len, r->args.data!= NULL? r->args.data: (u_char*)"");

	/* Actually redirect with given arguments (namely, query-string) */
	LOGD(ngx_log, "Performing internal redirection to URI '%s'... \n",
			p_new_uri);
	ngx_str_proxy_selected.len= strlen(p_new_uri);
	ngx_str_proxy_selected.data= (u_char*)p_new_uri;
	end_code= ngx_http_internal_redirect(r, &ngx_str_proxy_selected, NULL);

end:
	if(new_uri_dyn!= NULL)
		free(new_uri_dyn);
	return end_code;
}

/**
 * Creates tracker synchronization off-load task resources.
 * This function allocates and initializes the related resources to finally
 * launch the off-load task in a parallel thread (using the thread-pool
 * defined for this module).
 * @param main_conf Module's main configuration context structure.
 * @param ngx_log Nginx's log context structure.
 * @return Status code NGX_OK on succeed, NGX_ERROR otherwise
 * (see 'ngx_core.h').
 */
static ngx_int_t synchronize_buckets_information(
		ngx_http_tcdn_webcache_main_conf_t *main_conf, ngx_log_t *ngx_log)
{
	register uint64_t curr_ts_secs; //Current monotonic time-stamp [seconds]
	register uint64_t bucket_json_monot_ts_secs; //Last time updated
	register ngx_uint_t bucket_update_period;
	struct timespec ts_curr= {0};
	ngx_int_t end_code= NGX_OK;

	/* Check arguments */
	if(main_conf== NULL || ngx_log== NULL)
		return NGX_ERROR;
	LOGD(ngx_log, "Checking if buckets information is up to date.\n");

	/* Get current monotonic time-stamp */
	CHECK_DO(clock_gettime(CLOCK_MONOTONIC, &ts_curr)== 0, return NGX_ERROR);
	curr_ts_secs= (uint64_t)ts_curr.tv_sec;
	LOGD(ngx_log, "Current TS is: %"PRIu64"\n", curr_ts_secs);

	/* If "fresh period" timed-out, we have to refresh cached 'bucket.json' */

	bucket_update_period= main_conf->bucket_update_period;
	LOGD(ngx_log, "Buckets refresh time set to: %d\n",
			(int)bucket_update_period);

	bucket_json_monot_ts_secs= main_conf->bucket_json_monot_ts_secs;
	LOGD(ngx_log, "Last buckets refresh TS: %"PRIu64"\n",
			bucket_json_monot_ts_secs);

	if(curr_ts_secs> bucket_json_monot_ts_secs+ bucket_update_period) {
		ngx_thread_mutex_t *p_sync_mutex= &main_conf->sync_tracker_thr_mutex;

		/* Lock tracker synchronizing set */
		LOGD(ngx_log, "Trying to lock tracker synchronizing set... \n");
		ASSERT(ngx_thread_mutex_lock(p_sync_mutex, ngx_log)== NGX_OK);

		if(main_conf->flag_sync_tracker_locked== 0) {
			/* Synchronizing thread will be launched */
			end_code= synchronize_buckets_information_launch_thread(main_conf,
					ngx_log);
		} else {
			/* Synchronizing thread already launched... nothing to do */
			LOGD(ngx_log, "tracker synchronizing set already locked.\n");
		}

		ASSERT(ngx_thread_mutex_unlock(p_sync_mutex, ngx_log)== NGX_OK);
	} else {
		LOGD(ngx_log, "Buckets are up to date!\n");
	}
    return end_code;
}

/**
 * Launches buckets information synchronization thread.
 * @param main_conf Module's main configuration context structure.
 * @param ngx_log Nginx's log context structure.
 * @return Status code NGX_OK on succeed, NGX_ERROR otherwise
 * (see 'ngx_core.h').
 */
static ngx_int_t synchronize_buckets_information_launch_thread(
		ngx_http_tcdn_webcache_main_conf_t *main_conf, ngx_log_t *ngx_log)
{
	ngx_thread_pool_t *thread_pool;
	ngx_thread_task_t *thread_task;

	/* Check arguments */
	if(main_conf== NULL || ngx_log== NULL)
		return NGX_ERROR;

	/* Check thread pool (permanently available in main context) */
	thread_pool= main_conf->ngx_thread_pool;
	CHECK_DO(thread_pool!= NULL, return NGX_ERROR);

	/* Check task */
	thread_task= main_conf->ngx_sync_tracker_thread_task;
	CHECK_DO(thread_task!= NULL, return NGX_ERROR);

	/* Actually launch the off-load thread */
	LOGD(ngx_log, "Launching the off-load thread\n");
	CHECK_DO(ngx_thread_task_post(thread_pool, thread_task)== NGX_OK,
			return NGX_ERROR);

	/* Succeed-> lock synchronization flag */
	main_conf->flag_sync_tracker_locked= 1;
	LOGD(ngx_log, "tracker synchronizing set locked O.K.!\n");
	return NGX_OK;
}

/**
 * Tracker synchronization thread function.
 * This function is executed in a separate thread of the thread-pool.
 * @param data Opaque pointer to our private thread context structure; in our
 * case, a reference to the pointer to 'ngx_http_tcdn_webcache_main_conf_t'.
 * @param log_arg Nginx's log context structure for the thread's context.
 */
static void sync_tracker_thr(void *data, ngx_log_t *ngx_log)
{
	ngx_str_t *ref_tracker_url, *ref_bucket_uri;
	ngx_thread_mutex_t *p_sync_mutex;
	register uint64_t curr_ts_secs; //Current monotonic time-stamp [seconds]
	register int i, buckets_cache_idx_new, json_buckets_len;
	ngx_thread_mutex_t *p_buckets_mutex;
    int end_code= NGX_ERROR;
    ngx_http_tcdn_webcache_main_conf_t *main_conf= NULL; // alias
	char *tracker_fullurl= NULL; // release-me (heap allocated)
	size_t tracker_fullurl_size= 0;
    CURL *curl_handle= NULL; // release-me (heap allocated)
    curl_mem_ctx_t curl_mem_ctx= {0}; // release-me (has heap allocated member)
    CURLcode curl_code= CURLE_COULDNT_CONNECT; // initialize to any error...
    struct json_object *jobj_buckets= NULL, // release-me (heap allocated)
    		*jobj_buckets_cache= NULL; // release-me (heap allocated)
    struct timespec ts_curr= {0};

    /* Check arguments */
    if(data== NULL || ngx_log== NULL)
    	return;

    /* Get main configuration context structure */
    main_conf= *(ngx_http_tcdn_webcache_main_conf_t**)data;
    CHECK_DO(main_conf!= NULL, goto end);

    LOGD(ngx_log, "Entering tracker synchronization thread "
    		"(data pointer= %p)... \n", main_conf);

    /* Check bucket URL reference */
    ref_tracker_url= &main_conf->tracker_url;
    CHECK_DO(ref_tracker_url->data!= NULL && ref_tracker_url->len> 0, goto end);

    /* Get bucket path (WARNING: 'bucket_uri-data' is allowed to be NULL) */
    ref_bucket_uri= &main_conf->bucket_uri;

    /* Allocate buffer for the tracker full path */
    tracker_fullurl_size= ref_tracker_url->len+ 1;
    if(ref_bucket_uri->data!= NULL)
    	tracker_fullurl_size+= ref_bucket_uri->len;
    tracker_fullurl= (char*)calloc(1, tracker_fullurl_size);
    CHECK_DO(tracker_fullurl!= NULL, goto end);

    /* Compose tracker full URL to request the buckets information */
	snprintf(tracker_fullurl, tracker_fullurl_size, "%s%s",
			ref_tracker_url->data,
			ref_bucket_uri->data!= NULL? ref_bucket_uri->data: (u_char*)"");
    LOGD(ngx_log, "Requesting tracker: GET <- '%s'... ", tracker_fullurl);

    /* **** Prepare curl GET request **** */

    curl_mem_ctx.data= malloc(1); // will grown as needed by reallocation
    CHECK_DO(curl_mem_ctx.data!= NULL, goto end);
    curl_mem_ctx.size= 0; // no data at this point yet
    curl_mem_ctx.ngx_log= ngx_log;

    /* Initialize the curl session */
    curl_handle= curl_easy_init();
    CHECK_DO(curl_handle!= NULL, goto end);

    /* Specify URL to get */
    CHECK_DO(curl_easy_setopt(curl_handle, CURLOPT_URL, tracker_fullurl)==
    		CURLE_OK, goto end);

    /* Send all data to this function  */
    CHECK_DO(curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
    		curl_write_body_callback)== CURLE_OK, goto end);

    /* We pass our 'chunk' structure to the callback function */
    CHECK_DO(curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
    		(void *)&curl_mem_ctx)== CURLE_OK, goto end);

    /*
     * Some servers don't like requests that are made without a user-agent
     * field, so we provide one.
     */
    CHECK_DO(curl_easy_setopt(curl_handle, CURLOPT_USERAGENT,
    		"libcurl-agent/1.0")== CURLE_OK, goto end);

    /* Perform HTTP-GET method */
    if((curl_code= curl_easy_perform(curl_handle))!= CURLE_OK) {
    	ngx_log_error(NGX_LOG_ERR, ngx_log, 0,
    			"curl_easy_perform() failed: %s\n",
				curl_easy_strerror(curl_code));
    	CHECK_DO(0, goto end); // Force tracing error point
    }
    CHECK_DO(curl_mem_ctx.data!= NULL, goto end);
    LOGD(ngx_log, "successfully received buckets.json (%lu bytes retrieved)\n",
    		(long)curl_mem_ctx.size);

	/* Parse the JSON -this may be CPU-heavy- */
    LOGD(ngx_log, "Parsing buckets.json...\n");
	jobj_buckets= json_tokener_parse(curl_mem_ctx.data);
	json_buckets_len= json_object_array_length(jobj_buckets);
	LOGD(ngx_log, "The 'buckets.json' has %d buckets...\n", json_buckets_len);

	/* We only need '\"platform\": 8' buckets */
	jobj_buckets_cache= json_object_new_array();
	for(i= 0; i< json_buckets_len; i++) {
		json_bool flag_found;
		int32_t platform_val;
		struct json_object *jobj_value= NULL;
		struct json_object *jobj_bucket= json_object_array_get_idx(
				jobj_buckets, i);
		flag_found= json_object_object_get_ex(jobj_bucket, "platform",
				&jobj_value);
		if(flag_found== 0 || (platform_val= json_object_get_int(jobj_value))!=
				BUCKET_JSON_PLATFORM)
			continue;
		/* We've found a cache-proxy bucket; store it */
		// Increment the reference count of ''jobj_value, thereby grabbing
		// shared ownership of obj.
		jobj_bucket= json_object_get(jobj_bucket);
		CHECK_DO(json_object_array_add(jobj_buckets_cache, jobj_bucket)== 0,
				exit(-1));
	}
	json_buckets_len= json_object_array_length(jobj_buckets_cache);
	LOGD(ngx_log, "Tracker: filtered buckets.json to %d webcache buckets...\n",
			json_buckets_len);
	LOGD(ngx_log, "\n'%s'\n", json_object_to_json_string(
			jobj_buckets_cache)); //comment-me

    /* Release old buckets information; store new one */
    buckets_cache_idx_new= (main_conf->buckets_cache_idx+ 1)%
    		JOBJ_BUCKETS_CACHE_NUM;
	if(main_conf->jobj_buckets_cache[buckets_cache_idx_new]!= NULL) {
		struct json_object *jobj=
				main_conf->jobj_buckets_cache[buckets_cache_idx_new];
		register int loop_guard= 100, flag_obj_freed= 0;
		while(loop_guard> 0 && flag_obj_freed== 0) {
			// 'json_object_put()' returns 1 if the object was freed
			flag_obj_freed= json_object_put(jobj);
			loop_guard--;
		}
		CHECK_DO(flag_obj_freed== 1, goto end);
		main_conf->jobj_buckets_cache[buckets_cache_idx_new]= NULL;
	}
	main_conf->jobj_buckets_cache[buckets_cache_idx_new]= jobj_buckets_cache;
	jobj_buckets_cache= NULL; // Avoid aliasing
	CHECK_DO(main_conf->jobj_buckets_cache[buckets_cache_idx_new]!= NULL,
			goto end);

    /* Switch to new buckets information set */
    p_buckets_mutex= &main_conf->jobj_buckets_cache_mutex;
	ASSERT(ngx_thread_mutex_lock(p_buckets_mutex, ngx_log)== NGX_OK);
    main_conf->buckets_cache_idx= buckets_cache_idx_new;
    ASSERT(ngx_thread_mutex_unlock(p_buckets_mutex, ngx_log)== NGX_OK);

    /* Succeed -> update last refresh time-stamp */
	CHECK_DO(clock_gettime(CLOCK_MONOTONIC, &ts_curr)== 0, goto end);
	curr_ts_secs= (uint64_t)ts_curr.tv_sec;
	LOGD(ngx_log, "Current TS refreshed to: %"PRIu64"\n", curr_ts_secs);
    main_conf->bucket_json_monot_ts_secs= curr_ts_secs;

    end_code= NGX_OK;
end:
	/* Signal we have (successfully or not) completed the task */
	p_sync_mutex= &main_conf->sync_tracker_thr_mutex;
	ASSERT(ngx_thread_mutex_lock(p_sync_mutex, ngx_log)== NGX_OK);
	LOGD(ngx_log, "Clearing tracker synchronization lock flag...\n");
	ASSERT(main_conf->flag_sync_tracker_locked== 1);
	main_conf->flag_sync_tracker_locked= 0;
	ASSERT(ngx_thread_mutex_unlock(p_sync_mutex, ngx_log)== NGX_OK);
    LOGD(ngx_log, "Thread %s.\n", end_code== NGX_OK? "succeed":
    		"end with failure");
    if(tracker_fullurl!= NULL)
    	free(tracker_fullurl);
    if(curl_mem_ctx.data!= NULL)
    	free(curl_mem_ctx.data);
    if(curl_handle!= NULL)
    	curl_easy_cleanup(curl_handle);
	/* Decrement the reference count of json_object -free if it reaches zero- */
    if(jobj_buckets!= NULL) {
		register int loop_guard= 100, flag_obj_freed= 0;
		while(loop_guard> 0 && flag_obj_freed== 0) {
			// 'json_object_put()' returns 1 if the object was freed
			flag_obj_freed= json_object_put(jobj_buckets);
			loop_guard--;
		}
		ASSERT(flag_obj_freed== 1);
    }
    if(jobj_buckets_cache!= NULL) {
		register int loop_guard= 100, flag_obj_freed= 0;
		while(loop_guard> 0 && flag_obj_freed== 0) {
			// 'json_object_put()' returns 1 if the object was freed
			flag_obj_freed= json_object_put(jobj_buckets_cache);
			loop_guard--;
		}
		ASSERT(flag_obj_freed== 1);
    }
    return;
}

/**
 * Memory write callback used by our lib-curl handler to get request body.
 * @param contents Pointer to a (partial) chunk of the body content
 * @param size [bytes] of the units in which the chunk is received
 * @param nmemb number of size-units received composing the chunk
 * @param userp private user data pointer
 * @return total number of bytes received and processed.
 */
static size_t curl_write_body_callback(void *contents, size_t size,
		size_t nmemb, void *userp)
{
	ngx_log_t *ngx_log;
	char *p_data;
	size_t realsize= size* nmemb;
	curl_mem_ctx_t *curl_mem_ctx= (curl_mem_ctx_t*)userp;

	/* Check arguments */
	if(contents== NULL || size== 0 || nmemb== 0 || curl_mem_ctx== NULL ||
			(ngx_log= curl_mem_ctx->ngx_log)== NULL)
		return 0;

	p_data= (char*)realloc(curl_mem_ctx->data, curl_mem_ctx->size+ realsize+ 1);
	CHECK_DO(p_data!= NULL, return 0);
	curl_mem_ctx->data= p_data;

	memcpy(&(curl_mem_ctx->data[curl_mem_ctx->size]), contents, realsize);
	curl_mem_ctx->size+= realsize;
	curl_mem_ctx->data[curl_mem_ctx->size]= 0; // Add 'NULL' character.

	return realsize;
}

static void sync_tracker_thr_completion(ngx_event_t *ev)
{
	// We do not use this callback but it *must* be defined
	// (It's not avoided to be NULL)
	//LOGD(ev->log, "Tracker synchronization completed.\n");
}
