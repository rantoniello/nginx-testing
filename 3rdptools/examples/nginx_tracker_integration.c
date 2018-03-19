/**
 * //TODO
 * @file
 * @brief
 * @author
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

#include <mongoose.h>
#include <json-c/json.h>
#include <libutils/check_utils.h>
#include <libutils/mg_http.h>
#include <libutils/interr_usleep.h>

#define REPO_DIR "/home/ral/workspace/TID/cdn-webcache"

/*
 * Final-client related definitions.
 */
#define HTTP_FINAL_CLIENT_HDRHOST "img89.terra.es"

/*
 * Nginx reverse-proxy related definitions.
 */
#define HTTP_SERVER_NGINX_HOST "127.0.0.1"
#define HTTP_SERVER_NGINX_PORT "8080"

/*
 * "Fake-tracker" related definitions.
 */
#define BUCKETS_JSON_FILE REPO_DIR"/src/rpm/SOURCES/modules/tcdn_webcache"\
	"/ftests/buckets.json"
#define BUCKETS_JSON_SIZE_MAX (1024* 10)
#define HTTP_SERVER_FAKE_TRACKER_HOST "127.0.0.1"
#define HTTP_SERVER_FAKE_TRACKER_PORT "8081"

/*
 * "Origin-1" server related definitions.
 */
#define HTTP_SERVER_ORIGIN1_HOST "127.0.0.1"
#define HTTP_SERVER_ORIGIN1_PORT "8082"

/* Forward declarations */
typedef struct nginx_wrapper_ctx_s nginx_wrapper_ctx_t;

/* **** Prototypes **** */

static void http_get_nginx(const char *uri, const char *query_str);

static nginx_wrapper_ctx_t* nginx_wrapper_open(char *const argv[],
		char *const envp[]);
static void nginx_wrapper_close(nginx_wrapper_ctx_t **ref_nginx_wrapper_ctx,
		const char *fullpath_pidfile);

static mg_http_srv_ctx_t* fake_tracker_open();
static void fake_tracker_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx);

static mg_http_srv_ctx_t* fake_origin_1_open();
static void fake_origin_1_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx);

static void main_proc_quit_signal_handler();

/* **** Implementations **** */

static char *nginx_argv[]= {
		REPO_DIR"/3rdptools/_install_dir_x86/sbin/nginx",
		"-c",
		REPO_DIR"/src/rpm/SOURCES/modules/tcdn_webcache/ftests/"
			"test_basic_nginx_001.conf",
		NULL
};
static char *nginx_envp[]= {
		"LD_LIBRARY_PATH="REPO_DIR"/3rdptools/_install_dir_x86/lib",
		NULL
};

static char nginx_fdfile[]= REPO_DIR"/3rdptools/_install_dir_x86/logs/\
nginx.pid";

static char msg1[]= "{\"origin_server_id\":1}";

static interr_usleep_ctx_t* interr_usleep_ctx= NULL;

int main(int argc, char* argv[])
{
	sigset_t set;
	mg_http_srv_ctx_t *mg_http_srv_ctx_origin_1= NULL;
	mg_http_srv_ctx_t *mg_http_srv_ctx_fake_tracker= NULL;
	nginx_wrapper_ctx_t *nginx_wrapper_ctx= NULL;

	/* Set SIGNAL handlers to this process */
	sigfillset(&set);
	sigdelset(&set, SIGINT);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	signal(SIGINT, main_proc_quit_signal_handler);
	//signal(SIGQUIT, stream_proc_quit_signal_handler);
	//signal(SIGTERM, stream_proc_quit_signal_handler);

	interr_usleep_ctx= interr_usleep_open();

	/* Launch nginx daemon */
	nginx_wrapper_ctx= nginx_wrapper_open(nginx_argv, nginx_envp);
	CHECK_DO(nginx_wrapper_ctx!= NULL, exit(-1));

	/* Launch "fake-tracker" */
	mg_http_srv_ctx_fake_tracker= fake_tracker_open();
	CHECK_DO(mg_http_srv_ctx_fake_tracker!= NULL, exit(-1));

	/* Launch MG HTTP-server "origin-1" */
	mg_http_srv_ctx_origin_1= fake_origin_1_open();
	CHECK_DO(mg_http_srv_ctx_origin_1!= NULL, exit(-1));

	/* Just wait an instant to make sure server thread is up... */
	if(interr_usleep(interr_usleep_ctx, 1000* 500)== EINTR)
		goto end;

	/* Perform GET request to nginx location "/" */
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x576");
	printf("\nAs is the first request, buckets register is not available in "
			"NGINX so it fails...\n");

	printf("\nWe will wait 1 second and request again...\n");
	if(interr_usleep(interr_usleep_ctx, 1000* 1000* 1)== EINTR)
		goto end;

	/* Perform GET request to nginx location "/" */
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x576");
	printf("\nThe second request should succeed\n");
	if(interr_usleep(interr_usleep_ctx, 1000* 1000* 4)== EINTR)
		goto end;

	printf("If we do some requests in a row (more than 3), we will observe "
			"origin server does not respond, as content is cached by "
			"NGINX...\n");
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");

	printf("\nNow we will wait max-age=5 to expire... and request again... "
			"origin should answer this time.\n");
	if(interr_usleep(interr_usleep_ctx, 1000* 1000* 6)== EINTR)
		goto end;
	http_get_nginx("/any/path/media.mp4", "t0=0&res=720x480");

	/* Exit example */
end:
	printf("Shutting down example...!\n");

	/* Join and release MG HTTP-server "origin-1" */
	fake_origin_1_close(&mg_http_srv_ctx_origin_1);

	/* Joint fake tracker */
	fake_tracker_close(&mg_http_srv_ctx_fake_tracker);

	/* Kill nginx */
	nginx_wrapper_close(&nginx_wrapper_ctx, nginx_fdfile);

	interr_usleep_close(&interr_usleep_ctx);
	printf("Example finished.\n");
	return 0;
}

static void http_get_nginx(const char *uri, const char *query_str)
{
	char *srv_response_cstr= NULL;
	mg_http_cli_reqhdr_ctx_t mg_http_cli_reqhdr_ctx= {
			HTTP_FINAL_CLIENT_HDRHOST
	};

	printf("\nPerforming GET request to NGINX cache-proxy server: '%s:%s%s?"
			"%s' (HTTP host-header: '%s')\n",
			HTTP_SERVER_NGINX_HOST, HTTP_SERVER_NGINX_PORT, uri, query_str,
			HTTP_FINAL_CLIENT_HDRHOST);

	srv_response_cstr= mg_http_cli_request("GET", HTTP_SERVER_NGINX_HOST,
			HTTP_SERVER_NGINX_PORT, uri, query_str, &mg_http_cli_reqhdr_ctx,
			NULL);
	if(srv_response_cstr!= NULL) {
		free(srv_response_cstr);
		srv_response_cstr= NULL;
	}
}

typedef struct nginx_wrapper_ctx_s {
	pid_t process_pid;
} nginx_wrapper_ctx_t;

static nginx_wrapper_ctx_t* nginx_wrapper_open(char *const argv[],
		char *const envp[])
{
	pid_t cpid;

	/* Check arguments */
	CHECK_DO(argv!= NULL, exit(-1));

	cpid= fork();
	if(cpid== -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if(cpid== 0) {
		int i;

		/* Code executed by child */
		fprintf(stdout, "\nNginx process starting PID is %ld.\n",
				(long)getpid());

		/* Execute Nginx */
		fprintf(stdout, "Executing nginx as: '%s ", argv[0]);
		for(i= 1; argv[i]!= NULL; i++)
			fprintf(stdout, "%s ", argv[i]);
		if(envp!= NULL) {
			for(i= 0; envp[i]!= NULL; i++)
				fprintf(stdout, "%s ", envp[i]);
		}
		fprintf(stdout, "\n");
		fflush(stdout);
		execvpe(argv[0], argv, envp);
		exit(EXIT_SUCCESS);
	} else {
		nginx_wrapper_ctx_t *nginx_wrapper_ctx= NULL;

		/* Parent */

		/* Allocate context structure */
		nginx_wrapper_ctx= calloc(1, sizeof(nginx_wrapper_ctx_t));
		CHECK_DO(nginx_wrapper_ctx!= NULL, exit(-1));

		/* Initialize */
		nginx_wrapper_ctx->process_pid= cpid;

		return nginx_wrapper_ctx;
	}
}

static void nginx_wrapper_close(nginx_wrapper_ctx_t **ref_nginx_wrapper_ctx,
		const char *fullpath_pidfile)
{
	pid_t /*cpid,*/ cpid2, w;
	int ret_code, status, filedesc;
	nginx_wrapper_ctx_t *nginx_wrapper_ctx;
	char strpid[32]= {0};

	/* Check arguments */
	if(ref_nginx_wrapper_ctx== NULL ||
			(nginx_wrapper_ctx= *ref_nginx_wrapper_ctx)== NULL)
		return;

	/* Get process PID */
	//cpid= nginx_wrapper_ctx->process_pid;
	//CHECK_DO(cpid>= 0, exit(-1));

	/* Signal nginx to quit gracefully (try to...):
	 * Nginx can be signaled as follows:
	 * - quit – Shut down gracefully
	 * - reload – Reload the configuration file
	 * - reopen – Reopen log files
	 * - stop – Shut down immediately (fast shutdown)
	 */
	filedesc= open(fullpath_pidfile, O_RDONLY);
	CHECK_DO(filedesc>= 0, exit(-1));
	CHECK_DO(read(filedesc, &strpid, sizeof(strpid))> 0, exit(-1));
	cpid2= strtol(strpid, NULL, 10);
	close(filedesc);
	printf("\nSignaling nginx to exit (read PID= %d)\n", cpid2);
	ASSERT((ret_code= kill(cpid2, SIGQUIT))== 0);
	//ASSERT((ret_code= kill(cpid, SIGQUIT))== 0);
	if(ret_code!= 0) {
		ASSERT(kill(cpid2, SIGINT));
		//ASSERT(kill(cpid, SIGINT));
	}

	/* Wait nginx to finalize */
	do {
		w= waitpid(cpid2, &status, WUNTRACED| WCONTINUED);
		if(w== -1) {
			perror("\nwaitpid\n");
			exit(-1);
		}
		if(WIFEXITED(status)) {
			printf("\nnginx exited with status= %d\n", WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			printf("\nnginx killed by signal %d\n", WTERMSIG(status));
		} else if (WIFSTOPPED(status)) {
			printf("\nnginx stopped by signal %d\n", WSTOPSIG(status));
		} else if (WIFCONTINUED(status)) {
			printf("\nnginx continued\n");
		}
	} while(!WIFEXITED(status) && !WIFSIGNALED(status));

	/* Release context structure */
	free(nginx_wrapper_ctx);
	ref_nginx_wrapper_ctx= NULL;
}

static mg_http_srv_ctx_t* fake_tracker_open()
{
	int filedesc, json_buckets_len;
	ssize_t read_bytes= -1;
	char buckets_json_cstr[BODY_MAX]= {0};
	mg_http_srv_ctx_t *mg_http_srv_ctx= NULL;
	struct json_object *jobj_buckets= NULL;
	mg_http_srv_reqhdr_ctx_t mg_http_srv_reqhdr_ctx= {
			HTTP_SERVER_FAKE_TRACKER_HOST HTTP_SERVER_FAKE_TRACKER_PORT, //host
			0 //max-age
	};

	/* Check arguments */
	// Reserved for future use...

	printf("\nLaunching HTTP-server \"fake-tracker\"... \n");

	/* Get buckets JSON */
	printf("Getting buckets.json (%s)\n", BUCKETS_JSON_FILE);
	filedesc= open(BUCKETS_JSON_FILE, O_RDONLY);
	CHECK_DO(filedesc>= 0, exit(-1));
	read_bytes= read(filedesc, buckets_json_cstr, BODY_MAX);
	if(read_bytes< 0 || !(read_bytes< BODY_MAX)) {
		printf("Sample JSON size not supported [has to be 0..%d]\n", BODY_MAX);
		CHECK_DO(0, exit(-1));
	}
	close(filedesc);

	/* Parse the JSON -this may be CPU-heavy- */
	printf("Tracker: parsing buckets.json...\n");
	jobj_buckets= json_tokener_parse(buckets_json_cstr);
	json_buckets_len= json_object_array_length(jobj_buckets);
	printf("Tracker: buckets.json has %d buckets...\n", json_buckets_len);

	/* Launch MG HTTP-server for our fake tracker */
	mg_http_srv_ctx= mg_http_srv_open(HTTP_SERVER_FAKE_TRACKER_HOST,
			HTTP_SERVER_FAKE_TRACKER_PORT, &mg_http_srv_reqhdr_ctx,
			buckets_json_cstr);
	CHECK_DO(mg_http_srv_ctx!= NULL, exit(-1));

	/* Decrement the reference count of json_object -free if it reaches zero- */
	json_object_put(jobj_buckets);

	return mg_http_srv_ctx;
}

static void fake_tracker_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx)
{
	printf("\nClosing HTTP-server \"fake-tracker\"... \n");
	mg_http_srv_close(ref_mg_http_srv_ctx);
}

static mg_http_srv_ctx_t* fake_origin_1_open()
{
	mg_http_srv_ctx_t *mg_http_srv_ctx= NULL;
	mg_http_srv_reqhdr_ctx_t mg_http_srv_reqhdr_ctx= {
			HTTP_SERVER_ORIGIN1_HOST HTTP_SERVER_ORIGIN1_PORT, //host
			5 //max-age
	};

	/* Check arguments */
	// Reserved for future use...

	printf("\nLaunching HTTP-server \"origin-1\"... \n");

	mg_http_srv_ctx= mg_http_srv_open(HTTP_SERVER_ORIGIN1_HOST,
			HTTP_SERVER_ORIGIN1_PORT, &mg_http_srv_reqhdr_ctx, msg1);
	CHECK_DO(mg_http_srv_ctx!= NULL, exit(-1));

	return mg_http_srv_ctx;
}

static void fake_origin_1_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx)
{
	printf("\nClosing HTTP-server \"origin-1\"... \n");
	mg_http_srv_close(ref_mg_http_srv_ctx);
}

static void main_proc_quit_signal_handler()
{
	printf("Signaling application to finalize...\n");
	interr_usleep_unblock(interr_usleep_ctx);
}
