/**
 * @file mg_http.h
 * @brief Mongoose HTTP server and client wrapper module public interface.
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_MG_HTTP_H_
#define UTILS_SRC_MG_HTTP_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct mg_http_srv_ctx_s mg_http_srv_ctx_t;
typedef struct mg_http_cli_ctx_s mg_http_cli_ctx_t;

/**
 * Maximum permitted message body size in bytes
 */
#define BODY_MAX (512*1024)

/**
 * Client's HTTP-request headers context structure.
 */
typedef struct mg_http_cli_reqhdr_ctx_s {
	const char *host;
	// Reserved for future use: add other header-fields here
} mg_http_cli_reqhdr_ctx_t;

/**
 * Server's HTTP-response headers context structure.
 */
typedef struct mg_http_srv_reqhdr_ctx_s {
	const char *host;
	unsigned int max_age;
	// Reserved for future use: add other header-fields here
} mg_http_srv_reqhdr_ctx_t;

/* **** Prototypes **** */

/**
 * Instantiates HTTP server.
 * @param listening_host Server host-name.
 * @param listening_port Server listening port.
 * @param mg_http_srv_reqhdr_ctx Server's HTTP-response headers context
 * structure.
 * @param fake_response_body Server's HTTP-response body.
 * @return Pointer to the server's context structure on success, NULL if
 * fails.
 */
mg_http_srv_ctx_t* mg_http_srv_open(const char *listening_host,
		const char *listening_port,
		mg_http_srv_reqhdr_ctx_t *mg_http_srv_reqhdr_ctx,
		const char *fake_response_body);

/**
 * Release HTTP server instance previously obtained in a call to
 * 'mg_http_srv_open'.
 * @param ref_mg_http_srv_ctx Reference to the pointer to the HTTP server
 * instance context structure to be released, obtained in a previous
 * call to the 'mg_http_srv_open()' function. Pointer is set to NULL on return.
 */
void mg_http_srv_close(mg_http_srv_ctx_t **ref_mg_http_srv_ctx);

/**
 * Perform HTTP request (client side).
 * @param method HTTP method; e.g. "GET", "PUT", "POST", "DELETE".
 * @param host Destination host name.
 * @param port Destination host port.
 * @param location Destination location/path.
 * @param qstring Request query-string. Optional (NULL may be passed).
 * @param mg_http_cli_reqhdr_ctx Request HTTP-headers.
 * Optional (NULL may be passed).
 * @param body Request body. Optional (NULL may be passed).
 * @return Pointer to the server's response body, if applicable.
 * Otherwise, NULL is returned.
 */
char* mg_http_cli_request(const char *method, const char *host,
		const char *port, const char *location, const char *qstring,
		mg_http_cli_reqhdr_ctx_t *mg_http_cli_reqhdr_ctx, const char *body);

#endif /* UTILS_SRC_MG_HTTP_H_ */
