/**
 * @file mg_http.c
 * @brief Mongoose HTTP server and client wrapper module implementation.
 * Most code is taken from the examples at the official repository:
 * https://github.com/cesanta/mongoose/tree/master/examples
 * @author Rafael Antoniello
 */

#include "mg_http.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#include <mongoose.h>
#include "check_utils.h"

/* **** Definitions **** */

#define URI_MAX 4096
#define METH_MAX 16

/**
 * Server instance context structure.
 */
typedef struct mg_http_srv_ctx_s {
	/**
	 * HTTP server host name.
	 */
	char *listening_host;
	/**
	 * HTTP server listening port.
	 */
	char *listening_port;
	/**
	 * Headers context structure.
	 */
	struct mg_http_srv_reqhdr_ctx_s mg_http_srv_reqhdr_ctx;
	/**
	 * Set this flag to finalize server thread.
	 */
	volatile int flag_exit;
	/**
	 * HTTP server thread.
	 */
	pthread_t http_srv_thread;
	/**
	 * Server fixed "fake" response body.
	 */
	char fake_response[BODY_MAX];

	//Reserved for future use: add new fields in this structure...
} mg_http_srv_ctx_t;

typedef struct mg_http_cli_ctx_s {
	/**
	 * Set this flag to finalize client poll.
	 */
	volatile int flag_exit;
	/**
	 * Remote server response-body buffer.
	 */
	char response[BODY_MAX];
} mg_http_cli_ctx_t;

/* **** Prototypes **** */

static void srv_event_handler(struct mg_connection *c, int ev, void *p);
static void* srv_thr(void *t);

static void cli_event_handler(struct mg_connection *nc, int ev, void *p);

/* **** Implementations **** */

mg_http_srv_ctx_t* mg_http_srv_open(const char *listening_host,
		const char *listening_port,
		mg_http_srv_reqhdr_ctx_t *mg_http_srv_reqhdr_ctx,
		const char *fake_response_body)
{
	int fake_response_body_len;
	int ret_code, end_code= -1; // error by default
	mg_http_srv_ctx_t *mg_http_srv_ctx= NULL;

	/* Check arguments */
	CHECK_DO(listening_host!= NULL, return NULL);
	CHECK_DO(listening_port!= NULL, return NULL);
	CHECK_DO(fake_response_body!= NULL &&
			(fake_response_body_len= strlen(fake_response_body))< BODY_MAX,
			return NULL);

	/* Allocate module's context structure */
	mg_http_srv_ctx= (mg_http_srv_ctx_t*)calloc(1, sizeof(mg_http_srv_ctx_t));
	CHECK_DO(mg_http_srv_ctx!= NULL, goto end);

	/* Initialize context structure */

	mg_http_srv_ctx->listening_host= strdup(listening_host);
	CHECK_DO(mg_http_srv_ctx->listening_host!= NULL, goto end);

	mg_http_srv_ctx->listening_port= strdup(listening_port);
	CHECK_DO(mg_http_srv_ctx->listening_port!= NULL, goto end);

	if(mg_http_srv_reqhdr_ctx!= NULL) {
		if(mg_http_srv_reqhdr_ctx->host!= NULL &&
				strlen(mg_http_srv_reqhdr_ctx->host)> 0)
			mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.host=
					strdup(mg_http_srv_reqhdr_ctx->host);
		mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.max_age=
				mg_http_srv_reqhdr_ctx->max_age;
	}

	memcpy(mg_http_srv_ctx->fake_response, fake_response_body,
			fake_response_body_len);

	// Reserved for future use...

	/* Launch HTTP server thread */
	ret_code= pthread_create(&mg_http_srv_ctx->http_srv_thread, NULL, srv_thr,
			mg_http_srv_ctx);
	CHECK_DO(ret_code== 0, goto end);

	end_code= 0;
end:
	if(end_code!= 0)
		mg_http_srv_close(&mg_http_srv_ctx);
	return mg_http_srv_ctx;
}

void mg_http_srv_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx)
{
	mg_http_srv_ctx_t *mg_http_srv_ctx;
	void *thread_end_code= NULL;

	if(ref_mg_http_srv_ctx== NULL || (mg_http_srv_ctx= (mg_http_srv_ctx_t*)
			*ref_mg_http_srv_ctx)== NULL)
		return;

	/* Firstly join thread:
	 * - Set flag to exit;
	 */
	mg_http_srv_ctx->flag_exit= 1;
	printf("Waiting for MG HTTP-server %s:%s thread to join... ",
			mg_http_srv_ctx->listening_host,
			mg_http_srv_ctx->listening_port); //comment-me
	pthread_join(mg_http_srv_ctx->http_srv_thread, &thread_end_code);
	if(thread_end_code!= NULL) {
		ASSERT(*((int*)thread_end_code)== 0);
		free(thread_end_code);
		thread_end_code= NULL;
	}
	printf("thread joined O.K.\n"); //comment-me

	/* Release host name */
	if(mg_http_srv_ctx->listening_host!= NULL) {
		free(mg_http_srv_ctx->listening_host);
		mg_http_srv_ctx->listening_host= NULL;
	}

	/* Release port */
	if(mg_http_srv_ctx->listening_port!= NULL) {
		free(mg_http_srv_ctx->listening_port);
		mg_http_srv_ctx->listening_port= NULL;
	}

	// Reserved for future use: release other new variables here...

	/* Release context structure */
	free(mg_http_srv_ctx);
	*ref_mg_http_srv_ctx= NULL;
}

char* mg_http_cli_request(const char *method, const char *host,
		const char *port, const char *location, const char *qstring,
		mg_http_cli_reqhdr_ctx_t *mg_http_cli_reqhdr_ctx, const char *body)
{
	size_t content_size;
	struct mg_mgr mgr;
	struct mg_connection *nc;
	struct mg_connect_opts opts;
	const char *error_cstr= NULL;
	struct mg_http_cli_ctx_s mg_http_cli_ctx= {0};
	char host_port[32]= {0}; // 21 chars is enough for any ipv4 host:port

	/* Check arguments.
	 * Note that 'qstring', 'mg_http_cli_reqhdr_ctx' and 'body' may be NULL.
	 */
	CHECK_DO(method!= NULL, return NULL);
	CHECK_DO(host!= NULL, return NULL);
	CHECK_DO(port!= NULL, return NULL);
	CHECK_DO(location!= NULL, return NULL);

	memset(&opts, 0, sizeof(opts));
	opts.error_string= &error_cstr;
	opts.user_data= &mg_http_cli_ctx;

	mg_mgr_init(&mgr, NULL);
	snprintf(host_port, sizeof(host_port), "%s:%s", host, port);
	nc= mg_connect_opt(&mgr, host_port, cli_event_handler, opts);
	if(nc== NULL) {
		fprintf(stderr, "mg_connect(%s:%s) failed: %s\n", host, port,
				error_cstr);
		goto end;
	}
	mg_set_protocol_http_websocket(nc);

	mg_printf(nc, "%s %s%s%s HTTP/1.0\r\n", method, location, qstring? "?": "",
			qstring? qstring: "");
	if(mg_http_cli_reqhdr_ctx->host!= NULL)
		mg_printf(nc, "Host: %s\r\n", mg_http_cli_reqhdr_ctx->host);
	if(body!= NULL && (content_size= strlen(body))> 0) {
		mg_printf(nc, "Content-Length: %d\r\n", (int)content_size);
		mg_printf(nc, "\r\n");
		mg_printf(nc, "%s", body);
	} else {
		mg_printf(nc, "\r\n");
	}

	while(mg_http_cli_ctx.flag_exit== 0)
		mg_mgr_poll(&mgr, 1000);

end:
	mg_mgr_free(&mgr);
	if(strlen(mg_http_cli_ctx.response)> 0) {
		printf("\n\nClient get the response: '%s'\n",
				mg_http_cli_ctx.response); //comment-me
		return strdup(mg_http_cli_ctx.response);
	} else
		return NULL;
}

static void srv_event_handler(struct mg_connection *c, int ev, void *p)
{
	if(ev== MG_EV_HTTP_REQUEST) {
		register size_t uri_len= 0, method_len= 0, qs_len= 0, body_len= 0;
		const char *uri_p, *method_p, *qs_p, *body_p;
		struct http_message *hm= (struct http_message*)p;
		char *url_str= NULL, *method_str= NULL, *str_response= NULL,
				*qstring_str= NULL, *body_str= NULL;
		mg_http_srv_ctx_t *mg_http_srv_ctx= (mg_http_srv_ctx_t*)c->user_data;

		if((uri_p= hm->uri.p)!= NULL && (uri_len= hm->uri.len)> 0 &&
				uri_len< URI_MAX) {
			url_str= (char*)calloc(1, uri_len+ 1);
			if(url_str!= NULL)
				memcpy(url_str, uri_p, uri_len);
		}
		if((method_p= hm->method.p)!= NULL && (method_len= hm->method.len)> 0
				 && method_len< METH_MAX) {
			method_str= (char*)calloc(1, method_len+ 1);
			if(method_str!= NULL)
				memcpy(method_str, method_p, method_len);
		}
		if((qs_p= hm->query_string.p)!= NULL &&
				(qs_len= hm->query_string.len)> 0 && qs_len< URI_MAX) {
			qstring_str= (char*)calloc(1, qs_len+ 1);
			if(qstring_str!= NULL)
				memcpy(qstring_str, qs_p, qs_len);
		}
		if((body_p= hm->body.p)!= NULL && (body_len= hm->body.len)> 0
				&& body_len< BODY_MAX) {
			body_str= (char*)calloc(1, body_len+ 1);
			if(body_str!= NULL)
				memcpy(body_str, body_p, body_len);
		}

		/* Process HTTP request */
		if(url_str!= NULL && method_str!= NULL) {
			int i;
			//my_process_here(); // Reserved for future use...
			printf("\n\nMG HTTP-server received request:\n"
					"Method: '%s'; Url: '%s'; Query: '%s'\n",
					method_str? method_str: "", url_str? url_str: "",
							qstring_str? qstring_str: "");
			str_response= mg_http_srv_ctx->fake_response; // just easy example
			printf("Headers: ");
			for(i= 0; i< MG_MAX_HTTP_HEADERS; i++) {
				struct mg_str header_name= hm->header_names[i];
				struct mg_str header_value= hm->header_values[i];
				if(header_name.p!= NULL && header_value.p!= NULL)
					printf("'%.*s'-'%.*s'\n",
							(int)header_name.len, header_name.p,
							(int)header_value.len, header_value.p);
			}
			printf("<end-headers>\n");
		}

		/* Send response */
		if(str_response!= NULL && strlen(str_response)> 0) {
			int str_response_len= (int)strlen(str_response);
			if(str_response_len> 1024)
				printf("MG HTTP-server response is: '%.1024s \x1B[33m"
						"... <rest of string omitted as is too long> \x1B[0m' "
						"(len: %d)\n",
						str_response, str_response_len); //comment-me
			else
				printf("MG HTTP-server response is: '%s' (len: %d)\n",
						str_response, str_response_len); //comment-me
			mg_printf(c, "%s", "HTTP/1.1 200 OK\r\n");
			if(mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.host!= NULL) {
				mg_printf(c, "Server: %s\r\n",
						mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.host);
			}
			//mg_printf(c, "Content-Type: text/html\r\n");
			mg_printf(c, "Content-Length: %d\r\n", str_response_len+ 2);
			if(mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.max_age> 0) {
				mg_printf(c, "Cache-Control: public, max-age=%d\r\n",
						mg_http_srv_ctx->mg_http_srv_reqhdr_ctx.max_age);
			}
			mg_printf(c, "\r\n");
			mg_printf(c, "%s\r\n", str_response);
		} else {
			mg_printf(c, "%s", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
		}

		//if(str_response!= NULL) // static...
		//	free(str_response);
		if(url_str!= NULL)
			free(url_str);
		if(method_str!= NULL)
			free(method_str);
		if(qstring_str!= NULL)
			free(qstring_str);
		if(body_str!= NULL)
			free(body_str);
	} else if(ev== MG_EV_RECV) {
		mg_printf(c, "%s", "HTTP/1.1 202 ACCEPTED\r\nContent-Length: 0\r\n");
	} else if(ev== MG_EV_SEND) {
		mg_printf(c, "%s", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	}
}

/**
 * Runs HTTP server thread, listening to the given port.
 */
static void* srv_thr(void *t)
{
	struct mg_mgr mgr;
	struct mg_connection *c;
	mg_http_srv_ctx_t *mg_http_srv_ctx= (mg_http_srv_ctx_t*)t;
	struct mg_bind_opts opts;
	const char *error_str= NULL;

	/* Check argument */
	CHECK_DO(mg_http_srv_ctx!= NULL, return NULL);

	/* Create and configure the server */
	mg_mgr_init(&mgr, NULL);

	memset(&opts, 0, sizeof(opts));
	opts.error_string= &error_str;
	opts.user_data= mg_http_srv_ctx;
	c= mg_bind_opt(&mgr, mg_http_srv_ctx->listening_port, srv_event_handler,
			opts);
	if(c== NULL) {
		fprintf(stderr, "mg_bind_opt(%s:%s) failed: %s\n",
				mg_http_srv_ctx->listening_host,
				mg_http_srv_ctx->listening_port, error_str);
		goto end;
	}
	mg_set_protocol_http_websocket(c);

	printf("Server %s (bind to port %s) running.\n",
			mg_http_srv_ctx->listening_host,
			mg_http_srv_ctx->listening_port);
	while(mg_http_srv_ctx->flag_exit== 0)
		mg_mgr_poll(&mgr, 1000);

end:
	mg_mgr_free(&mgr);
	return NULL;
}

static void cli_event_handler(struct mg_connection *nc, int ev, void *p)
{
	struct http_message *hm= (struct http_message*)p;
	mg_http_cli_ctx_t *mg_http_cli_ctx= (mg_http_cli_ctx_t*)nc->user_data;
	int connect_status;

	switch(ev) {
	case MG_EV_CONNECT:
		connect_status= *(int*)p;
		if (connect_status!= 0) {
			printf("Error in HTTP connection: %s\n", strerror(connect_status));
			mg_http_cli_ctx->flag_exit= 1;
		}
		break;
	case MG_EV_HTTP_REPLY:
		if(hm->message.len> 0 && hm->message.p!= NULL) {
			int len= 0;
			const char *needle_len= "Content-Length: ", *needle_sep= "\r\n\r\n";
			char *msg_p= strstr(hm->message.p, needle_len);
			while(msg_p!= NULL && msg_p< hm->message.p+ hm->message.len) {
				msg_p+= strlen(needle_len);
				if((len= strtol(msg_p, NULL, 10))> 0) {
					msg_p= strstr(hm->message.p, needle_sep);
					if(msg_p)
						msg_p+= strlen(needle_sep);
					break;
				}
				msg_p= strstr(msg_p, needle_len);
			}
			if(len> 0 && len< BODY_MAX) {
				memcpy((void*)mg_http_cli_ctx->response, msg_p, len);
			} else if (len>= BODY_MAX){
				fprintf(stderr, "Message too big!'%s()'\n", __FUNCTION__);
			}
		}
		nc->flags|= MG_F_SEND_AND_CLOSE;
		mg_http_cli_ctx->flag_exit= 1;
		break;
	case MG_EV_CLOSE:
		mg_http_cli_ctx->flag_exit= 1;
		break;
	default:
		break;
	}
}
